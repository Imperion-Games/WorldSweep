// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepCollisionProfileSetter.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

#include "WorldSweepLog.h"

UWorldSweepCollisionProfileSetter::UWorldSweepCollisionProfileSetter()
    : CollisionProfileName(NAME_None)
    , bApplyChanges(false)
    , ViolationCount(0)
    , ChangedCount(0)
{
    EventFlags = static_cast<int32>(EWorldSweepEventFlags::Components);
}

void UWorldSweepCollisionProfileSetter::OnBatchStarted_Implementation()
{
    ViolationCount = 0;
    ChangedCount   = 0;
}

void UWorldSweepCollisionProfileSetter::OnComponentFound_Implementation(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds)
{
    if (!InActor || !InComponent || CollisionProfileName.IsNone())
    {
        return;
    }

    UPrimitiveComponent* Comp = Cast<UPrimitiveComponent>(InComponent);
    if (!Comp)
    {
        return;
    }

    const FName CurrentProfile = Comp->GetCollisionProfileName();
    if (CurrentProfile == CollisionProfileName)
    {
        return;
    }

    ++ViolationCount;

    if (bApplyChanges)
    {
        Comp->Modify();
        Comp->SetCollisionProfileName(CollisionProfileName);
        ++ChangedCount;

        UE_LOG(LogWorldSweep, Log, TEXT("[CollisionSetter] '%s/%s': '%s' → '%s'"),
            *InActor->GetActorLabel(),
            *Comp->GetName(),
            *CurrentProfile.ToString(),
            *CollisionProfileName.ToString());
    }
    else
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("[CollisionSetter] Profile mismatch: '%s/%s' has '%s', expected '%s'"),
            *InActor->GetActorLabel(),
            *Comp->GetName(),
            *CurrentProfile.ToString(),
            *CollisionProfileName.ToString());
    }
}

void UWorldSweepCollisionProfileSetter::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool WasCancelled)
{
    if (bApplyChanges)
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[CollisionSetter] Apply complete. Components changed: %d"), ChangedCount);
    }
    else
    {
        if (ViolationCount == 0)
        {
            UE_LOG(LogWorldSweep, Log, TEXT("[CollisionSetter] Audit complete. All components match profile '%s'."),
                *CollisionProfileName.ToString());
        }
        else
        {
            UE_LOG(LogWorldSweep, Warning, TEXT("[CollisionSetter] Audit complete. %d component(s) do not match profile '%s'."),
                ViolationCount,
                *CollisionProfileName.ToString());
        }
    }
}
