// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMediatorConverter.h"
#include "Collections/PCGExOmniCollection.h"
#include "Core/PCGExAssetCollection.h"

class FPCGExMediatorDomain;

/**
 * JSON shapes of the collection structs. An entry is its authored fields by UPROPERTY name (CPF_Edit, not
 * EditConst / deprecated / transient), plus "type" (the registered type id) and "entryId"; object references
 * travel as path strings. One converter instance per registered entry struct, found through the collection type
 * registry, so sibling plugins' types are covered without registration here. Decoding merges into the live
 * entry: an absent key leaves its field unchanged.
 */
class FPCGExCollectionEntryJsonConverter final : public FPCGExMediatorScriptStructConverter
{
public:
	explicit FPCGExCollectionEntryJsonConverter(const UScriptStruct* InStruct);

protected:
	virtual bool Encode(const void* Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, void* Temp, const void* Live) const override;
};

/** An Omni row: the payload's entry shape; "type" picks (or switches) the payload struct on decode. */
class FPCGExOmniCollectionEntryJsonConverter final : public TPCGExMediatorStructConverter<FPCGExOmniCollectionEntry>
{
protected:
	virtual bool Encode(const FPCGExOmniCollectionEntry& Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, FPCGExOmniCollectionEntry& Temp, const FPCGExOmniCollectionEntry& Live) const override;
};

/** A plain struct by its authored fields only (see FPCGExCollectionEntryJsonConverter), sparse; no curated keys. */
class FPCGExAuthoredStructJsonConverter final : public FPCGExMediatorScriptStructConverter
{
public:
	explicit FPCGExAuthoredStructJsonConverter(const UScriptStruct* InStruct);

protected:
	virtual bool Encode(const void* Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, void* Temp, const void* Live) const override;
};

/**
 * A Variant row (FPCGExVariantEntryOverride or FPCGExVariantPathOverride): its own fields by UPROPERTY name, and "Entry"
 * in the Omni row shape ("type" picks or switches the payload struct). An unset payload is omitted; null clears it.
 */
class FPCGExVariantRowJsonConverter final : public FPCGExMediatorScriptStructConverter
{
public:
	explicit FPCGExVariantRowJsonConverter(const UScriptStruct* InStruct);

protected:
	virtual bool Encode(const void* Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, void* Temp, const void* Live) const override;

private:
	const FStructProperty* EntryProperty = nullptr;
};

/** Staging data: authored "sockets" and "boundsModifier" both ways; "path" and "bounds" written for information only. */
class FPCGExAssetStagingDataJsonConverter final : public TPCGExMediatorStructConverter<FPCGExAssetStagingData>
{
protected:
	virtual bool Encode(const FPCGExAssetStagingData& Value, FJsonObject& Out) const override;
	virtual bool Decode(const FJsonObject& In, FPCGExAssetStagingData& Temp, const FPCGExAssetStagingData& Live) const override;
};

namespace PCGExCollectionsMediatorFormats
{
	inline const FName OmniEntryFormatId = FName(TEXT("pcgex.omni-collection-entry"));
	inline const FName StagingFormatId = FName(TEXT("pcgex.asset-staging"));
	inline const FName VariantSourceFormatId = FName(TEXT("pcgex.variant-source"));
	inline const FName VariantEntryOverrideFormatId = FName(TEXT("pcgex.variant-entry-override"));
	inline const FName VariantPathOverrideFormatId = FName(TEXT("pcgex.variant-path-override"));

	/** "pcgex.collection-entry/<TypeId>" */
	FName EntryFormatId(FName TypeId);

	/** Registers the fixed formats and one entry format per registered type on Domain; rescans when modules load. */
	void Register(FPCGExMediatorDomain& Domain);

	/** Stops the rescan; the domain's own Reset releases the formats. */
	void Unregister();
}
