# GRID RUNTIME PRESENTATION AND PATH CONTROL

## Purpose of this file

This file defines the architecture and implementation direction for runtime grid visualization, runtime path presentation, path prediction, and path injection in the existing `GridWorld` Unreal Engine plugin.

It supplements:

- the repository root `AGENTS.md`;
- the existing `GRID_WORLD_SYSTEM.md`;
- the implementation and documentation already present inside the plugin.

Do not repeat the general coding, compilation, folder, logging, profiling, lifecycle, documentation, Blueprint, or source-control rules already defined by `AGENTS.md`.

This document is incremental. It does not authorize replacing the current GridWorld architecture.

---

# 1. Existing implementation has priority

Before implementing any feature in this document, inspect the complete existing plugin and identify the types that already own:

- grid identity and cell data;
- grid navigation queries;
- grid path objects;
- path-following behavior;
- movement requests or movement tasks;
- path invalidation;
- runtime recalculation and replanning;
- navigation-data revisions;
- runtime rendering or debug visualization;
- Blueprint-facing APIs.

The plugin already contains a custom:

```text
UGridPathFollowingComponent
```

This component is part of the established architecture and must be reused.

Do not create a second path-following component, parallel movement pipeline, or competing source of path progress. Extend `UGridPathFollowingComponent` only where the new requirements cannot be expressed through its current public API, events, or internal extension points.

The same rule applies to every other existing type. Names proposed in this document are architectural working names, not instructions to duplicate an equivalent class that already exists.

When an equivalent type already exists:

1. use the existing type;
2. preserve its established responsibilities;
3. add the smallest coherent extension;
4. preserve existing Blueprint and serialized contracts unless a deliberate migration is required;
5. update existing documentation rather than creating contradictory documentation;
6. add a new class only when the responsibility is genuinely absent and cannot fit safely into an existing type.

If the current implementation differs from a working name in this document, prefer the real project type and document the mapping.

Do not silently replace a working implementation merely to make the code match this document literally.

---

# 2. Goals

The extension must provide four related but distinct capabilities:

1. **Runtime cell visualization**
   - display or hide navigable grid cells during gameplay;
   - support independent interaction states for unselected, hovered, and selected cells;
   - allow efficient visual changes through materials and per-instance data;
   - support large grids without one Actor or Component per cell.

2. **Runtime path presentation**
   - visualize a predicted path, an actively followed path, a recorded path, or another generic path;
   - support cell-based presentation, line-based presentation, or both;
   - update active-path presentation as the pawn advances or the path is recalculated.

3. **Path prediction**
   - calculate a path before movement begins;
   - support use cases such as dragging or moving the mouse over candidate destinations;
   - use the same authoritative pathfinding and query rules as real movement;
   - allow the predicted path to be committed as the path to execute.

4. **Path injection**
   - extend the existing movement request/task so a caller may provide a specific path instead of only a final destination;
   - validate injected paths through the existing GridWorld navigation authority;
   - execute injected paths through the existing `UGridPathFollowingComponent`;
   - preserve the plugin's existing invalidation and runtime recalculation behavior.

These features must remain generic. Do not add game-specific rules for the player, clones, paradoxes, rewind, doors, or puzzle actors.

---

# 3. Architectural boundaries

Keep the following responsibilities separate:

```text
AGridNavigationData and existing grid core
    authoritative cells, topology, costs, links, revisions, queries, path creation

Existing movement request/task layer
    starts, owns, aborts, and completes movement requests

UGridPathFollowingComponent
    follows the current path, tracks progress, receives invalidation/repath events,
    and communicates movement intent to the movement component

Runtime presentation layer
    displays cells and paths, but never decides navigation validity

Path prediction layer
    requests paths and manages preview lifetime, but never implements a second pathfinder

Input or cursor layer
    resolves user intent into a target cell, but does not own grid or path state
```

The runtime presentation layer is not authoritative.

It must never:

- make a blocked cell traversable;
- make a selected cell occupied;
- mutate path cost;
- become the source of path progress;
- directly move a pawn;
- recalculate a path independently from the existing navigation system;
- bypass `UGridPathFollowingComponent` for injected movement.

---

# 4. Module placement

First inspect the current plugin module layout.

Prefer adding runtime presentation code to the existing runtime module when:

- the plugin is still compact;
- the dependencies are already required;
- dedicated-server stripping can be handled cleanly;
- a separate module would create unnecessary public coupling.

Prefer a separate optional runtime module only when the existing architecture and dependency graph justify it.

Possible working module name:

```text
GridWorldPresentation
```

If created, its dependency direction must be:

```text
GridWorldPresentation -> GridWorld
GridWorldEditor       -> GridWorld
GridWorld             -X-> GridWorldPresentation
GridWorld             -X-> GridWorldEditor
```

The core GridWorld module must not require presentation assets or presentation objects in order to perform navigation.

Do not create a new module only because this document uses the term “presentation layer.” Architectural separation may be expressed through folders and interfaces inside the current runtime module.

---

# 5. Runtime cell visualization

## 5.1 Runtime presentation is not editor navigation debug rendering

The plugin may already have editor or debug rendering for navigation cells. Reuse shared read-only geometry-building utilities where safe, but keep runtime gameplay presentation separate from editor-only scene proxies and editor modules.

