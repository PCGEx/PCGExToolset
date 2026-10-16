// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorDumpCommandlet.h"

#include "PCGExMediatorDump.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UPCGExMediatorDumpCommandlet::UPCGExMediatorDumpCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UPCGExMediatorDumpCommandlet::Main(const FString& Params)
{
	FString OutPath;
	if (!FParse::Value(*Params, TEXT("out="), OutPath) || OutPath.IsEmpty())
	{
		OutPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PCGExMediator"), TEXT("dump.json"));
	}
	return PCGExMediator::DumpToFile(FPaths::ConvertRelativePathToFull(OutPath)) ? 0 : 1;
}
