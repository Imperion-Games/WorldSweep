// Copyright © ToaGames. All Rights Reserved.

#include "Execution/Strategies/WorldPartitionSweepStrategy.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionHelpers.h"

#include "Core/WorldSweepScript.h"
#include "Execution/WorldSweepRunner.h"
#include "Execution/WorldSweepSaveTracker.h"
#include "WorldSweepLog.h"

const TCHAR* FWorldPartitionSweepStrategy::GetModeTag() const
{
    return TEXT("WP");
}

FText FWorldPartitionSweepStrategy::GetProgressTitle() const
{
    return NSLOCTEXT("WorldSweep", "WorldPartitionProgressTitle", "WorldSweep: Running batch (World Partition)...");
}

float FWorldPartitionSweepStrategy::GetWorkUnitsPerPass(const FWorldSweepExecutionContext& InContext) const
{
    // The grid is not built until RunPass, so derive the cell count from the area and cell size.
    if (!InContext.SweepArea.IsValid || InContext.CellSize <= 0.0f)
    {
        return 0.0f;
    }

    const FVector Size = InContext.SweepArea.GetSize();
    const double CellSize = static_cast<double>(InContext.CellSize);
    const double CellsX = FMath::Max(1.0, FMath::CeilToDouble(Size.X / CellSize));
    const double CellsY = FMath::Max(1.0, FMath::CeilToDouble(Size.Y / CellSize));

    return static_cast<float>(CellsX * CellsY);
}

void FWorldPartitionSweepStrategy::RunPass(FWorldSweepExecutionContext& InOutContext, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FScopedSlowTask& InOutSlowTask, FWorldSweepResult& InOutResult)
{
    if (!InOutContext.SweepArea.IsValid)
    {
        WS_NOTIFY_ERROR(TEXT("WorldSweep [WP]: Sweep area is invalid. Aborting."));
        return;
    }

    UWorldPartition* Partition = nullptr;
    UActorDescContainerInstance* Container = nullptr;
    if (!ResolvePartition(InOutContext, Partition, Container))
    {
        return;
    }

    BuildCellGrid(InOutContext.SweepArea, InOutContext.CellSize);
    if (CellGrid.IsEmpty())
    {
        WS_NOTIFY_WARNING(TEXT("WorldSweep [WP]: Cell grid is empty for the given area and cell size. Aborting."));
        return;
    }

    FWorldSweepDedupState Dedup;

    for (int32 CellIndex = 0; CellIndex < CellGrid.Num(); ++CellIndex)
    {
        if (InOutSlowTask.ShouldCancel())
        {
            InOutResult.bWasCancelled = true;
            return;
        }

        InOutSlowTask.EnterProgressFrame(
            1.0f,
            FText::FromString(FString::Printf(TEXT("WorldSweep: Cell %d / %d"), CellIndex + 1, CellGrid.Num()))
        );

        ProcessCell(InOutContext, Partition, Container, CellGrid[CellIndex], InPass, InPassScripts, Dedup, InOutResult);
    }
}

void FWorldPartitionSweepStrategy::BuildCellGrid(const FBox& InSweepArea, float InCellSize)
{
    CellGrid.Reset();

    // World-space math runs in double: World Partition maps legitimately span the full
    // ±HALF_WORLD_MAX range, where float spacing is coarser than a centimetre and the
    // accumulating loop counter would drift.
    const FVector Min = InSweepArea.Min;
    const FVector Max = InSweepArea.Max;
    const double CellSize = static_cast<double>(InCellSize);

    if (CellSize <= 0.0)
    {
        return;
    }

    for (double X = Min.X; X < Max.X; X += CellSize)
    {
        for (double Y = Min.Y; Y < Max.Y; Y += CellSize)
        {
            const FVector CellMin(X, Y, Min.Z);
            const FVector CellMax(FMath::Min(X + CellSize, Max.X), FMath::Min(Y + CellSize, Max.Y), Max.Z);
            CellGrid.Add(FBox(CellMin, CellMax));
        }
    }
}

bool FWorldPartitionSweepStrategy::ResolvePartition(const FWorldSweepExecutionContext& InContext, UWorldPartition*& OutPartition, UActorDescContainerInstance*& OutContainer)
{
    OutPartition = nullptr;
    OutContainer = nullptr;

    UWorldPartition* Partition = InContext.World ? InContext.World->GetWorldPartition() : nullptr;
    if (!Partition)
    {
        WS_NOTIFY_ERROR(TEXT("WorldSweep [WP]: World Partition is null. Aborting."));
        return false;
    }

    UActorDescContainerInstance* Container = Partition->GetActorDescContainerInstance();
    if (!Container)
    {
        WS_NOTIFY_ERROR(TEXT("WorldSweep [WP]: Failed to get actor descriptor container. Aborting."));
        return false;
    }

    OutPartition = Partition;
    OutContainer = Container;
    return true;
}

