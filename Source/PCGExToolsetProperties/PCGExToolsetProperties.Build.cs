// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

// Properties authoring surface: the per-type value hook registry, the property-schema / overrides / choices formats,
// the schema-asset and Tuple bindings, the align-choices action and the properties guide. Sibling domains register
// their FPCGExProperty subtypes into the hook registry this module owns.
public class PCGExToolsetProperties : ModuleRules
{
	public PCGExToolsetProperties(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = System.Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH"));
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"PCG",
				"PCGExCore",
				"PCGExCoreEditor",
				"PCGExProperties",
				"PCGExPropertiesEditor",
				"PCGExMediator",
				"Json",
				"JsonUtilities"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd"
			}
		);
	}
}
