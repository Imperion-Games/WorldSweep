// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepNullMeshAuditor.generated.h"

/**
 * Audits actors for components with null static or skeletal mesh assets.
 * Logs a warning for every component whose mesh reference is unset.
 */
UCLASS(meta = (DisplayName = "Null Mesh Auditor"))
class WORLDSWEEP_API UWorldSweepNullMeshAuditor : public UWorldSweepScript
{
    GENERATED_BODY()

    //~ Begin UWorldSweepScript Interface
protected:
    virtual void OnBatchStarted_Implementation() override;
    virtual void OnComponentFound_Implementation(UActorComponent* InComponent, AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled) override;
    //~ End UWorldSweepScript Interface

public:

    UWorldSweepNullMeshAuditor();

    /** When true, audits UStaticMeshComponents for null static mesh assets. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|NullMeshAuditor")
    bool bCheckStaticMeshes;

    /** When true, audits USkeletalMeshComponents for null skeletal mesh assets. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|NullMeshAuditor")
    bool bCheckSkeletalMeshes;

private:

    int32 ViolationCount;
};
