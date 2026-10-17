// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class AActor;
class FJsonObject;
class FJsonValue;
class UWorld;

/**
 * The open level as a mediator subject: what in it is a document host or an action target, and spawning actors into
 * it. The spawn is the mechanics only (transactional actor, label, folder); a domain action wraps it with the class's
 * own creation rules, which a generic spawn cannot know.
 */
namespace PCGExMediator
{
	/** The level editor's world; null outside the editor. */
	PCGEXMEDIATOR_API UWorld* GetEditorWorld();

	/** { world, path, dirty, count, hosts: [{ kind, class, path, label, owner? }] }: every actor and component whose class is bound or is an action's target. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> DescribeLevel(const UWorld* World);

	/** A transactional actor of Class at Transform, labelled and filed when asked; null with a diagnostic (bad class, game world, spawn refused). Transacts with the caller. */
	PCGEXMEDIATOR_API AActor* SpawnEditorActor(UWorld* World, UClass* Class, const FTransform& Transform, const FString& Label = FString(), FName Folder = NAME_None);

	/** The library's spawn: a class by name or path into the editor world, transform in the dialect (null = identity), under its own transaction; { actor, label, class } or null. */
	PCGEXMEDIATOR_API TSharedPtr<FJsonObject> SpawnActorFromRequest(const FString& ClassNameOrPath, const TSharedPtr<FJsonValue>& TransformJson, const FString& Label);
}
