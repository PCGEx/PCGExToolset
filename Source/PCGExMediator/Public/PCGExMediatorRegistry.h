// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FPCGExMediatorParams;
struct IJsonObjectStructConverter;

/**
 * One JSON document kind: a struct, the converter that owns its JSON shape, and its self-description.
 * Registering a format also registers the converter with the engine's FJsonObjectStructInterfaceRegistry, so
 * the shape applies wherever the struct appears -- nested in any host, and through Epic's toolset library.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorFormat
{
	/** Envelope id, e.g. "pcgex.property-schema". */
	FName Id = NAME_None;

	/** Document version this converter writes and the highest it reads. */
	int32 Version = 1;

	const UScriptStruct* Struct = nullptr;

	/** Alive for the lifetime of the registration (FPCGExMediatorDomain::AddFormat owns it for you). */
	const IJsonObjectStructConverter* Converter = nullptr;

	/** JSON Schema of the document body (the envelope's "data"). */
	TFunction<TSharedPtr<FJsonObject>()> Describe;

	/** One line for listings. */
	FString Summary;

	bool IsValid() const
	{
		return !Id.IsNone() && Struct != nullptr && Converter != nullptr;
	}
};

/** Decoded value of a member earlier in binding order, not yet applied; null when the document did not carry it. */
using FPCGExMediatorDecodedMember = TFunctionRef<const void*(FName Member)>;

/**
 * How an object is authored through the mediator: which of its members travel, in what order, and how to
 * wrap a member's decode when the domain needs context around it (e.g. a schema scope while rows decode).
 * Every member decodes into scratch first; then all are applied in order through the host's own edit hooks.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorBinding
{
	const UClass* HostClass = nullptr;

	/** Export and import order. Each must be a UPROPERTY of HostClass (or a parent). */
	TArray<FName> Members;

	/** Optional. Invokes Import exactly once, or reports an Error and skips it to refuse; Decoded gives the scratch value of an earlier member (null = the live one stands). */
	TFunction<void(UObject* Host, FName Member, FPCGExMediatorDecodedMember Decoded, TFunctionRef<void()> Import)> WrapImport;

	/**
	 * Optional. Runs once every present member has decoded and before any is applied, under the import's diagnostics;
	 * Decoded gives each member's scratch (null = the live value stands). An Error reported here refuses the import,
	 * host untouched: the place for checks that span members or the whole document, which a converter cannot see.
	 */
	TFunction<void(const UObject* Host, FPCGExMediatorDecodedMember Decoded)> Validate;

	/** Optional. Runs after every member applied, inside the import transaction, for host work no edit hook covers. */
	TFunction<void(UObject* Host)> PostImport;

	/**
	 * Optional. What THIS instance offers an author, as data (not a schema): e.g. a schema host's resolved properties
	 * with their effective choices and lock. Null with an Error refuses the host; null without one, or unset, falls back
	 * to the class's object schema.
	 */
	TFunction<TSharedPtr<FJsonObject>(const UObject* Host)> DescribeInstance;

	FString Summary;

	bool IsValid() const
	{
		return HostClass != nullptr && !Members.IsEmpty();
	}
};

/**
 * A named operation on a target object, run through the mediator under one transaction: what a document cannot
 * express (an editor lever such as snapping rows onto a property's choices). Registered by domain editor modules.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorAction
{
	/** e.g. "pcgex.align-choices". */
	FName Id = NAME_None;

	/** Targets must derive from it; null accepts any object. */
	const UClass* TargetClass = nullptr;

	/**
	 * Runs on the game thread inside a transaction named Summary. Returns the result object (null = failure); a
	 * "changed": false field cancels the transaction so nothing lands in the undo history. Parameters are read through
	 * Params only, which paths what it reports under params/<key>; a keyless Report is a run-time finding at the
	 * action root, or under a stage segment (FPathScope) the action pushes.
	 */
	TFunction<TSharedPtr<FJsonObject>(UObject* Target, const FPCGExMediatorParams& Params)> Run;

	/** JSON Schema of Params. */
	TFunction<TSharedPtr<FJsonObject>()> DescribeParams;

	FString Summary;

	bool IsValid() const
	{
		return !Id.IsNone() && static_cast<bool>(Run);
	}
};

/**
 * A domain's authoring guide: the words that travel with its formats, so an agent learns how to think about the
 * hosts (identity, merge rules, which actions exist and when) from the registry and never from its own text. The
 * domain that owns the model owns the words; the hub only lists them.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorGuide
{
	/** e.g. "properties"; one guide per domain, the later registration wins. */
	FName Domain = NAME_None;

	/** The bound classes the guide covers. */
	TArray<const UClass*> Hosts;

	/** Instructions an agent reads as-is: plain prose, one paragraph per rule. */
	FString Text;

	/** Complete documents ImportJson accepts, as JSON text; each must parse as an object or the guide is refused. */
	TArray<FString> Examples;

	bool IsValid() const
	{
		return !Domain.IsNone() && !Text.IsEmpty();
	}
};

/** One object a presenter considers "what the user is looking at", with where that reading came from. */
struct PCGEXMEDIATOR_API FPCGExMediatorFocusCandidate
{
	UObject* Object = nullptr;
	/** e.g. "sketch level mode: active component"; shown when several candidates compete. */
	FString Source;
};

