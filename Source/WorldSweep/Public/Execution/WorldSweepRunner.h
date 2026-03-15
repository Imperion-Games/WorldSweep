// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldSweepRunner.generated.h"

class UWorldSweepBatch;
class UWorldSweepScript;
class UDataLayerManager;

/** Result summary returned after a batch run completes or is cancelled. */
USTRUCT(BlueprintType)
struct WORLDSWEEP_API FWorldSweepResult
{
    GENERATED_BODY()

    FWorldSweepResult();

public:

    /** Total number of cells visited during the run. */
    UPROPERTY(BlueprintReadOnly, Category = "WorldSweep|Result")
    int32 CellsProcessed;

    /** Total number of actors passed to OnActorFound across all cells and scripts. */
    UPROPERTY(BlueprintReadOnly, Category = "WorldSweep|Result")
    int32 ActorsProcessed;

    /** Whether the run was cancelled by the user before completing. */
    UPROPERTY(BlueprintReadOnly, Category = "WorldSweep|Result")
    bool bWasCancelled;
};

/** Executes a WorldSweep batch synchronously across a given area, loading each cell, running all scripts, then unloading before advancing. Progress is shown via FScopedSlowTask. */
UCLASS()
class WORLDSWEEP_API UWorldSweepRunner : public UObject
{
    GENERATED_BODY()

public:

    UWorldSweepRunner();

    /** Executes InBatch synchronously over InSweepArea in InWorld. Blocks the editor thread and shows a cancellable slow task dialog. Returns a result summary. */
    FWorldSweepResult Execute(UWorldSweepBatch* InBatch, const FBox& InSweepArea, UWorld* InWorld);

private:

    void BuildCellGrid(const FBox& InSweepArea, float InCellSize);
    bool PassesTagFilter(const AActor* InActor, const TArray<FName>& InTagFilter) const;
    bool PassesDataLayerFilter(const AActor* InActor, const UDataLayerManager* InDataLayerManager) const;

private:

    TArray<FBox> CellGrid;
};
