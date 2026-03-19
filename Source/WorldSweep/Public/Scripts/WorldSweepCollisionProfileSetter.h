// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepCollisionProfileSetter.generated.h"

/**
 * Audits or sets a collision profile on all primitive components of actors found during the sweep.
 *
 * Audit mode  (bApplyChanges = false): logs every component whose current profile differs from CollisionProfileName.
 * Apply mode  (bApplyChanges = true):  sets CollisionProfileName on non-conforming components and marks the actor package dirty.
 */
UCLASS(meta = (DisplayName = "Collision Profile Setter"))
class WORLDSWEEP_API UWorldSweepCollisionProfileSetter : public UWorldSweepScript
{
    GENERATED_BODY()

    //~ Begin UWorldSweepScript Interface
protected:
    virtual void OnBatchStarted_Implementation() override;
    virtual void OnComponentFound_Implementation(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled) override;
    //~ End UWorldSweepScript Interface

public:

    UWorldSweepCollisionProfileSetter();

    /** Collision profile name to check or apply to all primitive components on each actor. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|CollisionProfileSetter")
    FName CollisionProfileName;

    /**
     * When false (default), runs in audit mode: logs components whose profile differs from CollisionProfileName without modifying anything.
     * When true, applies CollisionProfileName to non-conforming components and marks the actor package dirty for saving.
     */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|CollisionProfileSetter")
    bool bApplyChanges;

private:

    int32 ViolationCount;
    int32 ChangedCount;
};
