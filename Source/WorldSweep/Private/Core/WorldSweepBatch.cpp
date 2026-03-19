// Copyright © ToaGames. All Rights Reserved.

#include "Core/WorldSweepBatch.h"

UWorldSweepBatch::UWorldSweepBatch()
    : CellSize(25600.0f)
    , ActorClassFilter(nullptr)
    , SweepMode(EWorldSweepMode::Auto)
    , bSaveModifications(false)
{
}