/**
 * A domain's presence in the editor: where its hosts are shown and how to show one. Focus answers the target aliases
 * (@focused / @selected) from what the domain alone can see (its asset editor, its level mode); Open brings a host in
 * front of the user its own way (select the actor, enter the mode). Both optional; the hub runs its generic chain
 * (asset editors, level selection, Content Browser selection; asset activation) around them.
 */
struct PCGEXMEDIATOR_API FPCGExMediatorPresenter
{
	/** e.g. "cluster-sketch"; one presenter per domain, the later registration wins. */
	FName Domain = NAME_None;

	/** Appends the objects the domain considers focused right now, most specific first. */
	TFunction<void(TArray<FPCGExMediatorFocusCandidate>&)> Focus;

	/** Shows Target the domain's way and says how in OutHow; false when the domain does not present this object. */
	TFunction<bool(UObject* Target, FString& OutHow)> Open;

	bool IsValid() const
	{
		return !Domain.IsNone() && (static_cast<bool>(Focus) || static_cast<bool>(Open));
	}
};

/** Game-thread only (asserted); registrations happen in StartupModule and are removed in ShutdownModule. */
class PCGEXMEDIATOR_API FPCGExMediatorRegistry
{
public:
	static void RegisterFormat(const FPCGExMediatorFormat& InFormat);
	static void UnregisterFormat(FName InId);

	static TSharedPtr<const FPCGExMediatorFormat> FindFormat(FName InId);
	static TSharedPtr<const FPCGExMediatorFormat> FindFormatForStruct(const UScriptStruct* InStruct);
	static void GetFormats(TArray<TSharedPtr<const FPCGExMediatorFormat>>& OutFormats);

	static void RegisterBinding(const FPCGExMediatorBinding& InBinding);
	static void UnregisterBinding(const UClass* InHostClass);

	/** Walks up the class chain, so a binding on a base class serves its subclasses. */
	static TSharedPtr<const FPCGExMediatorBinding> FindBinding(const UClass* InHostClass);
	static void GetBindings(TArray<TSharedPtr<const FPCGExMediatorBinding>>& OutBindings);

	static void RegisterAction(const FPCGExMediatorAction& InAction);
	static void UnregisterAction(FName InId);
	static TSharedPtr<const FPCGExMediatorAction> FindAction(FName InId);
	static void GetActions(TArray<TSharedPtr<const FPCGExMediatorAction>>& OutActions);

	/** False (logged) when the guide is invalid or an example is not a JSON object. */
	static bool RegisterGuide(const FPCGExMediatorGuide& InGuide);
	static void UnregisterGuide(FName InDomain);
	static TSharedPtr<const FPCGExMediatorGuide> FindGuide(FName InDomain);

	/** In registration order: the order the domain modules started, which is the order an agent reads them. */
	static void GetGuides(TArray<TSharedPtr<const FPCGExMediatorGuide>>& OutGuides);

	/** False (logged) when the presenter has neither Focus nor Open. */
	static bool RegisterPresenter(const FPCGExMediatorPresenter& InPresenter);
	static void UnregisterPresenter(FName InDomain);
	static void GetPresenters(TArray<TSharedPtr<const FPCGExMediatorPresenter>>& OutPresenters);
};

/**
 * A domain's registrations, released together: AddFormat / AddBinding from StartupModule, Reset from
 * ShutdownModule (by key only, so it is safe after the UObjects are gone). Owns the converters it creates.
 */
class PCGEXMEDIATOR_API FPCGExMediatorDomain
{
public:
	FPCGExMediatorDomain() = default;
	~FPCGExMediatorDomain();

	FPCGExMediatorDomain(const FPCGExMediatorDomain&) = delete;
	FPCGExMediatorDomain& operator=(const FPCGExMediatorDomain&) = delete;

	/** Constructs the converter in place and registers the format; the domain keeps the converter alive. */
	template <typename TConverter, typename... TArgs>
	TConverter& AddFormat(const FName Id, const int32 Version, const UScriptStruct* Struct, TFunction<TSharedPtr<FJsonObject>()> Describe, FString Summary, TArgs&&... Args)
	{
		TSharedRef<TConverter> Converter = MakeShared<TConverter>(Forward<TArgs>(Args)...);
		FPCGExMediatorFormat Format;
		Format.Id = Id;
		Format.Version = Version;
		Format.Struct = Struct;
		Format.Converter = &Converter.Get();
		Format.Describe = MoveTemp(Describe);
		Format.Summary = MoveTemp(Summary);
		AddFormat(Format, Converter);
		return Converter.Get();
	}

	/** Registers a format whose converter Owner keeps alive (or the caller does, when Owner is null). */
	void AddFormat(const FPCGExMediatorFormat& Format, const TSharedPtr<const IJsonObjectStructConverter>& Owner = nullptr);
	void AddBinding(const FPCGExMediatorBinding& Binding);
	void AddAction(const FPCGExMediatorAction& Action);
	void AddGuide(const FPCGExMediatorGuide& Guide);
	void AddPresenter(const FPCGExMediatorPresenter& Presenter);

	bool HasFormatForStruct(const UScriptStruct* Struct) const;

	/** Unregisters everything in reverse order of registration. */
	void Reset();

private:
	TArray<FName> FormatIds;
	TArray<const UScriptStruct*> FormatStructs;
	TArray<const UClass*> BindingClasses;
	TArray<FName> ActionIds;
	TArray<FName> GuideDomains;
	TArray<FName> PresenterDomains;
	TArray<TSharedPtr<const IJsonObjectStructConverter>> OwnedConverters;
};
