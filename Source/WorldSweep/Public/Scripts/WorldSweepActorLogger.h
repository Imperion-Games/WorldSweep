// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepActorLogger.generated.h"

/** Example WorldSweep script that logs all actors found in each cell. Use as a reference when creating scripts in C++ or Blueprint. */
UCLASS(meta = (DisplayName = "Actor Logger"))
class WORLDSWEEP_API UWorldSweepActorLogger : public UWorldSweepScript
{
    GENERATED_BODY()

    //~ Begin UWorldSweepScript Interface
protected:
    virtual void OnBatchStarted_Implementation() override;
    virtual void OnCellStarted_Implementation(const FBox& InCellBounds) override;
    virtual void OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled) override;
    //~ End UWorldSweepScript Interface

public:

    UWorldSweepActorLogger();

public:

    /** When true, logs the world-space location of each actor alongside its name. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|ActorLogger")
    bool bLogActorLocation;

private:

    int32 TotalActorsLogged;
};
