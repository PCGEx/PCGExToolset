// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorGuide.h"

#include "PCGExMediatorRegistry.h"
#include "PCGExPropertySchemaAsset.h"
#include "Elements/PCGExTuple.h"

namespace PCGExPropertyMediatorGuide
{
	void Register(FPCGExMediatorDomain& Domain)
	{
		FPCGExMediatorGuide Guide;
		Guide.Domain = DomainId;
		Guide.Hosts = {UPCGExPropertySchemaAsset::StaticClass(), UPCGExTupleSettings::StaticClass()};
		Guide.Text =
			TEXT("PROPERTY SCHEMAS (format pcgex.property-schema). A schema declares named, typed properties: \"properties\" is the ")
			TEXT("complete local list, each { name, type, default?, choices? } plus the keys its type needs (enum, allowedClass, range); ")
			TEXT("\"imports\" lists Property Schema assets whose properties are inherited, \"importOverrides\" is { name: value } of ")
			TEXT("enabled values on inherited properties, \"choicePatches\" is { name: patch } editing inherited choices. The document ")
			TEXT("replaces the whole schema. Properties are identified by name: a renamed property is a new one, and rows lose the ")
			TEXT("old one's values.\n")
			TEXT("OVERRIDE SETS (pcgex.property-overrides) are { name: value } of ENABLED values; the document is the full enabled ")
			TEXT("set, so a name absent from it is disabled. Weighted rows (pcgex.weighted-property-overrides) are { weight, values }.\n")
			TEXT("HOSTS. Property Schema asset: member Collection (a schema). Tuple node: Composition (a schema) then Values ")
			TEXT("(override rows merged by position; rows the document lacks are removed). Rows decode against the Composition the ")
			TEXT("same document carries, so send both when types change.\n")
			TEXT("CHOICES. A property may carry choices ({ label, value } items, optionally locked). DescribeTarget lists a host's ")
			TEXT("resolved properties with their effective choices after every import patch; read it before writing values against ")
			TEXT("that host. The action pcgex.align-choices with params { \"property\": name } snaps every enabled row whose value ")
			TEXT("is none of the property's choices onto the default choice; run on a Property Schema asset it sweeps every loaded ")
			TEXT("asset, node and component importing it.\n")
			TEXT("VALUES travel in the dialect: enums by enumerator name, vectors as arrays, rotators as { pitch, yaw, roll }, assets ")
			TEXT("and classes as object paths. DescribeFormat(\"pcgex.property-schema\") carries every type's value shape under $defs/values.\n")
			TEXT("LAY WORDING. Users say field, column, attribute, setting, option or parameter for a property; template, preset, data ")
			TEXT("asset or definition for a Property Schema asset; table or rows for a Tuple. Route them here without asking what a schema is.\n")
			TEXT("TYPE FROM WORDING, first that fits: a number, amount, size, height, weight, anything with decimals or a unit -> Float; ")
			TEXT("how many, count, number of, whole numbers -> Int32; whether, has, is, on/off, yes/no -> Bool; one of these, either A ")
			TEXT("or B, a short list of words -> Name with choices; color, tint -> Color (an [r, g, b, a] array, 0..1); position, offset, ")
			TEXT("direction, scale -> Vector; rotation, facing -> Rotator; mesh, asset, material -> SoftObjectPath and actor class -> ")
			TEXT("SoftClassPath, with \"allowedClass\" when the kind is named; label or name -> Name for identifiers, free text -> String.\n")
			TEXT("CHOICES, NOT ENUM. A listed set of allowed values has no enum asset: declare a Name property with \"choices\": ")
			TEXT("{ \"locked\": true, \"items\": [{ \"label\", \"value\" }] } and a \"default\" of { \"choice\": \"<label>\" }. Use ")
			TEXT("type Enum only for an enum the user names or the project has; never invent an enum path.\n")
			TEXT("BOUNDS. Stated limits go in \"range\": { \"min\", \"max\", \"clampMin\": true, \"clampMax\": true } on a numeric ")
			TEXT("property, in Unreal units (centimeters).");
		Guide.Examples = {
			TEXT("{\"format\": \"pcgex.object\", \"version\": 1, \"data\": {\"class\": \"/Script/PCGExProperties.PCGExTupleSettings\", \"members\": {")
			TEXT("\"Composition\": {\"properties\": [{\"name\": \"Height\", \"type\": \"Float\", \"default\": 100}, {\"name\": \"Kind\", \"type\": \"Int32\", \"default\": 0}]}, ")
			TEXT("\"Values\": [{\"Height\": 120, \"Kind\": 1}, {\"Height\": 80}]}}}"),
			TEXT("{\"format\": \"pcgex.object\", \"version\": 1, \"data\": {\"class\": \"/Script/PCGExProperties.PCGExPropertySchemaAsset\", \"members\": {")
			TEXT("\"Collection\": {\"properties\": [{\"name\": \"Density\", \"type\": \"Float\", \"default\": 1}]}}}}"),
			TEXT("{\"format\": \"pcgex.object\", \"version\": 1, \"data\": {\"class\": \"/Script/PCGExProperties.PCGExPropertySchemaAsset\", \"members\": {\"Collection\": {\"properties\": [")
			TEXT("{\"name\": \"Height\", \"type\": \"Float\", \"default\": 1000, \"range\": {\"min\": 300, \"max\": 3000, \"clampMin\": true, \"clampMax\": true}}, ")
			TEXT("{\"name\": \"Style\", \"type\": \"Name\", \"default\": {\"choice\": \"Residential\"}, \"choices\": {\"locked\": true, \"items\": [")
			TEXT("{\"label\": \"Residential\", \"value\": \"Residential\"}, {\"label\": \"Commercial\", \"value\": \"Commercial\"}, {\"label\": \"Industrial\", \"value\": \"Industrial\"}]}}, ")
			TEXT("{\"name\": \"FacadeColor\", \"type\": \"Color\", \"default\": [0.8, 0.8, 0.75, 1]}, ")
			TEXT("{\"name\": \"Floors\", \"type\": \"Int32\", \"default\": 3, \"range\": {\"min\": 1, \"max\": 40, \"clampMin\": true, \"clampMax\": true}}, ")
			TEXT("{\"name\": \"FlatRoof\", \"type\": \"Bool\", \"default\": false}]}}}}")
		};
		Domain.AddGuide(Guide);
	}
}
