// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorDump.h"

#include "PCGExLog.h"
#include "PCGExMediatorTransport.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DateTime.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace PCGExMediatorDump
{
	// Attaches a schema to every row of a ListAsJson array under OutField, by the row's id or class field.
	void AttachSchemas(const TSharedRef<FJsonObject>& Root, const TCHAR* ArrayField, const TCHAR* KeyField, const TCHAR* OutField, TFunctionRef<TSharedPtr<FJsonObject>(const FString& Key)> Describe)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Root->TryGetArrayField(ArrayField, Rows)) { return; }
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FString Key;
			if (!Row.IsValid() || !Row->TryGetObject(Entry) || !(*Entry)->TryGetStringField(KeyField, Key)) { continue; }
			if (const TSharedPtr<FJsonObject> Schema = Describe(Key)) { (*Entry)->SetObjectField(OutField, Schema); }
		}
	}
}

TSharedRef<FJsonObject> PCGExMediator::DumpAsJson()
{
	const TSharedRef<FJsonObject> Root = ListAsJson();

	PCGExMediatorDump::AttachSchemas(Root, TEXT("formats"), TEXT("id"), TEXT("schema"), [](const FString& Id) { return DescribeFormat(FName(*Id)); });
	PCGExMediatorDump::AttachSchemas(Root, TEXT("bindings"), TEXT("class"), TEXT("schema"), [](const FString& ClassPath)
	{
		const UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
		return Class ? DescribeObject(Class) : nullptr;
	});
	// The list omits params on purpose; the docs want them, so the dump carries each action's schema.
	PCGExMediatorDump::AttachSchemas(Root, TEXT("actions"), TEXT("id"), TEXT("params"), [](const FString& Id) { return DescribeAction(FName(*Id)); });
	Root->SetArrayField(TEXT("guides"), GuidesAsJson());

	TSharedRef<FJsonObject> Toolkit = MakeShared<FJsonObject>();
	Toolkit->SetStringField(TEXT("plugin"), TEXT("PCGExtendedToolkit"));
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PCGExtendedToolkit")))
	{
		Toolkit->SetStringField(TEXT("version"), Plugin->GetDescriptor().VersionName);
	}

	// Envelope keys first so a reader sees them before the bulk.
	TSharedRef<FJsonObject> Doc = MakeShared<FJsonObject>();
	Doc->SetStringField(TEXT("format"), TEXT("pcgex.mediator-dump"));
	Doc->SetNumberField(TEXT("version"), 1);
	Doc->SetStringField(TEXT("generated"), FDateTime::UtcNow().ToIso8601());
	Doc->SetObjectField(TEXT("toolkit"), Toolkit);
	for (const TPair<FString, TSharedPtr<FJsonValue>> Pair : Root->Values) { Doc->SetField(Pair.Key, Pair.Value); }
	return Doc;
}

bool PCGExMediator::DumpToFile(const FString& FilePath)
{
	if (!WriteFile(FilePath, DumpAsJson()))
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] could not write the dump to '%s'"), *FilePath);
		return false;
	}
	UE_LOG(LogPCGEx, Log, TEXT("[Mediator] dump written to '%s'"), *FilePath);
	return true;
}
