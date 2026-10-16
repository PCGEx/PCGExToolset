// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadataCommon.h"

class FJsonObject;
class FJsonValue;
struct FGameplayTag;
struct FGameplayTagContainer;
struct FPCGAttributePropertySelector;
struct FPCGExWeightCurve;

/**
 * Reflection in the dialect: one traversal over FProperty that encodes, decodes and describes any reflected value
 * the way the hand-written formats do, so a reflected host reads the same as a curated one.
 *   math structs (Vector2/Vector/Vector4/Rotator/Quat/Transform) -> the dialect shapes
 *   gameplay tag / tag container                                  -> registered tag name / array of them
 *   PCG attribute / property selectors                            -> the selector text ($Position.X, @Data.Name, Name.X)
 *   PCGEx input shorthands (constant or attribute)                -> a bare constant, or { "attribute": <selector or name> }
 *   FPCGExWeightCurve                                             -> [[t, v], ...] inline keys, or { "asset": <CurveFloat path> }
 *   enums (FEnumProperty, byte-with-enum)                         -> enumerator names
 *   object / class refs (not instanced) and soft refs            -> object path strings, "" = null
 *   structs with a registered mediator format                    -> that format's converter
 *   FInstancedStruct, maps, instanced objects                     -> the engine converter (its own shapes)
 *   other structs                                                 -> recursed, by UPROPERTY name
 *   arrays and sets                                               -> arrays
 * Encoding is SPARSE when defaults are supplied: a field identical to its default is omitted. Decoding merges:
 * an absent key leaves its field alone. Failures report at the current diagnostics path.
 */
namespace PCGExMediator::Reflect
{
	/** Which properties a struct walk includes; the default takes every property. */
	using FPropertyFilter = TFunctionRef<bool(const FProperty* Property)>;

	PCGEXMEDIATOR_API bool IncludeAll(const FProperty* Property);

	/** The dialect type a struct maps to, or Unknown. */
	PCGEXMEDIATOR_API EPCGMetadataTypes DialectTypeOf(const UScriptStruct* Struct);

	/** A tag is its name ("" for none); a container is the array of its explicitly added tags, parents never written. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeGameplayTag(const FGameplayTag& Tag);
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeGameplayTagContainer(const FGameplayTagContainer& Tags);
	/** "" and "None" read as no tag; bad syntax or a name absent from the project's tag dictionary is an Error. */
	PCGEXMEDIATOR_API bool DecodeGameplayTag(const TSharedPtr<FJsonValue>& Json, FGameplayTag& Out);
	/** An array of tag names replacing the whole container; any refused name leaves Out untouched. */
	PCGEXMEDIATOR_API bool DecodeGameplayTagContainer(const TSharedPtr<FJsonValue>& Json, FGameplayTagContainer& Out);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeGameplayTag();
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeGameplayTagContainer();

	/** The selector as typed in the editor (ToString, domain included); covers every struct of the selector family. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeAttributeSelector(const FPCGAttributePropertySelector& Selector);
	/** Update() from the text; a result IsValid() refuses (bad attribute-name characters) is an Error and leaves Out untouched. */
	PCGEXMEDIATOR_API bool DecodeAttributeSelector(const TSharedPtr<FJsonValue>& Json, FPCGAttributePropertySelector& Out);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeAttributeSelector();

	/** Whether Struct derives from FPCGExInputShorthandBase and carries a Constant member (the bases alone do not). */
	PCGEXMEDIATOR_API bool IsInputShorthand(const UScriptStruct* Struct);
	/**
	 * A shorthand on a constant is its bare value; one on the input data is { "attribute": <selector or name>, "cleanup"?: bool }.
	 * Any other authored member away from its default (a Direction's bFlip) forces the object form, "constant" carrying the value.
	 * Defaults is an instance of Struct, or null for a fresh one.
	 */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeInputShorthand(const UScriptStruct* Struct, const void* Value, const void* Defaults = nullptr);
	/** Reads both shapes above, a constant object (a Transform's), and the reflected shape (Input / Constant / Attribute) older documents carry. Merges; the Constant's clamps are enforced. */
	PCGEXMEDIATOR_API bool DecodeInputShorthand(const UScriptStruct* Struct, void* Value, const TSharedPtr<FJsonValue>& Json);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeInputShorthand(const UScriptStruct* Struct, int32 Depth = 3);

	/** { "asset": path } when an external curve is set, else the inline keys as [[t, v], [t, v, "Cubic"], ...] (mode omitted when Linear). */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeWeightCurve(const FPCGExWeightCurve& Curve);
	/** An array replaces the inline keys (cubic keys take auto tangents) and clears the asset; an object sets "asset" and / or "points". */
	PCGEXMEDIATOR_API bool DecodeWeightCurve(const TSharedPtr<FJsonValue>& Json, FPCGExWeightCurve& Out);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeWeightCurve();
	/** Same dialect value: same asset, same keys by time / value / mode. Key handles are minted per instance and never count. */
	PCGEXMEDIATOR_API bool WeightCurvesIdentical(const FPCGExWeightCurve& A, const FPCGExWeightCurve& B);

	/** Null when the property kind has no representation (delegates, interfaces, field paths). */
	PCGEXMEDIATOR_API TSharedPtr<FJsonValue> EncodeProperty(const FProperty* Property, const void* ValuePtr, const void* DefaultValuePtr = nullptr);

	PCGEXMEDIATOR_API bool DecodeProperty(const FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Json);

	/** Object of the struct's properties by name; with Defaults (memory of a default instance), fields at default are omitted. */
	PCGEXMEDIATOR_API void EncodeStruct(const UStruct* Struct, const void* Memory, const void* Defaults, FJsonObject& Out, FPropertyFilter Filter = &IncludeAll);

	/**
	 * Applies every key of In to the matching property. Keys in IgnoredKeys are the caller's; other unknown or
	 * filtered-out keys warn and are skipped. False when any value failed (the memory may then be partially
	 * written: decode into a temporary).
	 */
	PCGEXMEDIATOR_API bool DecodeStruct(const UStruct* Struct, void* Memory, const FJsonObject& In, FPropertyFilter Filter = &IncludeAll, TConstArrayView<FString> IgnoredKeys = {});

	/** JSON Schema fragment for one property; Depth bounds the recursion into plain structs. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeProperty(const FProperty* Property, int32 Depth = 3);

	/** Object schema with one entry per included property. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeStruct(const UStruct* Struct, FPropertyFilter Filter = &IncludeAll, int32 Depth = 3);
}
