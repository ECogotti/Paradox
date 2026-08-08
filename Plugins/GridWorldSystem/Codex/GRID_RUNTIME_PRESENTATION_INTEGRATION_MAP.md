# Grid runtime presentation integration map

This file records the existing-plugin ownership map required before implementing runtime presentation milestone 1. It supplements `GRID_RUNTIME_PRESENTATION_AND_PATH_CONTROL.md` and is specific to the current UE 5.8 implementation.

## Existing owners

- `FGridCellId`, `FGridCellData`, and `FGridWorldSnapshot` own stable identity, immutable cell data, chunks, and revisions. Presentation addresses cells only through `FGridCellId` and retains a snapshot only while preparing renderer data.
- `AGridNavigationData` remains the authoritative navigation owner. It publishes immutable snapshots, owns invalidation/replanning, serializes topology, and constructs the separate `UGridNavigationRenderingComponent` debug renderer.
- `UGridWorldSubsystem` is the read/query facade and publishes `OnGridWorldChanged`. Runtime presentation depends on this facade and never owns or mutates navigation data.
- `FGridNavigationPath` owns ordered logical cells, world points, revisions, and movement metadata. Milestone 2 snapshots its public `CellPath` and `Revisions` but never retains or replaces it.
- `UGridMoveToCellTask` remains the movement-request owner and submits projected destinations through the native Move To lifecycle. Milestone 1 does not extend movement requests.
- `UGridWorldPathFollowingComponent` remains the only GridWorld path-following authority. Its optional presentation session and events observe this authority without changing movement state.
- `UGridNavigationRenderingComponent` and its scene proxy remain debug-only. Runtime presentation may reuse the read-only cell surface geometry calculation, but not the debug component, navigation show flag, or debug CVar lifecycle.
- `UGridWorldBlueprintLibrary` remains the stateless navigation-query facade. Presentation APIs belong to a dedicated world subsystem and pointer component.

## Milestone 1 decisions

- Runtime presentation stays in the existing `GridWorld` runtime module; its Engine and RenderCore dependencies already cover instancing and material rendering. No optional module is justified at the current plugin size.
- `UGridRuntimeVisualizationSubsystem` owns semantic interaction state and lazily owns one transient, non-replicated visualization Actor. It is created only for Game and PIE worlds that are not dedicated servers.
- One `UHierarchicalInstancedStaticMeshComponent` is created per existing 16 x 16 `FGridChunkCoord`. The representative project snapshot contains approximately 3,300 cells, so chunked HISM provides spatial culling while keeping structural rebuilds localized to at most 256 instances per component.
- Cell-to-instance mappings are internal and revisioned. Public callers use `FGridCellId`; raw renderer components and instance indices are never exposed.
- Presentation is disabled by default, has no Tick, uses no visual collision, is local-only, and is never serialized or replicated.
- Topology publication and clear must broadcast the existing `OnGridWorldChanged` contract. Topology changes rebuild renderer structure; traversal/occupancy-only changes update custom data only for `FGridChangeSet::ChangedCells`.
- Hover and selection contributions are stored independently. The resolved interaction state is `Selected`, then `Hovered`, then `Unselected`, so clearing hover cannot clear selection.
- The default material custom-data layout is centralized and fixed at ten floats: interaction, path, navigation flags, emphasis, resolved RGBA, path progress, and custom style value.
- `UGridCellPointerComponent` is input-independent and event-driven. It accepts a screen position, world ray, or existing hit result, performs no Tick, and changes presentation only when the resolved persistent cell changes.

## Verified UE 5.8 contracts

- `UInstancedStaticMeshComponent::AddInstances` accepts batched transforms and a world-space flag.
- `SetNumCustomDataFloats`, `SetCustomData`, and `SetCustomDataValue` update per-instance material data; callers may defer render-state invalidation and dirty each touched component once.
- HISM rendering validates `MATUSAGE_InstancedStaticMeshes`; without that usage Unreal substitutes its default material. `UGridCellVisualStyle::CellMaterial` is the authoritative slot override, so the material stored only on the source Static Mesh is intentionally ignored.
- `UHierarchicalInstancedStaticMeshComponent` inherits the instanced custom-data APIs and provides `BuildTreeIfOutdated` for an explicit final tree build.
- `UInstancedStaticMeshComponent::SetCullDistances` configures per-component fade/cull ranges.
- `UWorldSubsystem::DoesSupportWorldType`, `Initialize`, and `Deinitialize` provide the required symmetric world lifetime.
- `APlayerController::GetHitResultAtScreenPosition` and `DeprojectScreenPositionToWorld` are available for screen-pointer resolution; world-ray input uses the ordinary `UWorld` line-trace API.

## Milestone 1 implementation record

