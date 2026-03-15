// Copyright © ToaGames. All Rights Reserved.

#include "Execution/WorldSweepRunner.h"
#include "Core/WorldSweepBatch.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepLog.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "Misc/ScopedSlowTask.h"
#include "EngineUtils.h"

// ---------------------------------------------------------------------------

FWorldSweepResult::FWorldSweepResult()
    : CellsProcessed(0)
    , ActorsProcessed(0)
    , bWasCancelled(false)
{
}

// ---------------------------------------------------------------------------

UWorldSweepRunner::UWorldSweepRunner()
{
}

FWorldSweepResult UWorldSweepRunner::Execute(UWorldSweepBatch* InBatch, const FBox& InSweepArea, UWorld* InWorld)
{
    FWorldSweepResult Result;

    if (!ensure(InBatch) || !ensure(InWorld))
    {
        UE_LOG(LogWorldSweep, Error, TEXT("Execute called with null batch or world. Aborting."));
        return Result;
    }

    if (InBatch->Scripts.IsEmpty())
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("Batch '%s' has no scripts. Nothing to execute."), *InBatch->GetName());
        return Result;
    }

    if (!InSweepArea.IsValid)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("Sweep area is invalid. Aborting."));
        return Result;
    }

    BuildCellGrid(InSweepArea, InBatch->CellSize);

    if (CellGrid.IsEmpty())
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("Cell grid is empty for the given area and cell size. Aborting."));
        return Result;
    }

    UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
    if (!WorldPartition)
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("World '%s' does not use World Partition. Cells will not be streamed — all loaded actors will be processed."), *InWorld->GetName());
    }

    TArray<UWorldSweepScript*> ActiveScripts;
    for (UWorldSweepScript* Script : InBatch->Scripts)
    {
        if (Script)
        {
            ActiveScripts.Add(Script);
        }
    }

    TMap<int32, TArray<UWorldSweepScript*>> ScriptsByPriority;
    for (UWorldSweepScript* Script : ActiveScripts)
    {
        ScriptsByPriority.FindOrAdd(Script->Priority).Add(Script);
    }

    TArray<int32> PriorityLevels;
    ScriptsByPriority.GetKeys(PriorityLevels);
    PriorityLevels.Sort();

    UClass* ActorClass = InBatch->ActorClassFilter ? InBatch->ActorClassFilter.Get() : AActor::StaticClass();

    const UDataLayerManager* DataLayerManager = UDataLayerManager::GetDataLayerManager(InWorld);

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Starting batch '%s' | %d cells | %d scripts | %d priority pass(es)"),
        *InBatch->GetName(), CellGrid.Num(), ActiveScripts.Num(), PriorityLevels.Num());

    for (UWorldSweepScript* Script : ActiveScripts)
    {
        Script->OnBatchStarted();
    }

    FScopedSlowTask SlowTask(
        static_cast<float>(CellGrid.Num() * PriorityLevels.Num()),
        FText::FromString(FString::Printf(TEXT("WorldSweep: Running '%s'..."), *InBatch->GetName()))
    );
    SlowTask.MakeDialog(true);

    for (int32 PassIndex = 0; PassIndex < PriorityLevels.Num(); ++PassIndex)
    {
        const int32 CurrentPriority = PriorityLevels[PassIndex];
        const TArray<UWorldSweepScript*>& PassScripts = ScriptsByPriority[CurrentPriority];

        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Priority pass %d (level %d) | %d script(s)"),
            PassIndex + 1, CurrentPriority, PassScripts.Num());

        for (int32 CellIndex = 0; CellIndex < CellGrid.Num(); ++CellIndex)
        {
            if (SlowTask.ShouldCancel())
            {
                UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Batch cancelled by user at priority pass %d, cell %d / %d."),
                    PassIndex + 1, CellIndex, CellGrid.Num());
                Result.bWasCancelled = true;
                break;
            }

            SlowTask.EnterProgressFrame(
                1.0f,
                FText::FromString(FString::Printf(TEXT("WorldSweep: Pass %d / %d — Cell %d / %d"),
                    PassIndex + 1, PriorityLevels.Num(), CellIndex + 1, CellGrid.Num()))
            );

            const FBox& CellBounds = CellGrid[CellIndex];

            for (UWorldSweepScript* Script : PassScripts)
            {
                if (Script->bHandleCellEvents)
                {
                    Script->OnPreCellLoad(CellBounds);
                }
            }

            // FLoaderAdapterShape is the UE5.5+ replacement for UWorldPartition::LoadEditorCells. Releasing it triggers unload.
            TUniquePtr<FLoaderAdapterShape> CellLoader;
            if (WorldPartition)
            {
                CellLoader = MakeUnique<FLoaderAdapterShape>(InWorld, CellBounds, TEXT("WorldSweep"));
                CellLoader->Load();
            }

            UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep: Pass %d | Cell %d loaded. Bounds: %s"),
                PassIndex + 1, CellIndex, *CellBounds.ToString());

            for (UWorldSweepScript* Script : PassScripts)
            {
                if (Script->bHandleCellEvents)
                {
                    Script->OnCellStarted(CellBounds);
                }
            }

            int32 ActorsInCell = 0;
            for (TActorIterator<AActor> It(InWorld, ActorClass); It; ++It)
            {
                AActor* Actor = *It;
                if (!Actor || !CellBounds.IsInsideOrOn(Actor->GetActorLocation()))
                {
                    continue;
                }

                if (!PassesDataLayerFilter(Actor, DataLayerManager))
                {
                    continue;
                }

                for (UWorldSweepScript* Script : PassScripts)
                {
                    if (!Script->bProcessActors)
                    {
                        continue;
                    }

                    if (!Script->ActorTagFilter.IsEmpty() && !PassesTagFilter(Actor, Script->ActorTagFilter))
                    {
                        continue;
                    }

                    Script->OnActorFound(Actor, CellBounds);
                }

                ++ActorsInCell;
                ++Result.ActorsProcessed;
            }

            UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep: Pass %d | Cell %d | %d actors processed."),
                PassIndex + 1, CellIndex, ActorsInCell);

            for (UWorldSweepScript* Script : PassScripts)
            {
                if (Script->bHandleCellEvents)
                {
                    Script->OnCellCompleted(CellBounds);
                }
            }

            if (CellLoader.IsValid())
            {
                CellLoader->Unload();
                CellLoader.Reset();
            }

            FWorldPartitionHelpers::DoCollectGarbage();

            ++Result.CellsProcessed;
        }

        if (Result.bWasCancelled)
        {
            break;
        }
    }

    for (UWorldSweepScript* Script : ActiveScripts)
    {
        Script->OnBatchCompleted(Result.CellsProcessed);
    }

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Batch '%s' finished. Cells: %d | Actors: %d | Cancelled: %s"),
        *InBatch->GetName(),
        Result.CellsProcessed,
        Result.ActorsProcessed,
        Result.bWasCancelled ? TEXT("Yes") : TEXT("No")
    );

    return Result;
}

