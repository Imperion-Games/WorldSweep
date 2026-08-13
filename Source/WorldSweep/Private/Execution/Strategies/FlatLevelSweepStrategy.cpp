// Copyright © ToaGames. All Rights Reserved.

#include "Execution/Strategies/FlatLevelSweepStrategy.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopedSlowTask.h"

#include "Core/WorldSweepScript.h"
#include "Execution/WorldSweepRunner.h"
#include "WorldSweepLog.h"

const TCHAR* FFlatLevelSweepStrategy::GetModeTag() const
{
    return TEXT("Flat");
}

FText FFlatLevelSweepStrategy::GetProgressTitle() const
{
    return NSLOCTEXT("WorldSweep", "FlatLevelProgressTitle", "WorldSweep: Running batch (Flat Level)...");
}

float FFlatLevelSweepStrategy::GetWorkUnitsPerPass(const FWorldSweepExecutionContext& InContext) const
{
    // The whole level is one cell, so a pass is a single unit of work.
    return 1.0f;
}

void FFlatLevelSweepStrategy::RunPass(FWorldSweepExecutionContext& InOutContext, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FScopedSlowTask& InOutSlowTask, FWorldSweepResult& InOutResult)
{
    if (InOutSlowTask.ShouldCancel())
    {
        InOutResult.bWasCancelled = true;
        return;
    }

    InOutSlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("WorldSweep", "FlatLevelProcessing", "WorldSweep: Processing actors..."));

    const FBox CellBounds = ResolveCellBounds(InOutContext);

    WorldSweepHelpers::DispatchPreCellLoad(CellBounds, InPassScripts);
    WorldSweepHelpers::DispatchCellStarted(CellBounds, InPassScripts);

    int32 ActorsInPass = 0;
    if (InPass.bNeedsActors)
    {
        ActorsInPass = ProcessActors(InOutContext, CellBounds, InPass, InPassScripts);
        InOutResult.ActorsProcessed += ActorsInPass;
    }

    UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Flat]: %d actor(s) processed."), ActorsInPass);

    WorldSweepHelpers::DispatchCellCompleted(CellBounds, InPassScripts);

    ++InOutResult.CellsProcessed;
}

FBox FFlatLevelSweepStrategy::ResolveCellBounds(const FWorldSweepExecutionContext& InContext)
{
    if (InContext.SweepArea.IsValid)
    {
        return InContext.SweepArea;
    }

    FBox Bounds(EForceInit::ForceInit);
    for (TActorIterator<AActor> It(InContext.World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor && !Actor->IsA<AWorldSettings>())
        {
            Bounds += Actor->GetActorLocation();
        }
    }

    return Bounds.IsValid ? Bounds.ExpandBy(500.0) : Bounds;
}

int32 FFlatLevelSweepStrategy::ProcessActors(const FWorldSweepExecutionContext& InContext, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts)
{
    int32 ActorsInPass = 0;

    for (TActorIterator<AActor> It(InContext.World, InContext.ActorClass); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor)
        {
            continue;
        }

        if (InContext.SweepArea.IsValid && !InContext.SweepArea.IsInsideOrOn(Actor->GetActorLocation()))
        {
            continue;
        }

        if (!WorldSweepHelpers::PassesDataLayerFilter(Actor, InContext.DataLayerManager))
        {
            continue;
        }

        WorldSweepHelpers::DispatchActorAndComponents(Actor, InCellBounds, InPass, InPassScripts);
        ++ActorsInPass;
    }

    return ActorsInPass;
}

