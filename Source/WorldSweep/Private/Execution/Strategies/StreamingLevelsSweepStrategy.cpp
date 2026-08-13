// Copyright © ToaGames. All Rights Reserved.

#include "Execution/Strategies/StreamingLevelsSweepStrategy.h"

#include "Engine/Level.h"
#include "Engine/LevelBounds.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopedSlowTask.h"
#include "WorldPartition/WorldPartitionHelpers.h"

#include "Core/WorldSweepScript.h"
#include "Execution/WorldSweepRunner.h"
#include "Execution/WorldSweepSaveTracker.h"
#include "WorldSweepLog.h"

namespace
{
    /**
     * RAII guard that forces a streaming level loaded for the duration of a scope and restores
     * its previous state on exit, collecting garbage if it had to unload. Replaces the
     * remember-a-flag-and-undo-it-on-every-return-path pattern, so no early return can leak a
     * level into the loaded state.
     */
    class FScopedLevelLoad
    {
    public:

        FScopedLevelLoad(UWorld* InWorld, ULevelStreaming* InStreamingLevel)
            : World(InWorld)
            , StreamingLevel(InStreamingLevel)
            , bWasAlreadyLoaded(InStreamingLevel->IsLevelLoaded())
        {
            if (!bWasAlreadyLoaded)
            {
                Apply(true);
            }
        }

        ~FScopedLevelLoad()
        {
            if (!bWasAlreadyLoaded)
            {
                Apply(false);
                FWorldPartitionHelpers::DoCollectGarbage();
            }
        }

        FScopedLevelLoad(const FScopedLevelLoad&) = delete;
        FScopedLevelLoad& operator=(const FScopedLevelLoad&) = delete;

    private:

        void Apply(bool InLoaded)
        {
            StreamingLevel->SetShouldBeLoaded(InLoaded);
            StreamingLevel->SetShouldBeVisible(InLoaded);
            World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
        }

    private:

        UWorld* World;
        ULevelStreaming* StreamingLevel;
        bool bWasAlreadyLoaded;
    };
}

const TCHAR* FStreamingLevelsSweepStrategy::GetModeTag() const
{
    return TEXT("Streaming");
}

FText FStreamingLevelsSweepStrategy::GetProgressTitle() const
{
    return NSLOCTEXT("WorldSweep", "StreamingProgressTitle", "WorldSweep: Running batch (Streaming Levels)...");
}

float FStreamingLevelsSweepStrategy::GetWorkUnitsPerPass(const FWorldSweepExecutionContext& InContext) const
{
    return InContext.World ? static_cast<float>(InContext.World->GetStreamingLevels().Num()) : 0.0f;
}

void FStreamingLevelsSweepStrategy::RunPass(FWorldSweepExecutionContext& InOutContext, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FScopedSlowTask& InOutSlowTask, FWorldSweepResult& InOutResult)
{
    UWorld* World = InOutContext.World;
    const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();

    if (StreamingLevels.IsEmpty())
    {
        WS_NOTIFY_WARNING(TEXT("WorldSweep [Streaming]: No streaming levels found. Aborting."));
        return;
    }

    for (int32 LevelIndex = 0; LevelIndex < StreamingLevels.Num(); ++LevelIndex)
    {
        if (InOutSlowTask.ShouldCancel())
        {
            InOutResult.bWasCancelled = true;
            return;
        }

        ULevelStreaming* StreamingLevel = StreamingLevels[LevelIndex];
        if (!StreamingLevel)
        {
            InOutSlowTask.EnterProgressFrame(1.0f);
            continue;
        }

        InOutSlowTask.EnterProgressFrame(
            1.0f,
            FText::FromString(FString::Printf(TEXT("WorldSweep: Level %d / %d (%s)"),
                LevelIndex + 1,
                StreamingLevels.Num(),
                *StreamingLevel->GetWorldAssetPackageName()))
        );

        ProcessLevel(InOutContext, StreamingLevel, InPass, InPassScripts, InOutResult);
    }
}

FBox FStreamingLevelsSweepStrategy::ResolveLevelBounds(const ULevel* InLevel)
{
    for (const TObjectPtr<AActor>& ActorPtr : InLevel->Actors)
    {
        if (const ALevelBounds* LevelBounds = Cast<ALevelBounds>(ActorPtr.Get()))
        {
            return LevelBounds->GetComponentsBoundingBox();
        }
    }

    FBox Bounds(EForceInit::ForceInit);
    for (const TObjectPtr<AActor>& ActorPtr : InLevel->Actors)
    {
        if (ActorPtr && !ActorPtr->IsA<AWorldSettings>())
        {
            Bounds += ActorPtr->GetActorLocation();
        }
    }
    return Bounds;
}

int32 FStreamingLevelsSweepStrategy::ProcessLevelActors(const FWorldSweepExecutionContext& InContext, const ULevel* InLevel, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts)
{
    int32 ActorsInLevel = 0;

    for (const TObjectPtr<AActor>& ActorPtr : InLevel->Actors)
    {
        AActor* Actor = ActorPtr.Get();
        if (!Actor || !Actor->IsA(InContext.ActorClass))
        {
            continue;
        }

        if (!WorldSweepHelpers::PassesDataLayerFilter(Actor, InContext.DataLayerManager))
        {
            continue;
        }

        WorldSweepHelpers::DispatchActorAndComponents(Actor, InCellBounds, InPass, InPassScripts);
        ++ActorsInLevel;
    }

    return ActorsInLevel;
}

void FStreamingLevelsSweepStrategy::ProcessLevel(FWorldSweepExecutionContext& InOutContext, ULevelStreaming* InStreamingLevel, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FWorldSweepResult& InOutResult)
{
    UWorld* World = InOutContext.World;
    const FString LevelName = InStreamingLevel->GetWorldAssetPackageName();

    // Bounds are not known until the level is loaded, so the pre-load event gets an invalid box.
    WorldSweepHelpers::DispatchPreCellLoad(FBox(EForceInit::ForceInit), InPassScripts);

    // Loads the level now if needed and restores its prior state on every exit path below.
    const FScopedLevelLoad LevelLoad(World, InStreamingLevel);

    const ULevel* LoadedLevel = InStreamingLevel->GetLoadedLevel();
    if (!LoadedLevel)
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweep [Streaming]: Failed to load level '%s'. Skipping."), *LevelName);
        return;
    }

    const FBox CellBounds = ResolveLevelBounds(LoadedLevel);

    // If a sweep area is set, skip levels that don't intersect it.
    if (InOutContext.SweepArea.IsValid && CellBounds.IsValid && !CellBounds.Intersect(InOutContext.SweepArea))
    {
        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Streaming]: Level '%s' outside sweep area. Skipping."), *LevelName);
        return;
    }

    WorldSweepHelpers::DispatchCellStarted(CellBounds, InPassScripts);

    int32 ActorsInLevel = 0;
    if (InPass.bNeedsActors)
    {
        ActorsInLevel = ProcessLevelActors(InOutContext, LoadedLevel, CellBounds, InPass, InPassScripts);
        InOutResult.ActorsProcessed += ActorsInLevel;
    }

    UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [Streaming]: Level '%s' | %d actor(s) processed."), *LevelName, ActorsInLevel);

    WorldSweepHelpers::DispatchCellCompleted(CellBounds, InPassScripts);

    // Save while the level is still loaded and before the scope-exit GC runs.
    if (InOutContext.SaveTracker)
    {
        InOutContext.SaveTracker->SaveDirty();
    }

    ++InOutResult.CellsProcessed;
}

