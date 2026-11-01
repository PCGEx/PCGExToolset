// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FPCGExMediatorDomain;

/** Mediator actions this module owns: reads and levers no document expresses. */
namespace PCGExCollectionsMediatorActions
{
	/** Read-only. Params { "variant"?: path }. The materials a collection's mesh entries render, with a Variant's swaps previewed. */
	inline const FName MaterialSlotsActionId = FName(TEXT("pcgex.collections.material-slots"));

	void Register(FPCGExMediatorDomain& Domain);
}
