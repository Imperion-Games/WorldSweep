// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/WorldSweepScript.h"
#include "WorldSweepBatch.generated.h"

/** Data asset defining a collection of WorldSweep scripts and execution parameters. Create one per logical batch operation via the Content Browser. */
UCLASS(BlueprintType)
class WORLDSWEEP_API UWorldSweepBatch : public UDataAsset
{
    GENERATED_BODY()

public:

    UWorldSweepBatch();

public:

    /** Scripts to execute during the batch. Instances are created inline and run in array order for every cell. */
    UPROPERTY(EditAnywhere, Instanced, Category = "WorldSweep|Batch")
    TArray<TObjectPtr<UWorldSweepScript>> Scripts;

    /** Size of each sweep cell in world units (cm). Smaller values give finer iteration but more load/unload cycles. Default matches the standard World Partition streaming cell size. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|Batch", meta = (ClampMin = "100.0", UIMin = "1000.0"))
    float CellSize;

    /** Optional actor class filter applied globally. When set, only actors of this class or any subclass are passed to OnActorFound. Leave empty to process all actors. */
    UPROPERTY(EditAnywhere, Category = "WorldSweep|Batch")
    TSubclassOf<AActor> ActorClassFilter;
};
