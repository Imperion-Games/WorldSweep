// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldSweepScript.generated.h"

/** Base class for WorldSweep scripts. Subclass in C++ or Blueprint to define batch actions executed per cell or per actor. */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew)
class WORLDSWEEP_API UWorldSweepScript : public UObject
{
    GENERATED_BODY()

public:

    UWorldSweepScript();

    /** Called once before the batch begins, after script instances are initialized. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnBatchStarted();
    virtual void OnBatchStarted_Implementation() {}

    /** Called before a cell is requested to load. Use to record timestamps or prepare per-cell state. Only fires if bHandleCellEvents is true. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnPreCellLoad(const FBox& InCellBounds);
    virtual void OnPreCellLoad_Implementation(const FBox& InCellBounds) {}

    /** Called after a cell is loaded and before actors are processed. Only fires if bHandleCellEvents is true. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnCellStarted(const FBox& InCellBounds);
    virtual void OnCellStarted_Implementation(const FBox& InCellBounds) {}

    /** Called for each actor found within the current cell. Only fires if bProcessActors is true. Respects ActorTagFilter if non-empty. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnActorFound(AActor* InActor, const FBox& InCellBounds);
    virtual void OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds) {}

    /** Called after all actors in the cell are processed, before the cell is unloaded. Only fires if bHandleCellEvents is true. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnCellCompleted(const FBox& InCellBounds);
    virtual void OnCellCompleted_Implementation(const FBox& InCellBounds) {}

    /** Called once after the entire batch finishes or is cancelled. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnBatchCompleted(int32 InTotalCellsProcessed);
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed) {}

public:

    /** Whether this script receives OnCellStarted and OnCellCompleted events. Disable to skip cell-level overhead. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSweep|Script")
    bool bHandleCellEvents;

    /** Whether this script receives OnActorFound per actor in each cell. Disable for cell-only scripts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSweep|Script")
    bool bProcessActors;

    /** Optional tag filter. If non-empty, OnActorFound is only called for actors with at least one matching tag. Ignored when bProcessActors is false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSweep|Script")
    TArray<FName> ActorTagFilter;

    /** Execution priority. Scripts with lower values run first. Scripts sharing the same priority run together in the same pass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSweep|Script")
    int32 Priority;
};
