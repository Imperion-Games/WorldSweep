// Copyright © ToaGames. All Rights Reserved.

#include "Commandlet/WorldSweepCommandlet.h"
#include "Core/WorldSweepBatch.h"
#include "Execution/WorldSweepRunner.h"
#include "WorldSweepLog.h"

#include "Engine/World.h"
#include "Editor.h"

// ---------------------------------------------------------------------------

UWorldSweepCommandlet::UWorldSweepCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UWorldSweepCommandlet::Main(const FString& InParams)
{
    // ----- Resolve world -----

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
    if (!World)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweepCommandlet: No world is loaded. Pass the map path before -run=WorldSweep."));
        return 1;
    }

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: World '%s' loaded."), *World->GetName());

    // ----- Resolve batch asset -----

    FString BatchPath;
    if (!FParse::Value(*InParams, TEXT("Batch="), BatchPath))
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweepCommandlet: Missing required argument -Batch=<AssetPath>."));
        return 1;
    }

    UWorldSweepBatch* Batch = LoadObject<UWorldSweepBatch>(nullptr, *BatchPath);
    if (!Batch)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweepCommandlet: Failed to load batch asset '%s'."), *BatchPath);
        return 1;
    }

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: Batch '%s' loaded (%d script(s))."),
        *Batch->GetName(), Batch->Scripts.Num());

    // ----- Resolve sweep area -----

    FBox SweepArea(EForceInit::ForceInit);

    float MinX = 0.0f;
    float MinY = 0.0f;
    float MaxX = 0.0f;
    float MaxY = 0.0f;

    const bool bHasMinX = FParse::Value(*InParams, TEXT("SweepMinX="), MinX);
    const bool bHasMinY = FParse::Value(*InParams, TEXT("SweepMinY="), MinY);
    const bool bHasMaxX = FParse::Value(*InParams, TEXT("SweepMaxX="), MaxX);
    const bool bHasMaxY = FParse::Value(*InParams, TEXT("SweepMaxY="), MaxY);

    if (bHasMinX || bHasMinY || bHasMaxX || bHasMaxY)
    {
        if (!bHasMinX || !bHasMinY || !bHasMaxX || !bHasMaxY)
        {
            UE_LOG(LogWorldSweep, Error,
                TEXT("WorldSweepCommandlet: Partial sweep area provided. All four of SweepMinX, SweepMinY, SweepMaxX, SweepMaxY are required together."));
            return 1;
        }

        if (MaxX <= MinX || MaxY <= MinY)
        {
            UE_LOG(LogWorldSweep, Error,
                TEXT("WorldSweepCommandlet: Invalid sweep area — Max must be greater than Min on both axes."));
            return 1;
        }

        // Extend Z to cover the full legal world height so no actors are missed.
        static constexpr float HalfWorldMax = 1048576.0f;
        SweepArea = FBox(FVector(MinX, MinY, -HalfWorldMax), FVector(MaxX, MaxY, HalfWorldMax));

        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: Sweep area set to (%.0f, %.0f) — (%.0f, %.0f)."),
            MinX, MinY, MaxX, MaxY);
    }
    else
    {
        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: No sweep area provided — mode-dependent defaults will apply."));
    }

    // ----- Execute -----

    UWorldSweepRunner* Runner = NewObject<UWorldSweepRunner>();
    Runner->AddToRoot();
    const FWorldSweepResult Result = Runner->Execute(Batch, SweepArea, World);
    Runner->RemoveFromRoot();

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: Finished. Cells: %d | Actors: %d | Cancelled: %s"),
        Result.CellsProcessed,
        Result.ActorsProcessed,
        Result.bWasCancelled ? TEXT("Yes") : TEXT("No"));

    return Result.bWasCancelled ? 2 : 0;
}
