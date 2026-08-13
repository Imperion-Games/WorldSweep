# WorldSweep

A batch scripting system for Unreal Engine that iterates over large World Partition maps cell by cell, running user-defined scripts against loaded actors and components. Designed for editor-only workflows: automated audits, bulk world modifications, and CI/CD pipelines.

---

## Table of Contents

- [Overview](#overview)
- [Installation](#installation)
- [Core Concepts](#core-concepts)
- [Batch Asset](#batch-asset)
- [Writing Scripts](#writing-scripts)
  - [Event Flags](#event-flags)
  - [C++ Script](#c-script)
  - [Blueprint Script](#blueprint-script)
- [Built-in Scripts](#built-in-scripts)
- [Running a Batch](#running-a-batch)
  - [Editor UI](#editor-ui)
  - [Commandlet (CI/CD)](#commandlet-cicd)
  - [C++ API](#c-api)
- [Execution Pipeline](#execution-pipeline)
- [Save & Source Control](#save--source-control)

---

## Overview

WorldSweep divides a sweep area into a grid of cells. For each cell it loads actors, fires events on your scripts, then unloads before moving on. Scripts receive only the events they opt into via `EventFlags`, so cells that don't need actors are never loaded unnecessarily.

Key features:

- **Three sweep modes**: World Partition, Streaming Levels, Flat Level (auto-detected by default)
- **Priority-based multi-pass execution**: scripts with different priorities run in separate passes over the world
- **Tag and class filtering**: narrow which actors reach your scripts
- **Auto-save & source control checkout**: dirtied packages saved and checked out per cell
- **Headless commandlet**: run sweeps from CI without opening the editor UI

---

## Installation

**Supported engine versions: UE 5.4 through 5.8.** Download the release zip matching
your engine. `main` tracks 5.8; older versions live on the `ue-5.7`, `ue-5.6`,
`ue-5.5`, and `ue-5.4` branches.

1. Copy the `WorldSweep` folder into your project's `Plugins/` directory.
2. Re-generate project files and build.
3. Enable the plugin in **Edit → Plugins → Editor → WorldSweep**.

### Editor-only, and what that means for cooking

WorldSweep ships a single **Editor**-type module. It is excluded from packaged
builds entirely, adds no runtime cost, and links nothing a shipping target needs.

One consequence is worth planning for: because `UWorldSweepScript` lives in an
editor-only module, a Blueprint subclass of it is a `/Game` asset whose parent
class does not exist in a cooked build. Keep your batch and script assets in a
directory excluded from cooking (for example `/Game/Editor/WorldSweep/`, added to
**Project Settings → Packaging → Directories to never cook**). Nothing that ships
should reference them.

---

## Core Concepts

| Concept | Description |
|---|---|
| `UWorldSweepScript` | Base class for all scripts. Subclass in C++ or Blueprint. |
| `UWorldSweepBatch` | Data asset that holds a list of scripts and execution settings. |
| `UWorldSweepRunner` | Executes a batch over a world. Called by the UI and commandlet. |
| `EWorldSweepEventFlags` | Bitmask that controls which events a script receives. |
| `FWorldSweepResult` | Returned by `Runner::Execute` with cells/actors processed and cancellation state. |

---

## Batch Asset

Create a batch via **Content Browser → right-click → Data Asset → WorldSweepBatch**.

| Property | Type | Default | Description |
|---|---|---|---|
| `Scripts` | `TArray<UWorldSweepScript*>` | n/a | Inline script instances to execute. |
| `CellSize` | `float` | `25600` | Grid cell size in cm. Should match your World Partition cell size. |
| `ActorClassFilter` | `TSubclassOf<AActor>` | `nullptr` | Optional global class filter. `nullptr` = all actors. |
| `SweepMode` | `EWorldSweepMode` | `Auto` | `Auto` / `WorldPartition` / `StreamingLevels` / `FlatLevel`. |
| `bSaveModifications` | `bool` | `false` | Automatically save packages dirtied by scripts after each cell. |

---

## Writing Scripts

### Event Flags

Set `EventFlags` on your script to subscribe to the events you need. Combine flags with `|`.

| Flag | Value | Events Received |
|---|---|---|
| `None` | `0x00` | No events (script is inert). |
| `CellEvents` | `0x01` | `OnPreCellLoad`, `OnCellStarted`, `OnCellCompleted` |
| `Actors` | `0x02` | `OnActorFound` |
| `Components` | `0x04` | `OnComponentFound` |

The runner aggregates flags across all scripts in a pass. If no script requests `Actors`, World Partition cells are not streamed in; they are skipped entirely.

### C++ Script

```cpp
// MyAuditScript.h
#pragma once
#include "Core/WorldSweepScript.h"
#include "MyAuditScript.generated.h"

UCLASS()
class MYGAME_API UMyAuditScript : public UWorldSweepScript
{
    GENERATED_BODY()

public:
    UMyAuditScript();

    virtual void OnBatchStarted_Implementation() override;
    virtual void OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds) override;
    virtual void OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled) override;

    UPROPERTY(EditAnywhere, Category = "MyAuditScript")
    bool bApplyChanges = false;
};
```

```cpp
// MyAuditScript.cpp
#include "MyAuditScript.h"

UMyAuditScript::UMyAuditScript()
{
    // Subscribe to actor events only; cells will be loaded
    EventFlags = static_cast<int32>(EWorldSweepEventFlags::Actors);
    Priority = 0;
}

void UMyAuditScript::OnBatchStarted_Implementation()
{
    UE_LOG(LogWorldSweep, Log, TEXT("[MyAudit] Starting (apply: %s)"), bApplyChanges ? TEXT("yes") : TEXT("no"));
}

void UMyAuditScript::OnActorFound_Implementation(AActor* InActor, const FBox& InCellBounds)
{
    if (!InActor) return;

    if (bApplyChanges)
    {
        // Modify actor and mark the package dirty so it gets saved
        InActor->SetActorLabel(TEXT("Fixed_") + InActor->GetActorLabel());
        InActor->MarkPackageDirty();
    }
    else
    {
        UE_LOG(LogWorldSweep, Log, TEXT("[MyAudit] Would rename: %s"), *InActor->GetName());
    }
}

void UMyAuditScript::OnBatchCompleted_Implementation(int32 InTotalCellsProcessed, bool bWasCancelled)
{
    UE_LOG(LogWorldSweep, Log, TEXT("[MyAudit] Done. %d cells, cancelled: %s"),
        InTotalCellsProcessed, bWasCancelled ? TEXT("yes") : TEXT("no"));
}
```

Then add an instance of `UMyAuditScript` to a `UWorldSweepBatch` asset.

### Blueprint Script

1. **Content Browser → right-click → Blueprint Class → parent: WorldSweepScript**
2. Open the Event Graph and override any of the following events:
   - `Event On Batch Started`
   - `Event On Pre Cell Load` *(requires `CellEvents` flag)*
   - `Event On Cell Started` *(requires `CellEvents` flag)*
   - `Event On Actor Found` *(requires `Actors` flag)*
   - `Event On Cell Completed` *(requires `CellEvents` flag)*
   - `Event On Component Found` *(requires `Components` flag)*
   - `Event On Batch Completed`
3. Set **EventFlags** and **Priority** in the Details panel.
4. Add an instance of your Blueprint script to a batch asset.

---

## Built-in Scripts

All built-in scripts default to `bApplyChanges = false` (dry-run / audit) unless noted.

### `WorldSweepActorLogger`
Logs every actor found in each cell. Useful as a reference implementation.

| Property | Type | Description |
|---|---|---|
| `bLogActorLocation` | `bool` | Include the actor's world-space location in the log. |

---

### `WorldSweepActorReplacer`
Replaces actors of one class with another, preserving transform.

| Property | Type | Description |
|---|---|---|
| `SourceClass` | `TSubclassOf<AActor>` | Class to find and replace. |
| `TargetClass` | `TSubclassOf<AActor>` | Class to spawn as replacement. |
| `bMatchExactClass` | `bool` | `true` = exact match; `false` = includes subclasses. |
| `bCopyTags` | `bool` | Copy actor tags from source to replacement. |
| `bCopyLabel` | `bool` | Copy actor display label. |
| `bApplyChanges` | `bool` | `false` = audit; `true` = destroy source and spawn target. |

---

### `WorldSweepCollisionProfileSetter`
Audits or sets a collision profile on every primitive component of swept actors.

| Property | Type | Description |
|---|---|---|
| `CollisionProfileName` | `FName` | Profile to check or apply (e.g., `BlockAll`). |
| `bApplyChanges` | `bool` | `false` = log non-conforming components; `true` = set profile and mark dirty. |

---

### `WorldSweepNamingConventionChecker`
Checks actor display labels against a regex pattern and logs violations.

| Property | Type | Description |
|---|---|---|
| `Pattern` | `FString` | Regex pattern (e.g., `^SM_` to require an `SM_` prefix). |
| `bLogMatchingActors` | `bool` | Also log actors that *do* match (default: only violations logged). |

---

### `WorldSweepNullMeshAuditor`
Finds actors that have mesh components with no assigned asset.

| Property | Type | Description |
|---|---|---|
| `bCheckStaticMeshes` | `bool` | Check `UStaticMeshComponent` for null mesh. |
| `bCheckSkeletalMeshes` | `bool` | Check `USkeletalMeshComponent` for null mesh. |

---

### `WorldSweepNaniteAuditor`
Audits or toggles Nanite on the static mesh assets referenced by swept actors. Each mesh asset is processed exactly once (deduplicated by path).

| Property | Type | Description |
|---|---|---|
| `bEnableNanite` | `bool` | Target Nanite state to audit against or apply. |
| `bApplyChanges` | `bool` | `false` = log meshes with wrong state; `true` = set state and mark dirty. |

---

### `WorldSweepCellLoadTimer`
Measures how long each cell takes to load. Reports min, max, and average on batch completion.

No configurable properties. Useful for profiling World Partition cell load times across large maps.

---

## Running a Batch

### Editor UI

1. Open **Tools → WorldSweep**.
2. Select a **Batch** asset in the picker.
3. *(World Partition only)* Set sweep bounds, or click **Use Entire World** to auto-fill.
4. Click **Run**. A cancellable progress dialog appears.
5. Review output in the **Message Log → WorldSweep** listing.

### Commandlet (CI/CD)

```bash
UnrealEditor.exe MyProject.uproject \
  -run=WorldSweep \
  -Map=/Game/Maps/MyLevel \
  -Batch=/Game/WorldSweep/MyBatch \
  -SweepMinX=0 -SweepMinY=0 -SweepMaxX=800000 -SweepMaxY=800000
```

| Argument | Required | Description |
|---|---|---|
| `-Map=<path>` | Yes | Soft object path to the map to open before running the sweep. |
| `-Batch=<path>` | Yes | Soft object path to the `UWorldSweepBatch` asset. |
| `-SweepMinX/Y`, `-SweepMaxX/Y` | No | Sweep area bounds. If omitted, derived from World Partition bounds or actor locations. |

**Exit codes:**

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | Invalid or missing arguments |
| `2` | Batch cancelled |

### C++ API

```cpp
#include "Core/WorldSweepBatch.h"
#include "Execution/WorldSweepRunner.h"

UWorldSweepBatch* Batch = LoadObject<UWorldSweepBatch>(nullptr, TEXT("/Game/WorldSweep/MyBatch"));

UWorldSweepRunner* Runner = NewObject<UWorldSweepRunner>();
Runner->AddToRoot();

FBox SweepArea(FVector(0, 0, 0), FVector(800000, 800000, 100000));
FWorldSweepResult Result = Runner->Execute(Batch, SweepArea, GetWorld());

UE_LOG(LogTemp, Log, TEXT("Cells: %d  Actors: %d  Cancelled: %s"),
    Result.CellsProcessed, Result.ActorsProcessed,
    Result.bWasCancelled ? TEXT("yes") : TEXT("no"));

Runner->RemoveFromRoot();
```

---

## Execution Pipeline

Scripts are grouped by **Priority** (lower runs first). Each priority level is a separate pass that fully iterates the world before the next pass begins, enabling multi-stage workflows (e.g., replace actors at priority 0, then audit the result at priority 1).

Within a pass the runner:

1. OR-combines event flags from all scripts in the pass.
2. Skips actor loading entirely if no script requests `Actors` or `Components`.
3. For each cell / streaming level / flat world:
   - `OnPreCellLoad` → load actors → `OnCellStarted`
   - For each actor: `OnActorFound` → `OnComponentFound` per component
   - `OnCellCompleted` → save dirty packages → GC
4. Calls `OnBatchCompleted` after all cells finish.

**Sweep modes:**

| Mode | Behavior |
|---|---|
| `WorldPartition` | Loads each grid cell via `FWorldPartitionReference`. Actors deduplicated by GUID across cells. |
| `StreamingLevels` | Iterates `ULevelStreaming` instances. Bounds derived from `ALevelBounds`. Original load state restored after each level. |
| `FlatLevel` | Single pass over the persistent level. No per-cell GC. |
| `Auto` | Selects `WorldPartition` if the world has a World Partition, `StreamingLevels` if it has streaming levels, otherwise `FlatLevel`. |

---

## Save & Source Control

When `bSaveModifications = true` on the batch:

- Packages dirtied by scripts are tracked via `UPackage::PackageDirtyStateChanged`.
- After each cell completes (before GC), dirty packages are saved with `FFileHelpers::SaveDirtyPackages`.
- If a source control provider is active, files are checked out automatically via `FSourceControlHelpers`.
- A final save pass runs after `OnBatchCompleted` to catch any remaining dirty packages.

Pre-existing dirty packages (dirty before the batch started) are excluded from tracking.
