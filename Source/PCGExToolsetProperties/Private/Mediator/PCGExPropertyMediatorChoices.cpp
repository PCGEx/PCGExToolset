// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorChoices.h"

#include "PCGExMediatorSchema.h"
#include "PCGExMediatorValues.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StructUtils/InstancedStruct.h"

namespace PCGExPropertyMediatorChoices
{
	using namespace PCGExMediator;

	/** A property's Value as the agent writes it (its JSON form), else the editor preview text. */
	FString Preview(const FInstancedStruct& Property)
	{
		const FPCGExProperty* Typed = Property.GetPtr<FPCGExProperty>();
		if (!Typed) { return TEXT("?"); }
		const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Property.GetScriptStruct());
		const TSharedPtr<FJsonValue> Json = (Hooks && Hooks->HasValue()) ? Hooks->EncodeValue(*Typed) : nullptr;
		if (!Json.IsValid()) { return PCGExProperties::GetValuePreviewText(FConstStructView(Property)).ToString(); }
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		// An empty identifier outside an object writes the bare value.
		FJsonSerializer::Serialize(Json, FString(), Writer);
		return Out;
	}

	/** "Small (1), Big (2)" */
	FString ListChoices(const FPCGExPropertyChoices& Choices)
	{
		TArray<FString> Parts;
		Parts.Reserve(Choices.Items.Num());
		for (const FPCGExPropertyChoice& Choice : Choices.Items)
		{
			Parts.Add(FString::Printf(TEXT("%s (%s)"), *Choice.Label, *Preview(Choice.Value)));
		}
		return FString::Join(Parts, TEXT(", "));
	}

	/** Index of the one choice labelled Label; INDEX_NONE when none has it, or several (bOutAmbiguous). */
	int32 IndexOfLabel(const FPCGExPropertyChoices& Choices, const FString& Label, bool& bOutAmbiguous)
	{
		bOutAmbiguous = false;
		int32 Found = INDEX_NONE;
		for (int32 i = 0; i < Choices.Items.Num(); ++i)
		{
			if (!Choices.Items[i].Label.Equals(Label, ESearchCase::CaseSensitive)) { continue; }
			if (Found != INDEX_NONE)
			{
				bOutAmbiguous = true;
				return INDEX_NONE;
			}
			Found = i;
		}
		return Found;
	}

	/** Every item of Items without an Id takes the Id of Previous's first unclaimed item carrying its label. */
	void CarryIdsByLabel(const TConstArrayView<FPCGExPropertyChoice> Previous, TArray<FPCGExPropertyChoice>& Items)
	{
		TBitArray<> Claimed(false, Previous.Num());
		for (FPCGExPropertyChoice& Item : Items)
		{
			if (Item.Id.IsValid()) { continue; }
			for (int32 i = 0; i < Previous.Num(); ++i)
			{
				if (!Claimed[i] && Previous[i].Id.IsValid() && Previous[i].Label.Equals(Item.Label, ESearchCase::CaseSensitive))
				{
					Item.Id = Previous[i].Id;
					Claimed[i] = true;
					break;
				}
			}
		}
	}

	/** The list the import row named Name inherits: Inherited is the collection resolved WITHOUT its own layer. */
	bool FindInheritedChoices(const TArray<FPCGExPropertyResolved>& Inherited, const FName Name, FPCGExPropertyChoices& Out)
	{
		for (const FPCGExPropertyResolved& Entry : Inherited)
		{
			if (Entry.OwningAsset && Entry.Source->Name == Name)
			{
				Entry.GetEffectiveChoices(Out);
				return true;
			}
		}
		return false;
	}

	void ResolveInherited(const FPCGExPropertySchemaCollection& Collection, TArray<FPCGExPropertyResolved>& Out)
	{
		const TConstArrayView<const FPCGExPropertyOverrides*> NoLayers;
		Collection.Resolve(Out, NoLayers, /*bIncludeOwnOverrides=*/false);
	}

	/** Items as [{ label, value }], Extra adding a patch item's keys; a type with no value hook writes labels alone, warned once. */
	void EncodeItems(const TArray<FPCGExPropertyChoice>& Items, const FPCGExPropertyMediatorHooks* Hooks, TArray<TSharedPtr<FJsonValue>>& Out, TFunctionRef<void(int32, FJsonObject&)> Extra)
	{
		const bool bHasValue = Hooks && Hooks->HasValue();
		bool bWarned = false;
		Out.Reserve(Items.Num());
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			FPathScope P(i);
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(Keys::Label, Items[i].Label);
			const FPCGExProperty* Carrier = Items[i].Value.GetPtr<FPCGExProperty>();
			const TSharedPtr<FJsonValue> Value = (bHasValue && Carrier) ? Hooks->EncodeValue(*Carrier) : nullptr;
			if (Value.IsValid())
			{
				Obj->SetField(Keys::Value, Value);
			}
			else if (!bWarned)
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("the property type has no JSON value representation; choices carry their label alone"));
				bWarned = true;
			}
			Extra(i, *Obj);
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	/**
	 * Items from [{ label, value?, replaces? }] onto carriers of Host's type. OutReplaces, when given, receives each
	 * item's "replaces" label ("" for none) and is kept parallel to OutItems.
	 */
	bool DecodeItems(const TArray<TSharedPtr<FJsonValue>>& Array, const FInstancedStruct& Host, const FPCGExPropertyMediatorHooks* Hooks, TArray<FPCGExPropertyChoice>& OutItems, TArray<FString>* OutReplaces)
	{
		bool bOk = true;
		TSet<FString> Labels;
		OutItems.Reserve(Array.Num());
		for (int32 i = 0; i < Array.Num(); ++i)
		{
			FPathScope P(i);
			if (!Array[i].IsValid() || Array[i]->Type != EJson::Object)
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("expected an object { \"label\", \"value\" }"));
				bOk = false;
				continue;
			}
			const FJsonObject& Obj = *Array[i]->AsObject();

			FPCGExPropertyChoice& Choice = OutItems.AddDefaulted_GetRef();
			if (OutReplaces) { OutReplaces->AddDefaulted(); }

			const TSharedPtr<FJsonValue> LabelJson = Obj.TryGetField(Keys::Label);
			if (!LabelJson.IsValid() || LabelJson->Type != EJson::String)
			{
				Report(EPCGExMediatorSeverity::Error, Keys::Label, TEXT("expected a string"));
				bOk = false;
				continue;
			}
			Choice.Label = LabelJson->AsString();
			if (Choice.Label.IsEmpty()) { Report(EPCGExMediatorSeverity::Warning, Keys::Label, TEXT("empty label")); }
			bool bDuplicate = false;
			Labels.Add(Choice.Label, &bDuplicate);
			if (bDuplicate) { Report(EPCGExMediatorSeverity::Warning, Keys::Label, FString::Printf(TEXT("'%s' labels several choices; patches name choices by label"), *Choice.Label)); }

			Choice.Value = PCGExProperties::MakeChoiceCarrier(FConstStructView(Host));
			if (!Choice.Value.IsValid())
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("the property type has no Value field to hold a choice"));
				bOk = false;
				continue;
			}
			if (const TSharedPtr<FJsonValue> ValueJson = Obj.TryGetField(Keys::Value))
			{
				FPathScope V(Keys::Value);
				if (!Hooks || !Hooks->HasValue())
				{
					Report(EPCGExMediatorSeverity::Warning, TEXT("the property type accepts no JSON value; the choice keeps the type's default"));
				}
				else if (!Hooks->DecodeValue(Choice.Value.GetMutable<FPCGExProperty>(), ValueJson))
				{
					bOk = false;
					continue;
				}
			}
			if (OutReplaces)
			{
				if (const TSharedPtr<FJsonValue> ReplacesJson = Obj.TryGetField(Keys::Replaces))
				{
					if (ReplacesJson->Type != EJson::String)
					{
						Report(EPCGExMediatorSeverity::Error, Keys::Replaces, TEXT("expected the label of an inherited choice"));
						bOk = false;
						continue;
					}
					OutReplaces->Last() = ReplacesJson->AsString();
				}
			}
		}
		return bOk;
	}

	TSharedRef<FJsonObject> DescribeItem(const bool bPatchItem)
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Label, Schema::String(TEXT("display label; patches and picks name a choice by it, so keep labels unique within a list")));
		Props->SetObjectField(Keys::Value, Schema::Typed(nullptr, TEXT("the choice's value, in the shape of the entry's type (see $defs/values/<type>); absent = the type's default")));
		if (bPatchItem)
		{
			Props->SetObjectField(Keys::Replaces, Schema::String(TEXT("label of the inherited choice this item stands in for under a new label; an item whose own label matches an inherited choice replaces it without this key")));
		}
		const FString Required[] = {Keys::Label};
		return Schema::Object(Props, FString(), Required);
	}
}

