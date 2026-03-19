// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldSweepScript.generated.h"

class UWorld;

/** Bitmask declaring which runner events a WorldSweep script opts into. OR the relevant flags together on EventFlags to receive any combination. The runner accumulates these across all scripts in a priority pass to decide what work to perform (e.g. skipping actor loading when no script in the pass needs it). */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EWorldSweepEventFlags : uint8
{
    None       = 0x00  UMETA(Hidden),

    /** Cell lifecycle events: OnPreCellLoad, OnCellStarted, OnCellCompleted. */
    CellEvents = 0x01  UMETA(DisplayName = "Cell Events"),

    /** Per-actor event: OnActorFound. Actors are loaded into memory when any script in the pass sets this flag. */
    Actors     = 0x02  UMETA(DisplayName = "Actors"),

    /** Per-component event: OnComponentFound. Actors are loaded when any script in the pass sets this flag, even without the Actors flag. */
    Components = 0x04  UMETA(DisplayName = "Components"),
};
ENUM_CLASS_FLAGS(EWorldSweepEventFlags)

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

    /** Called before a cell is requested to load. Use to record timestamps or prepare per-cell state. Only fires when EventFlags includes CellEvents. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnPreCellLoad(const FBox& InCellBounds);
    virtual void OnPreCellLoad_Implementation(const FBox& InCellBounds) {}

    /** Called after a cell is loaded and before actors are processed. Only fires when EventFlags includes CellEvents. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnCellStarted(const FBox& InCellBounds);
    virtual void OnCellStarted_Implementation(const FBox& InCellBounds) {}

    /** Called for each actor found within the current cell. Only fires when EventFlags includes Actors. Respects ActorTagFilter if non-empty. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnActorFound(AActor* InActor, const FBox& InCellBounds);
    virtual void OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds) {}

    /** Called after all actors in the cell are processed, before the cell is unloaded. Only fires when EventFlags includes CellEvents. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnCellCompleted(const FBox& InCellBounds);
    virtual void OnCellCompleted_Implementation(const FBox& InCellBounds) {}

    /** Called for each component on every actor found in the current cell. Only fires when EventFlags includes Components. Cast InComponent to the specific type you need. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnComponentFound(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds);
    virtual void OnComponentFound_Implementation(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds) {}

    /** Called once after the entire batch finishes or is cancelled. bWasCancelled is true when the user pressed Cancel before all cells were processed. */
    UFUNCTION(BlueprintNativeEvent, Category = "WorldSweep|Script")
    void OnBatchCompleted(int32 InTotalCellsProcessed, bool bWasCancelled);
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled) {}

public:

    /** Bitmask of EWorldSweepEventFlags. Controls which events this script receives. The runner ORs all script flags in a pass to determine what work to do — actor loading, cell events, component iteration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSweep|Script", meta = (Bitmask, BitmaskEnum = "/Script/WorldSweep.EWorldSweepEventFlags"))
    int32 EventFlags;

    /** Optional tag filter. If non-empty, OnActorFound and OnComponentFound are only called for actors with at least one matching tag. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSweep|Script")
    TArray<FName> ActorTagFilter;

    /** Execution priority. Scripts with lower values run first. Scripts sharing the same priority run together in the same pass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSweep|Script")
    int32 Priority;

    /** The world being swept. Set by the runner before OnBatchStarted and cleared after OnBatchCompleted. Use this to spawn or query actors during the batch. */
    UPROPERTY(Transient, BlueprintReadOnly, Category = "WorldSweep|Script")
    TObjectPtr<UWorld> World;

    bool NeedsCellEvents() const { return (EventFlags & static_cast<int32>(EWorldSweepEventFlags::CellEvents)) != 0; }
    bool NeedsActors()     const { return (EventFlags & static_cast<int32>(EWorldSweepEventFlags::Actors))     != 0; }
    bool NeedsComponents() const { return (EventFlags & static_cast<int32>(EWorldSweepEventFlags::Components)) != 0; }
};
