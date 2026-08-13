// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Execution/WorldSweepStrategy.h"

class ULevel;
class ULevelStreaming;

/** Streaming Levels strategy. Iterates world->GetStreamingLevels() sequentially. For each level: load if not already loaded, derive cell bounds from ALevelBounds (or actor extents as fallback), dispatch events, save, unload. Restores the pre-existing loaded state on exit. */
class FStreamingLevelsSweepStrategy : public FWorldSweepStrategyBase
{
protected:

    //~ Begin FWorldSweepStrategyBase Interface
    virtual const TCHAR* GetModeTag() const override;
    virtual FText GetProgressTitle() const override;
    virtual float GetWorkUnitsPerPass(const FWorldSweepExecutionContext& InContext) const override;
    virtual void RunPass(FWorldSweepExecutionContext& InOutContext, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FScopedSlowTask& InOutSlowTask, FWorldSweepResult& InOutResult) override;
    //~ End FWorldSweepStrategyBase Interface

private:

    /** Derive cell bounds from the level's ALevelBounds actor, falling back to the extents of its actor locations. */
    static FBox ResolveLevelBounds(const ULevel* InLevel);

    /** Dispatch events for every actor in InLevel that passes the class and data-layer filters. Returns the number dispatched. */
    static int32 ProcessLevelActors(const FWorldSweepExecutionContext& InContext, const ULevel* InLevel, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts);

    /** Process a single streaming level end to end: load, dispatch, save, restore its prior loaded state. */
    void ProcessLevel(FWorldSweepExecutionContext& InOutContext, ULevelStreaming* InStreamingLevel, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FWorldSweepResult& InOutResult);
};
