# WorldState runtime guide

## Participants and identity

`UWorldStateParticipantComponent` registers with the current world's `UWorldStateSubsystem` in `BeginPlay` and unregisters in `EndPlay`. Runtime instances own an `FGuid`-backed `FWorldStateParticipantId`; CDOs and templates intentionally have no ID. Normal duplication regenerates the ID, while PIE duplication preserves it. Duplicate live IDs are rejected by the registry.

The subsystem exists only in Game, PIE and GamePreview worlds and all operations must run on the Game Thread.

## Registration and capture

Call `FinalizeWorldStateRegistration()` after the expected participants have begun play. Validation checks duplicate selections, missing sources, expected source classes, canonical UE 5.8 property signatures and Scene Component transform conflicts.

`CaptureBaseline()` can publish exactly one valid baseline. A failed capture does not replace valid data, and a valid baseline cannot be overwritten. `CaptureRuntimeSnapshot()` publishes additional independent in-memory snapshots after the baseline exists.

Capture requests support complete, participant-ID, group and dirty-participant scopes. Baseline capture always behaves transactionally as `FailEntireSnapshot`.

For every selected property, the snapshot stores:

- participant and capture-source identity;
- root property name;
- expected source class and `FPropertyTypeName` signature;
- an independent `TArray<uint8>` payload;
- restore phase and soft-reference requirement.

Serialization uses `FStructuredArchiveFromArchive`, a memory reader/writer, and `FObjectAndNameAsStringProxyArchive` with synchronous loading disabled and `ArIsSaveGame == false`. Native structs, Blueprint User Defined Structs, fixed `ArrayDim` values, arrays, sets and maps use the same reflected pipeline.

## Supported and rejected properties

Supported values include reflected numeric and boolean values, enums, names, strings, text, native and Blueprint structs, supported containers, `TSoftObjectPtr`, `TSoftClassPtr`, `FSoftObjectPath` and `FSoftClassPath`.

Validation recursively rejects:

- raw UObject/Actor references and `TObjectPtr`;
- weak and lazy references;
- hard interface references;
- delegates and field paths;
- editor-only or deprecated properties;
- any supported-looking struct or container that contains a forbidden nested member.

The structured validation result includes the exact nested failure path.

## Restore API and lifecycle

Use:

- `RestoreBaseline(Request)` for the immutable baseline;
- `RestoreSnapshot(Request)` for a runtime snapshot ID;
- `RestoreParticipants(SnapshotId, ParticipantIds, Request)` for an explicit partial scope.

Accepted sessions keep one `FWorldStateRestoreSessionId` from `Started` through their single terminal event. Requests made while capture or restore is active return `RejectedBusy` and emit no lifecycle events.

The synchronous restore stages are:

1. accept and broadcast `Started`;
2. preflight source/property signatures, spawn strategy availability and dependency graph;
3. expand the requested scope and broadcast `ScopeResolved`;
4. restore participant existence and remove snapshot-absent managed participants for a complete restore;
5. restore Actor transforms and attachments;
6. restore selected Scene Component relative transforms parent-first;
7. deserialize selected properties;
8. resolve soft paths using `ResolveObject()` only;
9. broadcast participant reconstruction callbacks;
10. validate structural results and broadcast exactly one `Completed` or `Failed` event.

Participant callbacks are `OnWorldStatePreCapture`, `OnWorldStateCaptured`, `OnWorldStatePreRestore`, `OnWorldStatePropertiesRestored`, `OnWorldStateRestored` and `OnWorldStateRestoreFailed`. Global Blueprint and native delegates are available for `Started`, `ScopeResolved`, `Completed` and `Failed`.

If a failure occurs after world mutation begins, the subsystem enters `Failed`. A baseline or snapshot restore may be attempted from that state as recovery.

## Ordering and partial restore

`Early`, `Default` and `Late` phases create strong ordering edges. `RestoreAfter` and `RestoreBefore` add explicit participant edges. A deterministic Kahn topological sort uses participant ID as its tie-break. Cycles, missing dependency IDs and phase/dependency conflicts fail before mutation.

Partial restore defaults to `IncludeRequiredDependencies`. `ExactSelection` and `RejectIncompleteScope` reject a request that omits a prerequisite. `IncludeDependenciesAndDependents` expands in both directions.

## Existence and custom spawning

Existence policies are `ExistingOnly`, `RespawnIfMissing`, `DestroyIfAbsent`, `RespawnAndDestroy`, `Persistent` and `ExternallyManaged`.

The built-in `WorldState.DefaultActor` C++ strategy requires a loaded compatible class, a captured exact Actor name, an existing captured level, and a runtime-spawned Actor. It uses deferred construction. Before spawn, the subsystem reserves the participant identity; the new participant adopts it before registration.

Projects can register an `IWorldStateSpawnStrategy` through `RegisterSpawnStrategy`. Registration and removal are rejected while restore is active. Runtime code never loads an Actor class or soft-reference target synchronously.

The built-in strategy must preserve the captured object path. An external strategy may intentionally recreate a participant at a different path; the restore reports `RespawnPathChanged`, retains the participant ID, and then evaluates captured soft paths normally. Any optional path that no longer resolves is reported as `UnresolvedAllowed`, while a required path fails the reference phase.

Registry changes triggered by participant/global callbacks, spawn construction or Actor destruction are deferred and flushed outside active registry work. Restore iterates frozen participant IDs or weak-pointer copies, so `BeginPlay`/`EndPlay` cannot invalidate an active traversal.

## Dirty tracking and diagnostics

Call `MarkParticipantDirty()` when gameplay changes state relevant to a dirty-scope restore. Successful participant restore clears that marker.

`GetSnapshotSummary`, `GetParticipantStateSummaries` and `DumpWorldStateToLog` expose read-only diagnostics. The module owns the single `LogWorldState` category and its Info, Warning and Error macros.

Unreal Insights CPU scopes cover snapshot capture, property serialization/deserialization, preflight, existence, participant restore, reference resolution and validation. Visual debug labels are event-driven and appear only when both the `WorldState.Debug.Visual` console variable and the participant's `bEnableDebug` are enabled.

## Troubleshooting

- **Finalize fails:** inspect structured issues for duplicate selection, missing source, invalid ID, signature mismatch or a competing root transform selection.
- **Restore is rejected:** ensure registration is finalized, a valid baseline exists and no capture/restore callback is making a nested request.
- **A property is skipped or fails:** compare source name, expected class and type signature; no default value is written when deserialization is incompatible.
- **A soft reference is unresolved:** ensure the target already exists at the captured path. World State does not load it.
- **Respawn fails:** verify that the Actor was runtime-created, the class is loaded and the exact captured level/name are available, or register a custom strategy.
