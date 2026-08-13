// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepActorReplacer.generated.h"

class AActor;
class ULevel;

/** Replaces actors of one class with actors of another class, preserving transform. Collected per-cell and replaced after actor iteration to avoid iterator invalidation. Set bApplyChanges to false (default) for a dry-run audit; true to actually destroy and respawn actors. */
UCLASS(meta = (DisplayName = "Actor Replacer"))
class WORLDSWEEP_API UWorldSweepActorReplacer : public UWorldSweepScript
{
    GENERATED_BODY()

public:

    UWorldSweepActorReplacer();

    //~ Begin UWorldSweepScript Interface
    virtual void OnBatchStarted_Implementation() override;
    virtual void OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnCellCompleted_Implementation(const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool WasCancelled) override;
    //~ End UWorldSweepScript Interface

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

    /**
     * One actor queued for replacement. Object references are weak: this struct lives in a
     * plain (non-UPROPERTY) array, so a raw pointer would create no GC reference, and calling
     * IsValid() on a collected object is undefined behaviour. Weak pointers make the validity
     * check meaningful even if a script triggers a collection mid-cell.
     */
    struct FReplacementCandidate
    {
        FReplacementCandidate()
            : Transform(FTransform::Identity)
        {}

        TWeakObjectPtr<AActor> Actor;
        FTransform             Transform;
        TWeakObjectPtr<ULevel> Level;
        FString                Label;
        TArray<FName>          Tags;
    };

    TArray<FReplacementCandidate> PendingReplacements;
    int32 ReplacedCount;
    int32 FailedCount;
    int32 AuditCount;
};