void PCGExPropertyMediatorChoices::EncodeChoices(const FPCGExProperty& Property, const FPCGExPropertyMediatorHooks* Hooks, FJsonObject& Entry)
{
	const FPCGExPropertyChoices& Choices = Property.Choices;
	if (Choices.Items.IsEmpty() && !Choices.bLocked) { return; }

	FPathScope P(Keys::Choices);
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(Keys::Locked, Choices.bLocked);
	TArray<TSharedPtr<FJsonValue>> Items;
	{
		FPathScope I(Keys::Items);
		EncodeItems(Choices.Items, Hooks, Items, [](int32, FJsonObject&) {});
	}
	Obj->SetArrayField(Keys::Items, Items);
	Entry.SetObjectField(Keys::Choices, Obj);
}

bool PCGExPropertyMediatorChoices::DecodeChoices(FInstancedStruct& Property, const FJsonObject& Entry, const FPCGExPropertyMediatorHooks* Hooks, const FPCGExPropertyChoices* Previous)
{
	const TSharedPtr<FJsonValue> Json = Entry.TryGetField(Keys::Choices);
	if (!Json.IsValid()) { return true; }

	FPathScope P(Keys::Choices);
	if (Json->Type != EJson::Object)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("expected an object { \"locked\", \"items\" }"));
		return false;
	}
	const FJsonObject& Obj = *Json->AsObject();

	FPCGExPropertyChoices Edited;
	if (const TSharedPtr<FJsonValue> Locked = Obj.TryGetField(Keys::Locked))
	{
		if (Locked->Type != EJson::Boolean)
		{
			Report(EPCGExMediatorSeverity::Error, Keys::Locked, TEXT("expected a boolean"));
			return false;
		}
		Edited.bLocked = Locked->AsBool();
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Items = nullptr; Obj.TryGetArrayField(Keys::Items, Items))
	{
		FPathScope I(Keys::Items);
		if (!DecodeItems(*Items, Property, Hooks, Edited.Items, nullptr)) { return false; }
	}
	else if (Obj.HasField(Keys::Items))
	{
		Report(EPCGExMediatorSeverity::Error, Keys::Items, TEXT("expected an array"));
		return false;
	}

	if (Previous) { CarryIdsByLabel(Previous->Items, Edited.Items); }
	PCGExProperties::CommitDeclaredChoices(FStructView(Property), MoveTemp(Edited));
	return true;
}

