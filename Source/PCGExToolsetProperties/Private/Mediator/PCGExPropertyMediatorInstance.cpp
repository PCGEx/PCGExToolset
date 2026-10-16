// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorInstance.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExProperty.h"
#include "PCGExPropertySchema.h"
#include "PCGExPropertySchemaAsset.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Mediator/PCGExPropertyMediatorChoices.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "Mediator/PCGExPropertyTypeCatalog.h"
#include "StructUtils/InstancedStruct.h"

TSharedRef<FJsonObject> PCGExPropertyMediator::DescribeSchemaInstance(const FPCGExPropertySchemaCollection& Collection, const TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain)
{
	using namespace PCGExMediator;

	TArray<FPCGExPropertyResolved> Resolved;
	Collection.Resolve(Resolved, FallbackChain);

	TArray<TSharedPtr<FJsonValue>> Properties;
	Properties.Reserve(Resolved.Num());
	{
		FPathScope PropertiesPath(TEXT("properties"));
		for (int32 i = 0; i < Resolved.Num(); ++i)
		{
			FPathScope P(i);
			const FPCGExPropertyResolved& Entry = Resolved[i];

			// The effective value carrying the effective list, so the choices codec reads it like a declaration.
			const FInstancedStruct Effective = Entry.MakeEffectiveCopy();
			const FPCGExProperty* Property = Effective.GetPtr<FPCGExProperty>();
			if (!Property) { continue; }

			const UScriptStruct* Struct = Effective.GetScriptStruct();
			const FPCGExPropertyTypeInfo* Info = PCGExPropertyCatalog::FindByStruct(Struct);
			const FPCGExPropertyMediatorHooks* Hooks = FindHooks(Struct);

			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Entry.Source->Name.ToString());
			Obj->SetStringField(TEXT("type"), (Info && !Info->Entry.TypeName.IsNone()) ? Info->Entry.TypeName.ToString() : Struct->GetPathName());
			Obj->SetStringField(TEXT("declaredBy"), Entry.OwningAsset ? Entry.OwningAsset->GetPathName() : TEXT("local"));
			Obj->SetBoolField(TEXT("overridden"), Entry.OverrideValue != nullptr);
			if (Hooks && Hooks->HasValue())
			{
				if (const TSharedPtr<FJsonValue> Default = Hooks->EncodeValue(*Property)) { Obj->SetField(TEXT("default"), Default); }
			}
			PCGExPropertyMediatorChoices::EncodeChoices(*Property, Hooks, *Obj);
			if (!Entry.ChoicePatches.IsEmpty()) { Obj->SetBoolField(TEXT("patched"), true); }
			Properties.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Imports;
	for (const TObjectPtr<UPCGExPropertySchemaAsset>& Asset : Collection.ImportedSchemas)
	{
		if (Asset) { Imports.Add(MakeShared<FJsonValueString>(Asset->GetPathName())); }
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("properties"), Properties);
	Root->SetArrayField(TEXT("imports"), Imports);
	return Root;
}
