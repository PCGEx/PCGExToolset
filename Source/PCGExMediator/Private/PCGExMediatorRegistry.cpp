// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorRegistry.h"

#include "PCGExLog.h"
#include "JsonObjectStructInterface.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"

namespace PCGExMediatorRegistry
{
	struct FStorage
	{
		TMap<FName, TSharedRef<FPCGExMediatorFormat>> Formats;
		TMap<const UClass*, TSharedRef<FPCGExMediatorBinding>> Bindings;
		TMap<FName, TSharedRef<FPCGExMediatorAction>> Actions;
		TArray<TSharedRef<FPCGExMediatorGuide>> Guides;
		TArray<TSharedRef<FPCGExMediatorPresenter>> Presenters;
	};

	FStorage& Storage()
	{
		check(IsInGameThread());
		static FStorage Instance;
		return Instance;
	}
}

#pragma region FPCGExMediatorRegistry

void FPCGExMediatorRegistry::RegisterFormat(const FPCGExMediatorFormat& InFormat)
{
	if (!InFormat.IsValid())
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register format '%s': id, struct and converter are all required."), *InFormat.Id.ToString());
		return;
	}

	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (S.Formats.Contains(InFormat.Id))
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Format '%s' registered twice; the later registration wins."), *InFormat.Id.ToString());
		UnregisterFormat(InFormat.Id);
	}

	FJsonObjectStructInterfaceRegistry::RegisterStructConverter(InFormat.Struct, InFormat.Converter);
	S.Formats.Add(InFormat.Id, MakeShared<FPCGExMediatorFormat>(InFormat));
}

void FPCGExMediatorRegistry::UnregisterFormat(const FName InId)
{
	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (const TSharedRef<FPCGExMediatorFormat>* Found = S.Formats.Find(InId))
	{
		// Key only: UObjects may already be gone at module shutdown.
		FJsonObjectStructInterfaceRegistry::UnregisterStructConverter((*Found)->Struct);
		S.Formats.Remove(InId);
	}
}

TSharedPtr<const FPCGExMediatorFormat> FPCGExMediatorRegistry::FindFormat(const FName InId)
{
	if (const TSharedRef<FPCGExMediatorFormat>* Found = PCGExMediatorRegistry::Storage().Formats.Find(InId))
	{
		return *Found;
	}
	return nullptr;
}

TSharedPtr<const FPCGExMediatorFormat> FPCGExMediatorRegistry::FindFormatForStruct(const UScriptStruct* InStruct)
{
	for (const TPair<FName, TSharedRef<FPCGExMediatorFormat>>& Pair : PCGExMediatorRegistry::Storage().Formats)
	{
		if (Pair.Value->Struct == InStruct)
		{
			return Pair.Value;
		}
	}
	return nullptr;
}

void FPCGExMediatorRegistry::GetFormats(TArray<TSharedPtr<const FPCGExMediatorFormat>>& OutFormats)
{
	for (const TPair<FName, TSharedRef<FPCGExMediatorFormat>>& Pair : PCGExMediatorRegistry::Storage().Formats)
	{
		OutFormats.Add(Pair.Value);
	}
	OutFormats.Sort([](const TSharedPtr<const FPCGExMediatorFormat>& A, const TSharedPtr<const FPCGExMediatorFormat>& B)
	{
		return A->Id.LexicalLess(B->Id);
	});
}

void FPCGExMediatorRegistry::RegisterBinding(const FPCGExMediatorBinding& InBinding)
{
	if (!InBinding.IsValid())
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register a binding without a host class or members."));
		return;
	}

	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (S.Bindings.Contains(InBinding.HostClass))
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Binding for '%s' registered twice; the later registration wins."), *InBinding.HostClass->GetName());
	}
	S.Bindings.Add(InBinding.HostClass, MakeShared<FPCGExMediatorBinding>(InBinding));
}

void FPCGExMediatorRegistry::UnregisterBinding(const UClass* InHostClass)
{
	PCGExMediatorRegistry::Storage().Bindings.Remove(InHostClass);
}

TSharedPtr<const FPCGExMediatorBinding> FPCGExMediatorRegistry::FindBinding(const UClass* InHostClass)
{
	const PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	for (const UClass* Class = InHostClass; Class; Class = Class->GetSuperClass())
	{
		if (const TSharedRef<FPCGExMediatorBinding>* Found = S.Bindings.Find(Class))
		{
			return *Found;
		}
	}
	return nullptr;
}

