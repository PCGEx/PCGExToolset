// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorActions.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorParams.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorSchema.h"
#include "PCGExPropertyChoicesAlign.h"
#include "PCGExPropertyRowHost.h"
#include "PCGExPropertySchema.h"
#include "PCGExPropertySchemaAsset.h"
#include "Dom/JsonObject.h"

namespace PCGExPropertyMediatorActions
{
	using namespace PCGExMediator;

	namespace Keys
	{
		constexpr const TCHAR* const Property = TEXT("property");
	}

	/** The asset declaring PropertyName as Asset resolves it: Asset itself for a local, the owner of an imported declaration otherwise. */
	const UPCGExPropertySchemaAsset* FindDeclaringAsset(const UPCGExPropertySchemaAsset* Asset, const FName PropertyName)
	{
		TArray<FPCGExPropertyResolved> Resolved;
		Asset->Collection.Resolve(Resolved);
		for (const FPCGExPropertyResolved& Entry : Resolved)
		{
			if (Entry.Source->Name == PropertyName) { return Entry.OwningAsset ? Entry.OwningAsset : Asset; }
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> RunAlignChoices(UObject* Target, const FPCGExMediatorParams& Params)
	{
		FString PropertyText;
		if (!Params.Require(Keys::Property, PropertyText, TEXT("the name of the property whose choices rows align to"))) { return nullptr; }
		if (PropertyText.IsEmpty() || PropertyText.Len() >= NAME_SIZE)
		{
			Params.Report(EPCGExMediatorSeverity::Error, Keys::Property, TEXT("expected a non-empty property name"));
			return nullptr;
		}
		const FName PropertyName(*PropertyText);

		PCGExPropertyChoicesAlign::FResult Result;
		const TCHAR* Scope = nullptr;
		if (UPCGExPropertySchemaAsset* Asset = Cast<UPCGExPropertySchemaAsset>(Target))
		{
			const UPCGExPropertySchemaAsset* Declaring = FindDeclaringAsset(Asset, PropertyName);
			if (!Declaring)
			{
				Params.Report(EPCGExMediatorSeverity::Error, Keys::Property, FString::Printf(TEXT("'%s' is not a property %s declares or imports"), *PropertyText, *Asset->GetPathName()));
				return nullptr;
			}
			// The asset's own import rows are what its importers fall back to: they go first.
			Result += PCGExPropertyChoicesAlign::AlignHostRows(Asset, PropertyName, Declaring);
			Result += PCGExPropertyChoicesAlign::AlignImporters(Asset, PropertyName, Declaring);
			Scope = TEXT("importers");
		}
		else if (Target->GetClass()->ImplementsInterface(UPCGExPropertyRowHost::StaticClass()))
		{
			Result = PCGExPropertyChoicesAlign::AlignHostRows(Target, PropertyName);
			Scope = TEXT("host");
		}
		else
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s holds no property rows (not a schema asset, collection, Tuple node or property component)"), *Target->GetClass()->GetName()));
			return nullptr;
		}

		if (Result.HostsVisited == 0)
		{
			Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("no visited host resolves '%s' with choices; unloaded assets, transient objects and Blueprint templates are not visited"), *PropertyText));
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(Keys::Property, PropertyText);
		Out->SetStringField(TEXT("scope"), Scope);
		Out->SetNumberField(TEXT("rowsAligned"), Result.RowsAligned);
		Out->SetNumberField(TEXT("hostsTouched"), Result.HostsTouched);
		Out->SetNumberField(TEXT("hostsVisited"), Result.HostsVisited);
		Out->SetBoolField(TEXT("changed"), Result.RowsAligned > 0);
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeAlignParams()
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Property, Schema::String(TEXT("name of the property, as the target resolves it")));
		const FString Required[] = {Keys::Property};
		return Schema::Object(Props, TEXT("the property whose choices rows are snapped onto"), Required);
	}
}

void PCGExPropertyMediatorActions::Register(FPCGExMediatorDomain& Domain)
{
	FPCGExMediatorAction Action;
	Action.Id = AlignChoicesActionId;
	// A schema asset or any row host: Run tells them apart.
	Action.TargetClass = nullptr;
	Action.Run = &RunAlignChoices;
	Action.DescribeParams = &DescribeAlignParams;
	Action.Summary = TEXT("Align rows to a property's choices: every enabled row whose value is none of the choices snaps onto the default choice, or the first that fits. A Property Schema asset sweeps its own import rows and every LOADED importer; any other host only its own rows.");
	Domain.AddAction(Action);
}
