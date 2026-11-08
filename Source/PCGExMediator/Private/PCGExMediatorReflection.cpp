// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorReflection.h"

#include "PCGExMediatorConverter.h"
#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorLookup.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorSchema.h"
#include "PCGExMediatorValues.h"
#include "Curves/CurveFloat.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "JsonObjectConverter.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "StructUtils/InstancedStruct.h"
#include "Utils/PCGExWeightCurve.h"
#include "UObject/Class.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace PCGExMediatorReflection
{
	using namespace PCGExMediator;

	const FObjectProperty* AsPlainObjectRef(const FProperty* Property)
	{
		const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
		if (!ObjectProperty || Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_PersistentInstance)) { return nullptr; }
		return ObjectProperty;
	}

	const UEnum* EnumOf(const FProperty* Property, const FNumericProperty*& OutUnderlying)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			OutUnderlying = EnumProperty->GetUnderlyingProperty();
			return EnumProperty->GetEnum();
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			if (const UEnum* Enum = Numeric->GetIntPropertyEnum())
			{
				OutUnderlying = Numeric;
				return Enum;
			}
		}
		OutUnderlying = nullptr;
		return nullptr;
	}

	// Engine shapes for what the dialect does not cover (maps, instanced structs and objects).
	TSharedPtr<FJsonValue> EngineEncode(const FProperty* Property, const void* ValuePtr)
	{
		return FJsonObjectConverter::UPropertyToJsonValue(const_cast<FProperty*>(Property), ValuePtr);
	}

	bool EngineDecode(const FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Json)
	{
		FText FailReason;
		if (!FJsonObjectConverter::JsonValueToUProperty(Json, const_cast<FProperty*>(Property), ValuePtr, 0, 0, false, &FailReason))
		{
			Report(EPCGExMediatorSeverity::Error, FailReason.IsEmpty() ? TEXT("value rejected") : FailReason.ToString());
			return false;
		}
		return true;
	}

	bool ExpectString(const TSharedPtr<FJsonValue>& Json, const TCHAR* What, FString& Out)
	{
		if (!Json.IsValid() || Json->Type != EJson::String)
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("expected %s"), What));
			return false;
		}
		Out = Json->AsString();
		return true;
	}

	// A numeric value a details panel would clamp is refused instead: an agent gets the bound, not a silent rewrite.
	bool WithinClamps(const FProperty* Property, const void* ValuePtr)
	{
#if WITH_EDITORONLY_DATA
		const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
		if (!Numeric || Numeric->GetIntPropertyEnum()) { return true; }
		const double Value = Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(ValuePtr) : static_cast<double>(Numeric->GetSignedIntPropertyValue(ValuePtr));
		if (Property->HasMetaData(TEXT("ClampMin")) && Value < Property->GetFloatMetaData(TEXT("ClampMin")))
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%g is below the minimum %s"), Value, *Property->GetMetaData(TEXT("ClampMin"))));
			return false;
		}
		if (Property->HasMetaData(TEXT("ClampMax")) && Value > Property->GetFloatMetaData(TEXT("ClampMax")))
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%g is above the maximum %s"), Value, *Property->GetMetaData(TEXT("ClampMax"))));
			return false;
		}
#endif
		return true;
	}

	// Editor metadata onto a property's fragment: the tooltip as description (ahead of the shape hint), clamps as bounds.
	void AddPropertyMetadata(const FProperty* Property, FJsonObject& Fragment)
	{
#if WITH_EDITORONLY_DATA
		const FString Tooltip = Property->GetToolTipText().ToString();
		if (!Tooltip.IsEmpty())
		{
			FString Shape;
			Fragment.TryGetStringField(TEXT("description"), Shape);
			Fragment.SetStringField(TEXT("description"), Shape.IsEmpty() ? Tooltip : FString::Printf(TEXT("%s; %s"), *Tooltip, *Shape));
		}
		if (Property->IsA<FNumericProperty>())
		{
			if (Property->HasMetaData(TEXT("ClampMin"))) { Fragment.SetNumberField(TEXT("minimum"), Property->GetFloatMetaData(TEXT("ClampMin"))); }
			if (Property->HasMetaData(TEXT("ClampMax"))) { Fragment.SetNumberField(TEXT("maximum"), Property->GetFloatMetaData(TEXT("ClampMax"))); }
		}
#endif
	}

	// Weight curves compare by what their dialect carries (asset, key time / value / mode), not by full reflected identity.
	bool IdenticalInDialect(const FProperty* Property, const void* A, const void* B)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property); StructProperty && StructProperty->Struct == FPCGExWeightCurve::StaticStruct())
		{
			return Reflect::WeightCurvesIdentical(*Property->ContainerPtrToValuePtr<FPCGExWeightCurve>(A), *Property->ContainerPtrToValuePtr<FPCGExWeightCurve>(B));
		}
		return Property->Identical_InContainer(A, B);
	}

