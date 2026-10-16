// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExToolsetCollections.h"

void FPCGExToolsetCollectionsModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();
	// TODO: register the Collections mediator surface (hooks, formats, bindings, guide) once it moves here from PCGExCollectionsEditor.
}

void FPCGExToolsetCollectionsModule::ShutdownModule()
{
	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExToolsetCollectionsModule, PCGExToolsetCollections)