void FPCGExMediatorRegistry::GetBindings(TArray<TSharedPtr<const FPCGExMediatorBinding>>& OutBindings)
{
	for (const TPair<const UClass*, TSharedRef<FPCGExMediatorBinding>>& Pair : PCGExMediatorRegistry::Storage().Bindings)
	{
		OutBindings.Add(Pair.Value);
	}
	OutBindings.Sort([](const TSharedPtr<const FPCGExMediatorBinding>& A, const TSharedPtr<const FPCGExMediatorBinding>& B)
	{
		return A->HostClass->GetName() < B->HostClass->GetName();
	});
}

void FPCGExMediatorRegistry::RegisterAction(const FPCGExMediatorAction& InAction)
{
	if (!InAction.IsValid())
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register action '%s': id and Run are required."), *InAction.Id.ToString());
		return;
	}

	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (S.Actions.Contains(InAction.Id))
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Action '%s' registered twice; the later registration wins."), *InAction.Id.ToString());
	}
	S.Actions.Add(InAction.Id, MakeShared<FPCGExMediatorAction>(InAction));
}

void FPCGExMediatorRegistry::UnregisterAction(const FName InId)
{
	PCGExMediatorRegistry::Storage().Actions.Remove(InId);
}

TSharedPtr<const FPCGExMediatorAction> FPCGExMediatorRegistry::FindAction(const FName InId)
{
	if (const TSharedRef<FPCGExMediatorAction>* Found = PCGExMediatorRegistry::Storage().Actions.Find(InId))
	{
		return *Found;
	}
	return nullptr;
}

void FPCGExMediatorRegistry::GetActions(TArray<TSharedPtr<const FPCGExMediatorAction>>& OutActions)
{
	for (const TPair<FName, TSharedRef<FPCGExMediatorAction>>& Pair : PCGExMediatorRegistry::Storage().Actions)
	{
		OutActions.Add(Pair.Value);
	}
	OutActions.Sort([](const TSharedPtr<const FPCGExMediatorAction>& A, const TSharedPtr<const FPCGExMediatorAction>& B)
	{
		return A->Id.LexicalLess(B->Id);
	});
}

bool FPCGExMediatorRegistry::RegisterGuide(const FPCGExMediatorGuide& InGuide)
{
	if (!InGuide.IsValid())
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register guide '%s': domain and text are required."), *InGuide.Domain.ToString());
		return false;
	}
	for (int32 i = 0; i < InGuide.Examples.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Doc;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InGuide.Examples[i]), Doc) || !Doc.IsValid())
		{
			UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register guide '%s': example %d is not a JSON object."), *InGuide.Domain.ToString(), i);
			return false;
		}
	}

	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (FindGuide(InGuide.Domain).IsValid())
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Guide '%s' registered twice; the later registration wins."), *InGuide.Domain.ToString());
		UnregisterGuide(InGuide.Domain);
	}
	S.Guides.Add(MakeShared<FPCGExMediatorGuide>(InGuide));
	return true;
}

void FPCGExMediatorRegistry::UnregisterGuide(const FName InDomain)
{
	// Key only: the host classes may already be gone at module shutdown.
	PCGExMediatorRegistry::Storage().Guides.RemoveAll([InDomain](const TSharedRef<FPCGExMediatorGuide>& Guide) { return Guide->Domain == InDomain; });
}

TSharedPtr<const FPCGExMediatorGuide> FPCGExMediatorRegistry::FindGuide(const FName InDomain)
{
	for (const TSharedRef<FPCGExMediatorGuide>& Guide : PCGExMediatorRegistry::Storage().Guides)
	{
		if (Guide->Domain == InDomain) { return Guide; }
	}
	return nullptr;
}

void FPCGExMediatorRegistry::GetGuides(TArray<TSharedPtr<const FPCGExMediatorGuide>>& OutGuides)
{
	for (const TSharedRef<FPCGExMediatorGuide>& Guide : PCGExMediatorRegistry::Storage().Guides)
	{
		OutGuides.Add(Guide);
	}
}