Runtime cell visualization may be used by gameplay for hover and selection. It therefore cannot be treated only as development debug drawing.

The system must support:

- global runtime enable/disable;
- per-grid or per-visualizer enable/disable;
- optional visibility by local player or presentation context where feasible;
- creation only when requested;
- complete removal or deactivation of rendering cost when disabled;
- no requirement for collision on the visual instances.

Do not assume that enabling Unreal's standard navigation debug flag is sufficient for gameplay presentation.

## 5.2 No Actor or Blueprint tile per logical cell by default

Do not create one Actor, Blueprint Actor, Scene Component, Static Mesh Component, Dynamic Material Instance, or UObject per grid cell in the default backend.

A Blueprint floor tile may be supported as an authored level asset, but it must not become the authoritative representation of a navigation cell and must not be required for runtime grid visualization.

If the project floor is already built from tile Blueprints, integration may be provided through an adapter or contributor interface. The navigation visualization must still work on arbitrary geometry without requiring those actors.

## 5.3 Recommended renderer

Use an instanced rendering backend as the default implementation.

Recommended conceptual structure:

```text
AGridRuntimeVisualizationActor or existing equivalent owner
    -> one or more UInstancedStaticMeshComponent / UHierarchicalInstancedStaticMeshComponent
    -> stable instance per currently visualized cell
```

Partition components by a meaningful batching key such as:

- grid;
- grid layer;
- spatial chunk;
- mesh and material combination;
- presentation style;
- visibility group.

Do not create a separate component for each visual state.

Choose ISM or HISM through measurement on representative grids. The architecture must not rely on behavior exclusive to only one of them unless documented.

A chunked HISM backend is the preferred first candidate when:

- cells are numerous;
- transforms are mostly static;
- spatial culling is valuable;
- updates primarily change per-instance custom data.

An ISM backend may be better for smaller grids or frequent instance updates. Keep the renderer backend replaceable behind a focused interface or component boundary.

## 5.4 Shared materials do not require shared visual state

Instances may share a mesh and material while still displaying different states.

Use **Per Instance Custom Data** as the default mechanism for individual-cell material inputs.

Do not create one Dynamic Material Instance per cell.

A shared Dynamic Material Instance may be used for global style parameters such as:

- global opacity;
- global animation time;
- global fade distance;
- grid-wide color grading;
- global visibility transition.

Per-instance custom data should encode cell-specific state.

Possible conceptual channels:

```text
CustomData[0] = interaction state
CustomData[1] = path presentation state
CustomData[2] = navigation presentation state
CustomData[3] = emphasis or intensity
CustomData[4] = normalized path progress or order
CustomData[5] = optional custom style value
```

The exact channel layout must be centralized in one documented contract shared by C++ and the material setup. Do not scatter hard-coded custom-data indices across classes.

Provide a type or constants namespace equivalent to:

```text
FGridCellMaterialDataLayout
```

Changing material state should normally update custom data in place without removing and re-adding the instance.

Batch multiple state changes and dirty the render state only as often as required by the actual engine API.

## 5.5 Visual state is layered, not one expanding enum

The required interaction states are:

```text
Unselected
Hovered
Selected
```

Represent them with a dedicated interaction-state enum, for example:

```text
EGridCellInteractionVisualState
```

Do not add every future cell meaning to this enum.

A cell may simultaneously be:

- hovered;
- selected;
- part of a predicted path;
- part of an active path;
- already traversed;
- a destination;
- occupied;
- reserved;
- expensive;
- blocked.

Use independent visual layers.

Recommended conceptual state:

```text
FGridCellVisualState
    InteractionState
    PathState
    NavigationState or navigation flags
    Emphasis
    Optional custom values
```

Recommended layers:

### Interaction layer

```text
Unselected
Hovered
Selected
```

### Path layer

```text
None
Preview
ActiveRemaining
ActiveCurrent
ActiveTraversed
Destination
Invalid
Custom
```

### Navigation layer

Read-only presentation derived from authoritative navigation state when enabled, such as:

```text
Traversable
Blocked
HighCost
Occupied
Reserved
```

Do not force a single mutually exclusive enum to represent every combination.

## 5.6 Visual-state resolution

Provide one focused resolver or style policy that converts layered state into renderer data.

Working concept:

```text
UGridCellVisualStyle
UGridCellVisualResolver
```

Use existing project patterns if equivalent types already exist.

The style should define or reference:

- cell mesh;
- material;
- transform offset and scale;
- culling distances;
- material custom-data layout;
- visual values for hover and selection;
- visual values for path states;
- optional transitions;
- visibility rules for blocked or absent cells;
- renderer backend preferences.

The resolver should combine layers without modifying their source states.

Example:

```text
Interaction = Hovered
Path        = Preview
Navigation  = Traversable
```

The material may render preview color with a stronger hover outline. Neither state overwrites the other in authoritative data.

Styles must be configurable without rewriting the renderer. Prefer a Data Asset or another project-consistent configuration asset when designers need to author presentation.

## 5.7 Stable cell-to-instance mapping

Never expose an HISM or ISM instance index as the persistent identity of a cell.

Maintain an internal mapping such as:

```text
FGridCellId -> FGridCellVisualHandle
```

