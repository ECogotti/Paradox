# World State System

World State System is a generic Unreal Engine runtime plugin for capturing an explicit, immutable in-memory snapshot of selected Actor and authored Actor Component state and restoring it deterministically. It has no dependency on Paradox gameplay modules, does not write snapshots to disk, and does not use Tick.

## Setup

1. Enable **World State System** in the project Plugins window or in the `.uproject` descriptor.
2. Add a `WorldStateParticipantComponent` to every Actor whose state is managed.
3. In the component Details panel, select root properties from the Owner Actor or an authored Actor Component.
4. Optionally enable Actor existence, Actor transform, attachment, or non-root Scene Component relative-transform capture.
5. At runtime, finalize registration and capture the baseline through `UWorldStateSubsystem`.

The plugin contains two modules:

- `WorldState`: runtime registry, validation, serialization, in-memory snapshots, restore ordering, spawn strategies and diagnostics.
- `WorldStateEditor`: participant Details customization and the capture-property picker. Runtime targets never depend on it.

## Typical workflow

```text
Participants BeginPlay and register
        -> FinalizeWorldStateRegistration
        -> CaptureBaseline (once)
        -> gameplay mutates live state
        -> CaptureRuntimeSnapshot (optional)
        -> RestoreBaseline / RestoreSnapshot / RestoreParticipants
```

Live state always remains owned by gameplay Actors and Components. The subsystem owns copied snapshot payloads, never live property or object pointers as persistent identity.

## Core limits

- Snapshots are session-only and are not assets or save games.
- Only explicitly selected root properties are captured; nested member paths are not a selection unit.
- Hard, weak, lazy and interface UObject references are rejected. Use soft object/class references.
- Soft references are never loaded synchronously. Optional unresolved paths warn; required unresolved paths fail restore.
- Runtime-created Component capture sources are unsupported unless the project supplies a stable reconstruction contract.
- The built-in spawn strategy recreates runtime Actors only, using their captured class, exact name and captured level. Level-authored Actor recreation needs a project-provided C++ spawn strategy.
- Network replication and snapshot comparison are outside this milestone.

See the [runtime guide](../Source/WorldState/Docs/README.md), [editor guide](../Source/WorldStateEditor/Docs/README.md), and [automation coverage](../Source/WorldState/Docs/TESTING.md) for API and validation details.