void UWorldSweepRunner::BuildCellGrid(const FBox& InSweepArea, float InCellSize)
{
    CellGrid.Reset();

    const FVector Min = InSweepArea.Min;
    const FVector Max = InSweepArea.Max;

    for (float X = Min.X; X < Max.X; X += InCellSize)
    {
        for (float Y = Min.Y; Y < Max.Y; Y += InCellSize)
        {
            const FVector CellMin(X, Y, Min.Z);
            const FVector CellMax(FMath::Min(X + InCellSize, Max.X), FMath::Min(Y + InCellSize, Max.Y), Max.Z);
            CellGrid.Add(FBox(CellMin, CellMax));
        }
    }
}

bool UWorldSweepRunner::PassesTagFilter(const AActor* InActor, const TArray<FName>& InTagFilter) const
{
    for (const FName& Tag : InTagFilter)
    {
        if (InActor->ActorHasTag(Tag))
        {
            return true;
        }
    }
    return false;
}

bool UWorldSweepRunner::PassesDataLayerFilter(const AActor* InActor, const UDataLayerManager* InDataLayerManager) const
{
    if (!InDataLayerManager)
    {
        return true;
    }

    const TArray<const UDataLayerInstance*> ActorLayers = InActor->GetDataLayerInstances();
    if (ActorLayers.IsEmpty())
    {
        return true; // Not assigned to any data layer — always include.
    }

    for (const UDataLayerInstance* Layer : ActorLayers)
    {
        if (Layer && Layer->IsEffectiveLoadedInEditor())
        {
            return true;
        }
    }
    return false; // Has data layers but none are loaded in the editor panel — exclude.
}
