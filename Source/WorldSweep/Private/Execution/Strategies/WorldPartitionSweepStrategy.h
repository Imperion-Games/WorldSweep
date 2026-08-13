// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "Execution/WorldSweepStrategy.h"

class UActorDescContainerInstance;
class UWorldPartition;

/** World Partition strategy. Builds a 2D grid over the sweep area at the batch cell size, streams each cell in via FWorldPartitionReference, runs actor and component events, saves per-cell, then unloads via scope exit + DoCollectGarbage. */
class FWorldPartitionSweepStrategy : public FWorldSweepStrategyBase
{
protected:

    //~ Begin FWorldSweepStrategyBase Interface
    virtual const TCHAR* GetModeTag() const override;
    virtual FText GetProgressTitle() const override;
    virtual float GetWorkUnitsPerPass(const FWorldSweepExecutionContext& InContext) const override;
    virtual void RunPass(FWorldSweepExecutionContext& InOutContext, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FScopedSlowTask& InOutSlowTask, FWorldSweepResult& InOutResult) override;
    //~ End FWorldSweepStrategyBase Interface

private:

    /** Per-pass dedup state. World Partition actors are keyed by GUID (stable across GC); persistent-level actors by pointer (never collected). */
    struct FWorldSweepDedupState
    {
        TSet<FGuid>   WorldPartitionActors;
        TSet<AActor*> PersistentActors;
    };

    /** Populate CellGrid with a 2D tiling of InSweepArea at InCellSize. Safe to call repeatedly; resets first. */
    void BuildCellGrid(const FBox& InSweepArea, float InCellSize);

    /** Validate that the world exposes World Partition and an actor-descriptor container. Emits a toast and returns false when it does not. */
    static bool ResolvePartition(const FWorldSweepExecutionContext& InContext, UWorldPartition*& OutPartition, UActorDescContainerInstance*& OutContainer);

    /** Stream in every not-yet-seen World Partition actor intersecting InCellBounds, appending a hard reference for each to OutCellRefs. The caller owns those references and therefore controls when the actors unload. */
    static void LoadCellActors(const FWorldSweepExecutionContext& InContext, UWorldPartition* InPartition, UActorDescContainerInstance* InContainer, const FBox& InCellBounds, FWorldSweepDedupState& InOutDedup, TArray<FWorldPartitionReference>& OutCellRefs);

    /** Dispatch actor and component events for the actors held by InCellRefs. Returns the number dispatched. */
    static int32 DispatchLoadedActors(const FWorldSweepExecutionContext& InContext, const TArray<FWorldPartitionReference>& InCellRefs, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts);

    /** Dispatch persistent-level actors whose location falls inside InCellBounds. Returns the number dispatched. */
    static int32 ProcessPersistentActors(const FWorldSweepExecutionContext& InContext, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FWorldSweepDedupState& InOutDedup);

    /** Run one grid cell: load, dispatch, save, unload. */
    void ProcessCell(FWorldSweepExecutionContext& InOutContext, UWorldPartition* InPartition, UActorDescContainerInstance* InContainer, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FWorldSweepDedupState& InOutDedup, FWorldSweepResult& InOutResult);

private:

    /** 2D tiling of the sweep area, rebuilt at the start of every Execute(). */
    TArray<FBox> CellGrid;
};
