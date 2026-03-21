// Copyright © ToaGames. All Rights Reserved.

#include "Execution/WorldSweepRunner.h"
#include "Core/WorldSweepBatch.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepLog.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelBounds.h"
#include "Engine/World.h"
#include "Misc/ScopedSlowTask.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "ScopedTransaction.h"

FWorldSweepResult::FWorldSweepResult()
    : CellsProcessed(0)
    , ActorsProcessed(0)
    , bWasCancelled(false)
{
}

UWorldSweepRunner::UWorldSweepRunner()
    : ActorClass(nullptr)
    , DataLayerManager(nullptr)
    , bSaveModifications(false)
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

    // Expose the world to all scripts before OnBatchStarted fires.
    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        Script->World = InWorld;
    }

    bSaveModifications = InBatch->bSaveModifications;
    SetupSaveTracking();

    FScopedTransaction Transaction(FText::Format(
        INVTEXT("World Sweep: {0}"), FText::FromString(InBatch->GetName())));

    // Warn for any script that opted into no events — it will silently do nothing.
    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        if (Script->EventFlags == 0)
        {
            UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweep: Script '%s' has EventFlags = 0 and will not receive any events."),
                *Script->GetClass()->GetName());
        }
    }

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
        Script->OnBatchCompleted(Result.CellsProcessed, Result.bWasCancelled);
    }

    // Final save pass — catches anything left for Flat Level mode and any
    // last-cell packages from WP / Streaming that might have been missed.
    SaveAndCheckOutDirtyPackages();

    TeardownSaveTracking();

    // Clear the world reference from all scripts.
    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        Script->World = nullptr;
    }

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Batch '%s' finished. Cells: %d | Actors: %d | Cancelled: %s"),
        *InBatch->GetName(),
        Result.CellsProcessed,
        Result.ActorsProcessed,
        Result.bWasCancelled ? TEXT("Yes") : TEXT("No"));

    return Result;
}

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

    if (!InWP)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweep [WP]: World Partition is null. Aborting."));
        return Result;
    }

    UActorDescContainerInstance* Container = InWP->GetActorDescContainerInstance();
    if (!Container)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweep [WP]: Failed to get actor descriptor container. Aborting."));
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

        // OR all script flags together to determine what this pass needs at an aggregate level.
        int32 PassFlags = 0;
        for (const UWorldSweepScript* Script : PassScripts)
        {
            PassFlags |= Script->EventFlags;
        }

        const int32 CellFlag      = static_cast<int32>(EWorldSweepEventFlags::CellEvents);
        const int32 ActorFlag     = static_cast<int32>(EWorldSweepEventFlags::Actors);
        const int32 ComponentFlag = static_cast<int32>(EWorldSweepEventFlags::Components);

        const bool bPassNeedsCellEvents = (PassFlags & CellFlag)                      != 0;
        const bool bPassNeedsActors     = (PassFlags & (ActorFlag | ComponentFlag))   != 0;

        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [WP]: Priority pass %d (level %d) | %d script(s) | Cell: %s | Actors: %s | Components: %s"),
            PassIndex + 1, CurrentPriority, PassScripts.Num(),
            (PassFlags & CellFlag)      ? TEXT("Yes") : TEXT("No"),
            (PassFlags & ActorFlag)     ? TEXT("Yes") : TEXT("No"),
            (PassFlags & ComponentFlag) ? TEXT("Yes") : TEXT("No"));

        if (!bPassNeedsCellEvents && !bPassNeedsActors)
        {
            // Batch-only scripts — no cell work required.
            continue;
        }

        // WP streaming actors: deduplicated by GUID (stable across GC cycles).
        // Persistent-level actors: deduplicated by pointer (never GC'd, always stable).
        TSet<FGuid>   ProcessedWPActors;
        TSet<AActor*> ProcessedPersistent;

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
                if (Script->EventFlags & CellFlag) Script->OnPreCellLoad(CellBounds);
            }

            int32 ActorsInCell = 0;

            if (bPassNeedsActors)
            {
                // Scoped block: FWorldPartitionReference objects load their actor
                // synchronously on construction and release it on destruction.
                // Keeping them in a local array holds the actors in memory for the
                // duration of the cell, then unloads them when the block exits.
                {
                    // Query actor descriptors intersecting this cell WITHOUT loading actors.
                    // ForEachIntersectingActorDescInstance handles spatial and class filtering
                    // at the descriptor level. FWorldPartitionReference then loads each actor
                    // synchronously on construction via the default FImmediate loading context.
                    TArray<FWorldPartitionReference> CellRefs;
                    FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(InWP, CellBounds, ActorClass,
                        [&](const FWorldPartitionActorDescInstance* DescInstance) -> bool
                        {
                            const FGuid Guid = DescInstance->GetGuid();
                            if (Guid.IsValid() && !ProcessedWPActors.Contains(Guid))
                            {
                                // Mark as processed at the descriptor stage so subsequent cells
                                // never re-queue this actor, regardless of whether loading succeeds
                                // or whether GetActorGuid() returns a matching value at runtime.
                                ProcessedWPActors.Add(Guid);
                                CellRefs.Emplace(Container, Guid);
                            }
                            return true;
                        });

                    for (UWorldSweepScript* Script : PassScripts)
                    {
                        if (Script->EventFlags & CellFlag) Script->OnCellStarted(CellBounds);
                    }

                    // Process WP streaming actors loaded via references.
                    for (const FWorldPartitionReference& Ref : CellRefs)
                    {
                        AActor* Actor = Ref.GetActor();
                        if (!Actor || !PassesDataLayerFilter(Actor, DataLayerManager))
                        {
                            continue;
                        }

                        for (UWorldSweepScript* Script : PassScripts)
                        {
                            if (!(Script->EventFlags & ActorFlag)) continue;
                            if (!Script->ActorTagFilter.IsEmpty() && !PassesTagFilter(Actor, Script->ActorTagFilter)) continue;
                            Script->OnActorFound(Actor, CellBounds);
                        }

                        if (PassFlags & ComponentFlag)
                        {
                            DispatchComponentEvents(Actor, CellBounds, PassScripts);
                        }

                        ++ActorsInCell;
                        ++Result.ActorsProcessed;
                    }

                    // Process persistent-level actors (always loaded, not WP-managed).
                    // Filter by location to assign each one to exactly one sweep cell.
                    for (TActorIterator<AActor> It(InWorld, ActorClass); It; ++It)
                    {
                        AActor* Actor = *It;
                        if (!Actor || Actor->GetLevel() != InWorld->PersistentLevel)
                        {
                            continue;
                        }
                        if (!CellBounds.IsInsideOrOn(Actor->GetActorLocation()))
                        {
                            continue;
                        }
                        // Skip actors already dispatched via the WP descriptor path
                        // (some WP-registered actors, e.g. Landscape, live in the persistent level).
                        const FGuid ActorGuid = Actor->GetActorGuid();
                        if (ActorGuid.IsValid() && ProcessedWPActors.Contains(ActorGuid))
                        {
                            continue;
                        }
                        if (ProcessedPersistent.Contains(Actor))
                        {
                            continue;
                        }
                        if (!PassesDataLayerFilter(Actor, DataLayerManager))
                        {
                            continue;
                        }

                        ProcessedPersistent.Add(Actor);
                        // If this actor has a WP GUID, record it so that GC-induced pointer
                        // changes in later cells don't bypass the pointer-based dedup.
                        if (ActorGuid.IsValid())
                        {
                            ProcessedWPActors.Add(ActorGuid);
                        }

                        for (UWorldSweepScript* Script : PassScripts)
                        {
                            if (!(Script->EventFlags & ActorFlag)) continue;
                            if (!Script->ActorTagFilter.IsEmpty() && !PassesTagFilter(Actor, Script->ActorTagFilter)) continue;
                            Script->OnActorFound(Actor, CellBounds);
                        }

                        if (PassFlags & ComponentFlag)
                        {
                            DispatchComponentEvents(Actor, CellBounds, PassScripts);
                        }

                        ++ActorsInCell;
                        ++Result.ActorsProcessed;
                    }

                    for (UWorldSweepScript* Script : PassScripts)
                    {
                        if (Script->EventFlags & CellFlag) Script->OnCellCompleted(CellBounds);
                    }

                    // Save per-cell while WP actors are still in memory (before CellRefs scope exits and GC runs).
                    SaveAndCheckOutDirtyPackages();

                } // CellRefs destructs here — WP actors are released and queued for unload.

                FWorldPartitionHelpers::DoCollectGarbage();
            }
            else
            {
                // Cell-only pass — no actor loading needed, skip CellRefs and GC entirely.
                for (UWorldSweepScript* Script : PassScripts)
                {
                    if (Script->EventFlags & CellFlag) Script->OnCellStarted(CellBounds);
                }
                for (UWorldSweepScript* Script : PassScripts)
                {
                    if (Script->EventFlags & CellFlag) Script->OnCellCompleted(CellBounds);
                }
            }

            UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [WP]: Pass %d | Cell %d | %d actors processed."),
                PassIndex + 1, CellIndex, ActorsInCell);

            ++Result.CellsProcessed;
        }

        if (Result.bWasCancelled)
        {
            break;
        }
    }

    return Result;
}

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

        int32 PassFlags = 0;
        for (const UWorldSweepScript* Script : PassScripts)
        {
            PassFlags |= Script->EventFlags;
        }

        const int32 CellFlag      = static_cast<int32>(EWorldSweepEventFlags::CellEvents);
        const int32 ActorFlag     = static_cast<int32>(EWorldSweepEventFlags::Actors);
        const int32 ComponentFlag = static_cast<int32>(EWorldSweepEventFlags::Components);

        const bool bPassNeedsCellEvents = (PassFlags & CellFlag)                    != 0;
        const bool bPassNeedsActors     = (PassFlags & (ActorFlag | ComponentFlag)) != 0;

        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Streaming]: Priority pass %d (level %d) | %d script(s) | Cell: %s | Actors: %s | Components: %s"),
            PassIndex + 1, CurrentPriority, PassScripts.Num(),
            (PassFlags & CellFlag)      ? TEXT("Yes") : TEXT("No"),
            (PassFlags & ActorFlag)     ? TEXT("Yes") : TEXT("No"),
            (PassFlags & ComponentFlag) ? TEXT("Yes") : TEXT("No"));

        if (!bPassNeedsCellEvents && !bPassNeedsActors)
        {
            continue;
        }

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
                if (Script->EventFlags & CellFlag) Script->OnPreCellLoad(PreLoadBounds);
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
                if (Script->EventFlags & CellFlag) Script->OnCellStarted(CellBounds);
            }

            int32 ActorsInLevel = 0;
            if (bPassNeedsActors)
            {
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
                        if (!(Script->EventFlags & ActorFlag)) continue;
                        if (!Script->ActorTagFilter.IsEmpty() && !PassesTagFilter(Actor, Script->ActorTagFilter)) continue;
                        Script->OnActorFound(Actor, CellBounds);
                    }

                    if (PassFlags & ComponentFlag)
                    {
                        DispatchComponentEvents(Actor, CellBounds, PassScripts);
                    }

                    ++ActorsInLevel;
                    ++Result.ActorsProcessed;
                }
            }

            UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Streaming]: Pass %d | Level '%s' | %d actors processed."),
                PassIndex + 1, *StreamingLevel->GetWorldAssetPackageName(), ActorsInLevel);

            for (UWorldSweepScript* Script : PassScripts)
            {
                if (Script->EventFlags & CellFlag) Script->OnCellCompleted(CellBounds);
            }

            // Save per-level while the level is still loaded and before GC runs.
            SaveAndCheckOutDirtyPackages();

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

        int32 PassFlags = 0;
        for (const UWorldSweepScript* Script : PassScripts)
        {
            PassFlags |= Script->EventFlags;
        }

        const int32 CellFlag      = static_cast<int32>(EWorldSweepEventFlags::CellEvents);
        const int32 ActorFlag     = static_cast<int32>(EWorldSweepEventFlags::Actors);
        const int32 ComponentFlag = static_cast<int32>(EWorldSweepEventFlags::Components);

        const bool bPassNeedsCellEvents = (PassFlags & CellFlag)                    != 0;
        const bool bPassNeedsActors     = (PassFlags & (ActorFlag | ComponentFlag)) != 0;

        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Flat]: Priority pass %d (level %d) | %d script(s) | Cell: %s | Actors: %s | Components: %s"),
            PassIndex + 1, CurrentPriority, PassScripts.Num(),
            (PassFlags & CellFlag)      ? TEXT("Yes") : TEXT("No"),
            (PassFlags & ActorFlag)     ? TEXT("Yes") : TEXT("No"),
            (PassFlags & ComponentFlag) ? TEXT("Yes") : TEXT("No"));

        if (!bPassNeedsCellEvents && !bPassNeedsActors)
        {
            continue;
        }

        for (UWorldSweepScript* Script : PassScripts)
        {
            if (Script->EventFlags & CellFlag) { Script->OnPreCellLoad(CellBounds); Script->OnCellStarted(CellBounds); }
        }

        int32 ActorsInPass = 0;
        if (bPassNeedsActors)
        {
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
                    if (!(Script->EventFlags & ActorFlag)) continue;
                    if (!Script->ActorTagFilter.IsEmpty() && !PassesTagFilter(Actor, Script->ActorTagFilter)) continue;
                    Script->OnActorFound(Actor, CellBounds);
                }

                if (PassFlags & ComponentFlag)
                {
                    DispatchComponentEvents(Actor, CellBounds, PassScripts);
                }

                ++ActorsInPass;
                ++Result.ActorsProcessed;
            }
        }

        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Flat]: Pass %d | %d actors processed."),
            PassIndex + 1, ActorsInPass);

        for (UWorldSweepScript* Script : PassScripts)
        {
            if (Script->EventFlags & CellFlag) Script->OnCellCompleted(CellBounds);
        }

        ++Result.CellsProcessed;
    }

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