A visual handle may contain:

- renderer component identifier;
- instance index;
- renderer generation/revision;
- optional chunk identifier.

Instance indices may change after rebuilds or removals. Validate or rebuild mappings whenever renderer structure changes.

External systems should address cells using the plugin's existing stable cell identity type.

## 5.8 Runtime visibility lifecycle

The visualizer should support at least:

```text
EnableVisualization
DisableVisualization
SetVisualizationVisible
RebuildVisualization
RefreshCells
SetCellInteractionState
ClearInteractionStates
```

Use the actual existing naming conventions and APIs in the plugin.

Disabling visualization must not delete or modify authoritative grid data.

Choose intentionally between:

- hiding existing instances for rapid toggling;
- releasing visual resources for long-term disablement;
- lazy creation on first enable.

Expose configuration for the chosen policy only when it provides real value.

## 5.9 Cursor and pointer interaction

Do not require collision on cell visualization meshes.

The preferred interaction pipeline is:

```text
screen pointer or cursor
    -> world trace against gameplay geometry or an interaction plane
    -> world location
    -> existing GridWorld world-to-cell or projection query
    -> FGridCellId
    -> interaction state update
```

This keeps visual mesh choice independent from picking.

Provide or extend a focused component/service for pointer-to-cell resolution only if the plugin does not already contain one.

Working concept:

```text
UGridCellPointerComponent
```

It should:

- resolve the hovered cell;
- emit a change only when the cell identity changes;
- expose pressed, released, selected, and cleared events when useful;
- allow the caller to decide whether hover is valid for blocked or unreachable cells;
- avoid pathfinding by itself;
- avoid storing authoritative selection state in `AGridNavigationData`.

Selection ownership belongs to the gameplay/UI consumer or a dedicated interaction presentation service, not to the navigation graph.

---

# 6. Runtime path presentation

## 6.1 Path data and path presentation are different objects

The existing grid path type remains the authoritative path data.

Do not copy a path into an unrelated array of world points as the primary presentation source when the real path object can be retained safely.

A presentation session should reference or snapshot the existing grid path through its safe public API.

Separate:

```text
Path
    cells, points, links, cost, revisions, validity

Path presentation session
    visibility, style, lifetime, progress, presentation mode

Path renderer
    cell overlay, line, spline, markers, or another visual backend
```

## 6.2 Presentation sessions and handles

Support multiple simultaneous path presentations.

Do not store one global “current shown path.”

Provide a session/handle model equivalent to:

```text
FGridPathPresentationRequest
FGridPathPresentationHandle
UGridPathPresentationSubsystem or existing equivalent
```

A request should be able to describe:

- the path;
- presentation purpose;
- style;
- renderer strategy;
- owning object or owner token;
- priority;
- lifetime policy;
- initial progress;
- visibility;
- whether path replacement should animate, replace immediately, or preserve history.

A handle should support safe operations equivalent to:

```text
UpdatePath
UpdateProgress
SetVisible
SetStyle
MarkInvalid
Clear
Release
```

Handles must fail safely after their session is released or their world is torn down.

## 6.3 Presentation purposes

Use a generic purpose enum or tag-like category, for example:

```text
Preview
Active
Recorded
Reference
Debug
Custom
```

Do not encode game-specific actor types into the presentation system.

Presentation purpose may select defaults, but a renderer or style must remain configurable.

## 6.4 Cell-overlay path renderer

The first required renderer should reuse the cell visualization system.

It sets the path visual layer for the cells belonging to a presentation session.

Supported progress modes should include:

```text
AllCells
RemainingOnly
TraversedAndRemaining
CurrentAndRemaining
DestinationOnly
EndpointsAndTurns
```

At minimum implement:

- preview path;
- active remaining path;
- active traversed path or clearing after traversal;
- current path cell;
- destination cell.

Do not convert path cells to the interaction state `Selected`. Path presentation uses the dedicated path layer.

When multiple sessions affect the same cell, resolve them deterministically through:

- explicit priority;
- purpose priority;
- layer composition;
- or another documented conflict policy.

Do not let update order accidentally determine the final visual result.

## 6.5 Line, marker, and spline renderers

Support an additional renderer strategy for paths that should not appear as cell highlights.

Possible backends include:

- custom polyline scene proxy;
- instanced line segments;
- instanced arrows or points;
- Niagara;
- `USplineComponent`;
- `USplineMeshComponent` segments.

A spline is a presentation strategy only.

It must never replace the authoritative logical path or become movement input.

If smoothing is enabled:

- preserve path endpoints;
- do not imply that smoothed geometry is traversable;
- avoid visually cutting through blocked cells or walls where practical;
- allow a strict polyline mode for accurate debugging and tactical display.

The first implementation should prefer a predictable cell overlay and/or linear polyline before advanced spline deformation.

## 6.6 Active path progress source

`UGridPathFollowingComponent` is the authoritative source for active path-following progress.

Do not infer progress by polling pawn world location from the presentation subsystem when the existing path-following component already knows:

- current path;
- current segment;
- current or entered logical cell;
- request state;
- path replacement;
- invalidation;
- completion or abort.

Inspect its existing delegates and extension points first.

Add the smallest necessary notifications when missing, for example conceptually:

