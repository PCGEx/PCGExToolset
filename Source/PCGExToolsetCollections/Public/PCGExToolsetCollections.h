// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEditorModuleInterface.h"
#include "PCGExMediatorRegistry.h"

/** Collections authoring surface over the mediator hub and the Properties hook registry. */
class FPCGExToolsetCollectionsModule final : public IPCGExEditorModuleInterface
{
	PCGEX_MODULE_BODY

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** The formats, bindings and guide this module registers. */
	FPCGExMediatorDomain MediatorDomain;
};
