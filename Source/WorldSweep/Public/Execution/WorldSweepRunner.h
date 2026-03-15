// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/WorldSweepBatch.h"
#include "WorldSweepRunner.generated.h"

class UWorldSweepBatch;
class UWorldSweepScript;
class UDataLayerManager;
class UWorldPartition;
class ULevelStreaming;

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

/** Executes a WorldSweep batch across a given area, loading each cell, running all scripts, then unloading before advancing. Strategy is selected automatically based on the world type (World Partition, Streaming Levels, or Flat Level) unless overridden on the batch asset. */
UCLASS()
class WORLDSWEEP_API UWorldSweepRunner : public UObject
{
    GENERATED_BODY()

public:

    UWorldSweepRunner();

    /** Executes InBatch over InSweepArea in InWorld. Blocks the editor thread and shows a cancellable slow task dialog. Returns a result summary. InSweepArea is required for World Partition mode; optional in other modes. */
    FWorldSweepResult Execute(UWorldSweepBatch* InBatch, const FBox& InSweepArea, UWorld* InWorld);

private:

    EWorldSweepMode ResolveMode(const UWorldSweepBatch* InBatch, const UWorld* InWorld) const;

    FWorldSweepResult ExecuteWorldPartition(const FBox& InSweepArea, UWorld* InWorld, UWorldPartition* InWP);
    FWorldSweepResult ExecuteStreamingLevels(const FBox& InSweepArea, UWorld* InWorld);
    FWorldSweepResult ExecuteFlatLevel(const FBox& InSweepArea, UWorld* InWorld);

    void BuildCellGrid(const FBox& InSweepArea, float InCellSize);
    bool PassesTagFilter(const AActor* InActor, const TArray<FName>& InTagFilter) const;
    bool PassesDataLayerFilter(const AActor* InActor, const UDataLayerManager* InDataLayerManager) const;

private:

    TArray<FBox> CellGrid;

    // Transient execution context — valid only during Execute().
    TArray<UWorldSweepScript*> ActiveScripts;
    TMap<int32, TArray<UWorldSweepScript*>> ScriptsByPriority;
    TArray<int32> PriorityLevels;
    TObjectPtr<UClass> ActorClass;
    TObjectPtr<const UDataLayerManager> DataLayerManager;
};
