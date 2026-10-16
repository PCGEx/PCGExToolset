// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "PCGExMediatorDumpCommandlet.generated.h"

/**
 * Writes PCGExMediator::DumpAsJson to a file for the docs pipeline.
 * UnrealEditor-Cmd <project>.uproject -run=PCGExMediatorDump -out=<file>; the default file is Saved/PCGExMediator/dump.json.
 * Needs the editor target: the domain editor modules register the formats and guides at startup.
 */
UCLASS()
class PCGEXMEDIATOR_API UPCGExMediatorDumpCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UPCGExMediatorDumpCommandlet();

	virtual int32 Main(const FString& Params) override;
};
