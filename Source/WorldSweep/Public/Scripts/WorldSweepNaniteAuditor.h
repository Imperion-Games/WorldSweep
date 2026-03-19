// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepNaniteAuditor.generated.h"

/**
 * Audits or toggles Nanite on static mesh assets referenced by actors in the sweep.
 * Deduplicates by mesh asset path so each mesh is evaluated exactly once regardless
 * of how many actors reference it.
 *
 * Audit mode  (bApplyChanges = false): logs every mesh whose Nanite state differs from bEnableNanite.
 * Apply mode  (bApplyChanges = true):  sets Nanite to bEnableNanite on non-conforming meshes and marks them dirty.
 */
UCLASS(meta = (DisplayName = "Nanite Auditor"))
class WORLDSWEEP_API UWorldSweepNaniteAuditor : public UWorldSweepScript
{
    GENERATED_BODY()

    //~ Begin UWorldSweepScript Interface
protected:
    virtual void OnBatchStarted_Implementation() override;
    virtual void OnComponentFound_Implementation(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled) override;
    //~ End UWorldSweepScript Interface

public:

    UWorldSweepNaniteAuditor();

    /** Target Nanite state. True = Nanite should be enabled. False = Nanite should be disabled. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|NaniteAuditor")
    bool bEnableNanite;

    /**
     * When false (default), runs in audit mode: logs meshes whose Nanite state differs from bEnableNanite without modifying anything.
     * When true, applies the target state to non-conforming meshes and marks their packages dirty for saving.
     */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|NaniteAuditor")
    bool bApplyChanges;

private:

    TSet<FName> ProcessedMeshPaths;
    int32 ViolationCount;
    int32 ChangedCount;
};
