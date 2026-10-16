// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FPCGExMediatorDomain;

/** The authoring guide this module owns: how an agent thinks about property schemas, override sets and their hosts. */
namespace PCGExPropertyMediatorGuide
{
	inline const FName DomainId = FName(TEXT("properties"));

	void Register(FPCGExMediatorDomain& Domain);
}
