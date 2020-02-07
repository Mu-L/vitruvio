// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Vitruvio : ModuleRules
{
	public Vitruvio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PrecompileForTargets = PrecompileTargetsType.Any;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RHI",
				"RenderCore",
				"PRT",
				"Projects",
				"SlateCore",
				"Slate",
				"AppFramework",
				"CoreUObject",
				"InputCore",
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"SlateCore",
				"Slate",
				"AppFramework",
				"UnrealGeometryEncoder"
			}
		);
	}
}