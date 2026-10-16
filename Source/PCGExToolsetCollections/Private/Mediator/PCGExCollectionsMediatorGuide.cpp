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
			TEXT("Staging (bounds, material slots) is rebuilt from the assets after import.\n")
			TEXT("DISTRIBUTE TUPLE node: Composition (a schema) then Values, weighted override rows { weight, values } merged by position.\n")
			TEXT("PROPERTY VALUES this domain adds: a CollectionEntry value is { \"collection\": <asset path>, \"entryId\": <id> }; ")
			TEXT("a Range value is { \"min\", \"max\" }.\n")
			TEXT("LAY WORDING. Users say list, set, library or palette for an asset collection and item or row for an entry; route them here.");
		Guide.Examples = {
			TEXT("{\"format\": \"pcgex.object\", \"version\": 1, \"data\": {\"class\": \"/Script/PCGExCollections.PCGExMeshCollection\", \"members\": {")
			TEXT("\"Entries\": [{\"type\": \"Mesh\", \"StaticMesh\": \"/Engine/BasicShapes/Cube.Cube\", \"Weight\": 2, \"Category\": \"Props\"}]}}}")
		};
		Domain.AddGuide(Guide);
	}
}
