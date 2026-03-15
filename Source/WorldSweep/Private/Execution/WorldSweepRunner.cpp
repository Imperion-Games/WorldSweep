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
#include "Engine/LevelStreaming.h"
#include "Engine/LevelBounds.h"
#include "Engine/World.h"
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
    : ActorClass(nullptr)
    , DataLayerManager(nullptr)
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

    // Build transient execution context.
    ActiveScripts.Reset();
    for (UWorldSweepScript* Script : InBatch->Scripts)
    {
        if (Script)
        {
            ActiveScripts.Add(Script);
        }
    }

    ScriptsByPriority.Reset();
    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        ScriptsByPriority.FindOrAdd(Script->Priority).Add(Script.Get());
    }

    PriorityLevels.Reset();
    ScriptsByPriority.GetKeys(PriorityLevels);
    PriorityLevels.Sort();

    ActorClass = InBatch->ActorClassFilter ? InBatch->ActorClassFilter.Get() : AActor::StaticClass();
    DataLayerManager = UDataLayerManager::GetDataLayerManager(InWorld);

    const EWorldSweepMode ResolvedMode = ResolveMode(InBatch, InWorld);

    // Build the spatial cell grid upfront — used by the WP path; ignored by the others.
    BuildCellGrid(InSweepArea, InBatch->CellSize);

    static const TCHAR* ModeNames[] = { TEXT("Auto"), TEXT("World Partition"), TEXT("Streaming Levels"), TEXT("Flat Level") };
    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Starting batch '%s' | Mode: %s | %d scripts | %d priority pass(es)"),
        *InBatch->GetName(),
        ModeNames[static_cast<uint8>(ResolvedMode)],
        ActiveScripts.Num(),
        PriorityLevels.Num());

    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        Script->OnBatchStarted();
    }

    switch (ResolvedMode)
    {
        case EWorldSweepMode::WorldPartition:
        {
            UWorldPartition* WP = InWorld->GetWorldPartition();
            Result = ExecuteWorldPartition(InSweepArea, InWorld, WP);
            break;
        }
        case EWorldSweepMode::StreamingLevels:
            Result = ExecuteStreamingLevels(InSweepArea, InWorld);
            break;
        case EWorldSweepMode::FlatLevel:
            Result = ExecuteFlatLevel(InSweepArea, InWorld);
            break;
        default:
            break;
    }

    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        Script->OnBatchCompleted(Result.CellsProcessed);
    }

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Batch '%s' finished. Cells: %d | Actors: %d | Cancelled: %s"),
        *InBatch->GetName(),
        Result.CellsProcessed,
        Result.ActorsProcessed,
        Result.bWasCancelled ? TEXT("Yes") : TEXT("No"));

    return Result;
}

// ---------------------------------------------------------------------------

EWorldSweepMode UWorldSweepRunner::ResolveMode(const UWorldSweepBatch* InBatch, const UWorld* InWorld) const
{
    if (InBatch->SweepMode != EWorldSweepMode::Auto)
    {
        return InBatch->SweepMode;
    }

    if (InWorld->GetWorldPartition())
    {
        return EWorldSweepMode::WorldPartition;
    }

    if (!InWorld->GetStreamingLevels().IsEmpty())
    {
        return EWorldSweepMode::StreamingLevels;
    }

    return EWorldSweepMode::FlatLevel;
}

// ---------------------------------------------------------------------------

