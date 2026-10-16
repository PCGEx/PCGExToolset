// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMediatorDiagnostics.h"

class FJsonObject;
class FJsonValue;
struct FInstancedStruct;
struct FPCGExProperty;
struct FPCGExPropertyChoices;
struct FPCGExPropertyMediatorHooks;
struct FPCGExPropertySchemaCollection;

/**
 * Property Choices as JSON: a schema entry's "choices", an importing collection's "choicePatches", the
 * { "choice": label } value form and the locked-list check on decoded row values. Choice Ids never travel: a re-import
 * carries them over by label from the live target, and a patch names the choices it inherits by label. Diagnostics go
 * through PCGExMediator::Report at the current path.
 */
namespace PCGExPropertyMediatorChoices
{
	namespace Keys
	{
		constexpr const TCHAR* const Choices = TEXT("choices");
		constexpr const TCHAR* const Locked = TEXT("locked");
		constexpr const TCHAR* const Items = TEXT("items");
		constexpr const TCHAR* const Label = TEXT("label");
		constexpr const TCHAR* const Value = TEXT("value");
		constexpr const TCHAR* const Choice = TEXT("choice");
		constexpr const TCHAR* const ChoicePatches = TEXT("choicePatches");
		constexpr const TCHAR* const Mode = TEXT("mode");
		constexpr const TCHAR* const Lock = TEXT("lock");
		constexpr const TCHAR* const Remove = TEXT("remove");
		constexpr const TCHAR* const Replaces = TEXT("replaces");
	}

	/** Writes Entry's "choices" when Property lists any or is locked; with no value hook the items carry their label alone (Warning). */
	void EncodeChoices(const FPCGExProperty& Property, const FPCGExPropertyMediatorHooks* Hooks, FJsonObject& Entry);

	/**
	 * Reads Entry's "choices" onto Property, a declaration whose structural keys are already decoded; items take their
	 * Id from Previous (the live declaration's list) by label. An absent key leaves Property with no choices.
	 */
	bool DecodeChoices(FInstancedStruct& Property, const FJsonObject& Entry, const FPCGExPropertyMediatorHooks* Hooks, const FPCGExPropertyChoices* Previous);

	/** Whether Json is the { "choice": label } form. */
	bool IsChoicePick(const TSharedPtr<FJsonValue>& Json);

	/**
	 * A value slot: the choice form picks (and binds) one of Property's own choices, which needs no value hook;
	 * anything else decodes through Hooks, and no value hook is an Error.
	 */
	bool DecodeValueSlot(FInstancedStruct& Property, const TSharedPtr<FJsonValue>& Json, const FPCGExPropertyMediatorHooks* Hooks);

	/** Reports at Severity when Property's list is locked and its Value is none of the choices, listing them. False only on an Error. */
	bool CheckLockedChoices(const FInstancedStruct& Property, FName PropertyName, EPCGExMediatorSeverity Severity);

	/** Writes "choicePatches" when any import row of Collection stores one; inherited choices are named by label. */
	void EncodePatches(const FPCGExPropertySchemaCollection& Collection, FJsonObject& Out);

	/**
	 * Reads "choicePatches" onto Temp's import rows, which must already be reconciled against Temp's imports. Added
	 * items take their Id from Live's patch of the same property by label. The caller reconciles again afterwards.
	 */
	bool DecodePatches(const FJsonObject& In, FPCGExPropertySchemaCollection& Temp, const FPCGExPropertySchemaCollection& Live);

	TSharedRef<FJsonObject> DescribeChoices();
	TSharedRef<FJsonObject> DescribePatches();

	/** One sentence on the { "choice": label } form and the locked rule, for value-slot descriptions. */
	FString ChoiceFormDescription();
}
