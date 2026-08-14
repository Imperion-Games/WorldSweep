// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Constants and helpers shared by more than one translation unit inside the plugin.
 *
 * These previously lived in per-file anonymous namespaces. That is fine in a non-unity
 * build, where each .cpp is its own translation unit, but Unreal's default unity build
 * concatenates several .cpp files into one, merging those anonymous namespaces and
 * causing redefinition errors. Anything shared belongs here instead.
 */
namespace WorldSweepInternal
{
    /** Half the legal world height, in cm. Sweep areas span this range so actors at extreme altitudes are never clipped by a cell bounds check. */
    inline constexpr double HalfWorldMax = 1048576.0;

    /** Padding applied around bounds derived from raw actor locations, in cm. */
    inline constexpr double ActorBoundsPadding = 500.0;

    /**
     * Accumulate the locations of every actor in InWorld except AWorldSettings, whose
     * location is not meaningful for bounds. Pass OutActorCount to receive how many
     * actors contributed. Returns an invalid box when the world holds no such actors.
     */
    FBox AccumulateActorBounds(UWorld* InWorld, int32* OutActorCount = nullptr);
}
