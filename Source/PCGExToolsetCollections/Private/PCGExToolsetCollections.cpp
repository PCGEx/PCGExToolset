// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExToolsetCollections.h"

#include "Mediator/PCGExCollectionsMediatorBindings.h"
#include "Mediator/PCGExCollectionsMediatorFormats.h"
#include "Mediator/PCGExCollectionsMediatorGuide.h"
#include "Mediator/PCGExCollectionsMediatorHooks.h"

void FPCGExToolsetCollectionsModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	// Value hooks for the collection property types, entry formats (one per registered collection type, siblings
	// included), the collection / Distribute Tuple bindings and the guide.
	PCGExCollectionsMediator::RegisterHooks();
	PCGExCollectionsMediatorFormats::Register(MediatorDomain);
	PCGExCollectionsMediatorBindings::Register(MediatorDomain);
	PCGExCollectionsMediatorGuide::Register(MediatorDomain);
}

void FPCGExToolsetCollectionsModule::ShutdownModule()
{
	PCGExCollectionsMediatorFormats::Unregister();
	MediatorDomain.Reset();
	PCGExCollectionsMediator::UnregisterHooks();

	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExToolsetCollectionsModule, PCGExToolsetCollections)
