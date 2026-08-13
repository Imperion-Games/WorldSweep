// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepBatch.h"
#include "Execution/WorldSweepRunner.h"

class AActor;
class FScopedWorldSweepSaveTracker;
struct FScopedSlowTask;
class UActorComponent;
class UClass;
class UDataLayerManager;
class UWorld;
class UWorldSweepScript;

/** Precomputed aggregate needs for one priority pass. Values are the OR-fold of every script's EventFlags in that pass. Computed once at pass start so each cell iteration can short-circuit cheaply. */
struct WORLDSWEEP_API FWorldSweepPassContext
{
    FWorldSweepPassContext();

    /** Returns true when the pass would do no work at all and can be skipped wholesale. */
    bool IsEmpty() const;

public:

    /** OR-fold of every EWorldSweepEventFlags value across the pass. */
    int32 PassFlags;

    /** True when any script in the pass opted into CellEvents. */
    bool bNeedsCellEvents;

    /** True when any script opted into Actors or Components, meaning actors must be loaded. */
    bool bNeedsActors;

    /** True when any script opted into Components, meaning each actor's components must be iterated. */
    bool bNeedsComponents;
};

/** Shared execution state passed to every sweep strategy. Owned by the Runner for the lifetime of a single Execute() call. Strategies read from it, and call SaveTracker->SaveDirty() at cell/level boundaries when the batch opted into auto-save. */
struct WORLDSWEEP_API FWorldSweepExecutionContext
{
    FWorldSweepExecutionContext();

public:

    /** World being swept. Never null for the duration of a strategy Execute(). */
    UWorld* World;

    /** Region to sweep. Required in World Partition mode; an optional spatial filter elsewhere. */
    FBox SweepArea;

    /** Cell edge length in world units (cm), taken from the batch. */
    float CellSize;

    /** Global actor class filter. Never null: defaults to AActor::StaticClass(). */
    UClass* ActorClass;

    /** Data layer state used to skip actors hidden in the editor. Null in commandlet mode. */
    const UDataLayerManager* DataLayerManager;

    /** Scripts grouped by their Priority value. Kept alive by the Runner's UPROPERTY array. */
    TMap<int32, TArray<UWorldSweepScript*>> ScriptsByPriority;

    /** Sorted keys of ScriptsByPriority. Iterated in ascending order, one pass per entry. */
    TArray<int32> PriorityLevels;

    /** Save coordinator, or null when the batch did not opt into saving. */
    FScopedWorldSweepSaveTracker* SaveTracker;
};

/** Strategy interface. One concrete strategy per EWorldSweepMode; the Runner resolves the mode and instantiates the matching strategy. Adding a new mode is a matter of adding one enum entry and one class, with no edits to the Runner or to peer strategies. */
class WORLDSWEEP_API IWorldSweepStrategy
{
public:

    virtual ~IWorldSweepStrategy() = default;

    /** Iterate cells / levels / actors per this strategy and dispatch script events. Called once per Execute(). */
    virtual FWorldSweepResult Execute(FWorldSweepExecutionContext& InOutContext) = 0;
};

/**
 * Template Method base for the built-in strategies. Owns everything the three modes do
 * identically: slow-task creation, the ascending priority-pass loop, per-pass flag folding,
 * empty-pass skipping, cancellation, and diagnostic logging.
 *
 * Subclasses implement only the part that actually differs between modes: how one pass
 * walks its cells or levels.
 */
class WORLDSWEEP_API FWorldSweepStrategyBase : public IWorldSweepStrategy
{
public:

    //~ Begin IWorldSweepStrategy Interface
    virtual FWorldSweepResult Execute(FWorldSweepExecutionContext& InOutContext) override final;
    //~ End IWorldSweepStrategy Interface

protected:

    /** Short mode tag used in log lines, e.g. TEXT("WP"). */
    virtual const TCHAR* GetModeTag() const = 0;

    /** Title shown on the progress dialog. */
    virtual FText GetProgressTitle() const = 0;

    /** Slow-task work units consumed by a single pass. Multiplied by the pass count for the dialog total. */
    virtual float GetWorkUnitsPerPass(const FWorldSweepExecutionContext& InContext) const = 0;

    /**
     * Run one priority pass. Called once per entry in PriorityLevels, in ascending order,
     * and only when the pass has work to do. Set InOutResult.bWasCancelled and return to
     * abort the remaining passes.
     */
    virtual void RunPass(FWorldSweepExecutionContext& InOutContext, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts, FScopedSlowTask& InOutSlowTask, FWorldSweepResult& InOutResult) = 0;
};

/** Factory. Returns the strategy matching InMode. InMode must be a concrete mode: pass Auto to ResolveMode first. */
WORLDSWEEP_API TUniquePtr<IWorldSweepStrategy> MakeWorldSweepStrategy(EWorldSweepMode InMode);

/** Pure filter and dispatch helpers shared by every strategy. Free functions to avoid coupling strategies through inheritance for stateless logic. */
namespace WorldSweepHelpers
{
    /** Compute the OR-fold of every script's EventFlags in the pass and cache the derived booleans. */
    WORLDSWEEP_API FWorldSweepPassContext ComputePassContext(const TArray<UWorldSweepScript*>& InPassScripts);

    /** Returns true when InActor has at least one tag in InTagFilter, or InTagFilter is empty. */
    WORLDSWEEP_API bool PassesTagFilter(const AActor* InActor, const TArray<FName>& InTagFilter);

    /** Returns true when InActor is visible to the sweep given the current data-layer state. Always true in commandlet mode (no editor data-layer state) and when InDataLayerManager is null. */
    WORLDSWEEP_API bool PassesDataLayerFilter(const AActor* InActor, const UDataLayerManager* InDataLayerManager);

    /** Fire OnPreCellLoad on every script in InPassScripts that opted into the CellEvents flag. */
    WORLDSWEEP_API void DispatchPreCellLoad(const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts);

    /** Fire OnCellStarted on every script in InPassScripts that opted into the CellEvents flag. */
    WORLDSWEEP_API void DispatchCellStarted(const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts);

    /** Fire OnCellCompleted on every script in InPassScripts that opted into the CellEvents flag. */
    WORLDSWEEP_API void DispatchCellCompleted(const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts);

    /** Fire OnActorFound on every script in InPassScripts that opted into the Actors flag, respecting each script's ActorTagFilter. Returns true if any script received the event. */
    WORLDSWEEP_API bool DispatchActorEvents(AActor* InActor, const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts);

    /** Fire OnComponentFound on every script that opted into the Components flag, for every component on InActor. Respects each script's ActorTagFilter (component events inherit their owner's actor tags). */
    WORLDSWEEP_API void DispatchComponentEvents(AActor* InActor, const FBox& InCellBounds, const TArray<UWorldSweepScript*>& InPassScripts);

    /** Dispatch actor and, when the pass needs them, component events for one actor. Returns true when the actor was dispatched to at least one script. */
    WORLDSWEEP_API bool DispatchActorAndComponents(AActor* InActor, const FBox& InCellBounds, const FWorldSweepPassContext& InPass, const TArray<UWorldSweepScript*>& InPassScripts);
}