bool PCGExPropertyMediatorChoices::IsChoicePick(const TSharedPtr<FJsonValue>& Json)
{
	return Json.IsValid() && Json->Type == EJson::Object && Json->AsObject()->HasField(Keys::Choice);
}

bool PCGExPropertyMediatorChoices::DecodeValueSlot(FInstancedStruct& Property, const TSharedPtr<FJsonValue>& Json, const FPCGExPropertyMediatorHooks* Hooks)
{
	FPCGExProperty* Typed = Property.GetMutablePtr<FPCGExProperty>();
	if (!Typed)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("no property to write"));
		return false;
	}
	if (!IsChoicePick(Json))
	{
		if (!Hooks || !Hooks->HasValue())
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("type %s accepts no JSON value"), *Typed->GetTypeName().ToString()));
			return false;
		}
		return Hooks->DecodeValue(*Typed, Json);
	}

	FPathScope P(Keys::Choice);
	const TSharedPtr<FJsonValue> LabelJson = Json->AsObject()->TryGetField(Keys::Choice);
	if (!LabelJson.IsValid() || LabelJson->Type != EJson::String)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("expected the label of one of the property's choices"));
		return false;
	}
	const FString Label = LabelJson->AsString();
	if (Typed->Choices.Items.IsEmpty())
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' cannot be picked: the property offers no choices"), *Label));
		return false;
	}
	bool bAmbiguous = false;
	const int32 Index = IndexOfLabel(Typed->Choices, Label, bAmbiguous);
	if (Index == INDEX_NONE)
	{
		Report(EPCGExMediatorSeverity::Error, bAmbiguous
			                                      ? FString::Printf(TEXT("'%s' names several choices"), *Label)
			                                      : FString::Printf(TEXT("'%s' is not one of the choices: %s"), *Label, *ListChoices(Typed->Choices)));
		return false;
	}
	if (!PCGExProperties::ApplyChoice(FStructView(Property), Typed->Choices.Items[Index]))
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("choice '%s' does not fit this property"), *Label));
		return false;
	}
	return true;
}

