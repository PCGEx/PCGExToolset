// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

/**
 * Documents in and out. Every document is an envelope { "format", "version", "data" }; a struct document's
 * data is its format's body, an object document's data is { "class", "members": { <member>: <body> } }.
 * All calls are game-thread (asserted), editor-side, and report through the caller's PCGExMediator::FScope (or
 * the log). Imports never partially mutate: every member decodes into scratch first, then all are applied inside
 * one transaction, each followed by the host's own PostEditChangeProperty.
 */
namespace PCGExMediator
{
	namespace Keys
	{
		inline constexpr const TCHAR* const Format = TEXT("format");
		inline constexpr const TCHAR* const Version = TEXT("version");
		inline constexpr const TCHAR* const Data = TEXT("data");
		inline constexpr const TCHAR* const Class = TEXT("class");
		inline constexpr const TCHAR* const Members = TEXT("members");
	}

	inline const FName ObjectFormatId = FName(TEXT("pcgex.object"));
	inline constexpr int32 ObjectFormatVersion = 1;

	PCGEXMEDIATOR_API TSharedRef<FJsonObject> MakeEnvelope(FName FormatId, int32 Version, const TSharedPtr<FJsonValue>& Data);

	/** False (with diagnostics) on a foreign format or a version above SupportedVersion; a missing version reads as 1. */
	PCGEXMEDIATOR_API bool ReadEnvelope(const FJsonObject& Doc, FName ExpectedFormat, int32 SupportedVersion, TSharedPtr<FJsonValue>& OutData);

	// --- Structs (through their registered format) ---

	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> ExportStruct(const UScriptStruct* Struct, const void* Memory);
	PCGEXMEDIATOR_API bool ImportStruct(const FJsonObject& Doc, const UScriptStruct* Struct, void* Memory);

	template <typename T>
	TSharedPtr<FJsonObject> ExportStruct(const T& Value)
	{
		return ExportStruct(T::StaticStruct(), &Value);
	}

	template <typename T>
	bool ImportStruct(const FJsonObject& Doc, T& Value)
	{
		return ImportStruct(Doc, T::StaticStruct(), &Value);
	}

	// --- Objects (through their registered binding) ---

	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> ExportObject(const UObject* Host);
	PCGEXMEDIATOR_API bool ImportObject(const FJsonObject& Doc, UObject* Host);

	// --- Describe ---

	/** JSON Schema of a whole struct document (envelope included); null for an unknown format. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeFormat(FName FormatId);

	/** JSON Schema of an object document for HostClass: its bound members, each reflected in the dialect. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeObject(const UClass* HostClass);

	/** Reflected JSON Schema of any struct by path or name (the drill-down a collapsed fragment names); null when not a struct. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeReflectedStruct(const FString& StructNameOrPath);

	/** JSON Schema of a registered action's params (empty object schema when it declares none); null for an unknown action. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeAction(FName ActionId);

	/**
	 * A format id, else an action id, else a bound class, else a struct (its format's schema when one is registered,
	 * reflected otherwise); null when nothing matches. The one chain every describe surface uses.
	 */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeAny(const FString& FormatIdOrClassOrStruct);

	/**
	 * What a bound INSTANCE offers an author (its binding's DescribeInstance: e.g. resolved properties with effective
	 * choices), else the class's object schema. Null (with a diagnostic) when the host has no binding or its binding refuses it.
	 */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeTarget(const UObject* Host);

	// --- Actions ---

	/**
	 * Runs a registered action on Target inside one transaction named after the action; { ok, ...result } on success,
	 * null (with diagnostics) when the action is unknown, the target is of the wrong class or the action failed.
	 * A result carrying "changed": false cancels the transaction.
	 */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> RunAction(FName ActionId, UObject* Target, const TSharedPtr<FJsonObject>& Params);

	/**
	 * { formats: [{ id, version, struct, summary }], bindings: [{ class, members, summary }], actions: [{ id, target, summary }] }.
	 * Params schemas are not listed (the list is the one call every agent makes and must stay small): DescribeAction has them.
	 */
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> ListAsJson();

	/** Every registered guide in registration order: [{ domain, hosts: [class paths], text, examples: [documents] }]. */
	PCGEXMEDIATOR_API TArray<TSharedPtr<FJsonValue>> GuidesAsJson();

	// --- Text and files ---

	PCGEXMEDIATOR_API FString ToString(const TSharedRef<FJsonObject>& Doc, bool bPretty = true);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> FromString(const FString& Text);
	PCGEXMEDIATOR_API bool WriteFile(const FString& FilePath, const TSharedRef<FJsonObject>& Doc);
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> ReadFile(const FString& FilePath);

	/**
	 * An asset by object path, a node's settings by "<graph object path>:<node title or name>", or a target alias
	 * ("@focused", "@selected": PCGExMediatorFocus.h). Loads what it needs; null (with diagnostics) when nothing resolves.
	 */
	PCGEXMEDIATOR_API UObject* ResolveTarget(const FString& Target);
}
