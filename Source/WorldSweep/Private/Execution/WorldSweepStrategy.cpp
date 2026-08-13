// Copyright © ToaGames. All Rights Reserved.

#include "Execution/WorldSweepStrategy.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopedSlowTask.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#include "Core/WorldSweepScript.h"
#include "Execution/Strategies/FlatLevelSweepStrategy.h"
#include "Execution/Strategies/StreamingLevelsSweepStrategy.h"
#include "Execution/Strategies/WorldPartitionSweepStrategy.h"
#include "WorldSweepLog.h"

namespace
{
    /** Returns true when InScript opted into the CellEvents flag. */
    bool WantsCellEvents(const UWorldSweepScript* InScript)
    {
        return InScript && (InScript->EventFlags & static_cast<int32>(EWorldSweepEventFlags::CellEvents)) != 0;
    }
}

FWorldSweepPassContext::FWorldSweepPassContext()
    : PassFlags(0)
    , bNeedsCellEvents(false)
    , bNeedsActors(false)
    , bNeedsComponents(false)
{
}

bool FWorldSweepPassContext::IsEmpty() const
{
    return !bNeedsCellEvents && !bNeedsActors;
}

FWorldSweepExecutionContext::FWorldSweepExecutionContext()
    : World(nullptr)
    , SweepArea(EForceInit::ForceInit)
    , CellSize(0.0f)
    , ActorClass(nullptr)
    , DataLayerManager(nullptr)
    , SaveTracker(nullptr)
{
}

FWorldSweepResult FWorldSweepStrategyBase::Execute(FWorldSweepExecutionContext& InOutContext)
{
    FWorldSweepResult Result;

    const int32 PassCount = InOutContext.PriorityLevels.Num();

    FScopedSlowTask SlowTask(GetWorkUnitsPerPass(InOutContext) * static_cast<float>(PassCount), GetProgressTitle());
    if (!IsRunningCommandlet())
    {
        SlowTask.MakeDialog(true);
    }

    for (int32 PassIndex = 0; PassIndex < PassCount; ++PassIndex)
    {
        const int32 CurrentPriority = InOutContext.PriorityLevels[PassIndex];
        const TArray<UWorldSweepScript*>& PassScripts = InOutContext.ScriptsByPriority[CurrentPriority];

        const FWorldSweepPassContext Pass = WorldSweepHelpers::ComputePassContext(PassScripts);

        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweep [%s]: Priority pass %d / %d (level %d) | %d script(s) | Cell: %s | Actors: %s | Components: %s"),
            GetModeTag(),
            PassIndex + 1,
            PassCount,
            CurrentPriority,
            PassScripts.Num(),
            Pass.bNeedsCellEvents ? TEXT("Yes") : TEXT("No"),
            Pass.bNeedsActors     ? TEXT("Yes") : TEXT("No"),
            Pass.bNeedsComponents ? TEXT("Yes") : TEXT("No"));

        if (Pass.IsEmpty())
        {
            continue;
        }

        RunPass(InOutContext, Pass, PassScripts, SlowTask, Result);

        if (Result.bWasCancelled)
        {
            break;
        }
    }

    return Result;
}

TUniquePtr<IWorldSweepStrategy> MakeWorldSweepStrategy(EWorldSweepMode InMode)
{
    switch (InMode)
    {
        case EWorldSweepMode::WorldPartition:  return MakeUnique<FWorldPartitionSweepStrategy>();
        case EWorldSweepMode::StreamingLevels: return MakeUnique<FStreamingLevelsSweepStrategy>();
        case EWorldSweepMode::FlatLevel:       return MakeUnique<FFlatLevelSweepStrategy>();
        default:                               return nullptr;
    }
}

