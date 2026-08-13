// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Execution/WorldSweepStrategy.h"

/** Flat Level strategy. One cell covers the entire world (or the sweep area if valid). Fires cell events once per pass, iterates all actors in the world, dispatches actor and component events. No load/unload, no GC between passes. */
class FFlatLevelSweepStrategy : public FWorldSweepStrategyBase
{
protected:

    //~ Begin FWorldSweepStrategyBase Interface
    virtual const TCHAR* GetModeTag() const override;
    virtual FText GetProgressTitle() const override;
    virtual float GetWorkUnitsPerPass(const FWorldSweepExecutionContext& InContext) const override;
    virtual void RunPass(FWorldSweepExecutionContext& InOutContext, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FScopedSlowTask& InOutSlowTask, FWorldSweepResult& InOutResult) override;
    //~ End FWorldSweepStrategyBase Interface

private:

    /** Resolve the single cell covering the whole sweep. Uses the sweep area when valid, otherwise the bounds of every actor in the world. */
    static FBox ResolveCellBounds(const FWorldSweepExecutionContext& InContext);

    /** Iterate every actor in the world, applying the sweep-area and data-layer filters. Returns the number dispatched. */
    static int32 ProcessActors(const FWorldSweepExecutionContext& InContext, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts);
};