```text
OnGridPathAccepted
OnGridPathReplaced
OnGridPathProgressChanged
OnGridPathInvalidated
OnGridPathRecalculated
OnGridPathFinished
```

Use existing delegate names and semantics where they already exist.

Avoid high-frequency callbacks that repeat unchanged progress every frame. Prefer notifications when:

- the current logical cell changes;
- the current segment changes;
- the path object is replaced;
- the path is invalidated;
- the request completes or aborts.

The path presentation session for active movement should update from these events.

## 6.7 Recalculated path presentation

When the existing navigation/recalculation system replaces or repairs an active path:

1. preserve the movement request identity when the current implementation does so;
2. obtain the new authoritative path from the existing movement/path-following pipeline;
3. update the same active presentation session where appropriate;
4. refresh cell layers or line geometry;
5. clear cells no longer belonging to the path;
6. preserve traversed-history presentation only when requested by the presentation mode;
7. communicate invalid or failed recalculation through the path presentation state.

The presentation system must not launch its own recalculation.

---

# 7. Path prediction

## 7.1 Prediction uses the existing navigation query pipeline

Path prediction is a request for a normal grid path before movement begins.

It must use the existing:

- `AGridNavigationData`;
- path query types;
- filter implementation;
- supported-agent properties;
- topology and dynamic overlays;
- occupancy policy;
- path result type;
- determinism rules.

Do not implement a preview-only A* algorithm.

Given identical start, goal, agent, filter, and grid revisions, preview and movement path generation should produce equivalent logical paths.

## 7.2 Prediction controller/component

Add or extend a focused object that manages preview requests and lifetime.

Working concept:

```text
UGridPathPreviewComponent
```

It may live on a Player Controller, Pawn, UI interaction actor, or another owning object.

Responsibilities:

- determine or receive the path start;
- receive a candidate destination cell;
- construct a normal path query;
- request the path through existing GridWorld APIs;
- reject obsolete results;
- expose the latest preview result;
- optionally create/update a path presentation session;
- optionally commit the valid preview to the existing movement task.

It must not:

- implement pathfinding;
- own a duplicate grid;
- modify navigation state;
- own active movement after commit;
- directly command `UCharacterMovementComponent`.

## 7.3 Input independence

The prediction API must not depend specifically on mouse input.

The mouse-drag use case is one consumer:

```text
pointer moves
    -> pointer component resolves target cell
    -> preview component receives target cell
    -> preview query runs
    -> presentation session updates
```

The same API must also support:

- gamepad target selection;
- keyboard grid selection;
- AI planning;
- tactical UI;
- Blueprint-provided cells;
- replay inspection;
- automated tests.

## 7.4 Semantic request deduplication

Do not request a new path for every cursor pixel or every frame.

A new preview is required only when a semantic input changes, such as:

- start cell;
- goal cell;
- supported agent;
- navigation filter;
- query context;
- topology revision;
- relevant traversal revision;
- relevant cost revision;
- occupancy policy;
- maximum cost or partial-path policy.

Create a stable query signature or compare these inputs explicitly.

Repeated requests with the same semantic inputs should reuse the existing result where safe.

## 7.5 Preview result

Use or extend the existing path result structures.

A preview result should expose at least:

```text
Status
Path
StartCell
GoalCell
TotalCost
TotalLength
bIsPartial
RelevantGridRevisions
QuerySignature
RequestGeneration
FailureReason
```

Do not duplicate information already provided safely by the existing path object.

`RequestGeneration` or an equivalent token must allow the component to ignore a result that completes after a newer preview request has already replaced it.

## 7.6 Synchronous and asynchronous operation

Start with the plugin's existing query mode.

Do not add asynchronous preview pathfinding unless profiling or current plugin architecture justifies it.

If asynchronous prediction already exists or is introduced:

- use immutable or otherwise thread-safe grid snapshots;
- do not access gameplay UObjects inside the search algorithm;
- support logical cancellation or stale-result rejection;
- prevent unbounded queued requests while dragging;
- return completion to the appropriate game-thread owner;
- validate world and owner lifetime before publishing the result.

## 7.7 Preview validity and refresh

A preview may become stale because:

- topology changes;
- a traversal modifier changes;
- a relevant cost changes;
- the start cell changes;
- the destination becomes invalid;
- the agent or filter changes.

The preview component should subscribe to existing revision or invalidation notifications where practical.

Configurable behavior may include:

```text
KeepButMarkStale
ClearImmediately
RecalculateAutomatically
```

Do not silently present a stale preview as guaranteed executable.

## 7.8 Commit preview to movement

The preview system must allow the exact valid preview path to be submitted to the existing movement task through the path-injection API.

This avoids showing one route and then recalculating a different route immediately after confirmation.

Before commit, validate:

- navigation-data identity;
- agent compatibility;
- filter/query compatibility;
- current start compatibility;
- path revisions;
- path validity;
- destination validity.

If the preview is stale, route the decision through the configured injection/recalculation policy rather than silently executing outdated data.

After a successful commit:

- the movement task owns the movement request;
- `UGridPathFollowingComponent` owns following progress;
- the preview presentation may be promoted or replaced by an active presentation;
- the preview component must not continue to mutate the active path.

---

# 8. Path injection