- Public presentation API: `Presentation/GridPresentationTypes.h`, `Presentation/GridCellVisualStyle.h`, and `Presentation/GridRuntimeVisualizationSubsystem.h`.
- Input-independent pointer API: `Interaction/GridCellPointerComponent.h`.
- Private render ownership: `GridRuntimeVisualizationActor` plus the subsystem's generation-checked `FGridCellId` mapping and one HISM per `FGridChunkCoord`.
- Default plugin content: `SM_GridRuntimeCell`, `M_GridRuntimeCell`, and `DA_GridRuntimeCellStyle_Default` under `/GridWorldSystem/Presentation`.
- Reproducible asset authoring: `Codex/Tools/create_grid_runtime_presentation_assets.py`; it is an editor-only development tool and is not a runtime dependency.
- Navigation publication contract: `AGridNavigationData::PublishSnapshot`, `RefreshRuntimeOverlay`, and `ClearGridWorld` all update `LastChangeSet` and broadcast `UGridWorldSubsystem::OnGridWorldChanged` on the Game Thread when the subsystem exists.
- Validation coverage: `GridWorldPresentationTests.cpp` registers `GridWorld.Presentation.MaterialDataAndLayering`, `LifecycleMappingAndRefresh`, and `PointerProjection`.

## Milestone 2 decisions

- `UGridPathPresentationSubsystem` owns multiple world-local sessions and resolves them into the path layer already stored by `UGridRuntimeVisualizationSubsystem`. It creates no renderer objects and is absent on dedicated servers.
- `FGridPathPresentationHandle` contains only an opaque GUID. Create/update validates every cell against the current topology; clear retains the session, while release makes every copied handle stale.
- Manual and weak owner-bound lifetimes are supported without Tick. Expired owners are pruned after garbage collection and before public mutations.
- The first renderer remains cell overlay only. Per-session colors, line/spline rendering, path prediction, commit, and injection are deferred to later milestones.
- Overlap is resolved by descending explicit priority, Active over Preview, semantic state rank, then ascending immutable creation sequence. Update/container iteration order cannot change the result.
- `PathProgress` is the winning occurrence's normalized ordered index. Duplicate cell IDs use state strength and then the latest occurrence within the winning session.
- Topology removal marks affected sessions invalid and drops contributions for cells that no longer exist. Generic preview sessions do not infer invalidity from traversal/cost/occupancy revisions.
- `UGridWorldPathFollowingComponent` presentation is opt-in per component and disabled by default. Recalculation updates the same handle; progress is sampled during existing following only while visualization or listeners require it, and broadcasts only on logical index changes.
- The follower exposes Blueprint events for accepted/replaced/recalculated snapshots, logical progress, invalidation/repath failure, and final path-following result/flags. All bindings and sessions are removed symmetrically.
- Validation coverage is extended by `GridPathPresentationTests.cpp` under `GridWorld.Presentation.Path.*`.

## Milestone 3 decisions

- `UGridPathLineVisualizationSubsystem` is a renderer backend separate from both `UGridPathPresentationSubsystem` session semantics and `UGridRuntimeVisualizationSubsystem` cell rendering. It is Game/PIE-only, disabled by default, no-Tick, local, non-replicated, and absent on dedicated servers.
- The first line implementation is a strict polyline made from HISM segment instances plus optional HISM point markers. It performs no smoothing and never feeds movement or pathfinding.
- `UGridPathLineVisualStyle` is a dedicated Data Asset. Its segment/marker meshes, materials, semantic colors, dimensions, floor offset, culling, and shadows are not stored in `UGridCellVisualStyle`.
- Per-session booleans select cell overlay and line independently. `SetPathPresentationRenderers` changes the combination without replacing the opaque session. Backend enable/disable/visibility lifecycles remain global and independent.
- `UGridWorldPathFollowingComponent` retains `bPresentActivePath` as the master opt-in and adds independent cell/line backend settings. Repath still updates the same session handle.
- Default plugin content is `SM_GridRuntimePathLineSegment`, `SM_GridRuntimePathLineMarker`, `M_GridRuntimePathLine`, and `DA_GridRuntimePathLineStyle_Default` under `/GridWorldSystem/Presentation`.
- Reproducible asset authoring is recorded in `Codex/Tools/create_grid_runtime_path_line_assets.py`; the script is editor-only and is not a runtime dependency.
- Line material custom data is fixed at six floats: path state, normalized progress, and resolved RGBA. Renderer HISM components always disable collision, overlaps, navigation influence, decals, and Tick.
- Validation coverage adds `GridWorld.Presentation.Path.LineRenderer`, including the four renderer combinations, custom segment mesh, global lifecycle, material layout, collision/nav isolation, session preservation, and navigation immutability.

## Milestone 4 decisions

