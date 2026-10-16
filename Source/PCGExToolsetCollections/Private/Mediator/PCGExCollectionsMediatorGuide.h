// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FPCGExMediatorDomain;

/** The authoring guide this module owns: how an agent thinks about asset collections, their entries and the Distribute Tuple. */
namespace PCGExCollectionsMediatorGuide
{
	inline const FName DomainId = FName(TEXT("collections"));

	void Register(FPCGExMediatorDomain& Domain);
}
