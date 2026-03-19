// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepNullMeshAuditor.h"
#include "WorldSweepLog.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

UWorldSweepNullMeshAuditor::UWorldSweepNullMeshAuditor()
    : bCheckStaticMeshes(true)
    , bCheckSkeletalMeshes(true)
    , ViolationCount(0)
{
    EventFlags = static_cast<int32>(EWorldSweepEventFlags::Components);
}

void UWorldSweepNullMeshAuditor::OnBatchStarted_Implementation()
{
    ViolationCount = 0;
}

void UWorldSweepNullMeshAuditor::OnComponentFound_Implementation(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds)
{
    if (!InComponent || !InActor)
    {
        return;
    }

    if (bCheckStaticMeshes)
    {
        if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(InComponent))
        {
            if (!SMC->GetStaticMesh())
            {
                ++ViolationCount;
                UE_LOG(LogWorldSweep, Warning, TEXT("[NullMeshAuditor] Null static mesh on '%s/%s' at %s"),
                    *InActor->GetActorLabel(),
                    *SMC->GetName(),
                    *InActor->GetActorLocation().ToString());
            }
            return;
        }
    }

    if (bCheckSkeletalMeshes)
    {
        if (const USkeletalMeshComponent* SkMC = Cast<USkeletalMeshComponent>(InComponent))
        {
            if (!SkMC->GetSkeletalMeshAsset())
            {
                ++ViolationCount;
                UE_LOG(LogWorldSweep, Warning, TEXT("[NullMeshAuditor] Null skeletal mesh on '%s/%s' at %s"),
                    *InActor->GetActorLabel(),
                    *SkMC->GetName(),
                    *InActor->GetActorLocation().ToString());
            }
        }
    }
}

void UWorldSweepNullMeshAuditor::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled)
{
    if (ViolationCount == 0)
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[NullMeshAuditor] Audit complete. No null mesh references found."));
    }
    else
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("[NullMeshAuditor] Audit complete. %d null mesh reference(s) found."), ViolationCount);
    }
}