#pragma region Input shorthands

	namespace ShorthandKeys
	{
		constexpr const TCHAR* const Attribute = TEXT("attribute");
		constexpr const TCHAR* const Constant = TEXT("constant");
		constexpr const TCHAR* const Cleanup = TEXT("cleanup");
	}

	/** The four members every concrete shorthand shares, found by name so the ~40 typed structs need no registration. */
	struct FShorthandLayout
	{
		const FEnumProperty* Input = nullptr;
		const FProperty* Constant = nullptr;
		const FProperty* Attribute = nullptr;
		const FBoolProperty* Cleanup = nullptr;

		bool IsOwn(const FProperty* Property) const
		{
			return Property == Input || Property == Constant || Property == Attribute || Property == Cleanup;
		}

		bool ReadsAttribute(const void* Memory) const
		{
			return Input->GetUnderlyingProperty()->GetSignedIntPropertyValue(Input->ContainerPtrToValuePtr<void>(Memory)) == static_cast<int64>(EPCGExInputValueType::Attribute);
		}

		void SetInput(void* Memory, const EPCGExInputValueType Value) const
		{
			Input->GetUnderlyingProperty()->SetIntPropertyValue(Input->ContainerPtrToValuePtr<void>(Memory), static_cast<int64>(Value));
		}
	};

	bool ResolveShorthand(const UScriptStruct* Struct, FShorthandLayout& Out)
	{
		if (!Struct || !Struct->IsChildOf(FPCGExInputShorthandBase::StaticStruct())) { return false; }
		Out.Input = CastField<FEnumProperty>(Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FPCGExInputShorthandBase, Input)));
		Out.Cleanup = CastField<FBoolProperty>(Struct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FPCGExInputShorthandBase, bCleanupAttribute)));
		Out.Constant = Struct->FindPropertyByName(TEXT("Constant"));
		Out.Attribute = Struct->FindPropertyByName(TEXT("Attribute"));
		return Out.Input && Out.Cleanup && Out.Constant && Out.Attribute;
	}

#pragma endregion

#pragma region Weight curves

	namespace CurveKeys
	{
		constexpr const TCHAR* const Asset = TEXT("asset");
		constexpr const TCHAR* const Points = TEXT("points");
	}

	const TCHAR* InterpModeName(const ERichCurveInterpMode Mode)
	{
		switch (Mode)
		{
		case RCIM_Linear: return TEXT("Linear");
		case RCIM_Constant: return TEXT("Constant");
		case RCIM_Cubic: return TEXT("Cubic");
		case RCIM_None: return TEXT("None");
		default: return nullptr;
		}
	}

	bool ParseInterpMode(const FString& Text, ERichCurveInterpMode& Out)
	{
		FString Name = Text;
		Name.RemoveFromStart(TEXT("RCIM_"), ESearchCase::IgnoreCase);
		for (const ERichCurveInterpMode Mode : {RCIM_Linear, RCIM_Constant, RCIM_Cubic, RCIM_None})
		{
			if (Name.Equals(InterpModeName(Mode), ESearchCase::IgnoreCase))
			{
				Out = Mode;
				return true;
			}
		}
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not an interpolation mode: Linear, Constant, Cubic"), *Text.Left(64)));
		return false;
	}

	/** [[t, v], [t, v, "<mode>"], ...] onto a fresh curve; Out is written only when every point reads. */
	bool DecodeCurvePoints(const TArray<TSharedPtr<FJsonValue>>& Items, FRichCurve& Out)
	{
		FRichCurve Fresh;
		bool bOk = true;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			FPathScope P(i);
			if (!Items[i].IsValid() || Items[i]->Type != EJson::Array || Items[i]->AsArray().Num() < 2 || Items[i]->AsArray().Num() > 3)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("expected [t, v] or [t, v, \"Linear|Constant|Cubic\"]"));
				bOk = false;
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>& Point = Items[i]->AsArray();
			double Time = 0;
			double Value = 0;
			ERichCurveInterpMode Mode = RCIM_Linear;
			bool bPointOk;
			{
				FPathScope T(0);
				bPointOk = Values::Decode<double>(Point[0], Time);
			}
			{
				FPathScope V(1);
				bPointOk &= Values::Decode<double>(Point[1], Value);
			}
			if (Point.Num() == 3)
			{
				FPathScope M(2);
				FString Text;
				bPointOk &= ExpectString(Point[2], TEXT("an interpolation mode name (Linear, Constant, Cubic)"), Text) && ParseInterpMode(Text, Mode);
			}
			if (!bPointOk)
			{
				bOk = false;
				continue;
			}
			const FKeyHandle Handle = Fresh.AddKey(static_cast<float>(Time), static_cast<float>(Value));
			Fresh.SetKeyInterpMode(Handle, Mode, /*bAutoSetTangents=*/false);
		}
		if (!bOk) { return false; }
		Fresh.AutoSetTangents();
		Out = Fresh;
		return true;
	}

#pragma endregion
}

bool PCGExMediator::Reflect::IncludeAll(const FProperty*)
{
	return true;
}

EPCGMetadataTypes PCGExMediator::Reflect::DialectTypeOf(const UScriptStruct* Struct)
{
	if (!Struct) { return EPCGMetadataTypes::Unknown; }
	if (Struct == TBaseStructure<FVector2D>::Get()) { return EPCGMetadataTypes::Vector2; }
	if (Struct == TBaseStructure<FVector>::Get()) { return EPCGMetadataTypes::Vector; }
	if (Struct == TBaseStructure<FVector4>::Get()) { return EPCGMetadataTypes::Vector4; }
	if (Struct == TBaseStructure<FRotator>::Get()) { return EPCGMetadataTypes::Rotator; }
	if (Struct == TBaseStructure<FQuat>::Get()) { return EPCGMetadataTypes::Quaternion; }
	if (Struct == TBaseStructure<FTransform>::Get()) { return EPCGMetadataTypes::Transform; }
	if (Struct == TBaseStructure<FSoftObjectPath>::Get()) { return EPCGMetadataTypes::SoftObjectPath; }
	if (Struct == TBaseStructure<FSoftClassPath>::Get()) { return EPCGMetadataTypes::SoftClassPath; }
	return EPCGMetadataTypes::Unknown;
}

TSharedPtr<FJsonValue> PCGExMediator::Reflect::EncodeGameplayTag(const FGameplayTag& Tag)
{
	return MakeShared<FJsonValueString>(Tag.IsValid() ? Tag.GetTagName().ToString() : FString());
}