FWorldSweepResult UWorldSweepRunner::ExecuteWorldPartition(const FBox& InSweepArea, UWorld* InWorld, UWorldPartition* InWP)
{
    FWorldSweepResult Result;

    if (!InSweepArea.IsValid)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweep [WP]: Sweep area is invalid. Aborting."));
        return Result;
    }

    if (CellGrid.IsEmpty())
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweep [WP]: Cell grid is empty for the given area and cell size. Aborting."));
        return Result;
    }

    FScopedSlowTask SlowTask(
        static_cast<float>(CellGrid.Num() * PriorityLevels.Num()),
        FText::FromString(TEXT("WorldSweep: Running batch (World Partition)..."))
    );
    if (!IsRunningCommandlet())
    {
        SlowTask.MakeDialog(true);
    }

    for (int32 PassIndex = 0; PassIndex < PriorityLevels.Num(); ++PassIndex)
    {
        const int32 CurrentPriority = PriorityLevels[PassIndex];
        const TArray<UWorldSweepScript*>& PassScripts = ScriptsByPriority[CurrentPriority];

        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep [WP]: Priority pass %d (level %d) | %d script(s)"),
            PassIndex + 1, CurrentPriority, PassScripts.Num());

        for (int32 CellIndex = 0; CellIndex < CellGrid.Num(); ++CellIndex)
        {
            if (SlowTask.ShouldCancel())
            {
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

            TUniquePtr<FLoaderAdapterShape> CellLoader;
            if (InWP)
            {
                CellLoader = MakeUnique<FLoaderAdapterShape>(InWorld, CellBounds, TEXT("WorldSweep"));
                CellLoader->Load();
                FlushAsyncLoading();
                InWorld->UpdateLevelStreaming();
            }

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

            UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [WP]: Pass %d | Cell %d | %d actors processed."),
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

    return Result;
}

// ---------------------------------------------------------------------------

FWorldSweepResult UWorldSweepRunner::ExecuteStreamingLevels(const FBox& InSweepArea, UWorld* InWorld)
{
    FWorldSweepResult Result;

    const TArray<ULevelStreaming*>& StreamingLevels = InWorld->GetStreamingLevels();
    if (StreamingLevels.IsEmpty())
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweep [Streaming]: No streaming levels found. Aborting."));
        return Result;
    }

    FScopedSlowTask SlowTask(
        static_cast<float>(StreamingLevels.Num() * PriorityLevels.Num()),
        FText::FromString(TEXT("WorldSweep: Running batch (Streaming Levels)..."))
    );
    if (!IsRunningCommandlet())
    {
        SlowTask.MakeDialog(true);
    }

    for (int32 PassIndex = 0; PassIndex < PriorityLevels.Num(); ++PassIndex)
    {
        const int32 CurrentPriority = PriorityLevels[PassIndex];
        const TArray<UWorldSweepScript*>& PassScripts = ScriptsByPriority[CurrentPriority];

        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep [Streaming]: Priority pass %d (level %d) | %d script(s)"),
            PassIndex + 1, CurrentPriority, PassScripts.Num());

        for (int32 LevelIndex = 0; LevelIndex < StreamingLevels.Num(); ++LevelIndex)
        {
            if (SlowTask.ShouldCancel())
            {
                Result.bWasCancelled = true;
                break;
            }

            ULevelStreaming* StreamingLevel = StreamingLevels[LevelIndex];
            if (!StreamingLevel)
            {
                continue;
            }

            SlowTask.EnterProgressFrame(
                1.0f,
                FText::FromString(FString::Printf(TEXT("WorldSweep: Pass %d / %d — Level %d / %d (%s)"),
                    PassIndex + 1, PriorityLevels.Num(),
                    LevelIndex + 1, StreamingLevels.Num(),
                    *StreamingLevel->GetWorldAssetPackageName()))
            );

            // Fire OnPreCellLoad before load — bounds are not yet known, pass invalid box.
            const FBox PreLoadBounds(EForceInit::ForceInit);
            for (UWorldSweepScript* Script : PassScripts)
            {
                if (Script->bHandleCellEvents)
                {
                    Script->OnPreCellLoad(PreLoadBounds);
                }
            }

            // Load the level if it is not already loaded, and restore state after.
            const bool bWasLoaded = StreamingLevel->IsLevelLoaded();
            if (!bWasLoaded)
            {
                StreamingLevel->SetShouldBeLoaded(true);
                StreamingLevel->SetShouldBeVisible(true);
                InWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
            }

            ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel();
            if (!LoadedLevel)
            {
                UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweep [Streaming]: Failed to load level '%s'. Skipping."),
                    *StreamingLevel->GetWorldAssetPackageName());

                if (!bWasLoaded)
                {
                    StreamingLevel->SetShouldBeLoaded(false);
                    StreamingLevel->SetShouldBeVisible(false);
                    InWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
                }
                continue;
            }

            // Derive cell bounds from the ALevelBounds actor; fall back to actor locations.
            FBox CellBounds(EForceInit::ForceInit);
            for (const TObjectPtr<AActor>& ActorPtr : LoadedLevel->Actors)
            {
                if (ALevelBounds* LB = Cast<ALevelBounds>(ActorPtr.Get()))
                {
                    CellBounds = LB->GetComponentsBoundingBox();
                    break;
                }
            }

            if (!CellBounds.IsValid)
            {
                for (const TObjectPtr<AActor>& ActorPtr : LoadedLevel->Actors)
                {
                    if (ActorPtr && !ActorPtr->IsA<AWorldSettings>())
                    {
                        CellBounds += ActorPtr->GetActorLocation();
                    }
                }
            }

            // If a sweep area is set, skip levels that don't intersect it.
            if (InSweepArea.IsValid && CellBounds.IsValid && !CellBounds.Intersect(InSweepArea))
            {
                UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Streaming]: Level '%s' outside sweep area. Skipping."),
                    *StreamingLevel->GetWorldAssetPackageName());

                if (!bWasLoaded)
                {
                    StreamingLevel->SetShouldBeLoaded(false);
                    StreamingLevel->SetShouldBeVisible(false);
                    InWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
                    FWorldPartitionHelpers::DoCollectGarbage();
                }
                continue;
            }

            for (UWorldSweepScript* Script : PassScripts)
            {
                if (Script->bHandleCellEvents)
                {
                    Script->OnCellStarted(CellBounds);
                }
            }

            int32 ActorsInLevel = 0;
            for (const TObjectPtr<AActor>& ActorPtr : LoadedLevel->Actors)
            {
                AActor* Actor = ActorPtr.Get();
                if (!Actor || !Actor->IsA(ActorClass))
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

                ++ActorsInLevel;
                ++Result.ActorsProcessed;
            }

            UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Streaming]: Pass %d | Level '%s' | %d actors processed."),
                PassIndex + 1, *StreamingLevel->GetWorldAssetPackageName(), ActorsInLevel);

            for (UWorldSweepScript* Script : PassScripts)
            {
                if (Script->bHandleCellEvents)
                {
                    Script->OnCellCompleted(CellBounds);
                }
            }

            if (!bWasLoaded)
            {
                StreamingLevel->SetShouldBeLoaded(false);
                StreamingLevel->SetShouldBeVisible(false);
                InWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
                FWorldPartitionHelpers::DoCollectGarbage();
            }

            ++Result.CellsProcessed;
        }

        if (Result.bWasCancelled)
        {
            break;
        }
    }

    return Result;
}

