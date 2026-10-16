// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGExMediatorLibrary.generated.h"

/**
 * The mediator's operations as string-only reflected functions: the surface a separately shipped plugin
 * (the PCGEx Assistant) reaches by name through UFunction, with no link or header dependency, and the surface
 * Python and Blueprint see. Every result is a JSON object string; a failure is { "ok": false, "diagnostics": [...] }.
 */
UCLASS()
class PCGEXMEDIATOR_API UPCGExMediatorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** { formats: [{ id, version, struct, summary }], bindings: [{ class, members, summary }], actions: [{ id, target, summary }] }; DescribeFormat(<action id>) has an action's params schema. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString ListFormats();

	/** JSON Schema of a format id, of an action's params, of a bound class's object document or of a struct; a failure object when nothing matches. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString DescribeFormat(const FString& FormatIdOrClass);

	/** The object document of a bound target (asset path, or <graph path>:<node>); a failure object otherwise. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString ExportJson(const FString& Target);

	/** Imports a document into a bound target, undoable. Always { ok, diagnostics }. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString ImportJson(const FString& Target, const FString& Json);

	/**
	 * Creates an asset of a class (path, short or C++ name) in a long package path, through the class's factory when
	 * one exists. Unsaved and marked dirty; { ok, path, class } or a failure object (existing asset, bad class or path).
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString CreateAsset(const FString& ClassNameOrPath, const FString& PackagePath, const FString& AssetName);

	/** Saves the package of an asset given by object path. { ok, path } or a failure object. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString SaveAsset(const FString& ObjectPath);

	/** Undoes the editor's last transaction (an import is one). { ok, title } with what was undone, or a failure object. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString Undo();

	/**
	 * What a bound target offers an author as data: for a schema host, its resolved properties with their effective
	 * choices and lock; the class's object schema when the binding describes no instance. A failure object otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString DescribeTarget(const FString& Target);

	/** Runs a registered action (ListFormats "actions"; params per DescribeFormat(<action id>)) on a target, undoable. { ok, ...result } or a failure object. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString RunAction(const FString& ActionId, const FString& Target, const FString& ParamsJson);

	/**
	 * Brings a target in front of the user: its asset editor (through the Content Browser's activation path), a node in
	 * its graph editor, a level component by selecting its actor, or a domain's own way (the sketch mode). Every Target
	 * here also accepts "@focused" / "@selected". { ok, target, opened } or a failure object.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString Open(const FString& Target);

	/** { guides: [{ domain, hosts, text, examples }] }: every domain's authoring guide, in registration order. */
	UFUNCTION(BlueprintCallable, Category = "PCGEx|Mediator")
	static FString DescribeGuides();
};
