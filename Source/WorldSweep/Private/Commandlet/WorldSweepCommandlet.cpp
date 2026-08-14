// Copyright © ToaGames. All Rights Reserved.

#include "Commandlet/WorldSweepCommandlet.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "WorldPartition/WorldPartition.h"

#include "Core/WorldSweepBatch.h"
#include "Execution/WorldSweepRunner.h"
#include "WorldSweepInternal.h"
#include "WorldSweepLog.h"

UWorldSweepCommandlet::UWorldSweepCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

static void PrintUsage()
{
    UE_LOG(LogWorldSweep, Display, TEXT("WorldSweep Commandlet: run a WorldSweep batch from the command line."));
    UE_LOG(LogWorldSweep, Display, TEXT(""));
    UE_LOG(LogWorldSweep, Display, TEXT("Usage:"));
    UE_LOG(LogWorldSweep, Display, TEXT("  UnrealEditor.exe <Project>.uproject -run=WorldSweep -Map=<PackagePath> -Batch=<AssetPath> [options]"));
    UE_LOG(LogWorldSweep, Display, TEXT(""));
    UE_LOG(LogWorldSweep, Display, TEXT("Required:"));
    UE_LOG(LogWorldSweep, Display, TEXT("  -Map=/Game/Path/To/Map      Map to load before the sweep runs."));
    UE_LOG(LogWorldSweep, Display, TEXT("  -Batch=/Game/Path/BatchAsset Soft object path to a UWorldSweepBatch data asset."));
    UE_LOG(LogWorldSweep, Display, TEXT(""));
    UE_LOG(LogWorldSweep, Display, TEXT("Optional:"));
    UE_LOG(LogWorldSweep, Display, TEXT("  -SweepMinX= -SweepMinY= -SweepMaxX= -SweepMaxY="));
    UE_LOG(LogWorldSweep, Display, TEXT("       2D sweep area in world units (cm). Required for World Partition mode;"));
    UE_LOG(LogWorldSweep, Display, TEXT("       optional spatial filter in Streaming Levels and Flat Level modes."));
    UE_LOG(LogWorldSweep, Display, TEXT("  -help, -?  Print this usage and exit."));
    UE_LOG(LogWorldSweep, Display, TEXT(""));
    UE_LOG(LogWorldSweep, Display, TEXT("Exit codes:"));
    UE_LOG(LogWorldSweep, Display, TEXT("  0  Batch completed successfully and no script reported failure."));
    UE_LOG(LogWorldSweep, Display, TEXT("  1  Invalid or missing arguments."));
    UE_LOG(LogWorldSweep, Display, TEXT("  2  Batch execution was cancelled."));
    UE_LOG(LogWorldSweep, Display, TEXT("  3  Batch completed but one or more scripts returned true from HasFailed()."));
}

static bool WantsHelp(const FString& InParams)
{
    return FParse::Param(*InParams, TEXT("help"))
        || FParse::Param(*InParams, TEXT("h"))
        || FParse::Param(*InParams, TEXT("?"));
}

static UWorld* ResolveWorld(const FString& InParams)
{
    FString MapPath;
    if (FParse::Value(*InParams, TEXT("Map="), MapPath))
    {
        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweepCommandlet: Loading map '%s'..."), *MapPath);
        FEditorFileUtils::LoadMap(MapPath, false, true);
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
    if (!World)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweepCommandlet: No world loaded. Use -Map=<PackagePath> to specify a map."));
        return nullptr;
    }

    if (World->GetName() == TEXT("Untitled"))
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweepCommandlet: World is 'Untitled'. No map was loaded. Use -Map=<PackagePath>."));
    }

    UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweepCommandlet: World '%s' loaded."), *World->GetName());
    return World;
}

static UWorldSweepBatch* ResolveBatch(const FString& InParams)
{
    FString BatchPath;
    if (!FParse::Value(*InParams, TEXT("Batch="), BatchPath))
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweepCommandlet: Missing required argument -Batch=<AssetPath>."));
        return nullptr;
    }

    UWorldSweepBatch* Batch = LoadObject<UWorldSweepBatch>(nullptr, *BatchPath);
    if (!Batch)
    {
        UE_LOG(LogWorldSweep, Error, TEXT("WorldSweepCommandlet: Failed to load batch asset '%s'."), *BatchPath);
        return nullptr;
    }

    UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweepCommandlet: Batch '%s' loaded (%d script(s))."),
        *Batch->GetName(), Batch->Scripts.Num());
    return Batch;
}