bool PCGExPropertyMediatorChoices::CheckLockedChoices(const FInstancedStruct& Property, const FName PropertyName, const EPCGExMediatorSeverity Severity)
{
	const FPCGExProperty* Typed = Property.GetPtr<FPCGExProperty>();
	if (!Typed || !Typed->Choices.bLocked || Typed->Choices.Items.IsEmpty()) { return true; }
	if (PCGExProperties::FindMatchingChoice(FConstStructView(Property), Typed->Choices) != INDEX_NONE) { return true; }

	Report(Severity, FString::Printf(TEXT("'%s' is not one of the locked choices of %s: %s"), *Preview(Property), *PropertyName.ToString(), *ListChoices(Typed->Choices)));
	return Severity != EPCGExMediatorSeverity::Error;
}

void PCGExPropertyMediatorChoices::EncodePatches(const FPCGExPropertySchemaCollection& Collection, FJsonObject& Out)
{
	TArray<FPCGExPropertyResolved> Inherited;
	bool bResolved = false;
	TSharedPtr<FJsonObject> Patches;

	for (const FPCGExPropertyOverrideEntry& Row : Collection.ImportOverrides.Overrides)
	{
		const FPCGExPropertyChoicesPatch* Patch = Row.GetChoicesPatch();
		const FName Name = Row.GetPropertyName();
		if (!Patch || Name.IsNone()) { continue; }
		if (!bResolved)
		{
			ResolveInherited(Collection, Inherited);
			bResolved = true;
		}

		FPathScope P(Keys::ChoicePatches);
		FPathScope N(Name.ToString());
		FPCGExPropertyChoices InheritedList;
		FindInheritedChoices(Inherited, Name, InheritedList);
		const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Row.Value.GetScriptStruct());

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetField(Keys::Mode, Values::EncodeEnum(StaticEnum<EPCGExChoicesPatchMode>(), static_cast<int64>(Patch->Mode)));
		Obj->SetField(Keys::Lock, Values::EncodeEnum(StaticEnum<EPCGExChoicesLock>(), static_cast<int64>(Patch->Lock)));

		if (!Patch->Items.IsEmpty())
		{
			FPathScope I(Keys::Items);
			TArray<TSharedPtr<FJsonValue>> Items;
			EncodeItems(Patch->Items, Hooks, Items, [&](const int32 Index, FJsonObject& Item)
			{
				const FPCGExPropertyChoice& Choice = Patch->Items[Index];
				const int32 InheritedIndex = PCGExProperties::FindChoiceById(InheritedList, Choice.Id);
				if (InheritedIndex != INDEX_NONE)
				{
					// Same label: the decode re-attaches it by label; a renamed replacement needs the inherited label spelled out.
					const FString& InheritedLabel = InheritedList.Items[InheritedIndex].Label;
					if (!InheritedLabel.Equals(Choice.Label, ESearchCase::CaseSensitive)) { Item.SetStringField(Keys::Replaces, InheritedLabel); }
					return;
				}
				bool bAmbiguous = false;
				if (IndexOfLabel(InheritedList, Choice.Label, bAmbiguous) != INDEX_NONE || bAmbiguous)
				{
					Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("added choice '%s' shares its label with an inherited choice; a re-import replaces that choice instead"), *Choice.Label));
				}
			});
			Obj->SetArrayField(Keys::Items, Items);
		}

		if (!Patch->RemovedIds.IsEmpty())
		{
			FPathScope R(Keys::Remove);
			TArray<TSharedPtr<FJsonValue>> Removed;
			for (const FGuid& Id : Patch->RemovedIds)
			{
				const int32 InheritedIndex = PCGExProperties::FindChoiceById(InheritedList, Id);
				if (InheritedIndex == INDEX_NONE)
				{
					Report(EPCGExMediatorSeverity::Warning, TEXT("a removed choice is no longer inherited; dropped"));
					continue;
				}
				Removed.Add(MakeShared<FJsonValueString>(InheritedList.Items[InheritedIndex].Label));
			}
			Obj->SetArrayField(Keys::Remove, Removed);
		}

		if (!Patches.IsValid()) { Patches = MakeShared<FJsonObject>(); }
		Patches->SetObjectField(Name.ToString(), Obj);
	}

	if (Patches.IsValid()) { Out.SetObjectField(Keys::ChoicePatches, Patches); }
}

