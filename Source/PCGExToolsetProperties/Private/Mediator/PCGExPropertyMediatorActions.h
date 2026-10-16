// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FPCGExMediatorDomain;

/** Mediator actions this module owns: editor levers no document expresses. */
namespace PCGExPropertyMediatorActions
{
	/** Params { "property": name }. A Property Schema asset sweeps its own import rows and every loaded importer; any other row host its own rows. */
	inline const FName AlignChoicesActionId = FName(TEXT("pcgex.align-choices"));

	void Register(FPCGExMediatorDomain& Domain);
}
