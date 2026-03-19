// Copyright © ToaGames. All Rights Reserved.

#include "Core/WorldSweepScript.h"

UWorldSweepScript::UWorldSweepScript()
    : EventFlags(static_cast<int32>(EWorldSweepEventFlags::CellEvents) | static_cast<int32>(EWorldSweepEventFlags::Actors))
    , Priority(0)
{
}