## 8.1 Extend the existing movement task

Find the movement task/request already implemented by the plugin.

Extend that existing API so callers may choose between:

```text
Destination request
Injected path request
Committed preview request
```

Do not introduce a second movement task if the current task can safely represent these source modes.

Use a discriminated request structure or another existing project pattern that makes invalid combinations difficult to represent.

Working concept:

```text
FGridMoveRequest
    SourceType
    Destination or InjectedPath or PreviewHandle
    Agent and filter settings
    acceptance settings
    existing recalculation policy
    injection-specific policy where required
```

Use real existing type names when available.

Do not represent an injected path only as `TArray<FVector>`.

The request must retain grid semantics.

## 8.2 Accepted injected data

Prefer accepting the existing grid path type directly when ownership and lifetime are safe.

Also provide a serializable or Blueprint-friendly path description only when needed.

Working conceptual description:

```text
FGridInjectedPath
    GridId
    OrderedCells
    Optional connection metadata
    Optional world points
    OriginalGoal
    Agent signature
    Query/filter signature
    Topology revision
    Relevant dynamic revisions
    Injection mode
    Mandatory constraints
```

Ordered cells are the primary data. World points are derived or validated presentation/movement data, not the stable identity of the path.

Do not allow arbitrary points to masquerade as a valid grid path without cell and connection validation.

## 8.3 Injection modes

Design the API for the following generic modes:

### Exact

The supplied cell sequence is the path to follow.

Every consecutive transition must be valid according to the current GridWorld rules.

### Waypoints

The supplied cells are mandatory waypoints. The navigation system calculates valid segments between them.

### Prefix

The supplied path prefix must be followed first, after which the navigation system calculates a path toward an original final goal.

The first implementation may support only `Exact` if that is the smallest complete milestone, but the API must not make future waypoint or prefix support impossible.

Do not implement placeholder modes that report success without correct behavior.

## 8.4 Validation authority

Injected path validation belongs to the existing navigation authority, not to the presentation component or movement task.

Add or extend a focused validation API on the existing Navigation Data/query layer.

Validation must check as applicable:

1. grid identity;
2. path ownership and lifetime;
3. supported-agent compatibility;
4. query-filter compatibility;
5. current-start compatibility;
6. existence of all cells;
7. traversability of all required cells;
8. validity of every consecutive connection;
9. special-link permissions;
10. topology revision;
11. relevant traversal and cost revisions;
12. partial-path rules;
13. original-goal consistency;
14. required waypoint or prefix constraints.

Use a structured result equivalent to:

```text
FGridInjectedPathValidationResult
    bIsValid
    FailureReason
    InvalidCell
    InvalidSegment
    CurrentRevisions
    SuggestedRecovery
```

Possible generic failure reasons:

```text
InvalidGrid
InvalidPathObject
AgentMismatch
FilterMismatch
InvalidStart
MissingCell
BlockedCell
DisconnectedCells
ForbiddenLink
StaleTopology
StaleTraversalState
StaleCostState
InvalidGoal
InvalidConstraint
```

Use existing result enums and error contracts where they already exist.

## 8.5 Build an engine-compatible path

A validated injected path must become or remain the plugin's normal grid navigation path type, compatible with the existing engine path-following pipeline.

It must contain:

- ordered logical cells;
- valid world-space path points;
- connection metadata;
- current relevant revisions;
- final goal information;
- partial-path state where allowed;
- path-origin metadata;
- recalculation information required by the existing system.

Do not bypass the normal path object by manually feeding directions to the movement component.

## 8.6 Execute through `UGridPathFollowingComponent`

All injected movement must execute through the existing:

```text
UGridPathFollowingComponent
```

The intended flow is:

```text
existing movement task/request
    -> validate or materialize injected grid path
    -> submit the path through the existing movement-request pipeline
    -> UGridPathFollowingComponent accepts and follows the path
    -> existing CharacterMovement/NavMovement integration performs movement
```

Preserve existing:

- movement request IDs;
- abort behavior;
- pause/resume behavior if supported;
- acceptance rules;
- completion delegates;
- blocked-movement detection;
- cell-progress events;
- path invalidation;
- runtime recalculation;
- cleanup and ownership.

If `UGridPathFollowingComponent` already has a method for accepting an externally produced path, use it. If it does not, add the narrowest validated entry point rather than bypassing the component.

## 8.7 Path origin metadata

Extend the existing grid path metadata only if equivalent information is not already available.

Useful generic origin values:

```text
Computed
Preview
Injected
Repaired
Recalculated
```

Useful correlation data:

```text
PathInstanceId
ParentPathInstanceId
SourcePreviewId
OriginalMoveRequestId
```

This metadata supports debugging, presentation, replay integration, and tracing why a path changed.

Do not let metadata become a second source of path validity.

---

# 9. Existing recalculation and invalidation rules

## 9.1 Preserve the current system

The plugin already implements runtime path recalculation rules.

Inspect and use them.

Do not create a separate injected-path invalidation service or preview-only replanner.

Injected and preview-derived active paths must participate in the same existing mechanisms used by ordinary destination-generated paths:

- revision tracking;
- affected-path invalidation;
- observed-path updates;
- path replacement;
- movement-task policy;
- `UGridPathFollowingComponent` response;
- completion or failure reporting.

