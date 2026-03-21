// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepActorReplacer.h"
#include "WorldSweepLog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UWorldSweepActorReplacer::UWorldSweepActorReplacer()
    : bMatchExactClass(false)
    , bCopyTags(true)
    , bCopyLabel(true)
    , bApplyChanges(false)
    , ReplacedCount(0)
    , FailedCount(0)
    , AuditCount(0)
{
    EventFlags = static_cast<int32>(EWorldSweepEventFlags::CellEvents) | static_cast<int32>(EWorldSweepEventFlags::Actors);
}

void UWorldSweepActorReplacer::OnBatchStarted_Implementation()
{
    ReplacedCount = 0;
    FailedCount   = 0;
    AuditCount    = 0;
    PendingReplacements.Reset();
}

void UWorldSweepActorReplacer::OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds)
{
    if (!InActor || !SourceClass || !TargetClass)
    {
        return;
    }

    const bool bMatches = bMatchExactClass
        ? InActor->GetClass() == SourceClass
        : InActor->IsA(SourceClass);

    if (!bMatches)
    {
        return;
    }

    FReplacementCandidate Candidate;
    Candidate.Actor     = InActor;
    Candidate.Transform = InActor->GetActorTransform();
    Candidate.Level     = InActor->GetLevel();
    Candidate.Label     = InActor->GetActorLabel();
    Candidate.Tags      = InActor->Tags;

    PendingReplacements.Add(MoveTemp(Candidate));
}

void UWorldSweepActorReplacer::OnCellCompleted_Implementation(const FBox& InCellBounds)
{
    if (PendingReplacements.IsEmpty())
    {
        return;
    }

    if (!bApplyChanges)
    {
        for (const FReplacementCandidate& Candidate : PendingReplacements)
        {
            UE_LOG(LogWorldSweep, Warning, TEXT("[ActorReplacer] Would replace '%s' (%s → %s)"),
                *Candidate.Label,
                *SourceClass->GetName(),
                *TargetClass->GetName());
            ++AuditCount;
        }
        PendingReplacements.Reset();
        return;
    }

    if (!World)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("[ActorReplacer] World is null — cannot spawn replacement actors."));
        PendingReplacements.Reset();
        return;
    }

    for (const FReplacementCandidate& Candidate : PendingReplacements)
    {
        if (!IsValid(Candidate.Actor))
        {
            ++FailedCount;
            continue;
        }

        // Modify before destroy so the transaction captures the pre-change state.
        Candidate.Actor->Modify();
        Candidate.Actor->Destroy();

        // Spawn the replacement in the same level at the same transform.
        FActorSpawnParameters Params;
        Params.OverrideLevel           = Candidate.Level;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        AActor* NewActor = World->SpawnActor<AActor>(TargetClass, Candidate.Transform, Params);
        if (!NewActor)
        {
            UE_LOG(LogWorldSweep, Warning, TEXT("[ActorReplacer] Failed to spawn '%s' replacement at '%s'."),
                *TargetClass->GetName(),
                *Candidate.Label);
            ++FailedCount;
            continue;
        }

        NewActor->Modify();

        if (bCopyLabel)
        {
            NewActor->SetActorLabel(Candidate.Label);
        }

        if (bCopyTags)
        {
            NewActor->Tags = Candidate.Tags;
        }

        UE_LOG(LogWorldSweep, Log, TEXT("[ActorReplacer] Replaced '%s': %s → %s"),
            *Candidate.Label,
            *SourceClass->GetName(),
            *TargetClass->GetName());

        ++ReplacedCount;
    }

    PendingReplacements.Reset();
}

void UWorldSweepActorReplacer::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled)
{
    if (bApplyChanges)
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[ActorReplacer] Apply complete. Replaced: %d | Failed: %d"),
            ReplacedCount, FailedCount);
    }
    else
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[ActorReplacer] Dry-run complete. Would replace: %d actor(s) (%s → %s)"),
            AuditCount,
            SourceClass ? *SourceClass->GetName() : TEXT("None"),
            TargetClass ? *TargetClass->GetName() : TEXT("None"));
    }
}
