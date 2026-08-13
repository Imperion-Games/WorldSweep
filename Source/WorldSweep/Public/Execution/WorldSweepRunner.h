// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/WorldSweepBatch.h"
#include "WorldSweepRunner.generated.h"

struct FWorldSweepExecutionContext;

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

    /** Whether any script returned true from HasFailed() after the batch completed. */
    UPROPERTY(BlueprintReadOnly, Category = "WorldSweep|Result")
    bool bAnyScriptFailed;
};

/** Executes a WorldSweep batch across a given area. Resolves the sweep mode from the world type (or the batch override), delegates iteration to a matching IWorldSweepStrategy, and coordinates transaction, save-tracking, and script lifecycle around it. */
UCLASS()
class WORLDSWEEP_API UWorldSweepRunner : public UObject
{
    GENERATED_BODY()

public:

    UWorldSweepRunner();

    /** Executes InBatch over InSweepArea in InWorld. Blocks the editor thread and shows a cancellable slow-task dialog. InSweepArea is required for World Partition mode; optional in other modes. */
    FWorldSweepResult Execute(UWorldSweepBatch* InBatch, const FBox& InSweepArea, UWorld* InWorld);

private:

    /** Resolve a concrete sweep mode, detecting from the world type when the batch is set to Auto. Never returns Auto. */
    EWorldSweepMode ResolveMode(const UWorldSweepBatch* InBatch, const UWorld* InWorld) const;

    /** Populate ActiveScripts with the non-null entries of InBatch. Returns the number collected. */
    int32 CollectActiveScripts(const UWorldSweepBatch* InBatch);

    /** Fill InOutContext from the batch and world, and group the active scripts into ascending priority passes. */
    void BuildExecutionContext(FWorldSweepExecutionContext& InOutContext, const UWorldSweepBatch* InBatch, const FBox& InSweepArea, UWorld* InWorld) const;

    /** Emit one combined warning naming every active script whose EventFlags are zero. Such scripts receive no events at all. */
    void WarnOnFlaglessScripts() const;

    /** Assign InWorld to every active script's World property. Pass null to clear it after the run. */
    void SetScriptWorld(UWorld* InWorld) const;

    /** Returns true when any active script reports failure from HasFailed(). */
    bool AnyScriptFailed() const;

private:

    /** Transient execution context. UPROPERTY keeps scripts reachable during DoCollectGarbage() calls between cells. */
    UPROPERTY()
    TArray<TObjectPtr<UWorldSweepScript>> ActiveScripts;
};