TSharedPtr<FJsonValue> PCGExMediator::Reflect::EncodeGameplayTagContainer(const FGameplayTagContainer& Tags)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Reserve(Tags.Num());
	for (const FGameplayTag& Tag : Tags) { Items.Add(EncodeGameplayTag(Tag)); }
	return MakeShared<FJsonValueArray>(Items);
}

bool PCGExMediator::Reflect::DecodeGameplayTag(const TSharedPtr<FJsonValue>& Json, FGameplayTag& Out)
{
	using namespace PCGExMediatorReflection;

	FString Text;
	if (!ExpectString(Json, TEXT("a gameplay tag name string"), Text)) { return false; }
	Text.TrimStartAndEndInline();
	if (Text.IsEmpty() || Text == TEXT("None"))
	{
		Out = FGameplayTag();
		return true;
	}
	// The tag constructor is the manager's alone: a name the dictionary does not hold cannot become a tag.
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	FText Error;
	FString Fixed;
	if (!Manager.IsValidGameplayTagString(Text, &Error, &Fixed))
	{
		const FString Hint = Fixed.IsEmpty() ? FString() : FString::Printf(TEXT("; did you mean '%s'"), *Fixed);
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a valid tag name: %s%s"), *Text.Left(128), *Error.ToString(), *Hint));
		return false;
	}
	const FGameplayTag Tag = Manager.RequestGameplayTag(FName(*Text), false);
	if (!Tag.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("tag '%s' is not registered in the project's tag dictionary; register it first (the Gameplay Tags toolset's AddTag when that plugin is enabled, else Project Settings > Gameplay Tags), then import again"), *Text.Left(128)));
		return false;
	}
	Out = Tag;
	return true;
}

