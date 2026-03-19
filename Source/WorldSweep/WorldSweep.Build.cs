// Copyright © ToaGames. All Rights Reserved.

using UnrealBuildTool;

public class WorldSweep : ModuleRules
{
    public WorldSweep(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "EditorFramework",
            "ToolMenus",
            "InputCore",
            "WorldPartitionEditor",
            "PropertyEditor",
            "AssetTools",
            "ContentBrowser",
            "MessageLog",
            "Projects",
            "LevelEditor",
            "SourceControl",
        });
    }
}