bool PCGExPropertyMediatorChoices::DecodePatches(const FJsonObject& In, FPCGExPropertySchemaCollection& Temp, const FPCGExPropertySchemaCollection& Live)
{
	const TSharedPtr<FJsonObject>* PatchesPtr = nullptr;
	if (!In.TryGetObjectField(Keys::ChoicePatches, PatchesPtr))
	{
		if (In.HasField(Keys::ChoicePatches))
		{
			Report(EPCGExMediatorSeverity::Error, Keys::ChoicePatches, TEXT("expected an object keyed by imported property name"));
			return false;
		}
		return true;
	}

	FPathScope P(Keys::ChoicePatches);
	TArray<FPCGExPropertyResolved> Inherited;
	ResolveInherited(Temp, Inherited);

	bool bOk = true;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PatchesPtr)->Values)
	{
		FPathScope N(Pair.Key);
		if (Pair.Key.IsEmpty() || Pair.Key.Len() >= NAME_SIZE)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("not a valid property name"));
			bOk = false;
			continue;
		}
		const FName Name(*Pair.Key);
		FPCGExPropertyOverrideEntry* Row = Temp.ImportOverrides.FindEntryMutableByName(Name);
		if (!Row)
		{
			const bool bLocal = Temp.Schemas.ContainsByPredicate([Name](const FPCGExPropertySchema& Schema) { return Schema.Name == Name; });
			Report(EPCGExMediatorSeverity::Error, bLocal
				                                      ? TEXT("a local property: author its choices on the entry itself")
				                                      : TEXT("not a property of the imported schemas"));
			bOk = false;
			continue;
		}
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
		{
			Report(EPCGExMediatorSeverity::Error, TEXT("expected an object { \"mode\", \"lock\", \"items\", \"remove\" }"));
			bOk = false;
			continue;
		}
		const FJsonObject& Obj = *Pair.Value->AsObject();
		bool bRowOk = true;

		const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
		const bool bHasItems = Obj.TryGetArrayField(Keys::Items, ItemsArray);
		if (!bHasItems && Obj.HasField(Keys::Items))
		{
			Report(EPCGExMediatorSeverity::Error, Keys::Items, TEXT("expected an array"));
			bRowOk = false;
		}
		const TArray<TSharedPtr<FJsonValue>>* RemoveArray = nullptr;
		const bool bHasRemove = Obj.TryGetArrayField(Keys::Remove, RemoveArray);
		if (!bHasRemove && Obj.HasField(Keys::Remove))
		{
			Report(EPCGExMediatorSeverity::Error, Keys::Remove, TEXT("expected an array of inherited choice labels"));
			bRowOk = false;
		}

		FPCGExPropertyChoicesPatch Patch;
		if (const TSharedPtr<FJsonValue> ModeJson = Obj.TryGetField(Keys::Mode))
		{
			FPathScope M(Keys::Mode);
			int64 V = 0;
			if (Values::DecodeEnum(StaticEnum<EPCGExChoicesPatchMode>(), ModeJson, V)) { Patch.Mode = static_cast<EPCGExChoicesPatchMode>(V); }
			else { bRowOk = false; }
		}
		else
		{
			Patch.Mode = (bHasItems || bHasRemove) ? EPCGExChoicesPatchMode::Modify : EPCGExChoicesPatchMode::Inherit;
		}
		if (const TSharedPtr<FJsonValue> LockJson = Obj.TryGetField(Keys::Lock))
		{
			FPathScope L(Keys::Lock);
			int64 V = 0;
			if (Values::DecodeEnum(StaticEnum<EPCGExChoicesLock>(), LockJson, V)) { Patch.Lock = static_cast<EPCGExChoicesLock>(V); }
			else { bRowOk = false; }
		}
		if (!bRowOk)
		{
			bOk = false;
			continue;
		}
		if (Patch.Mode == EPCGExChoicesPatchMode::Inherit && (bHasItems || bHasRemove))
		{
			Report(EPCGExMediatorSeverity::Warning, TEXT("mode is Inherit: items and remove are dropped"));
		}
		else if (Patch.Mode == EPCGExChoicesPatchMode::Replace && bHasRemove)
		{
			Report(EPCGExMediatorSeverity::Warning, TEXT("mode is Replace: remove is dropped"));
		}

		FPCGExPropertyChoices InheritedList;
		FindInheritedChoices(Inherited, Name, InheritedList);
		const FPCGExPropertyMediatorHooks* Hooks = PCGExPropertyMediator::FindHooks(Row->Value.GetScriptStruct());

		if (bHasItems && Patch.Mode != EPCGExChoicesPatchMode::Inherit)
		{
			FPathScope I(Keys::Items);
			TArray<FString> Replaces;
			if (!DecodeItems(*ItemsArray, Row->Value, Hooks, Patch.Items, &Replaces))
			{
				bOk = false;
				continue;
			}
			for (int32 i = 0; i < Patch.Items.Num(); ++i)
			{
				FPathScope J(i);
				const bool bExplicit = !Replaces[i].IsEmpty();
				const FString& Target = bExplicit ? Replaces[i] : Patch.Items[i].Label;
				bool bAmbiguous = false;
				const int32 InheritedIndex = IndexOfLabel(InheritedList, Target, bAmbiguous);
				if (InheritedIndex != INDEX_NONE)
				{
					Patch.Items[i].Id = InheritedList.Items[InheritedIndex].Id;
				}
				else if (bExplicit)
				{
					Report(EPCGExMediatorSeverity::Error, Keys::Replaces, bAmbiguous
						                                                      ? FString::Printf(TEXT("'%s' names several inherited choices"), *Target)
						                                                      : FString::Printf(TEXT("'%s' is not an inherited choice of %s: %s"), *Target, *Pair.Key, *ListChoices(InheritedList)));
					bRowOk = false;
				}
				else if (bAmbiguous)
				{
					Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("'%s' matches several inherited choices; added beside them"), *Target));
				}
			}
		}

		if (bHasRemove && Patch.Mode == EPCGExChoicesPatchMode::Modify)
		{
			FPathScope R(Keys::Remove);
			for (int32 k = 0; k < RemoveArray->Num(); ++k)
			{
				FPathScope K(k);
				const TSharedPtr<FJsonValue>& LabelJson = (*RemoveArray)[k];
				if (!LabelJson.IsValid() || LabelJson->Type != EJson::String)
				{
					Report(EPCGExMediatorSeverity::Error, TEXT("expected the label of an inherited choice"));
					bRowOk = false;
					continue;
				}
				bool bAmbiguous = false;
				const int32 InheritedIndex = IndexOfLabel(InheritedList, LabelJson->AsString(), bAmbiguous);
				if (InheritedIndex == INDEX_NONE)
				{
					Report(EPCGExMediatorSeverity::Error, bAmbiguous
						                                      ? FString::Printf(TEXT("'%s' names several inherited choices"), *LabelJson->AsString())
						                                      : FString::Printf(TEXT("'%s' is not an inherited choice of %s: %s"), *LabelJson->AsString(), *Pair.Key, *ListChoices(InheritedList)));
					bRowOk = false;
					continue;
				}
				Patch.RemovedIds.AddUnique(InheritedList.Items[InheritedIndex].Id);
			}
		}

		if (!bRowOk)
		{
			bOk = false;
			continue;
		}

		// Choices this importer adds keep their identity across a re-import, like a declaration's own.
		if (const FPCGExPropertyOverrideEntry* LiveRow = Live.ImportOverrides.FindEntryByName(Name))
		{
			if (const FPCGExPropertyChoicesPatch* LivePatch = LiveRow->GetChoicesPatch()) { CarryIdsByLabel(LivePatch->Items, Patch.Items); }
		}
		PCGExProperties::CommitChoicesPatch(*Row, MoveTemp(Patch));
	}
	return bOk;
}