bool PCGExMediator::Reflect::DecodeGameplayTagContainer(const TSharedPtr<FJsonValue>& Json, FGameplayTagContainer& Out)
{
	if (!Json.IsValid() || Json->Type != EJson::Array)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("expected an array of gameplay tag names"));
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Items = Json->AsArray();
	FGameplayTagContainer Fresh;
	bool bOk = true;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		FPathScope P(i);
		FGameplayTag Tag;
		if (!DecodeGameplayTag(Items[i], Tag)) { bOk = false; }
		else if (Tag.IsValid()) { Fresh.AddTag(Tag); }
	}
	if (!bOk) { return false; }
	Out = MoveTemp(Fresh);
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeGameplayTag()
{
	return Schema::String(TEXT("a gameplay tag name registered in the project (Parent.Child); \"\" for none; an unregistered name is refused"));
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeGameplayTagContainer()
{
	return Schema::Array(DescribeGameplayTag(), TEXT("gameplay tag names; the document replaces the whole container"));
}

#pragma region Attribute selectors

TSharedPtr<FJsonValue> PCGExMediator::Reflect::EncodeAttributeSelector(const FPCGAttributePropertySelector& Selector)
{
	return MakeShared<FJsonValueString>(Selector.ToString());
}

bool PCGExMediator::Reflect::DecodeAttributeSelector(const TSharedPtr<FJsonValue>& Json, FPCGAttributePropertySelector& Out)
{
	using namespace PCGExMediatorReflection;

	FString Text;
	if (!ExpectString(Json, TEXT("a selector string ($Position.X, @Data.Name, Name.X)"), Text)) { return false; }

	// Update parses anything; IsValid is the one check the editor shows red for, so it is the refusal here.
	FPCGAttributePropertySelector Temp = Out;
	Temp.Update(Text);
	if (!Temp.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a valid attribute or property selector"), *Text.Left(128)));
		return false;
	}
	Out = Temp;
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeAttributeSelector()
{
	return Schema::String(TEXT("attribute or property selector as typed in the editor: Name, Name.X, $Position.Z, @Data.Name; \"\" for none"));
}

#pragma endregion

#pragma region Input shorthands

bool PCGExMediator::Reflect::IsInputShorthand(const UScriptStruct* Struct)
{
	PCGExMediatorReflection::FShorthandLayout Layout;
	return PCGExMediatorReflection::ResolveShorthand(Struct, Layout);
}

TSharedPtr<FJsonValue> PCGExMediator::Reflect::EncodeInputShorthand(const UScriptStruct* Struct, const void* Value, const void* Defaults)
{
	using namespace PCGExMediatorReflection;

	FShorthandLayout L;
	if (!ResolveShorthand(Struct, L))
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s is not an input shorthand"), *GetNameSafe(Struct)));
		return nullptr;
	}

	FInstancedStruct FreshDefaults;
	if (!Defaults)
	{
		FreshDefaults.InitializeAs(Struct);
		Defaults = FreshDefaults.GetMemory();
	}

	const bool bAttribute = L.ReadsAttribute(Value);
	bool bExtras = false;
	for (TFieldIterator<FProperty> It(Struct); It && !bExtras; ++It)
	{
		bExtras = !L.IsOwn(*It) && !IdenticalInDialect(*It, Value, Defaults);
	}

	if (!bAttribute && !bExtras)
	{
		return EncodeProperty(L.Constant, L.Constant->ContainerPtrToValuePtr<void>(Value), L.Constant->ContainerPtrToValuePtr<void>(Defaults));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (bAttribute)
	{
		FPathScope P(ShorthandKeys::Attribute);
		if (const TSharedPtr<FJsonValue> Attribute = EncodeProperty(L.Attribute, L.Attribute->ContainerPtrToValuePtr<void>(Value))) { Obj->SetField(ShorthandKeys::Attribute, Attribute); }
		if (!L.Cleanup->GetPropertyValue_InContainer(Value)) { Obj->SetBoolField(ShorthandKeys::Cleanup, false); }
	}
	else
	{
		FPathScope P(ShorthandKeys::Constant);
		if (const TSharedPtr<FJsonValue> Constant = EncodeProperty(L.Constant, L.Constant->ContainerPtrToValuePtr<void>(Value))) { Obj->SetField(ShorthandKeys::Constant, Constant); }
	}
	EncodeStruct(Struct, Value, Defaults, *Obj, [&L](const FProperty* Property) { return !L.IsOwn(Property); });
	return MakeShared<FJsonValueObject>(Obj);
}

bool PCGExMediator::Reflect::DecodeInputShorthand(const UScriptStruct* Struct, void* Value, const TSharedPtr<FJsonValue>& Json)
{
	using namespace PCGExMediatorReflection;

	FShorthandLayout L;
	if (!ResolveShorthand(Struct, L))
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s is not an input shorthand"), *GetNameSafe(Struct)));
		return false;
	}
	if (!Json.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("missing value"));
		return false;
	}

	// Merge into a copy so a failing key leaves the live struct whole.
	FInstancedStruct Temp;
	Temp.InitializeAs(Struct, static_cast<const uint8*>(Value));
	void* Memory = Temp.GetMutableMemory();

	auto DecodeConstant = [&](const TSharedPtr<FJsonValue>& ConstantJson)
	{
		void* Constant = L.Constant->ContainerPtrToValuePtr<void>(Memory);
		if (!DecodeProperty(L.Constant, Constant, ConstantJson) || !WithinClamps(L.Constant, Constant)) { return false; }
		L.SetInput(Memory, EPCGExInputValueType::Constant);
		return true;
	};

	bool bOk = true;
	if (Json->Type != EJson::Object)
	{
		bOk = DecodeConstant(Json);
	}
	else
	{
		const FJsonObject& Obj = *Json->AsObject();
		// FJsonObject keys compare case-insensitively (TMap<FString>), so a reflected "Constant" would read as the
		// dialect's "constant": the reflected shape is told apart by its exact-case property names first.
		auto HasExact = [&Obj](const FString& Key)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>> Pair : Obj.Values) { if (Pair.Key.Equals(Key, ESearchCase::CaseSensitive)) { return true; } }
			return false;
		};
		const bool bReflected = HasExact(L.Input->GetName()) || HasExact(L.Constant->GetName()) || HasExact(L.Attribute->GetName()) || HasExact(L.Cleanup->GetName());
		const TSharedPtr<FJsonValue> Attribute = bReflected ? nullptr : Obj.TryGetField(ShorthandKeys::Attribute);
		const TSharedPtr<FJsonValue> Constant = bReflected ? nullptr : Obj.TryGetField(ShorthandKeys::Constant);
		const TSharedPtr<FJsonValue> Cleanup = bReflected ? nullptr : Obj.TryGetField(ShorthandKeys::Cleanup);
		const bool bDialect = Attribute.IsValid() || Constant.IsValid() || Cleanup.IsValid();

		if (bReflected)
		{
			// The reflected shape, as documents written before the dialect carry it.
			bOk = DecodeStruct(Struct, Memory, Obj);
		}
		else if (!bDialect)
		{
			// Neither form: the object is the constant itself (a Transform's { location, rotation, scale }).
			bOk = DecodeConstant(Json);
		}
		else
		{
			if (Attribute.IsValid() && Constant.IsValid())
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("one of \"attribute\" or \"constant\", not both"));
				return false;
			}
			if (Attribute.IsValid())
			{
				FPathScope P(ShorthandKeys::Attribute);
				if (DecodeProperty(L.Attribute, L.Attribute->ContainerPtrToValuePtr<void>(Memory), Attribute)) { L.SetInput(Memory, EPCGExInputValueType::Attribute); }
				else { bOk = false; }
			}
			if (Constant.IsValid())
			{
				FPathScope P(ShorthandKeys::Constant);
				bOk &= DecodeConstant(Constant);
			}
			if (Cleanup.IsValid())
			{
				FPathScope P(ShorthandKeys::Cleanup);
				bool bCleanup = true;
				if (Values::Decode<bool>(Cleanup, bCleanup)) { L.Cleanup->SetPropertyValue_InContainer(Memory, bCleanup); }
				else { bOk = false; }
			}
			const FString OwnKeys[] = {ShorthandKeys::Attribute, ShorthandKeys::Constant, ShorthandKeys::Cleanup};
			bOk &= DecodeStruct(Struct, Memory, Obj, [&L](const FProperty* Property) { return !L.IsOwn(Property); }, OwnKeys);
		}
	}

	if (!bOk) { return false; }
	Struct->CopyScriptStruct(Value, Memory);
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeInputShorthand(const UScriptStruct* Struct, const int32 Depth)
{
	using namespace PCGExMediatorReflection;

	FShorthandLayout L;
	if (!ResolveShorthand(Struct, L)) { return nullptr; }

	TSharedPtr<FJsonObject> ConstantShape = DescribeProperty(L.Constant, Depth);
	if (!ConstantShape.IsValid()) { ConstantShape = Schema::Typed(nullptr); }
	AddPropertyMetadata(L.Constant, *ConstantShape);

	TSharedPtr<FJsonObject> AttributeShape = DescribeProperty(L.Attribute, Depth);
	if (!AttributeShape.IsValid()) { AttributeShape = Schema::String(); }

	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(ShorthandKeys::Attribute, AttributeShape.ToSharedRef());
	Props->SetObjectField(ShorthandKeys::Constant, ConstantShape.ToSharedRef());
	Props->SetObjectField(ShorthandKeys::Cleanup, Schema::Boolean(TEXT("whether the consuming node may delete the attribute under its Cleanup Consumable Attributes toggle; default true")));
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (L.IsOwn(*It)) { continue; }
		if (const TSharedPtr<FJsonObject> S = DescribeProperty(*It, Depth))
		{
			AddPropertyMetadata(*It, *S);
			Props->SetObjectField(It->GetName(), S.ToSharedRef());
		}
	}
	const TSharedRef<FJsonObject> ObjectForm = Schema::Object(Props, TEXT("\"attribute\" reads the value from the input data; \"constant\" is the bare value with other members set"));

	return Schema::OneOf({ConstantShape, ObjectForm}, FString::Printf(TEXT("%s: a bare value is a constant; { \"attribute\": ... } reads it from the input data"), *Struct->GetName()));
}

