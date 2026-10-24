// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMediatorDiagnostics.h"
#include "PCGExMediatorValues.h"

class FJsonObject;
class FJsonValue;
class UEnum;

/**
 * An action's parameters, the one way an action reads them. Every scope, read and report about a key is pathed
 * "params/<key>", and the hub pushes nothing around Run, so a keyless Report is a run-time finding at the action
 * root (or under a stage segment the action pushes): an agent tells a request it must fix from an asset it must look at.
 */
class PCGEXMEDIATOR_API FPCGExMediatorParams
{
public:
	static const TCHAR* const Segment;

	/** The root parameters of an action: keys are pathed params/<key>. */
	explicit FPCGExMediatorParams(const FJsonObject& InJson);

	/** A record inside a parameter (an array item, a sub-object): keys are pathed <key> under the scope the caller opened for it. */
	static FPCGExMediatorParams Nested(const FJsonObject& InJson);

	bool Has(const TCHAR* Key) const;

	/** Null when absent; a JSON null is present. */
	TSharedPtr<FJsonValue> Field(const TCHAR* Key) const;

	/** The path segment reads of Key report under: "params/<key>" at the root, "<key>" in a nested record. */
	FString PathOf(const TCHAR* Key) const;

	/** Reports about one key, at its path. */
	void Report(EPCGExMediatorSeverity Severity, const TCHAR* Key, const FString& Message) const;

	/** Reports about the parameters as a whole (keys that only make sense together), at "params" or the nested record's path. */
	void Report(EPCGExMediatorSeverity Severity, const FString& Message) const;

	/** Key through the value dialect. Absent leaves InOut untouched and succeeds (bOutPresent says so); a wrong shape is reported at the key. */
	template <typename T>
	bool Read(const TCHAR* Key, T& InOut, bool* bOutPresent = nullptr) const;

	/** Read, then an Error "missing" (": Hint" appended) when the key is absent. */
	template <typename T>
	bool Require(const TCHAR* Key, T& Out, const TCHAR* Hint = nullptr) const;

	bool ReadEnum(const TCHAR* Key, const UEnum* Enum, int64& InOut, bool* bOutPresent = nullptr) const;

	/** Absent leaves Out null and succeeds; anything but an array is reported at the key (Expected, else "expected an array"). */
	bool ReadArray(const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>*& Out, const TCHAR* Expected = nullptr) const;

	/** Absent leaves Out null and succeeds; anything but an object is reported at the key (Expected, else "expected an object"). */
	bool ReadObject(const TCHAR* Key, const TSharedPtr<FJsonObject>*& Out, const TCHAR* Expected = nullptr) const;

private:
	FPCGExMediatorParams(const FJsonObject& InJson, bool bInRoot);

	const FJsonObject& Json;
	bool bRoot = true;
};

/** Pushes a parameter's path for its lifetime; nested FPathScopes and keyless reports continue under it. */
class FPCGExMediatorParamScope : public PCGExMediator::FPathScope
{
public:
	FPCGExMediatorParamScope(const FPCGExMediatorParams& Params, const TCHAR* Key)
		: FPathScope(Params.PathOf(Key))
	{
	}
};

template <typename T>
bool FPCGExMediatorParams::Read(const TCHAR* Key, T& InOut, bool* bOutPresent) const
{
	const TSharedPtr<FJsonValue> Value = Field(Key);
	if (bOutPresent) { *bOutPresent = Value.IsValid(); }
	if (!Value.IsValid()) { return true; }
	const FPCGExMediatorParamScope Scope(*this, Key);
	return PCGExMediator::Values::Decode<T>(Value, InOut);
}

template <typename T>
bool FPCGExMediatorParams::Require(const TCHAR* Key, T& Out, const TCHAR* Hint) const
{
	bool bPresent = false;
	if (!Read(Key, Out, &bPresent)) { return false; }
	if (bPresent) { return true; }
	Report(EPCGExMediatorSeverity::Error, Key, Hint ? FString::Printf(TEXT("missing: %s"), Hint) : FString(TEXT("missing")));
	return false;
}