// ---------------------------------------------------------------------------

FWorldSweepResult UWorldSweepRunner::ExecuteFlatLevel(const FBox& InSweepArea, UWorld* InWorld)
{
    FWorldSweepResult Result;

    // Derive the effective cell bounds: use the sweep area if valid, otherwise encompass all actors.
    FBox CellBounds = InSweepArea;
    if (!CellBounds.IsValid)
    {
        for (TActorIterator<AActor> It(InWorld); It; ++It)
        {
            if (*It && !(*It)->IsA<AWorldSettings>())
            {
                CellBounds += (*It)->GetActorLocation();
            }
        }
        CellBounds = CellBounds.ExpandBy(500.0f);
    }

    FScopedSlowTask SlowTask(
        static_cast<float>(PriorityLevels.Num()),
        FText::FromString(TEXT("WorldSweep: Running batch (Flat Level)..."))
    );
    if (!IsRunningCommandlet())
    {
        SlowTask.MakeDialog(true);
    }

    for (int32 PassIndex = 0; PassIndex < PriorityLevels.Num(); ++PassIndex)
    {
        if (SlowTask.ShouldCancel())
        {
            Result.bWasCancelled = true;
            break;
        }

        SlowTask.EnterProgressFrame(
            1.0f,
            FText::FromString(FString::Printf(TEXT("WorldSweep: Pass %d / %d — Processing actors..."),
                PassIndex + 1, PriorityLevels.Num()))
        );

        const int32 CurrentPriority = PriorityLevels[PassIndex];
        const TArray<UWorldSweepScript*>& PassScripts = ScriptsByPriority[CurrentPriority];

        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep [Flat]: Priority pass %d (level %d) | %d script(s)"),
            PassIndex + 1, CurrentPriority, PassScripts.Num());

        for (UWorldSweepScript* Script : PassScripts)
        {
            if (Script->bHandleCellEvents)
            {
                Script->OnPreCellLoad(CellBounds);
                Script->OnCellStarted(CellBounds);
            }
        }

        int32 ActorsInPass = 0;
        for (TActorIterator<AActor> It(InWorld, ActorClass); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor)
            {
                continue;
            }

            if (InSweepArea.IsValid && !InSweepArea.IsInsideOrOn(Actor->GetActorLocation()))
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

            ++ActorsInPass;
            ++Result.ActorsProcessed;
        }

        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Flat]: Pass %d | %d actors processed."),
            PassIndex + 1, ActorsInPass);

        for (UWorldSweepScript* Script : PassScripts)
        {
            if (Script->bHandleCellEvents)
            {
                Script->OnCellCompleted(CellBounds);
            }
        }

        ++Result.CellsProcessed;
    }

    return Result;
}

// ---------------------------------------------------------------------------

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
    // In commandlet mode there is no editor session, so no data layers are marked as
    // loaded in the editor. Skipping this filter ensures all actors are visible to scripts.
    if (IsRunningCommandlet())
    {
        return true;
    }

    if (!InDataLayerManager)
    {
        return true;
    }

    const TArray<const UDataLayerInstance*> ActorLayers = InActor->GetDataLayerInstances();
    if (ActorLayers.IsEmpty())
    {
        return true;
    }

    for (const UDataLayerInstance* Layer : ActorLayers)
    {
        if (Layer && Layer->IsEffectiveLoadedInEditor())
        {
            return true;
        }
    }
    return false;
}
