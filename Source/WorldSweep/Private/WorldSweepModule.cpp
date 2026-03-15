// Copyright © ToaGames. All Rights Reserved.

#include "WorldSweepModule.h"
#include "WorldSweepLog.h"
#include "WorldSweepStyle.h"
#include "UI/SWorldSweepWindow.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "FWorldSweepModule"

DEFINE_LOG_CATEGORY(LogWorldSweep);

const FName FWorldSweepModule::WorldSweepTabName = TEXT("WorldSweep");

// ---------------------------------------------------------------------------

void FWorldSweepModule::StartupModule()
{
    FWorldSweepStyle::Initialize();

    // Register our tab with the level editor tab manager so it docks alongside
    // the World Partition Editor and Data Layers tabs in the same window.
    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
    LevelEditorModule.OnRegisterTabs().AddRaw(this, &FWorldSweepModule::RegisterWorldSweepTabs);
    LevelEditorModule.OnRegisterLayoutExtensions().AddRaw(this, &FWorldSweepModule::RegisterWorldSweepLayout);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FWorldSweepModule::RegisterMenus)
    );

    FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
    FMessageLogInitializationOptions InitOptions;
    InitOptions.bShowFilters = true;
    MessageLogModule.RegisterLogListing(
        "WorldSweep",
        LOCTEXT("WorldSweepLogLabel", "WorldSweep"),
        InitOptions
    );
}

void FWorldSweepModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);

    if (UToolMenus* ToolMenus = UToolMenus::Get())
    {
        ToolMenus->RemoveSection("LevelEditor.MainMenu.Tools", "WorldSweepSection");
    }

    if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
    {
        LevelEditorModule->OnRegisterTabs().RemoveAll(this);
        LevelEditorModule->OnRegisterLayoutExtensions().RemoveAll(this);

        if (TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager())
        {
            LevelEditorTabManager->UnregisterTabSpawner(WorldSweepTabName);
        }
    }

    if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
    {
        FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
        MessageLogModule.UnregisterLogListing("WorldSweep");
    }

    FWorldSweepStyle::Shutdown();
}

// ---------------------------------------------------------------------------

void FWorldSweepModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    FToolMenuSection& Section = ToolsMenu->FindOrAddSection("WorldSweepSection");
    Section.Label = LOCTEXT("WorldSweepSectionLabel", "WorldSweep");

    Section.AddMenuEntry(
        "OpenWorldSweep",
        LOCTEXT("OpenWorldSweepLabel", "WorldSweep"),
        LOCTEXT("OpenWorldSweepTooltip", "Open WorldSweep alongside the World Partition Editor and Data Layers."),
        FSlateIcon(FWorldSweepStyle::GetStyleSetName(), "WorldSweep.MenuIcon"),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
            if (!LevelEditorModule)
            {
                return;
            }

            TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager();
            if (!LevelEditorTabManager.IsValid())
            {
                return;
            }

            LevelEditorTabManager->TryInvokeTab(FWorldSweepModule::WorldSweepTabName);
            LevelEditorTabManager->TryInvokeTab(FName("WorldBrowserPartitionEditor"));
            LevelEditorTabManager->TryInvokeTab(FName("LevelEditorDataLayerBrowser"));
        }))
    );
}

void FWorldSweepModule::RegisterWorldSweepLayout(FLayoutExtender& InExtender)
{
    // Default position: open alongside the World Partition Editor tab.
    // This takes effect on fresh installs and when the user resets their layout.
    // After first arrangement the level editor persists the layout automatically.
    InExtender.ExtendLayout(
        FTabId("WorldBrowserPartitionEditor"),
        ELayoutExtensionPosition::After,
        FTabManager::FTab(WorldSweepTabName, ETabState::ClosedTab)
    );
}

void FWorldSweepModule::RegisterWorldSweepTabs(TSharedPtr<FTabManager> InTabManager)
{
    InTabManager->RegisterTabSpawner(
        WorldSweepTabName,
        FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
        {
            return SNew(SDockTab)
                .TabRole(ETabRole::NomadTab)
                [
                    SNew(SWorldSweepWindow)
                ];
        })
    )
    .SetDisplayName(LOCTEXT("WorldSweepTabTitle", "WorldSweep"))
    .SetTooltipText(LOCTEXT("WorldSweepTabTooltip", "Batch script execution across World Partition maps."))
    .SetIcon(FSlateIcon(FWorldSweepStyle::GetStyleSetName(), "WorldSweep.MenuIcon"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWorldSweepModule, WorldSweep)