#pragma endregion

#pragma region Weight curves

TSharedPtr<FJsonValue> PCGExMediator::Reflect::EncodeWeightCurve(const FPCGExWeightCurve& Curve)
{
	using namespace PCGExMediatorReflection;

	if (Curve.Curve.ExternalCurve)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(CurveKeys::Asset, Curve.Curve.ExternalCurve->GetPathName());
		return MakeShared<FJsonValueObject>(Obj);
	}

	TArray<TSharedPtr<FJsonValue>> Points;
	const TArray<FRichCurveKey>& Keys = Curve.Curve.EditorCurveData.GetConstRefOfKeys();
	Points.Reserve(Keys.Num());
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const FRichCurveKey& Key = Keys[i];
		TArray<TSharedPtr<FJsonValue>> Point;
		Point.Add(MakeShared<FJsonValueNumber>(Key.Time));
		Point.Add(MakeShared<FJsonValueNumber>(Key.Value));
		if (Key.InterpMode != RCIM_Linear)
		{
			const TCHAR* Mode = InterpModeName(Key.InterpMode.GetValue());
			if (Mode) { Point.Add(MakeShared<FJsonValueString>(Mode)); }
			else
			{
				FPathScope P(i);
				Report(EPCGExMediatorSeverity::Warning, TEXT("unknown interpolation mode written as Linear"));
			}
		}
		Points.Add(MakeShared<FJsonValueArray>(Point));
	}
	return MakeShared<FJsonValueArray>(Points);
}

bool PCGExMediator::Reflect::DecodeWeightCurve(const TSharedPtr<FJsonValue>& Json, FPCGExWeightCurve& Out)
{
	using namespace PCGExMediatorReflection;

	if (!Json.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("missing value"));
		return false;
	}

	FPCGExWeightCurve Temp = Out;
	if (Json->Type == EJson::Array)
	{
		if (!DecodeCurvePoints(Json->AsArray(), Temp.Curve.EditorCurveData)) { return false; }
		Temp.Curve.ExternalCurve = nullptr;
	}
	else if (Json->Type == EJson::Object)
	{
		const FJsonObject& Obj = *Json->AsObject();
		const TSharedPtr<FJsonValue> Asset = Obj.TryGetField(CurveKeys::Asset);
		const TSharedPtr<FJsonValue> Points = Obj.TryGetField(CurveKeys::Points);
		if (!Asset.IsValid() && !Points.IsValid())
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected [[t, v], ...] or { \"asset\": <CurveFloat path>, \"points\": [[t, v], ...] }"));
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>> Pair : Obj.Values)
		{
			if (Pair.Key != CurveKeys::Asset && Pair.Key != CurveKeys::Points) { Report(EPCGExMediatorSeverity::Warning, Pair.Key.Left(128), TEXT("not a weight curve key; ignored")); }
		}
		if (Points.IsValid())
		{
			FPathScope P(CurveKeys::Points);
			if (Points->Type != EJson::Array)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("expected an array of [t, v] points"));
				return false;
			}
			if (!DecodeCurvePoints(Points->AsArray(), Temp.Curve.EditorCurveData)) { return false; }
		}
		if (Asset.IsValid())
		{
			FPathScope P(CurveKeys::Asset);
			FString Path;
			if (!ExpectString(Asset, TEXT("a CurveFloat asset path"), Path)) { return false; }
			UCurveFloat* CurveAsset = nullptr;
			if (!Path.IsEmpty())
			{
				UObject* Object = ResolveObject(Path);
				CurveAsset = Cast<UCurveFloat>(Object);
				if (!CurveAsset)
				{
					Report(EPCGExMediatorSeverity::Error, Object ? FString::Printf(TEXT("'%s' is not a CurveFloat"), *Path) : FString::Printf(TEXT("'%s' not found"), *Path));
					return false;
				}
			}
			Temp.Curve.ExternalCurve = CurveAsset;
		}
	}
	else
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("expected [[t, v], ...] or { \"asset\": <CurveFloat path> }"));
		return false;
	}

	Out = Temp;
	return true;
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeWeightCurve()
{
	using namespace PCGExMediatorReflection;

	const TSharedRef<FJsonObject> Point = Schema::Array(
		Schema::Typed(nullptr, TEXT("t, value, then optionally the interpolation to the next key: Linear (default), Constant or Cubic")),
		TEXT("[t, v] or [t, v, \"Cubic\"]"), 2, 3);
	const TSharedRef<FJsonObject> Points = Schema::Array(Point, TEXT("inline keys over t in [0, 1]; cubic keys take auto tangents"));

	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(CurveKeys::Asset, Schema::String(TEXT("CurveFloat asset path; set, it is evaluated instead of the inline keys; \"\" for none")));
	Props->SetObjectField(CurveKeys::Points, Points);

	return Schema::OneOf({Points, Schema::Object(Props)}, TEXT("weight multiplier curve: inline points [[t, v], ...] (a bare array also clears any asset), or { \"asset\": ... }; the default is the single flat key [[0, 1]]"));
}