namespace WorldSweepHelpers
{
    FWorldSweepPassContext ComputePassContext(const TArray<UWorldSweepScript*>& InPassScripts)
    {
        FWorldSweepPassContext Result;
        for (const UWorldSweepScript* Script : InPassScripts)
        {
            if (Script)
            {
                Result.PassFlags |= Script->EventFlags;
            }
        }

        const int32 CellFlag      = static_cast<int32>(EWorldSweepEventFlags::CellEvents);
        const int32 ActorFlag     = static_cast<int32>(EWorldSweepEventFlags::Actors);
        const int32 ComponentFlag = static_cast<int32>(EWorldSweepEventFlags::Components);

        Result.bNeedsCellEvents = (Result.PassFlags & CellFlag)                    != 0;
        Result.bNeedsActors     = (Result.PassFlags & (ActorFlag | ComponentFlag)) != 0;
        Result.bNeedsComponents = (Result.PassFlags & ComponentFlag)               != 0;
        return Result;
    }

    bool PassesTagFilter(const AActor* InActor, const TArray<FName>& InTagFilter)
    {
        // An empty filter means "no filtering", so every actor passes.
        if (InTagFilter.IsEmpty())
        {
            return true;
        }

        if (!InActor)
        {
            return false;
        }

        for (const FName& Tag : InTagFilter)
        {
            if (InActor->ActorHasTag(Tag))
            {
                return true;
            }
        }
        return false;
    }

    bool PassesDataLayerFilter(const AActor* InActor, const UDataLayerManager* InDataLayerManager)
    {
        // Commandlet mode has no editor session, so no data layers are marked as loaded in the editor.
        // Skipping this filter ensures all actors are visible to scripts.
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

    void DispatchPreCellLoad(const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts)
    {
        for (UWorldSweepScript* Script : InPassScripts)
        {
            if (WantsCellEvents(Script))
            {
                Script->OnPreCellLoad(InCellBounds);
            }
        }
    }

    void DispatchCellStarted(const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts)
    {
        for (UWorldSweepScript* Script : InPassScripts)
        {
            if (WantsCellEvents(Script))
            {
                Script->OnCellStarted(InCellBounds);
            }
        }
    }

    void DispatchCellCompleted(const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts)
    {
        for (UWorldSweepScript* Script : InPassScripts)
        {
            if (WantsCellEvents(Script))
            {
                Script->OnCellCompleted(InCellBounds);
            }
        }
    }

    bool DispatchActorEvents(AActor* InActor, const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts)
    {
        const int32 ActorFlag = static_cast<int32>(EWorldSweepEventFlags::Actors);

        int32 DispatchCount = 0;
        for (UWorldSweepScript* Script : InPassScripts)
        {
            if (!Script || !(Script->EventFlags & ActorFlag))
            {
                continue;
            }
            if (!PassesTagFilter(InActor, Script->ActorTagFilter))
            {
                continue;
            }

            Script->OnActorFound(InActor, InCellBounds);
            ++DispatchCount;
        }
        return DispatchCount > 0;
    }

    void DispatchComponentEvents(AActor* InActor, const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts)
    {
        const int32 ComponentFlag = static_cast<int32>(EWorldSweepEventFlags::Components);

        TArray<UActorComponent*> Components;
        InActor->GetComponents(Components);

        for (UActorComponent* Component : Components)
        {
            if (!Component)
            {
                continue;
            }

            for (UWorldSweepScript* Script : InPassScripts)
            {
                if (!Script || !(Script->EventFlags & ComponentFlag))
                {
                    continue;
                }
                if (!PassesTagFilter(InActor, Script->ActorTagFilter))
                {
                    continue;
                }

                Script->OnComponentFound(Component, InActor, InCellBounds);
            }
        }
    }

    bool DispatchActorAndComponents(AActor* InActor, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts)
    {
        // Actor events fire before component events so a script always sees an actor
        // before that actor's components.
        const bool bReachedAnyScript = DispatchActorEvents(InActor, InCellBounds, InPassScripts);

        if (InPass.bNeedsComponents)
        {
            DispatchComponentEvents(InActor, InCellBounds, InPassScripts);
        }

        return bReachedAnyScript;
    }
}