void UWorldSweepRunner::SetupSaveTracking()
{
    SweepDirtyPackages.Reset();
    PreSweepDirtyPackageNames.Reset();

    if (!bSaveModifications)
    {
        return;
    }

    // Snapshot packages already dirty before the sweep so we don't save work that isn't ours.
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        if (It->IsDirty())
        {
            PreSweepDirtyPackageNames.Add(It->GetFName());
        }
    }

    PackageDirtyHandle = UPackage::PackageDirtyStateChangedEvent.AddLambda(
        [this](UPackage* Package)
        {
            if (Package && Package->IsDirty() && !PreSweepDirtyPackageNames.Contains(Package->GetFName()))
            {
                SweepDirtyPackages.AddUnique(Package);
            }
        });
}

void UWorldSweepRunner::TeardownSaveTracking()
{
    if (PackageDirtyHandle.IsValid())
    {
        UPackage::PackageDirtyStateChangedEvent.Remove(PackageDirtyHandle);
        PackageDirtyHandle.Reset();
    }

    SweepDirtyPackages.Reset();
    PreSweepDirtyPackageNames.Reset();
}

void UWorldSweepRunner::SaveAndCheckOutDirtyPackages()
{
    if (!bSaveModifications)
    {
        return;
    }

    // Prune entries that are no longer valid or have already been saved (no longer dirty).
    SweepDirtyPackages.RemoveAll([](const TObjectPtr<UPackage>& Pkg)
    {
        return !IsValid(Pkg) || !Pkg->IsDirty();
    });

    if (SweepDirtyPackages.IsEmpty())
    {
        return;
    }

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Saving %d package(s) dirtied during sweep..."), SweepDirtyPackages.Num());

    const bool bSCEnabled = ISourceControlModule::Get().IsEnabled();

    // Collect file paths and separate existing files from new (not yet on disk) ones.
    TSet<FString> ExistingFilePaths;
    TArray<FString> NewFilePaths;

    for (const TObjectPtr<UPackage>& Pkg : SweepDirtyPackages)
    {
        if (!IsValid(Pkg)) continue;

        FString FilePath;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Pkg->GetName(), FilePath, FPackageName::GetAssetPackageExtension()))
        {
            continue;
        }

        if (IFileManager::Get().FileExists(*FilePath))
        {
            ExistingFilePaths.Add(FilePath);
        }
        else
        {
            NewFilePaths.Add(FilePath);
        }
    }

    // Checkout existing files before saving so the write isn't rejected by the SCC provider.
    if (bSCEnabled && !ExistingFilePaths.IsEmpty())
    {
        SourceControlHelpers::CheckOutOrAddFiles(ExistingFilePaths.Array());
    }

    // Save all dirty packages silently (no prompts).
    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Reserve(SweepDirtyPackages.Num());
    for (const TObjectPtr<UPackage>& Pkg : SweepDirtyPackages)
    {
        if (IsValid(Pkg))
        {
            PackagesToSave.Add(Pkg.Get());
        }
    }
    UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);

    // Mark-for-add files that were just created and didn't exist before the save.
    if (bSCEnabled && !NewFilePaths.IsEmpty())
    {
        TArray<FString> CreatedFilePaths;
        for (const FString& Path : NewFilePaths)
        {
            if (IFileManager::Get().FileExists(*Path))
            {
                CreatedFilePaths.Add(Path);
            }
        }
        if (!CreatedFilePaths.IsEmpty())
        {
            SourceControlHelpers::CheckOutOrAddFiles(CreatedFilePaths);
        }
    }

    SweepDirtyPackages.Reset();
}

void UWorldSweepRunner::DispatchComponentEvents(AActor* InActor, const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts)
{
    const int32 ComponentFlag = static_cast<int32>(EWorldSweepEventFlags::Components);

    TArray<UActorComponent*> Components;
    InActor->GetComponents(Components);

    for (UActorComponent* Comp : Components)
    {
        if (!Comp)
        {
            continue;
        }

        for (UWorldSweepScript* Script : InPassScripts)
        {
            if (!(Script->EventFlags & ComponentFlag)) continue;
            if (!Script->ActorTagFilter.IsEmpty() && !PassesTagFilter(InActor, Script->ActorTagFilter)) continue;
            Script->OnComponentFound(Comp, InActor, InCellBounds);
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
