// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExToolsetProperties.h"

#include "Mediator/PCGExPropertyMediatorActions.h"
#include "Mediator/PCGExPropertyMediatorBindings.h"
#include "Mediator/PCGExPropertyMediatorFormats.h"
#include "Mediator/PCGExPropertyMediatorGuide.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "Mediator/PCGExPropertyTypeCatalog.h"

void FPCGExToolsetPropertiesModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	// Type catalog, per-type value hooks, struct formats, host bindings, actions, guide.
	PCGExPropertyCatalog::Startup();
	PCGExPropertyMediator::RegisterBuiltInHooks();
	PCGExPropertyMediatorFormats::Register(MediatorDomain);
	PCGExPropertyMediatorBindings::Register(MediatorDomain);
	PCGExPropertyMediatorActions::Register(MediatorDomain);
	PCGExPropertyMediatorGuide::Register(MediatorDomain);
}

void FPCGExToolsetPropertiesModule::ShutdownModule()
{
	MediatorDomain.Reset();
	PCGExPropertyMediator::UnregisterBuiltInHooks();
	PCGExPropertyCatalog::Shutdown();

	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExToolsetPropertiesModule, PCGExToolsetProperties)
