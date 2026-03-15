// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/** Registers and owns the WorldSweep Slate style set, including menu icons. */
class WORLDSWEEP_API FWorldSweepStyle
{
public:

    static void Initialize();
    static void Shutdown();

    static const ISlateStyle& Get();
    static FName GetStyleSetName();

private:

    static TSharedPtr<FSlateStyleSet> StyleInstance;
};
