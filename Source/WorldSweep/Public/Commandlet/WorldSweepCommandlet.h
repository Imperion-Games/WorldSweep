// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "WorldSweepCommandlet.generated.h"

/**
 * Runs a WorldSweep batch from the command line without opening the editor UI.
 * Intended for CI/CD pipelines and automated scripting workflows.
 *
 * Usage:
 *   UnrealEditor.exe <Project>.uproject <MapPath> -run=WorldSweep -Batch=<BatchAssetPath> [options]
 *
 * Required:
 *   -Batch=    Soft object path to the UWorldSweepBatch data asset.
 *              Example: -Batch=/Game/WorldSweep/Batches/MyBatch
 *
 * Optional:
 *   -SweepMinX= -SweepMinY= -SweepMaxX= -SweepMaxY=
 *              2D sweep area in world units (cm). Required for World Partition mode;
 *              optional spatial filter in Streaming Levels and Flat Level modes.
 *
 * Exit codes:
 *   0  Batch completed successfully.
 *   1  Invalid or missing arguments.
 *   2  Batch execution was cancelled.
 */
UCLASS()
class WORLDSWEEP_API UWorldSweepCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    UWorldSweepCommandlet();

    //~ Begin UCommandlet Interface
    virtual int32 Main(const FString& InParams) override;
    //~ End UCommandlet Interface
};
