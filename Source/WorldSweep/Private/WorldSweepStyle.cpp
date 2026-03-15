// Copyright © ToaGames. All Rights Reserved.

#include "WorldSweepStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FWorldSweepStyle::StyleInstance = nullptr;

void FWorldSweepStyle::Initialize()
{
    if (StyleInstance.IsValid())
    {
        return;
    }

    StyleInstance = MakeShared<FSlateStyleSet>(GetStyleSetName());

    const FString ResourcesDir = IPluginManager::Get().FindPlugin(TEXT("WorldSweep"))->GetBaseDir() / TEXT("Resources");
    StyleInstance->SetContentRoot(ResourcesDir);

    StyleInstance->Set("WorldSweep.MenuIcon", new FSlateImageBrush(ResourcesDir / TEXT("WorldSweep_16.png"), FVector2D(16.0f, 16.0f)));

    FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FWorldSweepStyle::Shutdown()
{
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
