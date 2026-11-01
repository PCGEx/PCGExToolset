// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorActions.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorLookup.h"
#include "PCGExMediatorParams.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorSchema.h"
#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExVariantCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Helpers/PCGExMaterialSwapTable.h"
#include "Materials/MaterialInterface.h"
#include "MeshSelectors/PCGISMDescriptor.h"

namespace PCGExCollectionsMediatorActions
{
	using namespace PCGExMediator;
	using FSwapTable = PCGExCollections::FMaterialSwapTable;

	namespace Keys
	{
		constexpr const TCHAR* const Variant = TEXT("variant");
	}

	// Where an entry's material comes from, in the order the staged selector layers them.
	namespace From
	{
		constexpr const TCHAR* const Mesh = TEXT("mesh");
		constexpr const TCHAR* const Override = TEXT("override");
		constexpr const TCHAR* const MaterialVariant = TEXT("materialVariant");
		constexpr const TCHAR* const Overlay = TEXT("overlay");
	}

	struct FMaterialUse
	{
		int32 Uses = 0;
		TSet<FSoftObjectPath> SwapsTo;
	};

	/** Walks collections the way a spawner meets their entries: sub-collections recursed, each entry with the collection holding it. */
	struct FMaterialSlotsWalk
	{
		const FSwapTable::FStages* Stages = nullptr;

		TArray<const UPCGExAssetCollection*> Visited;
		TArray<TSharedPtr<FJsonValue>> Entries;
		TMap<FSoftObjectPath, FMaterialUse> Materials;
		int32 NumUnstaged = 0;
		int32 NumOther = 0;

		void Collection(const UPCGExAssetCollection* InCollection)
		{
			if (!InCollection || Visited.Contains(InCollection)) { return; }
			Visited.Add(InCollection);

			InCollection->ForEachEntry([&](const FPCGExAssetCollectionEntry* InEntry, const int32 InIndex)
			{
				if (!InEntry) { return; }
				if (InEntry->bIsSubCollection)
				{
					Collection(InEntry->GetSubCollectionPtr());
					return;
				}
				// Swaps reach the staged static mesh selector only.
				if (!InEntry->IsType(PCGExAssetCollection::TypeIds::Mesh))
				{
					NumOther++;
					return;
				}
				MeshEntry(InCollection, static_cast<const FPCGExMeshCollectionEntry&>(*InEntry), InIndex);
			});
		}

		void MeshEntry(const UPCGExAssetCollection* InHost, const FPCGExMeshCollectionEntry& InEntry, const int32 InIndex)
		{
			const FSoftObjectPath MeshPath = InEntry.StaticMesh.ToSoftObjectPath();
			// Same validity rule as the staged selector's snapshot read.
			const bool bStaged = !InEntry.StaticMesh.IsNull() && InEntry.StagedMaterialsSource == MeshPath;
			if (!bStaged) { NumUnstaged++; }

			FPCGSoftISMComponentDescriptor Descriptor;
			InEntry.InitPCGSoftISMDescriptor(InHost, Descriptor);

			TArray<TSharedPtr<FJsonValue>> Items;
			auto Add = [&](const TSoftObjectPtr<UMaterialInterface>& InMaterial, const TCHAR* InFrom, const int32 InSlot, const int32 InPick)
			{
				if (InMaterial.IsNull()) { return; }
				const FSoftObjectPath Path = InMaterial.ToSoftObjectPath();

				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("material"), Path.ToString());
				Item->SetStringField(TEXT("from"), InFrom);
				if (InSlot >= 0) { Item->SetNumberField(TEXT("slot"), InSlot); }
				if (InPick >= 0) { Item->SetNumberField(TEXT("pick"), InPick); }

				FMaterialUse& Use = Materials.FindOrAdd(Path);
				Use.Uses++;
				if (Stages)
				{
					const FSoftObjectPath Swapped = FSwapTable::Resolve(*Stages, InEntry, InHost, Path);
					if (Swapped != Path)
					{
						Item->SetStringField(TEXT("swapsTo"), Swapped.ToString());
						Use.SwapsTo.Add(Swapped);
					}
				}
				Items.Add(MakeShared<FJsonValueObject>(Item));
			};