bool PCGExMediator::Reflect::WeightCurvesIdentical(const FPCGExWeightCurve& A, const FPCGExWeightCurve& B)
{
	if (A.Curve.ExternalCurve != B.Curve.ExternalCurve) { return false; }
	if (A.Curve.ExternalCurve) { return true; }

	const TArray<FRichCurveKey>& KeysA = A.Curve.EditorCurveData.GetConstRefOfKeys();
	const TArray<FRichCurveKey>& KeysB = B.Curve.EditorCurveData.GetConstRefOfKeys();
	if (KeysA.Num() != KeysB.Num()) { return false; }
	for (int32 i = 0; i < KeysA.Num(); ++i)
	{
		if (KeysA[i].Time != KeysB[i].Time || KeysA[i].Value != KeysB[i].Value || KeysA[i].InterpMode != KeysB[i].InterpMode) { return false; }
	}
	return true;
}

#pragma endregion

TSharedPtr<FJsonValue> PCGExMediator::Reflect::EncodeProperty(const FProperty* Property, const void* ValuePtr, const void* DefaultValuePtr)
{
	using namespace PCGExMediatorReflection;

	if (Property->ArrayDim != 1) { return EngineEncode(Property, ValuePtr); }

	if (const FObjectProperty* ObjectProperty = AsPlainObjectRef(Property))
	{
		const UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		return MakeShared<FJsonValueString>(Object ? Object->GetPathName() : FString());
	}
	if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
	{
		return MakeShared<FJsonValueString>(SoftProperty->GetPropertyValue(ValuePtr).ToSoftObjectPath().ToString());
	}

	const FNumericProperty* Underlying = nullptr;
	if (const UEnum* Enum = EnumOf(Property, Underlying))
	{
		return Values::EncodeEnum(Enum, Underlying->GetSignedIntPropertyValue(ValuePtr));
	}
	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(ValuePtr));
	}
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		if (Numeric->IsFloatingPoint()) { return MakeShared<FJsonValueNumber>(Numeric->GetFloatingPointPropertyValue(ValuePtr)); }
		return Values::Encode<int64>(Numeric->GetSignedIntPropertyValue(ValuePtr));
	}
	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(ValuePtr));
	}
	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return Values::Encode<FName>(NameProperty->GetPropertyValue(ValuePtr));
	}
	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(ValuePtr).ToString());
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		const UScriptStruct* Struct = StructProperty->Struct;
		if (const EPCGMetadataTypes Type = DialectTypeOf(Struct); Type != EPCGMetadataTypes::Unknown)
		{
			return Values::Encode(Type, ValuePtr);
		}
		if (Struct == FGameplayTag::StaticStruct()) { return EncodeGameplayTag(*static_cast<const FGameplayTag*>(ValuePtr)); }
		if (Struct == FGameplayTagContainer::StaticStruct()) { return EncodeGameplayTagContainer(*static_cast<const FGameplayTagContainer*>(ValuePtr)); }
		if (Struct == FPCGExWeightCurve::StaticStruct()) { return EncodeWeightCurve(*static_cast<const FPCGExWeightCurve*>(ValuePtr)); }
		if (Struct->IsChildOf(FPCGAttributePropertySelector::StaticStruct())) { return EncodeAttributeSelector(*static_cast<const FPCGAttributePropertySelector*>(ValuePtr)); }
		if (IsInputShorthand(Struct)) { return EncodeInputShorthand(Struct, ValuePtr, DefaultValuePtr); }
		if (const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct))
		{
			TSharedPtr<FJsonObject> Body;
			switch (ConverterToJson(*Format->Converter, ValuePtr, Body))
			{
			case EConverterOutcome::Done: return MakeShared<FJsonValueObject>(Body);
			case EConverterOutcome::Failed: return nullptr;
			default: break;
			}
		}
		if (Struct == FInstancedStruct::StaticStruct()) { return EngineEncode(Property, ValuePtr); }

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		EncodeStruct(Struct, ValuePtr, DefaultValuePtr, *Out);
		return MakeShared<FJsonValueObject>(Out);
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Items;
		Items.Reserve(Helper.Num());
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			FPathScope P(i);
			TSharedPtr<FJsonValue> Item = EncodeProperty(ArrayProperty->Inner, Helper.GetRawPtr(i));
			Items.Add(Item.IsValid() ? Item : MakeShared<FJsonValueNull>());
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper Helper(SetProperty, ValuePtr);
		TArray<TSharedPtr<FJsonValue>> Items;
		for (FScriptSetHelper::FIterator It(Helper); It; ++It)
		{
			TSharedPtr<FJsonValue> Item = EncodeProperty(SetProperty->ElementProp, Helper.GetElementPtr(It));
			if (Item.IsValid()) { Items.Add(Item); }
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	if (Property->IsA<FMapProperty>() || Property->IsA<FObjectProperty>())
	{
		return EngineEncode(Property, ValuePtr);
	}
	return nullptr;
}

