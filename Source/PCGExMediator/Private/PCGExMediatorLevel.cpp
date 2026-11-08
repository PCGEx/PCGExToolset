// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorLevel.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorLookup.h"
#include "PCGExMediatorRegistry.h"
#include "PCGExMediatorValues.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace PCGExMediatorLevel
{
	using namespace PCGExMediator;

	bool IsActionTarget(const UClass* Class, const TArray<TSharedPtr<const FPCGExMediatorAction>>& Actions)
	{
		for (const TSharedPtr<const FPCGExMediatorAction>& Action : Actions)
		{
			if (Action->TargetClass && Class->IsChildOf(Action->TargetClass)) { return true; }
		}
		return false;
	}

	/** A document host or an action target. */
	bool IsHostClass(const UClass* Class, const TArray<TSharedPtr<const FPCGExMediatorAction>>& Actions)
	{
		return Class && (FPCGExMediatorRegistry::FindBinding(Class).IsValid() || IsActionTarget(Class, Actions));
	}

	TSharedRef<FJsonObject> Row(const TCHAR* Kind, const UObject* Object, const AActor* Owner)
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("kind"), Kind);
		R->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
		R->SetStringField(TEXT("path"), Object->GetPathName());
		R->SetStringField(TEXT("label"), Owner ? Owner->GetActorNameOrLabel() : Object->GetName());
		if (Owner && Owner != Object) { R->SetStringField(TEXT("owner"), Owner->GetPathName()); }
		return R;
	}
}

UWorld* PCGExMediator::GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

bool PCGExMediator::RequireOpenLevel(const UWorld* World)
{
	if (!World)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("no world"));
		return false;
	}
	// Not open: never initialized, or Inactive (a map the editor initialized on load); TActorIterator lists either.
	if (!World->IsInitialized() || World->WorldType == EWorldType::Inactive)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("level is not open; open it in the editor to describe it"));
		return false;
	}
	return true;
}

bool PCGExMediator::RequireHostLevelOpen(const UObject* Host)
{
	return !Host || !Host->GetTypedOuter<ULevel>() || RequireOpenLevel(Host->GetWorld());
}

TSharedPtr<FJsonObject> PCGExMediator::DescribeLevel(const UWorld* World)
{
	using namespace PCGExMediatorLevel;

	if (!RequireOpenLevel(World)) { return nullptr; }
	TArray<TSharedPtr<const FPCGExMediatorAction>> Actions;
	FPCGExMediatorRegistry::GetActions(Actions);

	TArray<TSharedPtr<FJsonValue>> Hosts;
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor->IsTemplate()) { continue; }
		if (IsHostClass(Actor->GetClass(), Actions)) { Hosts.Add(MakeShared<FJsonValueObject>(Row(TEXT("actor"), Actor, Actor))); }
		for (const UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && IsHostClass(Component->GetClass(), Actions)) { Hosts.Add(MakeShared<FJsonValueObject>(Row(TEXT("component"), Component, Actor))); }
		}
	}

	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	S->SetStringField(TEXT("world"), World->GetName());
	S->SetStringField(TEXT("path"), World->GetPathName());
	S->SetBoolField(TEXT("dirty"), World->GetOutermost()->IsDirty());
	S->SetNumberField(TEXT("count"), Hosts.Num());
	S->SetArrayField(TEXT("hosts"), Hosts);
	S->SetStringField(TEXT("note"), TEXT("hosts are the actors and components whose class has a document binding or is an action target; a domain's describe-level action (when it has one) adds the domain's own state"));
	return S;
}

AActor* PCGExMediator::SpawnEditorActor(UWorld* World, UClass* Class, const FTransform& Transform, const FString& Label, const FName Folder)
{
	if (!World || World->IsGameWorld())
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("no editor world to spawn into"));
		return nullptr;
	}
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s is not an actor class"), Class ? *Class->GetName() : TEXT("<none>")));
		return nullptr;
	}
	if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("%s is abstract or deprecated"), *Class->GetName()));
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transactional;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor(Class, &Transform, Params);
	if (!Actor)
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("the world refused to spawn a %s"), *Class->GetName()));
		return nullptr;
	}
	if (!Label.IsEmpty()) { Actor->SetActorLabel(Label); }
	if (!Folder.IsNone()) { Actor->SetFolderPath(Folder); }
	return Actor;
}

TSharedPtr<FJsonObject> PCGExMediator::SpawnActorFromRequest(const FString& ClassNameOrPath, const TSharedPtr<FJsonValue>& TransformJson, const FString& Label)
{
	check(IsInGameThread());

	UClass* Class = FindType<UClass>(ClassNameOrPath);
	if (!Class)
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a class"), *ClassNameOrPath.Left(128)));
		return nullptr;
	}
	FTransform Transform = FTransform::Identity;
	if (TransformJson.IsValid() && TransformJson->Type != EJson::Null)
	{
		FPathScope P(TEXT("transform"));
		if (!Values::Decode<FTransform>(TransformJson, Transform)) { return nullptr; }
	}

	const FScopedTransaction Transaction(NSLOCTEXT("PCGExMediator", "SpawnActor", "PCGEx Mediator Spawn"));
	AActor* Actor = SpawnEditorActor(GetEditorWorld(), Class, Transform, Label);
	if (!Actor) { return nullptr; }

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("actor"), Actor->GetPathName());
	Out->SetStringField(TEXT("label"), Actor->GetActorNameOrLabel());
	Out->SetStringField(TEXT("class"), Class->GetPathName());
	Out->SetBoolField(TEXT("bound"), FPCGExMediatorRegistry::FindBinding(Class).IsValid());
	return Out;
}
