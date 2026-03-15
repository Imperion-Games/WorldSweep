// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepCellLoadTimer.generated.h"

/** Example WorldSweep script that measures and logs how long each cell takes to load. Reports min, max, and average load times on batch completion. */
UCLASS(meta = (DisplayName = "Cell Load Timer"))
class WORLDSWEEP_API UWorldSweepCellLoadTimer : public UWorldSweepScript
{
    GENERATED_BODY()

    //~ Begin UWorldSweepScript Interface
public:
    virtual void OnBatchStarted_Implementation() override;
    virtual void OnPreCellLoad_Implementation(const FBox& InCellBounds) override;
    virtual void OnCellStarted_Implementation(const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed) override;
    //~ End UWorldSweepScript Interface

public:

    UWorldSweepCellLoadTimer();

private:

    double CellLoadStartTime;
    double TotalLoadTime;
    double MinLoadTime;
    double MaxLoadTime;
    int32 CellsRecorded;
};
