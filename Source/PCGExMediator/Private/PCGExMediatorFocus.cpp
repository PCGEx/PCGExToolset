// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorFocus.h"

#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorRegistry.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "PCGEditor.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "Selection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

namespace PCGExMediatorFocus
{
	using namespace PCGExMediator;

	bool IsBoundClass(const UClass* Class)
	{
		return Class && FPCGExMediatorRegistry::FindBinding(Class).IsValid();
	}

	void Add(TArray<FPCGExMediatorFocusCandidate>& Out, UObject* Object, const FString& Source)
	{
		if (!Object || Out.ContainsByPredicate([Object](const FPCGExMediatorFocusCandidate& C) { return C.Object == Object; })) { return; }
		Out.Add({Object, Source});
	}

	void CollectPresenters(TArray<FPCGExMediatorFocusCandidate>& Out)
	{
		TArray<TSharedPtr<const FPCGExMediatorPresenter>> Presenters;
		FPCGExMediatorRegistry::GetPresenters(Presenters);
		for (const TSharedPtr<const FPCGExMediatorPresenter>& Presenter : Presenters)
		{
			if (!Presenter->Focus) { continue; }
			TArray<FPCGExMediatorFocusCandidate> Own;
			Presenter->Focus(Own);
			for (const FPCGExMediatorFocusCandidate& C : Own) { Add(Out, C.Object, Presenter->Domain.ToString() + TEXT(": ") + C.Source); }
		}
	}

	/** The most recently activated asset editor whose asset has a binding: one candidate at most. */
	void CollectAssetEditors(TArray<FPCGExMediatorFocusCandidate>& Out)
	{
		UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Subsystem) { return; }
		UObject* Latest = nullptr;
		FName LatestEditor;
		double LatestTime = -1.0;
		for (UObject* Asset : Subsystem->GetAllEditedAssets())
		{
			if (!Asset || !IsBoundClass(Asset->GetClass())) { continue; }
			IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Asset, false);
			if (!Instance) { continue; }
			if (const double Time = Instance->GetLastActivationTime(); Time > LatestTime)
			{
				LatestTime = Time;
				Latest = Asset;
				LatestEditor = Instance->GetEditorName();
			}
		}
		if (Latest) { Add(Out, Latest, FString::Printf(TEXT("asset editor %s, most recently activated"), *LatestEditor.ToString())); }
	}

	void CollectLevelSelection(TArray<FPCGExMediatorFocusCandidate>& Out)
	{
		if (!GEditor) { return; }
		TArray<UActorComponent*> Components;
		GEditor->GetSelectedComponents()->GetSelectedObjects<UActorComponent>(Components);
		for (UActorComponent* Component : Components)
		{
			if (IsBoundClass(Component->GetClass())) { Add(Out, Component, TEXT("selected component")); }
		}
		if (!Out.IsEmpty()) { return; }
		TArray<AActor*> Actors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(Actors);
		for (const AActor* Actor : Actors)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (IsBoundClass(Component->GetClass())) { Add(Out, Component, FString::Printf(TEXT("component of selected actor %s"), *Actor->GetActorNameOrLabel())); }
			}
		}
	}

	void CollectContentBrowser(TArray<FPCGExMediatorFocusCandidate>& Out)
	{
		FContentBrowserModule* Module = FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser"));
		if (!Module) { return; }
		TArray<FAssetData> Selected;
		Module->Get().GetSelectedAssets(Selected);
		for (const FAssetData& Data : Selected)
		{
			// The class resolves without loading the asset; only a bound one is loaded, which the user's selection asks for.
			if (!IsBoundClass(Data.GetClass())) { continue; }
			Add(Out, Data.GetAsset(), TEXT("Content Browser selection"));
		}
	}

	typedef void (*FStage)(TArray<FPCGExMediatorFocusCandidate>&);

	UObject* Resolve(const TConstArrayView<FStage> Stages, const TCHAR* NothingMessage)
	{
		for (const FStage Stage : Stages)
		{
			TArray<FPCGExMediatorFocusCandidate> Candidates;
			Stage(Candidates);
			if (Candidates.IsEmpty()) { continue; }
			if (Candidates.Num() == 1) { return Candidates[0].Object; }
			TArray<FString> Lines;
			for (const FPCGExMediatorFocusCandidate& C : Candidates) { Lines.Add(FString::Printf(TEXT("%s (%s)"), *C.Object->GetPathName(), *C.Source)); }
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("several candidates; name one by path: %s"), *FString::Join(Lines, TEXT("; "))));
			return nullptr;
		}
		Report(EPCGExMediatorSeverity::Error, NothingMessage);
		return nullptr;
	}

	/** The Content Browser's own activation: the type's preconditions (a save prompt, say) run before the editor opens. */
	bool OpenAsset(UObject* Asset, FString& OutHow)
	{
		UAssetEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!Subsystem) { return false; }
		const FAssetData Data = IAssetRegistry::Get()->GetAssetByObjectPath(FSoftObjectPath(Asset));
		if (Data.IsValid())
		{
			if (const TSharedPtr<IAssetTypeActions> Actions = IAssetTools::Get().GetAssetTypeActionsForClass(Asset->GetClass()).Pin())
			{
				if (Actions->GetValidAssetsForPreviewOrEdit(MakeArrayView(&Data, 1), false).IsEmpty())
				{
					Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("opening '%s' was declined by its asset type (or the user)"), *Asset->GetPathName()));
					return true;
				}
			}
		}
		if (!Subsystem->OpenEditorForAsset(Asset))
		{
			Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("no editor opened for '%s'"), *Asset->GetPathName()));
			return true;
		}
		if (FContentBrowserModule* Module = FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser"))) { Module->Get().SyncBrowserToAssets(TArray<UObject*>{Asset}, false, false); }
		const IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Asset, true);
		OutHow = FString::Printf(TEXT("asset editor %s, synced in the Content Browser"), Instance ? *Instance->GetEditorName().ToString() : TEXT("(unknown)"));
		return true;
	}

	/** A node's settings: open the graph and jump to the node. */
	bool OpenNode(UObject* Target, FString& OutHow)
	{
		const UPCGNode* Node = Cast<UPCGNode>(Target->GetOuter());
		UPCGGraph* Graph = Node ? Node->GetGraph() : nullptr;
		if (!Cast<UPCGSettings>(Target) || !Graph) { return false; }
		FString GraphHow;
		if (!OpenAsset(Graph, GraphHow)) { return false; }
		UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Graph, true);
		// FAssetEditorToolkit::GetEditorName is the toolkit's FName, "PCGEditor" for FPCGEditor; the cast is safe only then.
		if (Instance && Instance->GetEditorName() == FName(TEXT("PCGEditor")))
		{
			static_cast<FPCGEditor*>(Instance)->JumpToNode(Node);
			OutHow = FString::Printf(TEXT("graph editor on %s, jumped to node %s"), *Graph->GetPathName(), *Node->GetName());
		}
		else
		{
			OutHow = FString::Printf(TEXT("graph editor on %s (no node focus: %s)"), *Graph->GetPathName(), *GraphHow);
		}
		return true;
	}

	bool OpenComponent(UObject* Target, FString& OutHow)
	{
		UActorComponent* Component = Cast<UActorComponent>(Target);
		AActor* Actor = Component ? Component->GetOwner() : nullptr;
		if (!Actor || Component->IsTemplate() || !GEditor) { return false; }
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(Actor, true, true);
		GEditor->MoveViewportCamerasToActor(*Actor, true);
		OutHow = FString::Printf(TEXT("selected actor %s in the level and framed it"), *Actor->GetActorNameOrLabel());
		return true;
	}
}

