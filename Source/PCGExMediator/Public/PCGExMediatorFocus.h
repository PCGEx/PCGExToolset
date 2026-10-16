// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The target aliases and the open command: what the user is looking at, and showing them a host.
 *
 * "@focused" composes candidates in stages and stops at the first stage that yields any: the domain presenters, the
 * most recently activated asset editor on a bound asset, the level selection (bound components, then the bound
 * components of selected actors), the Content Browser selection. "@selected" runs the two selection stages only.
 * Exactly one candidate resolves; several at one stage is an error listing them, never a guess.
 */
namespace PCGExMediator
{
	constexpr const TCHAR* const FocusedAlias = TEXT("@focused");
	constexpr const TCHAR* const SelectedAlias = TEXT("@selected");

	/** True for any "@..." target; the alias is resolved by ResolveAlias, which refuses unknown ones. */
	PCGEXMEDIATOR_API bool IsTargetAlias(const FString& Target);

	/** The one object an alias names right now; null with a diagnostic (nothing, several, or an unknown alias). */
	PCGEXMEDIATOR_API UObject* ResolveAlias(const FString& Alias);

	/**
	 * Brings Target in front of the user: a presenter's own way first, else a node by opening its graph editor and
	 * jumping to it, else the asset it belongs to through the Content Browser's activation path (the type's
	 * preconditions run, the browser syncs), else a level component by selecting its actor. Result
	 * { target, opened } or null with a diagnostic.
	 */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> OpenTarget(UObject* Target);
}
