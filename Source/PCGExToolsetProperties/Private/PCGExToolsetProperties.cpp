// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExToolsetProperties.h"

void FPCGExToolsetPropertiesModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();
	// TODO: register the Properties mediator surface (hooks, formats, bindings, actions, guide) once it moves here from PCGExPropertiesEditor.
}

void FPCGExToolsetPropertiesModule::ShutdownModule()
{
	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExToolsetPropertiesModule, PCGExToolsetProperties)
