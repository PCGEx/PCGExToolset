// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEditorModuleInterface.h"
#include "PCGExMediatorRegistry.h"

/** Properties authoring surface over the mediator hub: owns the per-type hook registry every sibling domain registers into. */
class FPCGExToolsetPropertiesModule final : public IPCGExEditorModuleInterface
{
	PCGEX_MODULE_BODY

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** The formats, bindings, actions and guide this module registers. */
	FPCGExMediatorDomain MediatorDomain;
};