TSharedRef<FJsonObject> PCGExPropertyMediatorChoices::DescribeChoices()
{
	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(Keys::Locked, Schema::Boolean(TEXT("editors offer the choices alone; on import a row value outside a locked list is refused, a declaration default outside it is kept with a warning")));
	Props->SetObjectField(Keys::Items, Schema::Array(DescribeItem(false)));
	return Schema::Object(Props, TEXT("favourite values offered by the property's editors; absent = none. Choice ids are internal: a re-import matches choices by label, so importer patches and picks survive it."));
}

TSharedRef<FJsonObject> PCGExPropertyMediatorChoices::DescribePatches()
{
	TSharedRef<FJsonObject> PatchProps = MakeShared<FJsonObject>();
	PatchProps->SetObjectField(Keys::Mode, Schema::Describe(Values::DescribeEnumShape(StaticEnum<EPCGExChoicesPatchMode>()).ToSharedRef(),
	                                                        TEXT("Inherit keeps the imported list, Modify removes then adds, Replace uses items alone; absent = Modify when items or remove is present, else Inherit")));
	PatchProps->SetObjectField(Keys::Lock, Schema::Describe(Values::DescribeEnumShape(StaticEnum<EPCGExChoicesLock>()).ToSharedRef(),
	                                                        TEXT("Inherit keeps the imported lock; Locked / Unlocked override it for this collection's rows")));
	PatchProps->SetObjectField(Keys::Items, Schema::Array(DescribeItem(true), TEXT("this collection's own choices (Modify: added, or replacing an inherited one by label; Replace: the whole list)")));
	PatchProps->SetObjectField(Keys::Remove, Schema::Array(Schema::String(TEXT("label of an inherited choice")), TEXT("inherited choices dropped for this collection's rows (Modify only)")));
	const TSharedRef<FJsonObject> Patch = Schema::Object(PatchProps, TEXT("one imported property's rewrite; applies whether or not the property's value override is enabled. Inherited = the list the imported schema and the assets below it produce."));

	TSharedRef<FJsonObject> S = Schema::Typed(TEXT("object"), TEXT("Per IMPORTED property name, how this collection rewrites its choices for its own rows and importers. Absent = no patch; the document replaces every stored patch, like importOverrides."));
	S->SetObjectField(TEXT("additionalProperties"), Patch);
	return S;
}

FString PCGExPropertyMediatorChoices::ChoiceFormDescription()
{
	return TEXT("A value slot also accepts { \"choice\": \"<label>\" } to pick one of the property's choices and bind to it: a picked row follows later edits of that choice, a plain value equal to it does not. "
		"A plain value outside a LOCKED list is refused. DescribeTarget(<host>) lists each property's effective choices and lock on that host, every import patch applied.");
}
