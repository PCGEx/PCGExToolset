// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediator.h"

#include "PCGExLog.h"
#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorDump.h"
#include "PCGExMediatorFocus.h"
#include "PCGExMediatorLookup.h"
#include "PCGExMediatorTransport.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "FPCGExMediatorModule"

namespace PCGExMediatorCommands
{
	using namespace PCGExMediator;

	void List(const TArray<FString>& Args)
	{
		UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(ListAsJson()));
	}

	void Guides(const TArray<FString>& Args)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("guides"), GuidesAsJson());
		UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(Root));
	}

	void Describe(const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.describe <format id | action id | class | struct | target>"));
			return;
		}

		TSharedPtr<FJsonObject> Schema = DescribeAny(Args[0]);
		if (!Schema.IsValid())
		{
			// Not a type: a bound instance describes what it offers.
			FPCGExMediatorDiagnostics Diagnostics;
			FScope Scope(Diagnostics);
			if (const UObject* Target = ResolveTarget(Args[0])) { Schema = DescribeTarget(Target); }
		}
		if (!Schema.IsValid())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] '%s' is neither a registered format, an action, a bound class, a struct nor a bound target."), *Args[0]);
			return;
		}
		UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(Schema.ToSharedRef()));
	}

	void Action(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.action <action id> <target> [params json]"));
			return;
		}

		FPCGExMediatorDiagnostics Diagnostics;
		TSharedPtr<FJsonObject> Result;
		{
			FScope Scope(Diagnostics);
			const TSharedPtr<FJsonObject> Params = Args.Num() > 2 ? FromString(FString::Join(TArrayView<const FString>(Args).RightChop(2), TEXT(" "))) : nullptr;
			if (Args.Num() <= 2 || Params.IsValid())
			{
				if (UObject* Target = ResolveTarget(Args[1])) { Result = RunAction(SafeName(Args[0]), Target, Params); }
			}
		}
		LogDiagnostics(Diagnostics);
		if (Result.IsValid())
		{
			UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(Result.ToSharedRef()));
		}
	}

	void Open(const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.open <target | @focused | @selected>"));
			return;
		}

		FPCGExMediatorDiagnostics Diagnostics;
		TSharedPtr<FJsonObject> Result;
		{
			FScope Scope(Diagnostics);
			if (UObject* Target = ResolveTarget(Args[0])) { Result = OpenTarget(Target); }
		}
		LogDiagnostics(Diagnostics);
		if (Result.IsValid())
		{
			UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(Result.ToSharedRef()));
		}
	}

	void Export(const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.export <target> [file]"));
			return;
		}

		FPCGExMediatorDiagnostics Diagnostics;
		TSharedPtr<FJsonObject> Doc;
		{
			FScope Scope(Diagnostics);
			if (UObject* Target = ResolveTarget(Args[0]))
			{
				Doc = ExportObject(Target);
				if (Doc.IsValid() && Args.Num() > 1) { WriteFile(Args[1], Doc.ToSharedRef()); }
			}
		}
		LogDiagnostics(Diagnostics);
		if (Doc.IsValid() && Args.Num() == 1)
		{
			UE_LOG(LogPCGEx, Log, TEXT("%s"), *ToString(Doc.ToSharedRef()));
		}
	}

	void Import(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.import <target> <file>"));
			return;
		}

		FPCGExMediatorDiagnostics Diagnostics;
		bool bOk = false;
		{
			FScope Scope(Diagnostics);
			if (UObject* Target = ResolveTarget(Args[0]))
			{
				if (const TSharedPtr<FJsonObject> Doc = ReadFile(Args[1]))
				{
					bOk = ImportObject(*Doc, Target);
				}
			}
		}
		LogDiagnostics(Diagnostics);
		UE_LOG(LogPCGEx, Log, TEXT("[Mediator] import %s"), bOk ? TEXT("succeeded") : TEXT("failed"));
	}

	void Dump(const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] usage: pcgex.mediator.dump <file>"));
			return;
		}
		DumpToFile(Args[0]);
	}
}

void FPCGExMediatorModule::StartupModule()
{
	IPCGExModuleInterface::StartupModule();

	IConsoleManager& Console = IConsoleManager::Get();
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.list"), TEXT("List the registered JSON formats and object bindings."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::List), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.guides"), TEXT("Print every domain's authoring guide: text and example documents."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Guides), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.describe"), TEXT("Print the JSON Schema of a format id, a bound class or a struct. Usage: pcgex.mediator.describe <format id | class | struct>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Describe), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.export"), TEXT("Export a bound object as JSON, to the log or a file. Usage: pcgex.mediator.export <asset path | graph path:node> [file]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Export), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.import"), TEXT("Import a JSON file into a bound object (undoable). Usage: pcgex.mediator.import <asset path | graph path:node> <file>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Import), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.action"), TEXT("Run a registered action on a bound object (undoable). Usage: pcgex.mediator.action <action id> <asset path | graph path:node> [params json]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Action), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.dump"), TEXT("Write every format, binding, action and guide with their schemas to a JSON file (the docs pipeline's input). Usage: pcgex.mediator.dump <file>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Dump), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("pcgex.mediator.open"), TEXT("Bring a target in front of the user: asset editor, graph node, level actor, or a domain's own mode. Usage: pcgex.mediator.open <target | @focused | @selected>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PCGExMediatorCommands::Open), ECVF_Default));
}

void FPCGExMediatorModule::ShutdownModule()
{
	IConsoleManager& Console = IConsoleManager::Get();
	for (IConsoleObject* Command : ConsoleCommands)
	{
		if (Command) { Console.UnregisterConsoleObject(Command); }
	}
	ConsoleCommands.Reset();

	IPCGExModuleInterface::ShutdownModule();
}

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExMediatorModule, PCGExMediator)
