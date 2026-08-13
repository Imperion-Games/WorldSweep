# Changelog

Supported engine versions: **UE 5.4 through 5.8**.

`main` tracks the newest engine (5.8). Each older version has its own branch
(`ue-5.7`, `ue-5.6`, `ue-5.5`, `ue-5.4`) and its own release artifact. Download
the zip matching your engine version.

---

## v1.0

First public release. Built and verified against UE 5.4.4, 5.5.4, 5.6.1, 5.7.4, and 5.8.1.
Download the zip matching your engine version.

### Core System
- `UWorldSweepBatch` data asset and `UWorldSweepScript` base class for defining and grouping batch operations
- Three execution modes: World Partition (cell-by-cell), Streaming Levels, and Flat Level (auto-detected or manually overridden)
- Priority-based multi-pass execution: scripts with lower priority values run first within a batch
- Event flag system: scripts opt into only the events they need (`OnActorFound`, `OnComponentFound`, `OnCellStarted`, etc.) so a pass that needs no actors never streams any
- Full undo/redo support via `FScopedTransaction` wrapping the entire batch execution
- Source control integration: modified packages are checked out or marked for add automatically
- "Use Entire World" bounds includes persistent-level actors in the sweep area calculation

### Editor UI
- `SWorldSweepWindow` with sweep bounds picker, drag-adjustable area, and cell grid visualization
- `SWorldSweepMapView` viewport with zoom, pan, and live cell overlay
- Progress dialog with cancellation support
- Toast notifications for misconfiguration errors

### Commandlet
- `WorldSweepCommandlet` for CI/CD and headless execution via `-run=WorldSweep -Map= -Batch=`
- Defaults to full world bounds when no sweep area is specified
- Exit codes: `0` success, `1` invalid arguments, `2` cancelled, `3` a script reported failure

### Built-in Scripts
- `ActorLogger`: reference implementation, logs found actors to the message log
- `ActorReplacer`: swaps actors of one class for another, preserving transforms
- `CollisionProfileSetter`: audits or applies collision profiles to primitive components
- `NamingConventionChecker`: validates actor names against a configurable regex pattern
- `NullMeshAuditor`: detects static and skeletal mesh components with null mesh references
- `NaniteAuditor`: audits or applies Nanite state across static mesh assets, deduplicated by asset path
- `CellLoadTimer`: profiles World Partition cell load times for performance analysis

### Architecture
- Sweep iteration extracted behind an `IWorldSweepStrategy` interface with one concrete strategy per mode. Adding a mode means adding one enum entry and one class, with no edits to the runner or to peer strategies.
- `FWorldSweepStrategyBase` template method owns the slow task, the priority-pass loop, event-flag folding, empty-pass skipping, and cancellation, so the three modes implement only what actually differs between them.
- `FScopedWorldSweepSaveTracker` is an `FGCObject`, keeping tracked packages alive across the `DoCollectGarbage()` calls that run between cells.
- Public headers declare their own module dependencies, so consuming modules can subclass `UWorldSweepScript` from their own code.
- World-space math uses double precision throughout, correct across the full legal world range rather than only near the origin.