- `UGridPathPreviewComponent` is the focused, input-independent prediction owner. It has no Tick; consumers supply a controller and persistent goal cell. `AParadoxPlayerController` is one mouse/touch consumer, not part of the generic API.
- Preview uses `FPathFindingQuery`, `AGridNavigationData::FindPath`, the controller's agent properties, and `UPathFollowingComponent::OnPathfindingQuery`. There is no preview-only search implementation.
- The semantic dedup key is NavData identity, projected start, goal, agent, fully initialized filter signature, topology/traversal, relevant occupancy/traffic revisions, partial presentation policy, and injected invalidation policy. Repeated cursor pixels inside one cell reuse the result.
- Stale policy is explicit: retain/mark, clear, or synchronously recalculate. Commit always rechecks the current start/signature and never exports a stale result.
- Partial results separately support show/allow, show/block, and hide/block. The project default is show/allow.
- Preview owns one weak owner-lifetime presentation session and independently selects cell overlay and strict line. Both are enabled by default in the Paradox consumer; renderer-disabled state preserves preview semantics.

## Milestone 5 decisions

- `FGridInjectedPath` is the Blueprint-safe exact handoff: ordered cell IDs, effective `OriginalGoalCell`, gameplay `RequestedGoalCell`, runtime NavData identity, path/preview GUIDs, agent/filter signature, revisions, traffic revision, partial state, invalidation policy, and an authority-stamped dynamic-conflict validation flag. Arbitrary world points are not accepted. The requested/effective split lets `StopBeforeOccupied` retain intent while executing and rendering the exact prefix ending immediately before a contested goal.
- `AGridNavigationData` is the only validation/materialization authority. It reuses `FGridAStar::CanTraverse` through `ValidatePath`, including ordinary adjacency, authored links, channels, occupancy/reservations, area costs, partial consistency, and start/goal checks.
- `bAllowDynamicAgentConflictsDuringValidation` is opt-in C++ replay support. It suppresses live-agent/traffic revision staleness during exact validation but never weakens topology, traversal, links, filters, start/goal, or live ordinary blocking-occupancy checks. Materialization copies both the flag and the original dynamic policy into `FGridNavigationPath`. `ReservedCorridor` then waits on intermediate agent conflicts without abandoning the exact sequence; a persistent final-cell reservation is reported from the predecessor for goal-contention resolution.
- Null filter input resolves to `UGridNavigationQueryFilter` before signing; the resolved class is retained. `OnPathfindingQuery` is applied before hashing and validation so requester occupancy/reservation context is identical between preview and commit.
- `UGridMoveToCellTask` remains the AI movement owner. Exact payloads are materialized as `FGridNavigationPath` and submitted through `AAIController::RequestMove`; native request ID, observer, pause/resume, completion, and teardown behavior are preserved.
- `FailOnInvalidation` aborts exact execution. `RecalculateToOriginalGoal` is the default and uses the existing native repath pipeline; a payload already stale when a queued action starts performs one normal query to its retained goal.
- `FGridNavigationPath` origin/correlation metadata is diagnostic only: Computed, Preview, Injected, Recalculated plus instance, parent, and source-preview IDs.
- `GameplayActionsGridWorld` extends its existing request schema with `PathSource` and `InjectedPath`. `PostLoad` migrates pre-Milestone-5 Definition assets while preserving authored destination settings.
- Exact is the only injection mode implemented. Waypoint, prefix, repair/rejoin, and mandatory-waypoint recovery are deferred to Milestone 6 rather than exposed as non-functional modes.
- Validation coverage adds `GridWorld.Presentation.Path.Prediction.ExactInjectionValidation`; build and complete `GridWorld.Presentation.*` / `GridWorld.*` suites remain mandatory.

## Paradox interaction-overlay integration record

- `UGridCellOverlayPresentationSubsystem` is the generic owner-scoped extension used by Paradox
  interaction affordances. It belongs to the GridWorld runtime module because it composes the
  existing cell presentation layers and HISM mapping; it contains no Paradox, Smart Object,
  Gameplay Action, selection, or puzzle dependency.
- The semantic states are deliberately generic `Primary` and `Secondary`. Paradox assigns Free and
  unavailable meaning before submitting deduplicated entries. GridWorld only validates stable cell
  identity, owns session lifetime/overlap resolution, and publishes the resolved layer.
- Overlay state is stored beside, not inside, direct hover/selection, path, or navigation state.
  Rebuilding one contribution cannot clear another. The default style's visible precedence does
  not discard the lower-priority semantic channels available to custom materials.
- Sessions require a weak owner and use create/update/clear/visible/priority/release operations.
  They create no renderer object; the only renderer remains
  `UGridRuntimeVisualizationSubsystem` and its chunked HISM Actor.
- Presentation validates cells against the immutable snapshot but never calls navigation mutation,
  occupancy, reservation, pathfinding, or claim APIs. Topology publication drops stale rendered
  contributions while leaving surviving session data inspectable for an owner-driven refresh.
- `UGridCellVisualStyle` now owns `PrimaryOverlayColor` and `SecondaryOverlayColor`. Paradox supplies
  `/Game/Data/GridWorld/DA_ParadoxGridCellStyle`, reusing GridWorld's existing block mesh and runtime
  material for both path and interaction-cell presentation.
- Validation coverage is `GridWorld.Presentation.CellOverlay.*`, including ownership, priority,
  path coexistence, style resolution, navigation immutability, and owner-GC cleanup.
