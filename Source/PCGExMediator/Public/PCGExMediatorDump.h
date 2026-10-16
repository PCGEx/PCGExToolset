// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class FJsonObject;

namespace PCGExMediator
{
	/**
	 * Everything the registries hold, in one document: formats and bindings with their full schemas, actions with
	 * their params schemas, every guide, the toolkit version and a UTC timestamp. The docs pipeline renders the agent
	 * authoring pages from it, so it is the single source the skill text and the knowledge base both derive from.
	 */
	PCGEXMEDIATOR_API TSharedRef<FJsonObject> DumpAsJson();

	/** DumpAsJson written pretty-printed to FilePath; false (logged) when the write fails. */
	PCGEXMEDIATOR_API bool DumpToFile(const FString& FilePath);
}
