// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorGuide.h"

#include "PCGExMediatorRegistry.h"
#include "Core/PCGExAssetCollection.h"
#include "Elements/PCGExDistributeTuple.h"

namespace PCGExCollectionsMediatorGuide
{
	void Register(FPCGExMediatorDomain& Domain)
	{
		FPCGExMediatorGuide Guide;
		Guide.Domain = DomainId;
		Guide.Hosts = {UPCGExAssetCollection::StaticClass(), UPCGExDistributeTupleSettings::StaticClass()};
		Guide.Text =
			TEXT("ASSET COLLECTIONS (every UPCGExAssetCollection subclass: mesh, actor, Omni, ...) bind three members, applied in this ")
			TEXT("order: CollectionProperties (a property schema the entry overrides resolve against), CategoryOverrides (per-category ")
			TEXT("override sets) and Entries. Entries merge BY POSITION: row i of the document merges into row i of the host, rows past ")
			TEXT("the host's count are created, rows the document lacks are removed; inside a row an absent key keeps the existing value ")
			TEXT("(the struct default on a new row). To append without touching existing rows, send one placeholder row per existing ")
			TEXT("entry: { \"type\": \"<TypeId>\" }.\n")
			TEXT("ENTRY ROWS carry \"type\", the entry type id (Mesh, ...): a typed collection accepts only its own type, an Omni ")
			TEXT("collection any registered one. The rest of the row is the entry struct by UPROPERTY name; ")
			TEXT("DescribeFormat(\"pcgex.collection-entry/<TypeId>\") is its schema, with nested descriptors and variations collapsed to ")
			TEXT("the struct path DescribeFormat expands. \"entryId\" is a stable identity the collection mints: keep it on an existing ")
			TEXT("row (ExportJson carries it), omit it on a new one. Exports are sparse: a field at its struct default is omitted. ")
			TEXT("Staging (bounds, sockets, the mesh's own slot materials) is rebuilt from the assets after import. A material ")
			TEXT("override row's SlotIndex -1 writes the slot matching the row's position; a Single variant's negative SlotIndex writes slot 0.\n")
			TEXT("VARIANT COLLECTIONS bind CollectionProperties, CategoryOverrides, Sources, PathOverrides and MaterialSwaps (no Entries). ")
			TEXT("Sources are { SourceCollection, Overrides: [{ SourceEntryId, Entry }] }: each row replaces the source entry with that ")
			TEXT("EntryId. PathOverrides are [{ MatchAsset, Entry }]: every source entry staging that asset is replaced. Entry is an ")
			TEXT("entry row like an Omni row (\"type\" required for a new one, any type); null unsets it and the row does nothing.\n")
			TEXT("MATERIAL SWAPS ([{ MatchMaterial, Replacement, TagFilter? }] on a Variant) re-skin everything spawned past a Staging : Swap ")
			TEXT("using that Variant, whatever collection it came from: every slot rendering MatchMaterial (an override, a material variant ")
			TEXT("pick, the mesh's own material or the overlay) renders Replacement instead. A material's rows are tried in order and the ")
			TEXT("first whose tag filter passes for the spawned entry applies; a row after an unfiltered one (or one with the same filter) ")
			TEXT("never applies, and A -> A keeps A for the entries it passes. Tag filters are constants (RequireAll / RequireAny / ")
			TEXT("Exclude, set \"bUseTagFilter\": true); TagSources \"Collection\" reads the tags of the collection holding the entry. ")
			TEXT("Swaps reach static meshes spawned through the staged mesh selector only (\"Apply Material Swaps\", on by default). ")
			TEXT("Before writing swaps, run pcgex.collections.material-slots on the source collection (or the Variant) to list the materials ")
			TEXT("its entries render; with a Variant it previews each swap per entry. An entry reported unstaged lists overrides only: ")
			TEXT("rebuild its collection's staging.\n")
			TEXT("DISTRIBUTE TUPLE node: Composition (a schema) then Values, weighted override rows { weight, values } merged by position.\n")
			TEXT("PROPERTY VALUES this domain adds: a CollectionEntry value is { \"collection\": <asset path>, \"entryId\": <id> }; ")
			TEXT("a Range value is { \"min\", \"max\" }.\n")
			TEXT("LAY WORDING. Users say list, set, library or palette for an asset collection and item or row for an entry; theme, ")
			TEXT("skin, biome or season for a Variant; re-skin, recolor, retexture or replace a material for material swaps; route them here.");
		Guide.Examples = {
			TEXT("{\"format\": \"pcgex.object\", \"version\": 1, \"data\": {\"class\": \"/Script/PCGExCollections.PCGExMeshCollection\", \"members\": {")
			TEXT("\"Entries\": [{\"type\": \"Mesh\", \"StaticMesh\": \"/Engine/BasicShapes/Cube.Cube\", \"Weight\": 2, \"Category\": \"Props\"}]}}}"),
			TEXT("{\"format\": \"pcgex.object\", \"version\": 1, \"data\": {\"class\": \"/Script/PCGExCollections.PCGExVariantCollection\", \"members\": {")
			TEXT("\"MaterialSwaps\": [{\"MatchMaterial\": \"/Game/Mat/M_Grass.M_Grass\", \"Replacement\": \"/Game/Mat/M_Snow.M_Snow\"}, ")
			TEXT("{\"MatchMaterial\": \"/Game/Mat/M_Bark.M_Bark\", \"Replacement\": \"/Game/Mat/M_FrostBark.M_FrostBark\", ")
			TEXT("\"bUseTagFilter\": true, \"TagFilter\": {\"RequireAny\": \"Pine\"}}]}}}")
		};
		Domain.AddGuide(Guide);
	}
}
