// Copyright © ToaGames. All Rights Reserved.

#include "Scripts/WorldSweepCellLoadTimer.h"
#include "WorldSweepLog.h"
#include "HAL/PlatformTime.h"

UWorldSweepCellLoadTimer::UWorldSweepCellLoadTimer()
    : CellLoadStartTime(0.0)
    , TotalLoadTime(0.0)
    , MinLoadTime(TNumericLimits<double>::Max())
    , MaxLoadTime(0.0)
    , CellsRecorded(0)
{
    EventFlags = static_cast<int32>(EWorldSweepEventFlags::CellEvents);
}

void UWorldSweepCellLoadTimer::OnBatchStarted_Implementation()
{
    CellLoadStartTime = 0.0;
    TotalLoadTime     = 0.0;
    MinLoadTime       = TNumericLimits<double>::Max();
    MaxLoadTime       = 0.0;
    CellsRecorded     = 0;

    UE_LOG(LogWorldSweep, Log, TEXT("[CellLoadTimer] Batch started. Measuring cell load times."));
}

void UWorldSweepCellLoadTimer::OnPreCellLoad_Implementation(const FBox& InCellBounds)
{
    CellLoadStartTime = FPlatformTime::Seconds();
}

void UWorldSweepCellLoadTimer::OnCellStarted_Implementation(const FBox& InCellBounds)
{
    const double LoadTime = FPlatformTime::Seconds() - CellLoadStartTime;

    TotalLoadTime += LoadTime;
    MinLoadTime    = FMath::Min(MinLoadTime, LoadTime);
    MaxLoadTime    = FMath::Max(MaxLoadTime, LoadTime);
    ++CellsRecorded;

    UE_LOG(LogWorldSweep, Verbose, TEXT("[CellLoadTimer] Cell loaded in %.4f s. Center: %s"),
        LoadTime, *InCellBounds.GetCenter().ToString());
}

void UWorldSweepCellLoadTimer::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled)
{
    if (CellsRecorded == 0)
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("[CellLoadTimer] No cells were recorded."));
        return;
    }

    const double AverageLoadTime = TotalLoadTime / static_cast<double>(CellsRecorded);

    UE_LOG(LogWorldSweep, Log, TEXT("[CellLoadTimer] Batch complete. Cells: %d | Total: %.4f s | Avg: %.4f s | Min: %.4f s | Max: %.4f s"),
        CellsRecorded,
        TotalLoadTime,
        AverageLoadTime,
        MinLoadTime,
        MaxLoadTime
    );
}
