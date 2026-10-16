// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FPCGExPropertyOverrides;
struct FPCGExPropertySchemaCollection;

namespace PCGExPropertyMediator
{
	/**
	 * What a schema host offers an author right now, for a binding's DescribeInstance: every resolved property with
	 * its name, type, declaring asset, effective default, and its effective choices and lock after every import patch.
	 * { "properties": [ { name, type, declaredBy, overridden, default?, choices?, patched? } ], "imports": [paths] }.
	 * FallbackChain as in FPCGExPropertySchemaCollection::Resolve.
	 */
	PCGEXTOOLSETPROPERTIES_API TSharedRef<FJsonObject> DescribeSchemaInstance(const FPCGExPropertySchemaCollection& Collection, TConstArrayView<const FPCGExPropertyOverrides*> FallbackChain = {});
}
