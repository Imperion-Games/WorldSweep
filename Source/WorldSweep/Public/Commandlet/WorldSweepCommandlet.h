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
 *   UnrealEditor.exe <Project>.uproject -run=WorldSweep -Map=<PackagePath> -Batch=<BatchAssetPath> [options]
 *
 * Required:
 *   -Map=      Package path of the map to load before the sweep runs. The engine ignores a
 *              positional map argument when -run= is present, so the map must be named here.
 *              Example: -Map=/Game/Maps/MyOpenWorld
 *   -Batch=    Soft object path to the UWorldSweepBatch data asset.
 *              Example: -Batch=/Game/WorldSweep/Batches/MyBatch
 *
 * Optional:
 *   -SweepMinX= -SweepMinY= -SweepMaxX= -SweepMaxY=
 *              2D sweep area in world units (cm). Required for World Partition mode;
 *              optional spatial filter in Streaming Levels and Flat Level modes.
 *
 * Exit codes:
 *   0  Batch completed successfully and no script reported failure.
 *   1  Invalid or missing arguments.
 *   2  Batch execution was cancelled.
 *   3  Batch completed but one or more scripts returned true from HasFailed().
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
