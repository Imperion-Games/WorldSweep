// Copyright © ToaGames. All Rights Reserved.

#include "WorldSweepStyle.h"

#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FWorldSweepStyle::StyleInstance = nullptr;

void FWorldSweepStyle::Initialize()
{
    if (StyleInstance.IsValid())
    {
        return;
    }

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WorldSweep"));
    if (!Plugin.IsValid())
    {
        // The plugin is always discoverable when this module is loaded, so this only fires if the
        // install is corrupt. Bail rather than dereference null and take the editor down.
        return;
    }

    StyleInstance = MakeShared<FSlateStyleSet>(GetStyleSetName());

    const FString ResourcesDir = Plugin->GetBaseDir() / TEXT("Resources");
    StyleInstance->SetContentRoot(ResourcesDir);

    StyleInstance->Set("WorldSweep.MenuIcon", new FSlateImageBrush(ResourcesDir / TEXT("WorldSweep_16.png"), FVector2D(16.0f, 16.0f)));

    FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FWorldSweepStyle::Shutdown()
{
    if (!StyleInstance.IsValid())
    {
        return;
    }

    FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
    StyleInstance.Reset();
}

const ISlateStyle& FWorldSweepStyle::Get()
{
    return *StyleInstance;
}

FName FWorldSweepStyle::GetStyleSetName()
{
    static FName StyleSetName(TEXT("WorldSweepStyle"));
    return StyleSetName;
}
