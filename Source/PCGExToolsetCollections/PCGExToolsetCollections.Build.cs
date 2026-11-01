// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

// Collections authoring surface: entry formats per registered collection type, Omni and Variant rows, asset staging,
// the collection-base, Variant and Distribute Tuple bindings, the material-slots read action, the CollectionEntry /
// Range value hooks and the collections guide.
public class PCGExToolsetCollections : ModuleRules
{
	public PCGExToolsetCollections(ReadOnlyTargetRules Target) : base(Target)
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
				"PCGExCollections",
				"PCGExCollectionsEditor",
				"PCGExToolsetProperties",
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