			if (bStaged)
			{
				for (int32 Slot = 0; Slot < InEntry.StagedMaterials.Num(); Slot++)
				{
					// An authored override hides the mesh's own material from every pick.
					if (Descriptor.OverrideMaterials.IsValidIndex(Slot) && !Descriptor.OverrideMaterials[Slot].IsNull()) { continue; }
					Add(InEntry.StagedMaterials[Slot], From::Mesh, Slot, INDEX_NONE);
				}
			}
			for (int32 Slot = 0; Slot < Descriptor.OverrideMaterials.Num(); Slot++)
			{
				Add(Descriptor.OverrideMaterials[Slot], From::Override, Slot, INDEX_NONE);
			}
			switch (InEntry.MaterialVariants)
			{
			case EPCGExMaterialVariantsMode::None:
				break;
			case EPCGExMaterialVariantsMode::Single:
				for (int32 Pick = 0; Pick < InEntry.MaterialOverrideVariants.Num(); Pick++)
				{
					Add(InEntry.MaterialOverrideVariants[Pick].Material, From::MaterialVariant, PCGExMeshCollection::ResolveSingleSlot(InEntry.SlotIndex), Pick);
				}
				break;
			case EPCGExMaterialVariantsMode::Multi:
				for (int32 Pick = 0; Pick < InEntry.MaterialOverrideVariantsList.Num(); Pick++)
				{
					const TArray<FPCGExMaterialOverrideEntry>& Overrides = InEntry.MaterialOverrideVariantsList[Pick].Overrides;
					for (int32 Row = 0; Row < Overrides.Num(); Row++)
					{
						Add(Overrides[Row].Material, From::MaterialVariant, Overrides[Row].ResolveSlot(Row), Pick);
					}
				}
				break;
			default:
				Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("%s entry %d: unknown material variants mode %d; its material variants are not listed"), *InHost->GetPathName(), InIndex, static_cast<int32>(InEntry.MaterialVariants)));
				break;
			}
			Add(Descriptor.OverlayMaterial, From::Overlay, INDEX_NONE, INDEX_NONE);

			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("collection"), InHost->GetPathName());
			Out->SetNumberField(TEXT("index"), InIndex);
			if (InEntry.EntryId != 0) { Out->SetNumberField(TEXT("entryId"), InEntry.EntryId); }
			Out->SetStringField(TEXT("mesh"), MeshPath.ToString());
			Out->SetBoolField(TEXT("staged"), bStaged);
			Out->SetArrayField(TEXT("materials"), Items);
			Entries.Add(MakeShared<FJsonValueObject>(Out));
		}
	};

	TArray<TSharedPtr<FJsonValue>> SortedPaths(const TSet<FSoftObjectPath>& InPaths)
	{
		TArray<FSoftObjectPath> Paths = InPaths.Array();
		FSwapTable::SortCanonical(Paths);
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(Paths.Num());
		for (const FSoftObjectPath& Path : Paths) { Out.Add(MakeShared<FJsonValueString>(Path.ToString())); }
		return Out;
	}

	TSharedPtr<FJsonObject> RunMaterialSlots(UObject* Target, const FPCGExMediatorParams& Params)
	{
		const UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(Target);
		if (!Collection)
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s is not an asset collection"), *GetNameSafe(Target)));
			return nullptr;
		}

		const UPCGExVariantCollection* SwapVariant = Cast<UPCGExVariantCollection>(Collection);
		FSoftObjectPath VariantPath;
		bool bVariantGiven = false;
		if (!Params.Read(Keys::Variant, VariantPath, &bVariantGiven)) { return nullptr; }
		if (bVariantGiven)
		{
			SwapVariant = nullptr;
			if (!VariantPath.IsNull())
			{
				SwapVariant = Cast<UPCGExVariantCollection>(ResolveObject(VariantPath.ToString()));
				if (!SwapVariant)
				{
					Params.Report(EPCGExMediatorSeverity::Error, Keys::Variant, FString::Printf(TEXT("'%s' is not a Variant collection"), *VariantPath.ToString().Left(256)));
					return nullptr;
				}
			}
		}

		// The Variant's swaps as the one stage a Staging : Swap using it alone would chain.
		FSwapTable::FStages Stages;
		int32 NumRules = 0;
		if (SwapVariant)
		{
			TArray<PCGExCollections::FMaterialSwapRow> Rows;
			int32 NumUnreachable = 0;
			int32 NumAttributeClauses = 0;
			SwapVariant->GetMaterialSwaps(Rows, NumUnreachable, NumAttributeClauses);

			FSwapTable::FStage& Stage = Stages.AddDefaulted_GetRef();
			for (const PCGExCollections::FMaterialSwapRow& Row : Rows)
			{
				if (FSwapTable::AddRule(Stage, Row.Match, Row.Rule) == FSwapTable::EAddResult::Added) { NumRules++; }
			}
		}

		FMaterialSlotsWalk Walk;
		Walk.Stages = SwapVariant ? &Stages : nullptr;
		// A Variant's swaps reach both the entries it leaves alone and the ones it puts in.
		if (const UPCGExVariantCollection* TargetVariant = Cast<UPCGExVariantCollection>(Collection))
		{
			for (const FPCGExVariantSource& Group : TargetVariant->Sources) { Walk.Collection(Group.SourceCollection); }
		}
		Walk.Collection(Collection);

		TArray<FSoftObjectPath> MaterialPaths;
		Walk.Materials.GetKeys(MaterialPaths);
		FSwapTable::SortCanonical(MaterialPaths);
		TArray<TSharedPtr<FJsonValue>> Materials;
		Materials.Reserve(MaterialPaths.Num());
		for (const FSoftObjectPath& Path : MaterialPaths)
		{
			const FMaterialUse& Use = Walk.Materials[Path];
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("material"), Path.ToString());
			Item->SetNumberField(TEXT("uses"), Use.Uses);
			if (!Use.SwapsTo.IsEmpty()) { Item->SetArrayField(TEXT("swapsTo"), SortedPaths(Use.SwapsTo)); }
			Materials.Add(MakeShared<FJsonValueObject>(Item));
		}

		TArray<TSharedPtr<FJsonValue>> Collections;
		for (const UPCGExAssetCollection* Visited : Walk.Visited) { Collections.Add(MakeShared<FJsonValueString>(Visited->GetPathName())); }

		if (Walk.NumUnstaged > 0)
		{
			Report(EPCGExMediatorSeverity::Warning, FString::Printf(TEXT("%d mesh entr%s without a material snapshot: their own slot materials are not listed and swaps reach only their overrides until the collection is restaged (Rebuild staging in the collection editor)"),
			                                                        Walk.NumUnstaged, Walk.NumUnstaged == 1 ? TEXT("y") : TEXT("ies")));
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		if (SwapVariant)
		{
			Out->SetStringField(TEXT("variant"), SwapVariant->GetPathName());
			Out->SetNumberField(TEXT("swapRules"), NumRules);
		}
		Out->SetArrayField(TEXT("collections"), Collections);
		Out->SetArrayField(TEXT("materials"), Materials);
		Out->SetArrayField(TEXT("entries"), Walk.Entries);
		Out->SetNumberField(TEXT("unstaged"), Walk.NumUnstaged);
		Out->SetNumberField(TEXT("otherEntries"), Walk.NumOther);
		Out->SetBoolField(TEXT("changed"), false);
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeMaterialSlotsParams()
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Props->SetObjectField(Keys::Variant, Schema::String(TEXT("object path of a Variant collection whose MaterialSwaps to preview; defaults to the target when it is a Variant, \"\" for no preview")));
		return Schema::Object(Props, TEXT("optional swap preview"));
	}
}

void PCGExCollectionsMediatorActions::Register(FPCGExMediatorDomain& Domain)
{
	FPCGExMediatorAction Action;
	Action.Id = MaterialSlotsActionId;
	Action.TargetClass = UPCGExAssetCollection::StaticClass();
	Action.Run = &RunMaterialSlots;
	Action.DescribeParams = &DescribeMaterialSlotsParams;
	Action.Summary = TEXT("Read-only: the materials a collection's mesh entries render (the mesh's own slots from the staging snapshot, descriptor overrides, material variant picks, overlay), sub-collections included; a Variant target also lists its sources' entries. With a Variant (the target, or \"variant\"), each material shows what its MaterialSwaps turn it into for that entry, tag filters evaluated.");
	Domain.AddAction(Action);
}
