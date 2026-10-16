// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System;
using System.IO;
using UnrealBuildTool;

// Editor-side JSON authoring hub. Every consumer is editor-time (MCP, console, asset import/export, tests);
// domain modules in this plugin and its siblings register their models here from their own StartupModule.
// Runtime modules never depend on it.
public class PCGExMediator : ModuleRules
{
	public PCGExMediator(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bNoPCH = System.Environment.GetEnvironmentVariable("PCGEX_NO_PCH") == "1" || File.Exists(Path.Combine(ModuleDirectory, "..", "..", "Config", ".noPCH"));
		PCHUsage = bNoPCH ? PCHUsageMode.NoPCHs : PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = true;
		MinSourceFilesForUnityBuildOverride = 4;
		PrecompileForTargets = PrecompileTargetsType.Any;

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);

		PrivateIncludePaths.AddRange(
			new string[]
			{
			}
		);

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"PCG",
				"PCGExCore",
				"Json",
				"JsonUtilities"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"AssetTools", // CreateAsset through the class's factory; Open through the type's activation path
				"AssetRegistry", // Open resolves the FAssetData the activation path takes
				"ContentBrowser", // @selected reads the browser selection; Open syncs the browser
				"PCGEditor", // Open jumps to a node in the graph editor
				"AssetDefinition", // PCGEditorCommon.h (via PCGEditor.h) instantiates FAssetCategoryPath constants in every including TU
				"Projects", // CreateAsset refuses engine plugin content by mount point
				"GameplayTags" // the tag dialect resolves names through the tag manager
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
