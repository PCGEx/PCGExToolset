// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorBindings.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "Collections/PCGExOmniCollection.h"
#include "Collections/PCGExVariantCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Elements/PCGExDistributeTuple.h"
#include "Helpers/PCGExMaterialSwapTable.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "Mediator/PCGExPropertyMediatorInstance.h"
#include "UObject/UnrealType.h"

namespace PCGExCollectionsMediatorBindings
{
	using namespace PCGExMediator;

	TSharedPtr<FJsonObject> DescribeCollectionInstance(const UObject* Host)
	{
		const UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(Host);
		if (!Collection) { return nullptr; }
		return PCGExPropertyMediator::DescribeSchemaInstance(Collection->CollectionProperties);
	}

	// Rows (and the entries Variant rows carry) resolve their overrides against the schema the document carries, else the live one.
	void WrapCollectionImport(UObject* Host, const FName Member, FPCGExMediatorDecodedMember Decoded, TFunctionRef<void()> Import)
	{
		const UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(Host);
		if (Collection && Member != GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties))
		{
			const FPCGExPropertySchemaCollection* Schema = static_cast<const FPCGExPropertySchemaCollection*>(Decoded(GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties)));
			PCGExPropertyMediator::FOverridesSchemaScope Scope((Schema ? Schema : &Collection->CollectionProperties)->BuildSchema());
			Import();
			return;
		}
		Import();
	}

	// Warnings match what the Variant editor flags; only a path the registry knows is not a material refuses the import.
	void AuditMaterialSwaps(const TArray<FPCGExMaterialSwap>& InSwaps)
	{
		FPathScope Member(GET_MEMBER_NAME_CHECKED(UPCGExVariantCollection, MaterialSwaps).ToString());

		TArray<PCGExCollections::FMaterialSwapRow> Rows;
		int32 NumUnreachable = 0;
		int32 NumAttributeClauses = 0;
		TArray<bool> Unreachable;
		UPCGExVariantCollection::GetMaterialSwaps(InSwaps, Rows, NumUnreachable, NumAttributeClauses, &Unreachable);

		PCGExCollectionHelpers::FNonMaterialCheck NonMaterialCheck;
		for (int32 i = 0; i < InSwaps.Num(); i++)
		{
			FPathScope Row(i);
			const FPCGExMaterialSwap& Swap = InSwaps[i];

			if (Swap.MatchMaterial.IsNull() || Swap.Replacement.IsNull())
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("never applies: a swap needs both MatchMaterial and Replacement"));
				continue;
			}
			if (NonMaterialCheck.IsKnownNonMaterial(Swap.MatchMaterial.ToSoftObjectPath()))
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("MatchMaterial"), TEXT("not a material: it can never match a slot"));
			}
			if (NonMaterialCheck.IsKnownNonMaterial(Swap.Replacement.ToSoftObjectPath()))
			{
				Report(EPCGExMediatorSeverity::Error, TEXT("Replacement"), TEXT("not a material: the slot would render the mesh's own material"));
			}
			if (Unreachable[i])
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("never applies: an earlier row for this MatchMaterial always wins (no tag filter, or an equivalent one)"));
			}

			FName RequireAll;
			FName RequireAny;
			FName Exclude;
			if (Swap.bUseTagFilter && Swap.TagFilter.GetConstantClauses(RequireAll, RequireAny, Exclude) > 0)
			{
				Report(EPCGExMediatorSeverity::Warning, TEXT("TagFilter"), TEXT("a clause reading an attribute reads nothing on a Variant: give it a constant"));
			}
		}
	}
}

void PCGExCollectionsMediatorBindings::Register(FPCGExMediatorDomain& Domain)
{
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExAssetCollection::StaticClass();
		Binding.Members = {
			GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties),
			GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CategoryOverrides),
			PCGExAssetCollection::EntriesPropertyName
		};
		Binding.DescribeInstance = &DescribeCollectionInstance;
		Binding.WrapImport = &WrapCollectionImport;
		// A heterogeneous host needs its per-type globals and machinery for every type the rows now carry.
		Binding.PostImport = [](UObject* Host)
		{
			if (UPCGExOmniCollection* Omni = Cast<UPCGExOmniCollection>(Host)) { Omni->EDITOR_EnsureTypeSetup(); }
		};
		Binding.Summary = TEXT("Asset collection: property schema, category overrides and entries (typed or Omni host).");
		Domain.AddBinding(Binding);
	}
	{
		// Variants hold no Entries: their rows live per source group and in path rules.
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExVariantCollection::StaticClass();
		Binding.Members = {
			GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties),
			GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CategoryOverrides),
			GET_MEMBER_NAME_CHECKED(UPCGExVariantCollection, Sources),
			GET_MEMBER_NAME_CHECKED(UPCGExVariantCollection, PathOverrides),
			GET_MEMBER_NAME_CHECKED(UPCGExVariantCollection, MaterialSwaps)
		};
		Binding.DescribeInstance = &DescribeCollectionInstance;
		Binding.WrapImport = &WrapCollectionImport;
		Binding.Validate = [](const UObject*, FPCGExMediatorDecodedMember Decoded)
		{
			if (const TArray<FPCGExMaterialSwap>* Swaps = static_cast<const TArray<FPCGExMaterialSwap>*>(Decoded(GET_MEMBER_NAME_CHECKED(UPCGExVariantCollection, MaterialSwaps))))
			{
				AuditMaterialSwaps(*Swaps);
			}
		};
		Binding.Summary = TEXT("Variant collection: property schema, category overrides, per-source entry overrides, asset-path rules and material swaps.");
		Domain.AddBinding(Binding);
	}
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExDistributeTupleSettings::StaticClass();
		Binding.Members = {GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Composition), GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values)};
		Binding.DescribeInstance = [](const UObject* Host) -> TSharedPtr<FJsonObject>
		{
			const UPCGExDistributeTupleSettings* Tuple = Cast<UPCGExDistributeTupleSettings>(Host);
			if (!Tuple) { return nullptr; }
			return PCGExPropertyMediator::DescribeSchemaInstance(Tuple->Composition);
		};
		Binding.WrapImport = [](UObject* Host, const FName Member, FPCGExMediatorDecodedMember Decoded, TFunctionRef<void()> Import)
		{
			const UPCGExDistributeTupleSettings* Tuple = Cast<UPCGExDistributeTupleSettings>(Host);
			if (Tuple && Member == GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values))
			{
				const FPCGExPropertySchemaCollection* Composition = static_cast<const FPCGExPropertySchemaCollection*>(Decoded(GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Composition)));
				PCGExPropertyMediator::FOverridesSchemaScope Scope((Composition ? Composition : &Tuple->Composition)->BuildSchema());
				Import();
				return;
			}
			Import();
		};
		Binding.Summary = TEXT("Distribute Tuple node: composition schema and weighted value rows.");
		Domain.AddBinding(Binding);
	}
}
