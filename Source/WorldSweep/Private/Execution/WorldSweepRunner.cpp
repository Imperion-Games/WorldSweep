// Copyright © ToaGames. All Rights Reserved.

#include "Execution/WorldSweepRunner.h"

#include "Engine/World.h"
#include "ScopedTransaction.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartition.h"

#include "Core/WorldSweepBatch.h"
#include "Core/WorldSweepScript.h"
#include "Execution/WorldSweepSaveTracker.h"
#include "Execution/WorldSweepStrategy.h"
#include "WorldSweepLog.h"

namespace
{
    /** Human-readable name for a resolved (never Auto) sweep mode, for log lines. */
    const TCHAR* LexToString(EWorldSweepMode InMode)
    {
        switch (InMode)
        {
            case EWorldSweepMode::WorldPartition:  return TEXT("World Partition");
            case EWorldSweepMode::StreamingLevels: return TEXT("Streaming Levels");
            case EWorldSweepMode::FlatLevel:       return TEXT("Flat Level");
            default:                               return TEXT("Auto");
        }
    }
}

FWorldSweepResult::FWorldSweepResult()
    : CellsProcessed(0)
    , ActorsProcessed(0)
    , bWasCancelled(false)
    , bAnyScriptFailed(false)
{
}

UWorldSweepRunner::UWorldSweepRunner()
{
}

FWorldSweepResult UWorldSweepRunner::Execute(UWorldSweepBatch* InBatch, const FBox& InSweepArea, UWorld* InWorld)
{
    FWorldSweepResult Result;

    if (!ensure(InBatch) || !ensure(InWorld))
    {
        WS_NOTIFY_ERROR(TEXT("WorldSweep: Execute called with null batch or world. Aborting."));
        return Result;
    }

    if (CollectActiveScripts(InBatch) == 0)
    {
        WS_NOTIFY_WARNING(TEXT("WorldSweep: Batch '%s' has no scripts. Nothing to execute."), *InBatch->GetName());
        return Result;
    }

    FWorldSweepExecutionContext Context;
    BuildExecutionContext(Context, InBatch, InSweepArea, InWorld);

    const EWorldSweepMode ResolvedMode = ResolveMode(InBatch, InWorld);

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweep: Starting batch '%s' | Mode: %s | %d scripts | %d priority pass(es)"),
        *InBatch->GetName(),
        LexToString(ResolvedMode),
        ActiveScripts.Num(),
        Context.PriorityLevels.Num());

    SetScriptWorld(InWorld);
    WarnOnFlaglessScripts();

    // RAII: registers the package-dirty listener now, removes it in the destructor.
    FScopedWorldSweepSaveTracker SaveTracker(InBatch->bSaveModifications);
    Context.SaveTracker = &SaveTracker;

    FScopedTransaction Transaction(FText::Format(
        INVTEXT("World Sweep: {0}"), FText::FromString(InBatch->GetName())));

    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        Script->OnBatchStarted();
    }

    if (TUniquePtr<IWorldSweepStrategy> Strategy = MakeWorldSweepStrategy(ResolvedMode))
    {
        Result = Strategy->Execute(Context);
    }

    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        Script->OnBatchCompleted(Result.CellsProcessed, Result.bWasCancelled);
    }

    Result.bAnyScriptFailed = AnyScriptFailed();

    // Final save pass: catches Flat Level mode, which has no per-cell boundary, plus any
    // last-cell packages the World Partition and Streaming paths might have missed.
    SaveTracker.SaveDirty();

    SetScriptWorld(nullptr);

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

int32 UWorldSweepRunner::CollectActiveScripts(const UWorldSweepBatch* InBatch)
{
    // Held via UPROPERTY on this Runner so the scripts survive the DoCollectGarbage()
    // calls that strategies trigger between cells.
    ActiveScripts.Reset();

    for (UWorldSweepScript* Script : InBatch->Scripts)
    {
        if (Script)
        {
            ActiveScripts.Add(Script);
        }
    }

    return ActiveScripts.Num();
}

void UWorldSweepRunner::BuildExecutionContext(FWorldSweepExecutionContext& InOutContext, const UWorldSweepBatch* InBatch, const FBox& InSweepArea, UWorld* InWorld) const
{
    InOutContext.World            = InWorld;
    InOutContext.SweepArea        = InSweepArea;
    InOutContext.CellSize         = InBatch->CellSize;
    InOutContext.ActorClass       = InBatch->ActorClassFilter ? InBatch->ActorClassFilter.Get() : AActor::StaticClass();
    InOutContext.DataLayerManager = UDataLayerManager::GetDataLayerManager(InWorld);

    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        InOutContext.ScriptsByPriority.FindOrAdd(Script->Priority).Add(Script.Get());
    }

    InOutContext.ScriptsByPriority.GetKeys(InOutContext.PriorityLevels);
    InOutContext.PriorityLevels.Sort();
}

void UWorldSweepRunner::WarnOnFlaglessScripts() const
{
    // Collect all offenders and emit a single combined warning so the log and the
    // notification toast are not duplicated once per script.
    TArray<FString> ZeroFlagScripts;
    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        if (Script->EventFlags == 0)
        {
            ZeroFlagScripts.Add(Script->GetClass()->GetName());
        }
    }

    if (!ZeroFlagScripts.IsEmpty())
    {
        WS_NOTIFY_WARNING(TEXT("WorldSweep: %d script(s) have EventFlags = 0 and will receive no events: %s"),
            ZeroFlagScripts.Num(), *FString::Join(ZeroFlagScripts, TEXT(", ")));
    }
}

void UWorldSweepRunner::SetScriptWorld(UWorld* InWorld) const
{
    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        Script->World = InWorld;
    }
}

bool UWorldSweepRunner::AnyScriptFailed() const
{
    for (const TObjectPtr<UWorldSweepScript>& Script : ActiveScripts)
    {
        if (Script->HasFailed())
        {
            return true;
        }
    }
    return false;
}
