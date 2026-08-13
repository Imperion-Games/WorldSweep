// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/** Registers and owns the WorldSweep Slate style set, including menu icons. */
class WORLDSWEEP_API FWorldSweepStyle
{
public:

    /** Creates and registers the WorldSweep style set. Safe to call once from module startup. */
    static void Initialize();

    /** Unregisters and destroys the WorldSweep style set. Call from module shutdown to pair with Initialize. */
    static void Shutdown();

    /** Returns the registered WorldSweep Slate style set for querying brushes and icons. */
    static const ISlateStyle& Get();

    /** Returns the canonical style set name used to register brushes with Slate. */
    static FName GetStyleSetName();

private:

    static TSharedPtr<FSlateStyleSet> StyleInstance;
};
