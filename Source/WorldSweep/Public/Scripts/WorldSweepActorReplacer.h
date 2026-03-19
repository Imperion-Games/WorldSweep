// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepActorReplacer.generated.h"

/** Replaces actors of one class with actors of another class, preserving transform. Collected per-cell and replaced after actor iteration to avoid iterator invalidation. Set bApplyChanges to false (default) for a dry-run audit; true to actually destroy and respawn actors. */
UCLASS(meta = (DisplayName = "Actor Replacer"))
class WORLDSWEEP_API UWorldSweepActorReplacer : public UWorldSweepScript
{
    GENERATED_BODY()

public:

    UWorldSweepActorReplacer();

    virtual void OnBatchStarted_Implementation() override;
    virtual void OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnCellCompleted_Implementation(const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled) override;

public:

    /** Class of actors to find and replace. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|ActorReplacer")
    TSubclassOf<AActor> SourceClass;

    /** Class to spawn in place of each matched actor. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|ActorReplacer")
    TSubclassOf<AActor> TargetClass;

    /** When true, only actors whose class is exactly SourceClass are replaced. When false, subclasses are also matched. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|ActorReplacer")
    bool bMatchExactClass;

    /** Copy actor tags from the source actor to the spawned replacement. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|ActorReplacer")
    bool bCopyTags;

    /** Copy the actor label (display name) from the source actor to the spawned replacement. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|ActorReplacer")
    bool bCopyLabel;

    /** When false (default), only logs what would be replaced without making any changes. Set to true to apply replacements. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|ActorReplacer")
    bool bApplyChanges;

private:

    struct FReplacementCandidate
    {
        AActor*    Actor     = nullptr;
        FTransform Transform = FTransform::Identity;
        ULevel*    Level     = nullptr;
        FString    Label;
        TArray<FName> Tags;
    };

    TArray<FReplacementCandidate> PendingReplacements;
    int32 ReplacedCount;
    int32 FailedCount;
    int32 AuditCount;
};
