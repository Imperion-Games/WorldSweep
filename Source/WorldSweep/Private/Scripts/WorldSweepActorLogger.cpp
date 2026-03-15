// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepActorLogger.h"
#include "WorldSweepLog.h"
#include "GameFramework/Actor.h"

UWorldSweepActorLogger::UWorldSweepActorLogger()
    : bLogActorLocation(false)
    , TotalActorsLogged(0)
{
    bHandleCellEvents = true;
    bProcessActors = true;
}

void UWorldSweepActorLogger::OnBatchStarted_Implementation()
{
    TotalActorsLogged = 0;
    UE_LOG(LogWorldSweep, Log, TEXT("[ActorLogger] Batch started."));
}

void UWorldSweepActorLogger::OnCellStarted_Implementation(const FBox& InCellBounds)
{
    UE_LOG(LogWorldSweep, Verbose, TEXT("[ActorLogger] Cell started. Center: %s"), *InCellBounds.GetCenter().ToString());
}

void UWorldSweepActorLogger::OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds)
{
    if (!InActor)
    {
        return;
    }

    if (bLogActorLocation)
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[ActorLogger] %s | Class: %s | Location: %s"),
            *InActor->GetName(),
            *InActor->GetClass()->GetName(),
            *InActor->GetActorLocation().ToString()
        );
    }
    else
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[ActorLogger] %s | Class: %s"),
            *InActor->GetName(),
            *InActor->GetClass()->GetName()
        );
    }

    ++TotalActorsLogged;
}

void UWorldSweepActorLogger::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed)
{
    UE_LOG(LogWorldSweep, Log, TEXT("[ActorLogger] Batch complete. Total actors logged: %d across %d cells."),
        TotalActorsLogged,
        InTotalCellsProcessed
    );
}