bool PCGExMediator::Reflect::DecodeProperty(const FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Json)
{
	using namespace PCGExMediatorReflection;

	if (!Json.IsValid())
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("missing value"));
		return false;
	}
	if (Property->ArrayDim != 1) { return EngineDecode(Property, ValuePtr, Json); }

	if (const FObjectProperty* ObjectProperty = AsPlainObjectRef(Property))
	{
		FString Text;
		if (!ExpectString(Json, TEXT("an object path string"), Text)) { return false; }
		UObject* Object = nullptr;
		if (!Text.IsEmpty())
		{
			Object = ResolveObject(Text);
			if (!Object)
			{
				Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' not found"), *Text));
				return false;
			}
			if (!Object->IsA(ObjectProperty->PropertyClass))
			{
				Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a %s"), *Text, *ObjectProperty->PropertyClass->GetName()));
				return false;
			}
		}
		ObjectProperty->SetObjectPropertyValue(ValuePtr, Object);
		return true;
	}
	if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
	{
		FSoftObjectPath Path;
		if (!Values::Decode<FSoftObjectPath>(Json, Path)) { return false; }
		SoftProperty->SetPropertyValue(ValuePtr, FSoftObjectPtr(Path));
		return true;
	}

	const FNumericProperty* Underlying = nullptr;
	if (const UEnum* Enum = EnumOf(Property, Underlying))
	{
		int64 Value = 0;
		if (!Values::DecodeEnum(Enum, Json, Value)) { return false; }
		Underlying->SetIntPropertyValue(ValuePtr, Value);
		return true;
	}
	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		bool bValue = false;
		if (!Values::Decode<bool>(Json, bValue)) { return false; }
		BoolProperty->SetPropertyValue(ValuePtr, bValue);
		return true;
	}
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		if (Numeric->IsFloatingPoint())
		{
			double Value = 0;
			if (!Values::Decode<double>(Json, Value)) { return false; }
			Numeric->SetFloatingPointPropertyValue(ValuePtr, Value);
			return true;
		}
		int64 Value = 0;
		if (!Values::Decode<int64>(Json, Value)) { return false; }
		if (!Numeric->CanHoldValue(Value))
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%lld does not fit a %s"), Value, *Numeric->GetCPPType()));
			return false;
		}
		Numeric->SetIntPropertyValue(ValuePtr, Value);
		return true;
	}
	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		FString Text;
		if (!ExpectString(Json, TEXT("a string"), Text)) { return false; }
		StringProperty->SetPropertyValue(ValuePtr, Text);
		return true;
	}
	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		FName Name;
		if (!Values::Decode<FName>(Json, Name)) { return false; }
		NameProperty->SetPropertyValue(ValuePtr, Name);
		return true;
	}
	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		FString Text;
		if (!ExpectString(Json, TEXT("a string"), Text)) { return false; }
		TextProperty->SetPropertyValue(ValuePtr, FText::FromString(Text));
		return true;
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		const UScriptStruct* Struct = StructProperty->Struct;
		if (const EPCGMetadataTypes Type = DialectTypeOf(Struct); Type != EPCGMetadataTypes::Unknown)
		{
			return Values::Decode(Json, Type, ValuePtr);
		}
		if (Struct == FGameplayTag::StaticStruct()) { return DecodeGameplayTag(Json, *static_cast<FGameplayTag*>(ValuePtr)); }
		if (Struct == FGameplayTagContainer::StaticStruct()) { return DecodeGameplayTagContainer(Json, *static_cast<FGameplayTagContainer*>(ValuePtr)); }
		if (Struct == FPCGExWeightCurve::StaticStruct()) { return DecodeWeightCurve(Json, *static_cast<FPCGExWeightCurve*>(ValuePtr)); }
		if (Struct->IsChildOf(FPCGAttributePropertySelector::StaticStruct())) { return DecodeAttributeSelector(Json, *static_cast<FPCGAttributePropertySelector*>(ValuePtr)); }
		if (IsInputShorthand(Struct)) { return DecodeInputShorthand(Struct, ValuePtr, Json); }
		if (const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct))
		{
			if (Json->Type != EJson::Object)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("expected an object"));
				return false;
			}
			switch (ConverterFromJson(*Format->Converter, ValuePtr, Json->AsObject()))
			{
			case EConverterOutcome::Done: return true;
			case EConverterOutcome::Failed: return false;
			default: break;
			}
		}
		if (Struct == FInstancedStruct::StaticStruct()) { return EngineDecode(Property, ValuePtr, Json); }

		if (Json->Type != EJson::Object)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected an object"));
			return false;
		}
		// Merge into a copy so a failing key leaves the live struct whole.
		FInstancedStruct Temp;
		Temp.InitializeAs(Struct, static_cast<const uint8*>(ValuePtr));
		if (!DecodeStruct(Struct, Temp.GetMutableMemory(), *Json->AsObject())) { return false; }
		Struct->CopyScriptStruct(ValuePtr, Temp.GetMemory());
		return true;
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		if (Json->Type != EJson::Array)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected an array"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Items = Json->AsArray();
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		// Resize in place like the engine: surviving rows keep their state and merge, new rows start at default.
		Helper.Resize(Items.Num());
		bool bOk = true;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			FPathScope P(i);
			if (!DecodeProperty(ArrayProperty->Inner, Helper.GetRawPtr(i), Items[i])) { bOk = false; }
		}
		return bOk;
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		if (Json->Type != EJson::Array)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected an array"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Items = Json->AsArray();
		FScriptSetHelper Helper(SetProperty, ValuePtr);
		Helper.EmptyElements(Items.Num());
		bool bOk = true;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			FPathScope P(i);
			const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
			if (!DecodeProperty(SetProperty->ElementProp, Helper.GetElementPtr(Index), Items[i])) { bOk = false; }
		}
		Helper.Rehash();
		return bOk;
	}
	if (Property->IsA<FMapProperty>() || Property->IsA<FObjectProperty>())
	{
		return EngineDecode(Property, ValuePtr, Json);
	}

	Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s has no JSON representation"), *Property->GetClass()->GetName()));
	return false;
}

void PCGExMediator::Reflect::EncodeStruct(const UStruct* Struct, const void* Memory, const void* Defaults, FJsonObject& Out, FPropertyFilter Filter)
{
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Filter(Property)) { continue; }
		if (Defaults && PCGExMediatorReflection::IdenticalInDialect(Property, Memory, Defaults)) { continue; }

		FPathScope P(Property->GetName());
		const TSharedPtr<FJsonValue> Value = EncodeProperty(Property, Property->ContainerPtrToValuePtr<void>(Memory), Defaults ? Property->ContainerPtrToValuePtr<void>(Defaults) : nullptr);
		if (!Value.IsValid())
		{
			Report(EPCGExMediatorSeverity::Warning, TEXT("no JSON representation; omitted"));
			continue;
		}
		Out.SetField(Property->GetName(), Value);
	}
}

