// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExMediatorParams.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

const TCHAR* const FPCGExMediatorParams::Segment = TEXT("params");

FPCGExMediatorParams::FPCGExMediatorParams(const FJsonObject& InJson)
	: Json(InJson)
{
}

FPCGExMediatorParams::FPCGExMediatorParams(const FJsonObject& InJson, const bool bInRoot)
	: Json(InJson), bRoot(bInRoot)
{
}

FPCGExMediatorParams FPCGExMediatorParams::Nested(const FJsonObject& InJson)
{
	return FPCGExMediatorParams(InJson, false);
}

bool FPCGExMediatorParams::Has(const TCHAR* Key) const
{
	return Json.HasField(Key);
}

TSharedPtr<FJsonValue> FPCGExMediatorParams::Field(const TCHAR* Key) const
{
	return Json.TryGetField(Key);
}

FString FPCGExMediatorParams::PathOf(const TCHAR* Key) const
{
	return bRoot ? FString::Printf(TEXT("%s/%s"), Segment, Key) : FString(Key);
}

void FPCGExMediatorParams::Report(const EPCGExMediatorSeverity Severity, const TCHAR* Key, const FString& Message) const
{
	PCGExMediator::Report(Severity, PathOf(Key), Message);
}

void FPCGExMediatorParams::Report(const EPCGExMediatorSeverity Severity, const FString& Message) const
{
	if (bRoot) { PCGExMediator::Report(Severity, Segment, Message); }
	else { PCGExMediator::Report(Severity, Message); }
}

bool FPCGExMediatorParams::ReadEnum(const TCHAR* Key, const UEnum* Enum, int64& InOut, bool* bOutPresent) const
{
	const TSharedPtr<FJsonValue> Value = Field(Key);
	if (bOutPresent) { *bOutPresent = Value.IsValid(); }
	if (!Value.IsValid()) { return true; }
	const FPCGExMediatorParamScope Scope(*this, Key);
	return PCGExMediator::Values::DecodeEnum(Enum, Value, InOut);
}

bool FPCGExMediatorParams::ReadArray(const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>*& Out, const TCHAR* Expected) const
{
	Out = nullptr;
	const TSharedPtr<FJsonValue> Value = Field(Key);
	if (!Value.IsValid()) { return true; }
	if (Value->TryGetArray(Out)) { return true; }
	Out = nullptr;
	Report(EPCGExMediatorSeverity::Error, Key, Expected ? FString(Expected) : FString(TEXT("expected an array")));
	return false;
}

bool FPCGExMediatorParams::ReadObject(const TCHAR* Key, const TSharedPtr<FJsonObject>*& Out, const TCHAR* Expected) const
{
	Out = nullptr;
	const TSharedPtr<FJsonValue> Value = Field(Key);
	if (!Value.IsValid()) { return true; }
	if (Value->TryGetObject(Out)) { return true; }
	Out = nullptr;
	Report(EPCGExMediatorSeverity::Error, Key, Expected ? FString(Expected) : FString(TEXT("expected an object")));
	return false;
}