void FWorldPartitionSweepStrategy::LoadCellActors(const FWorldSweepExecutionContext& InContext, UWorldPartition* InPartition, UActorDescContainerInstance* InContainer, const FBox& InCellBounds, FWorldSweepDedupState& InOutDedup, TArray<FWorldPartitionReference>& OutCellRefs)
{
    // FWorldPartitionReference loads its actor synchronously on construction and releases it on
    // destruction, so the caller's array lifetime is what keeps the cell resident.
    FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(InPartition, InCellBounds, InContext.ActorClass,
        [&](const FWorldPartitionActorDescInstance* DescInstance) -> bool
        {
            const FGuid Guid = DescInstance->GetGuid();
            if (Guid.IsValid() && !InOutDedup.WorldPartitionActors.Contains(Guid))
            {
                // Mark as processed at the descriptor stage so subsequent cells never re-queue
                // this actor, regardless of whether loading succeeds or whether GetActorGuid()
                // returns a matching value at runtime.
                InOutDedup.WorldPartitionActors.Add(Guid);
                OutCellRefs.Emplace(InContainer, Guid);
            }
            return true;
        });
}

int32 FWorldPartitionSweepStrategy::DispatchLoadedActors(const FWorldSweepExecutionContext& InContext, const TArray<FWorldPartitionReference>& InCellRefs, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts)
{
    int32 ActorsInCell = 0;

    for (const FWorldPartitionReference& Ref : InCellRefs)
    {
        AActor* Actor = Ref.GetActor();
        if (!Actor || !WorldSweepHelpers::PassesDataLayerFilter(Actor, InContext.DataLayerManager))
        {
            continue;
        }

        WorldSweepHelpers::DispatchActorAndComponents(Actor, InCellBounds, InPass, InPassScripts);
        ++ActorsInCell;
    }

    return ActorsInCell;
}

int32 FWorldPartitionSweepStrategy::ProcessPersistentActors(const FWorldSweepExecutionContext& InContext, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FWorldSweepDedupState& InOutDedup)
{
    int32 ActorsInCell = 0;

    // Persistent-level actors are always loaded and not World Partition managed. Filter by
    // location so each one is assigned to exactly one sweep cell.
    for (TActorIterator<AActor> It(InContext.World, InContext.ActorClass); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor->GetLevel() != InContext.World->PersistentLevel)
        {
            continue;
        }
        if (!InCellBounds.IsInsideOrOn(Actor->GetActorLocation()))
        {
            continue;
        }

        // Skip actors already dispatched via the WP descriptor path (some WP-registered
        // actors, e.g. Landscape, live in the persistent level).
        const FGuid ActorGuid = Actor->GetActorGuid();
        if (ActorGuid.IsValid() && InOutDedup.WorldPartitionActors.Contains(ActorGuid))
        {
            continue;
        }
        if (InOutDedup.PersistentActors.Contains(Actor))
        {
            continue;
        }
        if (!WorldSweepHelpers::PassesDataLayerFilter(Actor, InContext.DataLayerManager))
        {
            continue;
        }

        InOutDedup.PersistentActors.Add(Actor);

        // If this actor has a WP GUID, record it so GC-induced pointer changes in later cells
        // don't bypass the pointer-based dedup.
        if (ActorGuid.IsValid())
        {
            InOutDedup.WorldPartitionActors.Add(ActorGuid);
        }

        WorldSweepHelpers::DispatchActorAndComponents(Actor, InCellBounds, InPass, InPassScripts);
        ++ActorsInCell;
    }

    return ActorsInCell;
}

void FWorldPartitionSweepStrategy::ProcessCell(FWorldSweepExecutionContext& InOutContext, UWorldPartition* InPartition, UActorDescContainerInstance* InContainer, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FWorldSweepDedupState& InOutDedup, FWorldSweepResult& InOutResult)
{
    WorldSweepHelpers::DispatchPreCellLoad(InCellBounds, InPassScripts);

    if (!InPass.bNeedsActors)
    {
        // Cell-only pass: no actor loading needed, so skip streaming and GC entirely.
        WorldSweepHelpers::DispatchCellStarted(InCellBounds, InPassScripts);
        WorldSweepHelpers::DispatchCellCompleted(InCellBounds, InPassScripts);
        ++InOutResult.CellsProcessed;
        return;
    }

    int32 ActorsInCell = 0;
    {
        // CellRefs must outlive OnCellCompleted and the save pass: scripts may still touch the
        // cell's actors in that window, and the packages have to be written while resident.
        TArray<FWorldPartitionReference> CellRefs;
        LoadCellActors(InOutContext, InPartition, InContainer, InCellBounds, InOutDedup, CellRefs);

        WorldSweepHelpers::DispatchCellStarted(InCellBounds, InPassScripts);

        ActorsInCell  = DispatchLoadedActors(InOutContext, CellRefs, InCellBounds, InPass, InPassScripts);
        ActorsInCell += ProcessPersistentActors(InOutContext, InCellBounds, InPass, InPassScripts, InOutDedup);

        WorldSweepHelpers::DispatchCellCompleted(InCellBounds, InPassScripts);

        // Save per-cell while WP actors are still in memory, before the references release below.
        if (InOutContext.SaveTracker)
        {
            InOutContext.SaveTracker->SaveDirty();
        }

    } // CellRefs destructs here. WP actors are released and queued for unload.

    FWorldPartitionHelpers::DoCollectGarbage();

    InOutResult.ActorsProcessed += ActorsInCell;
    ++InOutResult.CellsProcessed;

    UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [WP]: Cell processed | %d actor(s)."), ActorsInCell);
}