bool FPCGExMediatorRegistry::RegisterPresenter(const FPCGExMediatorPresenter& InPresenter)
{
	if (!InPresenter.IsValid())
	{
		UE_LOG(LogPCGEx, Error, TEXT("[Mediator] Refusing to register presenter '%s': a domain and Focus or Open are required."), *InPresenter.Domain.ToString());
		return false;
	}
	PCGExMediatorRegistry::FStorage& S = PCGExMediatorRegistry::Storage();
	if (S.Presenters.ContainsByPredicate([&InPresenter](const TSharedRef<FPCGExMediatorPresenter>& P) { return P->Domain == InPresenter.Domain; }))
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[Mediator] Presenter '%s' registered twice; the later registration wins."), *InPresenter.Domain.ToString());
		UnregisterPresenter(InPresenter.Domain);
	}
	S.Presenters.Add(MakeShared<FPCGExMediatorPresenter>(InPresenter));
	return true;
}

void FPCGExMediatorRegistry::UnregisterPresenter(const FName InDomain)
{
	PCGExMediatorRegistry::Storage().Presenters.RemoveAll([InDomain](const TSharedRef<FPCGExMediatorPresenter>& P) { return P->Domain == InDomain; });
}

void FPCGExMediatorRegistry::GetPresenters(TArray<TSharedPtr<const FPCGExMediatorPresenter>>& OutPresenters)
{
	for (const TSharedRef<FPCGExMediatorPresenter>& Presenter : PCGExMediatorRegistry::Storage().Presenters)
	{
		OutPresenters.Add(Presenter);
	}
}

#pragma endregion

#pragma region FPCGExMediatorDomain

FPCGExMediatorDomain::~FPCGExMediatorDomain()
{
	Reset();
}

void FPCGExMediatorDomain::AddFormat(const FPCGExMediatorFormat& Format, const TSharedPtr<const IJsonObjectStructConverter>& Owner)
{
	if (!Format.IsValid()) { return; }
	FPCGExMediatorRegistry::RegisterFormat(Format);
	FormatIds.Add(Format.Id);
	FormatStructs.Add(Format.Struct);
	if (Owner.IsValid()) { OwnedConverters.Add(Owner); }
}

void FPCGExMediatorDomain::AddBinding(const FPCGExMediatorBinding& Binding)
{
	if (!Binding.IsValid()) { return; }
	FPCGExMediatorRegistry::RegisterBinding(Binding);
	BindingClasses.Add(Binding.HostClass);
}

void FPCGExMediatorDomain::AddAction(const FPCGExMediatorAction& Action)
{
	if (!Action.IsValid()) { return; }
	FPCGExMediatorRegistry::RegisterAction(Action);
	ActionIds.Add(Action.Id);
}

void FPCGExMediatorDomain::AddGuide(const FPCGExMediatorGuide& Guide)
{
	if (!Guide.IsValid() || !FPCGExMediatorRegistry::RegisterGuide(Guide)) { return; }
	GuideDomains.Add(Guide.Domain);
}

void FPCGExMediatorDomain::AddPresenter(const FPCGExMediatorPresenter& Presenter)
{
	if (!Presenter.IsValid() || !FPCGExMediatorRegistry::RegisterPresenter(Presenter)) { return; }
	PresenterDomains.Add(Presenter.Domain);
}

bool FPCGExMediatorDomain::HasFormatForStruct(const UScriptStruct* Struct) const
{
	return FormatStructs.Contains(Struct);
}

void FPCGExMediatorDomain::Reset()
{
	for (int32 i = PresenterDomains.Num() - 1; i >= 0; --i) { FPCGExMediatorRegistry::UnregisterPresenter(PresenterDomains[i]); }
	PresenterDomains.Reset();
	for (int32 i = GuideDomains.Num() - 1; i >= 0; --i) { FPCGExMediatorRegistry::UnregisterGuide(GuideDomains[i]); }
	for (int32 i = ActionIds.Num() - 1; i >= 0; --i) { FPCGExMediatorRegistry::UnregisterAction(ActionIds[i]); }
	for (int32 i = BindingClasses.Num() - 1; i >= 0; --i) { FPCGExMediatorRegistry::UnregisterBinding(BindingClasses[i]); }
	for (int32 i = FormatIds.Num() - 1; i >= 0; --i) { FPCGExMediatorRegistry::UnregisterFormat(FormatIds[i]); }
	GuideDomains.Reset();
	ActionIds.Reset();
	BindingClasses.Reset();
	FormatIds.Reset();
	FormatStructs.Reset();
	OwnedConverters.Reset();
}

#pragma endregion