bool PCGExMediator::IsTargetAlias(const FString& Target)
{
	return Target.StartsWith(TEXT("@"));
}

UObject* PCGExMediator::ResolveAlias(const FString& Alias)
{
	using namespace PCGExMediatorFocus;

	check(IsInGameThread());

	if (Alias.Equals(FocusedAlias, ESearchCase::IgnoreCase))
	{
		const FStage Stages[] = {&CollectPresenters, &CollectAssetEditors, &CollectLevelSelection, &CollectContentBrowser};
		return Resolve(Stages, TEXT("nothing is focused: no domain reports a live host, no asset editor is open on a bound asset, and neither the level nor the Content Browser selection holds one; name a target by path"));
	}
	if (Alias.Equals(SelectedAlias, ESearchCase::IgnoreCase))
	{
		const FStage Stages[] = {&CollectLevelSelection, &CollectContentBrowser};
		return Resolve(Stages, TEXT("nothing bound is selected in the level or the Content Browser; name a target by path"));
	}
	Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' is not a target alias; the aliases are %s and %s"), *Alias.Left(64), FocusedAlias, SelectedAlias));
	return nullptr;
}

TSharedPtr<FJsonObject> PCGExMediator::OpenTarget(UObject* Target)
{
	using namespace PCGExMediatorFocus;

	check(IsInGameThread());

	if (!Target)
	{
		Report(EPCGExMediatorSeverity::Error, TEXT("no target"));
		return nullptr;
	}

	FString How;
	bool bOpened = false;
	TArray<TSharedPtr<const FPCGExMediatorPresenter>> Presenters;
	FPCGExMediatorRegistry::GetPresenters(Presenters);
	for (const TSharedPtr<const FPCGExMediatorPresenter>& Presenter : Presenters)
	{
		if (Presenter->Open && Presenter->Open(Target, How))
		{
			bOpened = true;
			break;
		}
	}
	if (!bOpened) { bOpened = OpenNode(Target, How); }
	if (!bOpened)
	{
		UObject* Asset = Target->IsAsset() ? Target : Target->GetOutermostObject();
		if (Asset && Asset->IsAsset() && Asset->GetOutermost() != GetTransientPackage())
		{
			bOpened = OpenAsset(Asset, How);
			if (bOpened && Asset != Target) { How += FString::Printf(TEXT(" (the asset holding %s)"), *Target->GetName()); }
		}
	}
	if (!bOpened) { bOpened = OpenComponent(Target, How); }
	if (!bOpened)
	{
		Report(EPCGExMediatorSeverity::Error, FString::Printf(TEXT("'%s' cannot be opened: not a saved asset, a node, a level component, nor something a domain presents"), *Target->GetPathName()));
		return nullptr;
	}
	if (How.IsEmpty()) { return nullptr; } // a reported refusal on the way

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("target"), Target->GetPathName());
	Out->SetStringField(TEXT("opened"), How);
	return Out;
}