## 9.2 Injection policy extends, not replaces, current recalculation policy

An injected path may carry additional semantic constraints, but its behavior must be expressed as an extension of the plugin's existing recalculation policy.

Generic recovery behaviors may include:

### FailOnInvalidation

Abort or fail when the injected constraint can no longer be satisfied.

Use for strict exact paths when deviation is not allowed.

### RecalculateToOriginalGoal

Discard the invalid remainder and calculate a normal path to the original final goal using the existing query settings.

### RepairAndRejoin

Calculate a detour from the current position to a valid future cell or segment of the injected path, then continue along the valid remainder.

### RecalculateThroughMandatoryWaypoints

Rebuild affected segments while preserving mandatory waypoint constraints.

Map these concepts onto the current plugin's real policy types. Do not create duplicate enums if the existing policy can be extended coherently.

## 9.3 Exact path semantics

Define exactness explicitly.

An `Exact` injected path means the logical ordered cells are mandatory unless the selected recovery policy explicitly permits deviation after invalidation.

Visual smoothing between logical cell points does not change exact path semantics.

The path-following component may smooth physical movement while still reporting the correct logical cell progression, provided it does not skip mandatory logical transitions.

## 9.4 Repair and rejoin

If implemented, repair must be performed by the existing GridWorld pathfinding/query layer.

Do not implement local steering as “repair.”

A repair result must identify:

- current path progress;
- invalid section;
- selected rejoin cell or segment;
- generated detour;
- retained remainder;
- new revisions;
- relationship to the parent path.

Select rejoin candidates deterministically.

Do not always replan every remaining suffix if a smaller valid repair is possible, but implement correctness before optimization.

## 9.5 Presentation response to recalculation

The path presentation system listens to the result of the existing recalculation process.

It must not predict that recalculation succeeded before a new authoritative path is published.

On successful path replacement:

- update the active presentation session;
- preserve correlation metadata;
- clear obsolete cell states;
- apply the chosen traversed-history policy.

On failure:

- mark the presentation invalid or clear it according to style/policy;
- allow the movement task's normal failure result to remain authoritative.

---

# 10. Suggested runtime types

The following are conceptual responsibilities. Reuse or extend existing types before creating any of them.

## Presentation ownership

```text
UGridRuntimeVisualizationSubsystem
AGridRuntimeVisualizationActor
UGridCellInstanceRendererComponent
```

## Style and state

```text
UGridCellVisualStyle
UGridPathVisualStyle
FGridCellVisualState
EGridCellInteractionVisualState
EGridCellPathVisualState
FGridCellVisualHandle
FGridCellMaterialDataLayout
```

## Pointer interaction

```text
UGridCellPointerComponent
FGridCellPointerQuery
FGridCellPointerResult
```

## Path presentation

```text
UGridPathPresentationSubsystem
FGridPathPresentationRequest
FGridPathPresentationHandle
EGridPathPresentationPurpose
EGridPathProgressPresentationMode
IGridPathRenderer or strategy equivalent
```

## Path prediction

```text
UGridPathPreviewComponent
FGridPathPreviewRequest
FGridPathPreviewResult
FGridPathPreviewHandle
```

## Injection

```text
FGridInjectedPath
EGridPathInjectionMode
FGridInjectedPathValidationResult
```

Do not create all of these as public reflected types automatically. Keep implementation-only types private and expose only the API required by actual consumers.

---

# 11. Suggested code placement

Adapt this structure to the existing plugin rather than reorganizing working code unnecessarily.

```text
Source/GridWorld/
    Public/
        Presentation/
            GridRuntimeVisualizationSubsystem.h
            GridCellVisualStyle.h
            GridPathVisualStyle.h
            GridPresentationTypes.h

        Interaction/
            GridCellPointerComponent.h

        Navigation/
            existing GridNavigationPath.h
            existing GridPathFollowingComponent.h
            existing movement task/request headers
            GridInjectedPathTypes.h
            GridPathPreviewTypes.h

        Components/
            GridPathPreviewComponent.h

    Private/
        Presentation/
            GridRuntimeVisualizationSubsystem.cpp
            GridRuntimeVisualizationActor.h
            GridRuntimeVisualizationActor.cpp
            GridCellInstanceRendererComponent.h
            GridCellInstanceRendererComponent.cpp
            GridPathPresentationSubsystem.cpp
            GridCellOverlayPathRenderer.cpp
            GridLinePathRenderer.cpp

        Interaction/
            GridCellPointerComponent.cpp

        Navigation/
            extensions to existing GridPathFollowingComponent.cpp
            extensions to existing movement task/request implementation
            GridInjectedPathValidation.cpp

        Components/
            GridPathPreviewComponent.cpp

        Tests/
            GridRuntimeVisualizationTests.cpp
            GridPathPresentationTests.cpp
            GridPathPreviewTests.cpp
            GridPathInjectionTests.cpp
```

Create only the files required by the active milestone.

If the current plugin already organizes these responsibilities elsewhere, preserve that structure unless a focused move is required.

---

# 12. Public API principles specific to these features

## 12.1 Address cells by stable identity

Public presentation, hover, preview, and injection APIs must use the plugin's existing stable cell identity type.

Do not expose renderer instance indices as cell IDs.