bool PCGExMediator::Reflect::DecodeStruct(const UStruct* Struct, void* Memory, const FJsonObject& In, FPropertyFilter Filter, TConstArrayView<FString> IgnoredKeys)
{
	using namespace PCGExMediatorReflection;

	bool bOk = true;
	for (const TPair<FString, TSharedPtr<FJsonValue>> Pair : In.Values)
	{
		if (IgnoredKeys.Contains(Pair.Key)) { continue; }
		FPathScope P(Pair.Key);
		const FProperty* Property = FindFProperty<FProperty>(Struct, *Pair.Key);
		if (!Property || !Filter(Property))
		{
			Report(EPCGExMediatorSeverity::Warning, TEXT("not an authored field; ignored"));
			continue;
		}
		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Memory);
		if (!DecodeProperty(Property, ValuePtr, Pair.Value) || !WithinClamps(Property, ValuePtr)) { bOk = false; }
	}
	return bOk;
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeProperty(const FProperty* Property, const int32 Depth)
{
	using namespace PCGExMediatorReflection;

	if (AsPlainObjectRef(Property))
	{
		const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
		return Schema::String(FString::Printf(TEXT("object path of a %s; \"\" for none"), *ObjectProperty->PropertyClass->GetName()));
	}
	if (const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
	{
		return Schema::String(FString::Printf(TEXT("class path of a %s subclass; \"\" for none"), *GetNameSafe(SoftClass->MetaClass)));
	}
	if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
	{
		return Schema::String(FString::Printf(TEXT("object path of a %s; \"\" for none"), *GetNameSafe(SoftProperty->PropertyClass)));
	}

	const FNumericProperty* Underlying = nullptr;
	if (const UEnum* Enum = EnumOf(Property, Underlying)) { return Values::DescribeEnumShape(Enum); }
	if (Property->IsA<FBoolProperty>()) { return Schema::Boolean(); }
	if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
	{
		return Numeric->IsFloatingPoint() ? Schema::Number() : Schema::Integer();
	}
	if (Property->IsA<FStrProperty>() || Property->IsA<FTextProperty>()) { return Schema::String(); }
	if (Property->IsA<FNameProperty>()) { return Values::DescribeShape(EPCGMetadataTypes::Name); }

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		const UScriptStruct* Struct = StructProperty->Struct;
		if (const EPCGMetadataTypes Type = DialectTypeOf(Struct); Type != EPCGMetadataTypes::Unknown) { return Values::DescribeShape(Type); }
		if (Struct == FGameplayTag::StaticStruct()) { return DescribeGameplayTag(); }
		if (Struct == FGameplayTagContainer::StaticStruct()) { return DescribeGameplayTagContainer(); }
		if (Struct == FPCGExWeightCurve::StaticStruct()) { return DescribeWeightCurve(); }
		if (Struct->IsChildOf(FPCGAttributePropertySelector::StaticStruct())) { return DescribeAttributeSelector(); }
		if (IsInputShorthand(Struct)) { return DescribeInputShorthand(Struct, Depth); }
		if (const TSharedPtr<const FPCGExMediatorFormat> Format = FPCGExMediatorRegistry::FindFormatForStruct(Struct); Format.IsValid() && Format->Describe)
		{
			TSharedPtr<FJsonObject> S = Format->Describe();
			if (S.IsValid()) { Schema::Comment(S.ToSharedRef(), FString::Printf(TEXT("format %s v%d"), *Format->Id.ToString(), Format->Version)); }
			return S;
		}
		if (Struct == FInstancedStruct::StaticStruct())
		{
			return Schema::Typed(TEXT("object"), TEXT("instanced struct: \"_structType\" (struct path, deriving from the property's BaseStruct) plus its fields in the engine's reflected shape; naming a type other than the held one resets the value to that type's defaults before the fields apply"));
		}
		if (Depth <= 0)
		{
			// Collapsed: a descriptor tree is tens of KB; the fragment names where the full shape is.
			return Schema::Comment(
				Schema::Typed(TEXT("object"), FString::Printf(TEXT("%s: fields by UPROPERTY name, collapsed for size"), *Struct->GetName())),
				FString::Printf(TEXT("DescribeFormat(\"%s\") lists them"), *Struct->GetPathName()));
		}
		TSharedPtr<FJsonObject> S = DescribeStruct(Struct, &IncludeAll, Depth - 1);
		if (S.IsValid()) { Schema::Describe(S.ToSharedRef(), Struct->GetName()); }
		return S;
	}
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		return Schema::Array(DescribeProperty(ArrayProperty->Inner, Depth), TEXT("rows merge by position: row i of the document into row i of the host, extra rows are created, missing rows removed"));
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		return Schema::Array(DescribeProperty(SetProperty->ElementProp, Depth), TEXT("set; the document replaces the whole set"));
	}
	if (Property->IsA<FMapProperty>()) { return Schema::Typed(TEXT("object"), TEXT("map in the engine's reflected shape")); }
	if (Property->IsA<FObjectProperty>()) { return Schema::Typed(TEXT("object"), TEXT("instanced object in the engine's reflected shape")); }
	return Schema::Typed(nullptr, FString::Printf(TEXT("%s: not representable"), *Property->GetClass()->GetName()));
}

TSharedPtr<FJsonObject> PCGExMediator::Reflect::DescribeStruct(const UStruct* Struct, FPropertyFilter Filter, const int32 Depth)
{
	using namespace PCGExMediatorReflection;

	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Filter(Property)) { continue; }
		if (const TSharedPtr<FJsonObject> S = DescribeProperty(Property, Depth))
		{
			AddPropertyMetadata(Property, *S);
			Props->SetObjectField(Property->GetName(), S.ToSharedRef());
		}
	}
	return Schema::Object(Props);
}