/** Parse the four explicit sweep-area arguments. Returns false on a partial or malformed set; OutHandled is false when none were supplied. */
static bool ParseExplicitSweepArea(const FString& InParams, FBox& OutSweepArea, bool& OutHandled)
{
    OutHandled = false;

    // World-space coordinates are parsed as double: World Partition maps legitimately reach the
    // full ±HALF_WORLD_MAX range, where float cannot represent a centimetre.
    double MinX = 0.0;
    double MinY = 0.0;
    double MaxX = 0.0;
    double MaxY = 0.0;

    const TPair<const TCHAR*, double*> Corners[] = {
        { TEXT("SweepMinX="), &MinX },
        { TEXT("SweepMinY="), &MinY },
        { TEXT("SweepMaxX="), &MaxX },
        { TEXT("SweepMaxY="), &MaxY },
    };

    int32 SuppliedCount = 0;
    for (const TPair<const TCHAR*, double*>& Corner : Corners)
    {
        if (FParse::Value(*InParams, Corner.Key, *Corner.Value))
        {
            ++SuppliedCount;
        }
    }

    if (SuppliedCount == 0)
    {
        return true;
    }

    OutHandled = true;

    if (SuppliedCount != UE_ARRAY_COUNT(Corners))
    {
        UE_LOG(LogWorldSweep, Error,
            TEXT("WorldSweepCommandlet: Partial sweep area provided (%d of 4). All four of SweepMinX, SweepMinY, SweepMaxX, SweepMaxY are required together."),
            SuppliedCount);
        return false;
    }

    if (MaxX <= MinX || MaxY <= MinY)
    {
        UE_LOG(LogWorldSweep, Error,
            TEXT("WorldSweepCommandlet: Invalid sweep area. Max must be greater than Min on both axes."));
        return false;
    }

    // Extend Z to cover the full legal world height so no actors are missed.
    OutSweepArea = FBox(FVector(MinX, MinY, -WorldSweepInternal::HalfWorldMax), FVector(MaxX, MaxY, WorldSweepInternal::HalfWorldMax));

    UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweepCommandlet: Sweep area set to (%.0f, %.0f) to (%.0f, %.0f)."),
        MinX, MinY, MaxX, MaxY);
    return true;
}

static bool ResolveSweepArea(const FString& InParams, UWorld* InWorld, FBox& OutSweepArea)
{
    OutSweepArea = FBox(EForceInit::ForceInit);

    bool bHandled = false;
    if (!ParseExplicitSweepArea(InParams, OutSweepArea, bHandled))
    {
        return false;
    }
    if (bHandled)
    {
        return true;
    }

    // No sweep area specified. Derive full world bounds automatically.
    int32 ActorCount = 0;
    const FBox ActorBounds = WorldSweepInternal::AccumulateActorBounds(InWorld, &ActorCount);

    if (const UWorldPartition* Partition = InWorld->GetWorldPartition())
    {
        // Expand the WP editor bounds to include persistent-level actors outside the grid.
        OutSweepArea = Partition->GetEditorWorldBounds();
        OutSweepArea += ActorBounds;

        // Pin Z to the full legal world height so sky/atmosphere actors at extreme altitudes are
        // never missed by the cell bounds check.
        OutSweepArea.Min.Z = -WorldSweepInternal::HalfWorldMax;
        OutSweepArea.Max.Z =  WorldSweepInternal::HalfWorldMax;

        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweepCommandlet: No sweep area specified. Derived from World Partition bounds plus %d loaded actor(s)."), ActorCount);
        return true;
    }

    if (ActorBounds.IsValid)
    {
        OutSweepArea = ActorBounds.ExpandBy(WorldSweepInternal::ActorBoundsPadding);
        UE_LOG(LogWorldSweep, Verbose, TEXT("WorldSweepCommandlet: No sweep area specified. Derived bounds from %d actor(s)."), ActorCount);
    }
    else
    {
        UE_LOG(LogWorldSweep, Warning, TEXT("WorldSweepCommandlet: No sweep area specified and world contains no actors to derive bounds from. Streaming Levels mode will process all sub-levels; other modes may produce no results."));
    }

    return true;
}

int32 UWorldSweepCommandlet::Main(const FString& InParams)
{
    if (WantsHelp(InParams))
    {
        PrintUsage();
        return 0;
    }

    UWorld* World = ResolveWorld(InParams);
    if (!World)
    {
        return 1;
    }

    UWorldSweepBatch* Batch = ResolveBatch(InParams);
    if (!Batch)
    {
        return 1;
    }

    FBox SweepArea(EForceInit::ForceInit);
    if (!ResolveSweepArea(InParams, World, SweepArea))
    {
        return 1;
    }

    UWorldSweepRunner* Runner = NewObject<UWorldSweepRunner>();
    Runner->AddToRoot();
    const FWorldSweepResult Result = Runner->Execute(Batch, SweepArea, World);
    Runner->RemoveFromRoot();

    UE_LOG(LogWorldSweep, Log, TEXT("WorldSweepCommandlet: Finished. Cells: %d | Actors: %d | Cancelled: %s | ScriptFailed: %s"),
        Result.CellsProcessed,
        Result.ActorsProcessed,
        Result.bWasCancelled    ? TEXT("Yes") : TEXT("No"),
        Result.bAnyScriptFailed ? TEXT("Yes") : TEXT("No"));

    if (Result.bWasCancelled)    return 2;
    if (Result.bAnyScriptFailed) return 3;
    return 0;
}
