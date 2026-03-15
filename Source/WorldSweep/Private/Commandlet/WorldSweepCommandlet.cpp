// Copyright © ToaGames. All Rights Reserved.

#include "Commandlet/WorldSweepCommandlet.h"
#include "Core/WorldSweepBatch.h"
#include "Execution/WorldSweepRunner.h"
#include "WorldSweepLog.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "WorldPartition/WorldPartition.h"
#include "FileHelpers.h"
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

    FString MapPath;
    if (FParse::Value(*InParams, TEXT("Map="), MapPath))
    {
        UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: Loading map '%s'..."), *MapPath);
        FEditorFileUtils::LoadMap(MapPath, false, true);
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
    if (!World)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweepCommandlet: No world loaded. Use -Map=<PackagePath> to specify a map."));
        return 1;
    }

    if (World->GetName() == TEXT("Untitled"))
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweepCommandlet: World is 'Untitled' — no map was loaded. Use -Map=<PackagePath>."));
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
        // No sweep area specified — derive full world bounds automatically.
        if (UWorldPartition* WP = World->GetWorldPartition())
        {
            SweepArea = WP->GetRuntimeWorldBounds();

            // Expand to include persistent-level actors outside the WP grid.
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (*It && !(*It)->IsA<AWorldSettings>())
                {
                    SweepArea += (*It)->GetActorLocation();
                }
            }

            UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: No sweep area specified — derived from World Partition bounds + loaded actors."));
        }
        else
        {
            int32 ActorCount = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (*It && !(*It)->IsA<AWorldSettings>())
                {
                    SweepArea += (*It)->GetActorLocation();
                    ++ActorCount;
                }
            }

            if (SweepArea.IsValid)
            {
                SweepArea = SweepArea.ExpandBy(500.0f);
                UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: No sweep area specified — derived bounds from %d actors."),
                    ActorCount);
            }
            else
            {
                UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweepCommandlet: No sweep area specified and world contains no actors to derive bounds from. Streaming Levels mode will process all sub-levels; other modes may produce no results."));
            }
        }
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
