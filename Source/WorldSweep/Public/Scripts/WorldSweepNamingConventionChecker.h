// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepNamingConventionChecker.generated.h"

/**
 * Checks actor display names (GetActorLabel) against a configurable regex pattern.
 * Logs a warning for every actor whose display name does not match.
 */
UCLASS(meta = (DisplayName = "Naming Convention Checker"))
class WORLDSWEEP_API UWorldSweepNamingConventionChecker : public UWorldSweepScript
{
    GENERATED_BODY()

public:

    UWorldSweepNamingConventionChecker();

    //~ Begin UWorldSweepScript Interface
    virtual void OnBatchStarted_Implementation() override;
    virtual void OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool WasCancelled) override;
    virtual bool HasFailed_Implementation() const override;
    //~ End UWorldSweepScript Interface

    /**
     * Regular expression matched against each actor's display name (label, not internal name).
     * Actors whose label does not produce a match are logged as violations.
     * Example: "^SM_" flags any actor whose label does not start with "SM_".
     */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|NamingConventionChecker")
    FString Pattern;

    /** When true, also logs actors whose label matches the pattern. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|NamingConventionChecker")
    bool bLogMatchingActors;

private:

    int32 ViolationCount;
    int32 CheckedCount;
};