## 12.2 Return handles for owned presentation sessions

Do not require consumers to retain internal renderer pointers.

Use validated handles or existing subsystem-owned identifiers for:

- cell selection presentations where useful;
- path presentation sessions;
- preview sessions.

## 12.3 Keep path data read-only to consumers

Do not expose mutable internal cell arrays or path points.

Consumers may request a new path, inject a validated path description, or read path data through safe APIs.

## 12.4 Distinguish request failure from no-path result

Preview and injection results must distinguish:

- invalid request;
- no valid path;
- partial path;
- stale result;
- cancelled or superseded request;
- successful path;
- successful path later invalidated.

## 12.5 Blueprint exposure

Expose Blueprint APIs only for workflows designers actually need, such as:

- enable or disable runtime cell visualization;
- set hovered or selected cell;
- clear cell interaction presentation;
- create, update, or clear a path presentation;
- request a path preview;
- commit a preview;
- start movement with an injected path;
- inspect structured failure results.

Do not expose raw ISM/HISM components, material custom-data indices, mutable path internals, or generator storage.

---

# 13. Performance requirements

## 13.1 Cell visualization

The disabled state must have negligible per-frame cost.

Avoid:

- per-cell Tick;
- per-cell Dynamic Material Instances;
- rebuilding all instances for a hover change;
- scanning every cell to clear one previous hover;
- full-grid material-data uploads for a small state change;
- collision on every visual cell;
- one draw component per state.

Track changed cells and update only affected instances.

## 13.2 Path presentation

Path updates should be proportional to changed path cells or segments where practical.

When replacing a path, compute the difference between old and new cell sets if that is cheaper than clearing and rebuilding the full presentation for representative path lengths.

Do not over-engineer incremental diffing before measuring actual path sizes.

## 13.3 Pointer interaction

The pointer component may perform a world trace per relevant input update, but it must not calculate a path unless the resolved target cell or another semantic query input changes.

## 13.4 Preview queries

Throttle through semantic deduplication, not arbitrary visual delay alone.

If a configurable debounce is added, it must complement cell-change deduplication rather than replace it.

## 13.5 Profiling scopes

Use the profiling rules in `AGENTS.md` for meaningful operations such as:

- visualization build;
- chunk instance rebuild;
- batch custom-data update;
- preview query;
- injection validation;
- path repair and rejoin;
- line or spline geometry rebuild.

Do not add low-value scopes to trivial state setters.

---

# 14. Networking and dedicated-server behavior

Do not replicate presentation state by default.

Runtime cell and path visualization are normally local presentation concerns.

The authoritative movement/path request may already be replicated or server-controlled through the existing plugin/game architecture. Do not change that architecture casually.

Where multiplayer or server-authoritative movement is relevant:

- injected path acceptance must occur on the authoritative side according to existing rules;
- clients must not make an unvalidated path authoritative;
- path presentation may use predicted local data but must distinguish it from accepted active movement;
- dedicated servers must not create visualization actors, meshes, materials, or presentation subsystems that perform rendering work.

Do not add network support beyond current project requirements, but avoid designs that make server stripping impossible.

---

# 15. Serialization and replay considerations

Do not serialize renderer instance indices, material state, or presentation handles as persistent gameplay data.

If an injected path or predicted path must be recorded, serialize stable semantic data such as:

- grid identity;
- ordered cell identities;
- original goal;
- injection mode;
- mandatory constraints;
- relevant query settings;
- path origin metadata;
- revision information only when meaningful for validation.

Presentation should be reconstructed from semantic path data.

Do not add replay-specific game rules in this extension. Provide generic data and events that a replay system may consume later.

---

# 16. Automated validation

Add tests using the existing plugin test patterns.

## 16.1 Runtime cell visualization tests

Verify at least:

1. enabling visualization creates or activates the expected renderer;
2. disabling visualization removes rendering activity without modifying navigation data;
3. a cell maps to a stable visual handle while renderer structure is unchanged;
4. hover affects only the intended cell interaction layer;
5. selection and path state can coexist on the same cell;
6. clearing hover does not clear selection or path state;
7. material custom-data layout is applied consistently;
8. renderer rebuild invalidates and reconstructs instance mappings safely.

## 16.2 Path presentation tests

Verify at least:

1. a preview path applies preview state to its cells;
2. an active path uses a distinct path state;
3. progress updates current, remaining, and traversed cells correctly;
4. two presentation sessions resolve overlap deterministically;
5. replacing a path clears obsolete cell states;
6. releasing a session removes only that session's contribution;
7. a recalculated active path updates the same presentation correctly.

## 16.3 Path prediction tests

Verify at least:

1. identical semantic requests are deduplicated;
2. changing only the cursor world position inside the same cell does not recalculate;
3. changing the goal cell requests a new path;
4. preview uses the same logical path as a normal equivalent query;
5. stale results are rejected after a newer request;
6. relevant revision changes invalidate or refresh the preview according to policy;
7. a valid preview can be committed to movement.

## 16.4 Path injection tests

Verify at least:

1. a valid exact path is accepted;
2. an invalid cell is rejected with a structured reason;
3. a disconnected cell pair is rejected;
4. agent or filter mismatch is rejected;
5. a stale path follows the selected recovery policy;
6. injected movement executes through `UGridPathFollowingComponent`;
7. normal abort and completion behavior remains unchanged;
8. invalidation reaches the existing recalculation system;
9. strict exact injection fails when deviation is forbidden;
10. a repair/rejoin policy produces a valid replacement path when implemented;
11. path presentation follows the authoritative replacement path;
12. destination-only movement requests continue to behave as before.

---

# 17. Implementation milestones

Do not implement every feature in one uncontrolled change.

## Milestone 0 — Existing-plugin integration map

Before adding code:

- identify all relevant existing types;
- document which type owns each responsibility;
- confirm how `UGridPathFollowingComponent` receives paths and reports progress;
- confirm how the existing movement task starts destination-based movement;
- confirm how runtime invalidation and recalculation work;
- confirm existing path and revision types;
- identify current rendering/debug utilities that can be reused safely;
- record proposed extensions without renaming or replacing working types.

No production architecture should be added before this map is complete.

## Milestone 1 — Runtime cell visualization

Implement:

- enable/disable lifecycle;
- instanced renderer;
- stable cell-to-instance mapping;
- material custom-data layout;
- unselected, hovered, and selected interaction states;
- layered state model;
- configurable visual style;
- pointer-to-cell integration without visual collision.

Do not implement path presentation by abusing the selected state.

## Milestone 2 — Cell-based path presentation

Implement:

- path presentation sessions and handles;
- preview and active path purposes;
- path-layer contribution to cell visuals;
- current, remaining, traversed, and destination states;
- deterministic overlap resolution;
- connection to existing `UGridPathFollowingComponent` progress events.

## Milestone 3 — Optional line renderer

Implement a basic strict polyline or instanced-segment renderer.

Add spline or spline-mesh presentation only after the basic renderer is correct and the use case requires it.

## Milestone 4 — Path prediction

Implement:

- input-independent preview component/service;
- semantic query deduplication;
- preview result and stale-result handling;
- automatic presentation update;
- revision response;
- safe preview lifetime.

## Milestone 5 — Exact path injection

Extend the existing movement task and `UGridPathFollowingComponent` integration to support:

- exact injected cell paths;
- authoritative validation;
- normal movement request ownership;
- normal abort/completion;
- existing invalidation and recalculation pipeline;
- preview-to-active commit.

## Milestone 6 — Advanced injection recovery

Only when required, add:

- waypoint injection;
- prefix injection;
- repair and rejoin;
- mandatory waypoint recalculation;
- richer presentation of repaired paths.

---

# 18. Required implementation decisions to document

During implementation, document the real project decisions for:

- ISM versus HISM and the measured workload used to choose;
- chunk size or batching key;
- cell material custom-data channel layout;
- overlap priority among multiple presentation sessions;
- ownership and spawning of runtime visualization actors/components;
- path-presentation lifetime and handle ownership;
- exact events exposed by `UGridPathFollowingComponent` for progress and replacement;
- preview query signature;
- commit validation behavior;
- injected path ownership and lifetime;
- exact mapping to the plugin's current recalculation policy;
- behavior when the pawn is no longer on the injected path start;
- behavior when only costs, not traversability, change;
- behavior for partial paths;
- dedicated-server stripping.

Do not leave these as accidental consequences of implementation details.

---

# 19. Non-goals

This extension must not:

- replace `AGridNavigationData`;
- replace the existing `UGridPathFollowingComponent`;
- create a second grid pathfinder;
- create a second movement-task pipeline;
- require a tile Blueprint per cell;
- create one renderer object per cell;
- use material instance creation per cell;
- treat renderer instance indices as stable cell identity;
- use visual selection as navigation state;
- use path visualization as movement authority;
- let spline smoothing alter the logical path;
- let preview queries bypass normal filters or revisions;
- let injected paths bypass validation;
- let injection bypass existing invalidation or runtime recalculation;
- embed player-specific mouse logic into core navigation APIs;
- add clone, replay, paradox, puzzle, or project-specific rules;
- replicate local presentation state by default;
- move runtime presentation code into the editor module.

---

# 20. Definition of architectural completion

This extension is architecturally complete when:

- the existing plugin implementation has been mapped and reused;
- `UGridPathFollowingComponent` remains the single grid path-following authority;
- runtime cells can be enabled and disabled without changing navigation data;
- unselected, hovered, and selected states work through an optimized instanced renderer;
- cell state supports material-driven effects without one material instance per cell;
- interaction, path, and navigation presentation layers can coexist;
- a path can be shown through a generic presentation session;
- active path progress comes from `UGridPathFollowingComponent`;
- a path can be predicted through the normal GridWorld query pipeline;
- a valid preview can be committed without silently choosing a different route;
- the existing movement task accepts a validated injected path;
- injected movement executes through the existing path-following and movement pipeline;
- injected paths obey the plugin's current invalidation and recalculation rules;
- destination-based movement continues to work unchanged;
- editor navigation visualization remains separate from gameplay runtime presentation;
- the system remains generic and free of game-specific rules.

The core invariant is:

> GridWorld calculates and validates paths, the existing movement task owns movement requests, `UGridPathFollowingComponent` follows and updates active paths, and the runtime presentation layer only displays the resulting cells and paths.
