// Copyright © ToaGames. All Rights Reserved.

using UnrealBuildTool;

public class WorldSweep : ModuleRules
{
    public WorldSweep(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // Public dependencies must cover every module reachable from a header under
        // Public/. Consumers subclass UWorldSweepScript from their own editor modules,
        // so those headers have to compile standalone in the consumer's build:
        //   CoreUObject   - UObject/Object.h, UObject/GCObject.h, .generated.h
        //   Engine        - Engine/DataAsset.h, Commandlets/Commandlet.h
        //   Slate         - SLATE_BEGIN_ARGS / SNew in the widget headers
        //   SlateCore     - Widgets/SCompoundWidget.h, Widgets/SLeafWidget.h, Styling/SlateStyle.h
        //   AssetRegistry - AssetRegistry/AssetData.h in SWorldSweepWindow.h
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "AssetRegistry",
        });

        // Implementation-only dependencies. Nothing under Public/ may include a header
        // owned by these modules.
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "InputCore",       // EKeys::LeftMouseButton / RightMouseButton in the map view
            "LevelEditor",     // FLevelEditorModule tab + layout registration
            "MessageLog",      // FMessageLogModule listing registration
            "Projects",        // IPluginManager, for the Resources/ content root
            "PropertyEditor",  // SObjectPropertyEntryBox in the batch picker
            "SourceControl",   // SourceControlHelpers::CheckOutOrAddFiles
            "ToolMenus",       // Tools menu entry
            "UnrealEd",        // GEditor, FEditorFileUtils, FScopedTransaction
        });
    }
}
