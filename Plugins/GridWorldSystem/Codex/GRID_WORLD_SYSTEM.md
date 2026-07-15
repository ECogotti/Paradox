# GRID WORLD SYSTEM

## Purpose of this file

This file defines the domain-specific architecture and implementation direction for the `GridWorld` Unreal Engine plugin.

It supplements the repository root `AGENTS.md`. Do not duplicate the generic coding, compilation, documentation, logging, profiling, folder, lifecycle, Blueprint, or source-control rules already defined there. Apply those rules automatically while following this architecture.

The plugin is the foundational spatial system for the project. It must provide a reusable grid-based navigation implementation integrated with Unreal Engine's native Navigation System.

---

# 1. Architectural goals

The plugin must provide:

- a logical world grid composed of stable cells;
- conversion between world locations and grid coordinates;
- native Unreal navigation data backed by the grid;
- grid pathfinding, path cost, path length, projection, and reachability queries;
- compatibility with `UNavigationSystemV1`;
- compatibility with standard navigation requests such as `AIController::MoveTo`;
- paths consumable by `UPathFollowingComponent` and `UCharacterMovementComponent`;
- editor generation inside navigation bounds;
- editor and runtime visualization of grid navigation data;
- base traversal costs and runtime cost modifiers;
- static and dynamic blocked cells;
- occupancy and reservation information without forcing occupancy to block navigation;
- deterministic results suitable for recording and replaying cell destinations;
- extension points that do not contain game-specific rules.

The grid is used by other systems for movement, pathfinding, interactions, reachability, recorded destinations, and tactical visualization. It must therefore expose cell identity and semantic cell data, not only world-space path points.

---

# 2. Non-goals

The first implementation must not:

- depend on Recast or convert a Recast NavMesh into cells;
- contain rules for clones, rewind, paradoxes, doors, terminals, pressure plates, noise, or specific puzzle actors;
- replace `UCharacterMovementComponent`;
- require a custom `UPathFollowingComponent` before standard path following has been validated;
- implement arbitrary moving or rotating local gravity;
- implement World Partition navigation streaming before the core system is stable;
- create one Actor, Component, UObject, or mesh per cell;
- make every cell property Blueprint-overridable;
- use runtime occupancy as a blocker by default;
- rebuild the full grid for every small dynamic change;
- introduce a second authoritative grid state outside the Navigation Data.

---

# 3. Core architectural decisions

## 3.1 The grid is native Navigation Data

Implement the system as a custom subclass of `ANavigationData`.

Working name:

```text
AGridNavigationData : ANavigationData
```

`AGridNavigationData` is the authoritative owner of navigable grid state for its world and supported agent configuration.

The plugin must integrate with `UNavigationSystemV1` rather than running a parallel pathfinding service. Navigation requests made through the engine must be able to select and query this Navigation Data.

Do not use `ARecastNavMesh` as storage, as a preprocessing requirement, or as the source of truth.

Before implementing overrides, inspect the exact engine source for the project engine version. Use `ANavigationGraph` and `ARecastNavMesh` as reference implementations where useful, but do not copy assumptions that are specific to polygons or Recast.

## 3.2 One authoritative state

All authoritative grid data belongs to `AGridNavigationData` or to non-UObject data structures owned by it.

A world subsystem may exist only as a thin resolver and public facade. It must not own a duplicate cell array, duplicate graph, or duplicate traversal state.

The intended ownership is:

```text
UNavigationSystemV1
    -> registers/selects AGridNavigationData

AGridNavigationData
    -> owns published grid snapshots
    -> owns generator lifetime
    -> owns runtime modifier state
    -> answers navigation and grid queries

FGridNavDataGenerator
    -> builds replacement data for AGridNavigationData
    -> does not become a second runtime authority
```

## 3.3 Engine paths contain grid semantics

The Navigation Data must return a valid Unreal navigation path containing world-space path points for standard path following.

Use a custom path type when the engine-version implementation allows it cleanly.

Working name:

```text
FGridNavigationPath : FNavigationPath
```

It should retain the ordered logical cells in addition to world-space points.

A grid path should expose at least:

- ordered cell references;
- world-space path points;
- total traversal cost;
- total path length;
- grid topology revision;
- relevant dynamic-state revision;
- whether the path is partial;
- connection metadata for non-standard transitions.

Do not require consumers to reconstruct cells by projecting path points back onto the grid.

## 3.4 Standard movement first

The first movement integration must use:

```text
AIController / navigation request
    -> UNavigationSystemV1
    -> AGridNavigationData
    -> FNavigationPath
    -> UPathFollowingComponent
    -> UCharacterMovementComponent
```

Use normal walking movement for the first complete integration. The Navigation Data decides where the character can go; `UCharacterMovementComponent` remains responsible for physical movement, acceleration, braking, collision, floor handling, and network movement behavior.

Do not create a custom Character Movement Component unless a later requirement cannot be implemented through Navigation Data, path data, or path following.

A custom path-following component is optional and must be introduced only after the standard component is tested. Valid later reasons include:

- strict notification when entering or leaving logical cells;
- cell reservations;
- mandatory traversal of every recorded cell;
- specialized handling of grid links;
- grid-specific acceptance rules;
- keeping a logical cell path while visually smoothing movement.

## 3.5 Stable and deterministic cell identity

A cell must have a logical identity that survives ordinary rebuilds.

Use separate concepts for persistent identity and runtime access.

Recommended value types:

```text
FGridCellCoord
    X
    Y
    Layer

FGridCellId
    GridId
    Coord

FGridCellHandle
    runtime Navigation Data reference or identifier
    Coord
    optional revision information
```

`FGridCellCoord` must be a lightweight value type suitable for hashing, maps, sets, serialization, and Blueprint use.

`FGridCellId` is the stable logical identifier used by systems that need to retain a destination across resets or rebuilds. `GridId` must not be derived from a transient pointer or Actor name that can change unexpectedly.

`FGridCellHandle` may be optimized for runtime queries but must validate that it still refers to compatible published data.

Pathfinding determinism requirements:

- use a fixed neighbor expansion order;
- use deterministic tie-breaking in A*;
- avoid relying on unordered container iteration;
- prefer integer or fixed-scale traversal costs for core graph decisions;
- ensure identical grid state and query settings produce the same cell sequence;
- explicitly define diagonal costs if diagonal movement is enabled;
- preserve deterministic behavior in partial-path selection.

## 3.6 Separate topology, traversal state, and occupancy

Do not combine every cell concern into one mutable flag field.

Use three conceptual layers:

### Base topology

Generated from bounds, geometry, agent properties, authored links, and static modifiers.

Examples:

- cell existence;
- floor position and normal;
- base traversability;
- base area or category;
- base cost;
- neighbor connectivity;
- permanent links.

### Dynamic traversal overlay

Changes whether and how a cell may be traversed without requiring the base geometry to be regenerated.

Examples:

- temporary blocker;
- enabled or disabled connection;
- additive or multiplicative cost;
- temporary area/category override;
- agent-channel restrictions.

### Occupancy and reservation overlay

Describes who is currently using or reserving a cell.

Occupancy must remain queryable independently from traversability. By default, an occupied cell remains navigable. A query policy may opt into treating selected occupancy channels as blocking or expensive.

This distinction is mandatory because multiple agents may be allowed to occupy or cross the same logical cell.

---

# 4. Plugin modules

Use two modules from the beginning unless engine constraints discovered during the implementation spike justify a different split.

```text
Plugins/GridWorld/
    GridWorld.uplugin
    Source/
        GridWorld/
        GridWorldEditor/
    CODEX/
        GRID_WORLD_SYSTEM.md
    Docs/
```

The exact location of this file may be plugin-level `CODEX/GRID_WORLD_SYSTEM.md` once the plugin exists.

## 4.1 GridWorld runtime module

Responsibilities:

- cell coordinate and identity types;
- grid data storage;
- `AGridNavigationData`;
- grid path type;
- query filter implementation;
- generator interface and runtime generator code;
- navigation modifiers;
- occupancy and reservation APIs;
- public grid query facade;
- runtime debug data and rendering component declarations where required;
- serialization and data versions;
- automated runtime tests.

The runtime module must not depend on the editor module.

## 4.2 GridWorldEditor module

Responsibilities:

- editor-only visualization support that cannot live in runtime code;
- details customizations;
- build and rebuild commands;
- viewport controls and overlay filters;
- editor validation messages;
- optional custom bounds actor customization;
- editor-only tests.

Do not move core generation or pathfinding logic into the editor module. The runtime module must be able to build or update navigation when runtime generation is enabled.

---

# 5. Recommended code structure

This is a domain-specific structure for this plugin. Do not copy generic repository folder rules into local documentation.

```text
Source/GridWorld/
    Public/
        GridWorldModule.h

        Core/
            GridCellCoord.h
            GridCellId.h
            GridCellHandle.h
            GridTransform.h
            GridTypes.h

        Data/
            GridCellData.h
            GridChunk.h
            GridDataSnapshot.h
            GridDataVersion.h

        Navigation/
            GridNavigationData.h
            GridNavigationPath.h
            GridNavigationQuery.h
            GridNavigationQueryFilter.h
            GridNavigationLink.h

        Generation/
            GridGenerationSettings.h
            GridGenerationTypes.h

        Modifiers/
            GridNavigationModifierComponent.h
            GridOccupancyComponent.h
            GridNavigationContributorInterface.h
            GridNavigationQueryContextInterface.h

        Settings/
            GridWorldSettings.h

        Subsystems/
            GridWorldSubsystem.h

        Blueprint/
            GridWorldBlueprintLibrary.h

        Debug/
            GridNavigationRenderingComponent.h
            GridDebugTypes.h

    Private/
        GridWorldModule.cpp

        Core/
        Data/

        Navigation/
            GridNavigationData.cpp
            GridNavigationPath.cpp
            GridNavigationQuery.cpp
            GridNavigationQueryFilter.cpp
            GridAStar.cpp
            GridAStar.h

        Generation/
            GridNavDataGenerator.cpp
            GridNavDataGenerator.h
            GridGeometrySampler.cpp
            GridGeometrySampler.h
            GridChunkBuilder.cpp
            GridChunkBuilder.h

        Modifiers/
        Settings/
        Subsystems/
        Blueprint/

        Debug/
            GridNavigationRenderingComponent.cpp
            GridNavigationSceneProxy.cpp
            GridNavigationSceneProxy.h

        Serialization/
            GridNavigationSerialization.cpp
            GridNavigationSerialization.h

        Tests/
            GridCoordinateTests.cpp
            GridPathfindingTests.cpp
            GridModifierTests.cpp
            GridNavigationIntegrationTests.cpp

Source/GridWorldEditor/
    Public/
        GridWorldEditorModule.h

    Private/
        GridWorldEditorModule.cpp

        Commands/
            GridWorldEditorCommands.cpp
            GridWorldEditorCommands.h

        Customizations/
            GridNavigationDataCustomization.cpp
            GridNavigationDataCustomization.h

        Viewport/
            GridWorldViewportExtension.cpp
            GridWorldViewportExtension.h

        Validation/
            GridWorldEditorValidation.cpp
            GridWorldEditorValidation.h

        Tests/
```

Create only files needed by the current milestone. This tree describes boundaries and intended locations, not a requirement to create empty placeholders.

---

# 6. Main runtime types and responsibilities

## 6.1 `AGridNavigationData`

This is the central engine integration type.

Responsibilities:

- register as Navigation Data for supported agents;
- own the currently published grid data snapshot;
- construct and own its generator;
- answer point projection requests;
- answer surface-constrained movement requests where required by the engine;
- answer pathfinding and test-path requests;
- calculate path length and cost;
- perform navigation raycasts with grid semantics;
- expose navigable bounds;
- respond to navigation-bound changes;
- receive dirty-area rebuild requests;
- create its rendering component;
- publish data revisions;
- invalidate or request replanning of affected observed paths;
- serialize generated data when supported by the selected runtime-generation mode.

Do not put editor UI, actor-specific puzzle logic, or movement animation logic in this class.

Avoid turning it into a god object. Delegate algorithms, generation, storage, and rendering preparation to focused non-UObject classes owned by it.

## 6.2 `FGridNavDataGenerator`

Working base:

```text
FGridNavDataGenerator : FNavDataGenerator
```

Confirm the exact engine base type and required overrides in the engine source before implementation.

Responsibilities:

- build all navigation data inside applicable bounds;
- rebuild affected chunks for dirty areas;
- cancel builds safely;
- report pending and running build tasks;
- gather generation input on the Game Thread;
- perform thread-safe pure-data processing off-thread where useful;
- publish complete replacement chunks or snapshots atomically;
- never expose partially built state to queries.

The generator must not directly mutate published cell arrays while path queries may be reading them.

## 6.3 `FGridDataSnapshot`

Represents an immutable published view used by pathfinding and queries.

Recommended contents:

- grid transform;
- cell size and layer spacing;
- bounds and dimensions;
- chunks;
- cell topology;
- adjacency data;
- stable grid identifier;
- topology revision;
- generation settings hash;
- agent configuration hash.

Prefer immutable snapshots or immutable chunks so synchronous and asynchronous queries can safely retain a consistent view.

Dynamic overlays may use separate revisioned data if copying the complete topology would be wasteful.

## 6.4 `FGridChunk`

Partition the grid into fixed-size chunks from the first real implementation, even if the first map is small.

Chunking enables:

- localized dirty rebuilds;
- localized rendering updates;
- efficient cell lookup;
- future streaming;
- smaller replacement units for immutable snapshots;
- targeted path invalidation.

Do not implement World Partition streaming merely because chunks exist.

Chunk size must be configurable or defined as a stable implementation constant with a documented rationale.

## 6.5 `FGridCellData`

Keep frequently queried pathfinding data compact.

Separate hot path data from rarely used metadata where practical.

Likely core fields:

- base traversal flags;
- area/category identifier;
- base traversal cost;
- floor location or height;
- floor normal or compact slope data when needed;
- neighbor mask or compact adjacency reference;
- link references;
- generation validity flags.

Do not store UObject references in every cell.

## 6.6 `FGridNavigationPath`

Responsibilities:

- retain ordered grid cells;
- provide standard path points to Unreal path following;
- retain relevant revisions;
- retain per-segment link metadata;
- expose a safe read-only grid path API;
- support invalidation when affected cells or links change.

World-space points should normally represent cell centers, connection portals, or authored link endpoints. Keep the logical cell sequence independent from optional visual smoothing.

## 6.7 `FGridNavigationQueryFilter`

Integrate with Unreal's navigation query-filter model rather than inventing an unrelated global filtering system.

The filter must support generic policies such as:

- excluded areas or categories;
- per-area costs;
- diagonal movement permission;
- blocked traversal channels;
- occupancy policy;
- reservation policy;
- link permission;
- maximum cost where supported;
- partial path permission where owned by the query.

Use standard `UNavigationQueryFilter` classes as the designer-facing configuration entry point where possible. Implement a custom filter backend only where required for grid-specific evaluation.

Do not place game-specific conditions in the filter.

## 6.8 `UGridWorldSubsystem`

This subsystem is optional but recommended as a thin world-level facade.

Allowed responsibilities:

- resolve the relevant `AGridNavigationData` for an agent or query;
- expose safe grid queries to systems that should not know actor-discovery details;
- forward high-level change notifications;
- provide a world-level entry point for Blueprint utility functions.

Forbidden responsibilities:

- owning a duplicate grid;
- running a separate pathfinder;
- becoming the source of truth for occupancy;
- bypassing `UNavigationSystemV1` for engine navigation requests.

## 6.9 `UGridWorldBlueprintLibrary`

Keep Blueprint nodes as thin validated wrappers over the subsystem or Navigation Data.

Initial useful nodes may include:

- resolve grid cell from world location;
- get world location for a cell;
- project a point to the grid;
- test whether a cell exists;
- test whether a cell is traversable for an agent/filter;
- request a cell path;
- request reachable cells within cost;
- inspect cell data through a read-only snapshot struct.

Do not expose raw mutable arrays or internal chunk storage.

---

# 7. Bounds and grid transform

## 7.1 Editor workflow

The intended authoring workflow is comparable to Unreal's NavMesh workflow:

1. Place a navigation bounds volume around the navigable ship or level area.
2. Configure the grid generation settings.
3. Build navigation.
4. Toggle navigation visualization.
5. See generated cells instead of Recast polygons.
6. Inspect blocked cells, costs, links, layers, dirty regions, and validation errors.

## 7.2 Bounds actor decision

Preferred working type:

```text
AGridNavigationBoundsVolume
```

First inspect whether deriving from `ANavMeshBoundsVolume` is compatible with the engine's navigation-bound registration and whether the base class contains assumptions that make subclassing unsafe.

Use this decision order:

1. If subclassing is clean, derive from the standard navigation bounds volume and add only grid-specific authoring properties.
2. If subclassing is not clean, use the standard navigation bounds volume for engine registration and associate grid settings through Navigation Data defaults or a dedicated companion component/actor.
3. Do not create a bounds system invisible to `UNavigationSystemV1` unless engine integration proves impossible.

The Navigation System may work with world-axis-aligned dirty bounds even when the logical grid has its own local transform.

## 7.3 Grid orientation

Represent conversions through an explicit `FGridTransform` owned by the snapshot.

Grid bounds authoring supports the complete Actor Transform:

- translation, pitch, yaw, and roll define the grid's rigid local frame;
- non-zero Actor scale changes the bounds extents without scaling logical cell dimensions;
- collision sampling follows the grid-local negative Z axis;
- floor slope, agent capsule clearance, Character Movement, and step/drop validation continue to use Unreal world up;
- rotating a volume does not introduce local gravity or make vertical surfaces walkable.

In editor, a completed Transform edit on `AGridNavigationBoundsVolume` must automatically request a GridWorld rebuild through Unreal's native navigation-bounds notification and `FGridNavDataGenerator::OnNavigationBoundsChanged`. Do not register a second editor movement callback that duplicates the native rebuild.

Use local grid coordinates for cell indexing and convert through `FGridTransform`.

Do not derive stable cell identity from raw world-space floating-point locations.

---

# 8. Generation pipeline

Implement generation as explicit phases so failures can be inspected and individual stages can later be optimized.

```text
Navigation bounds
    -> grid layout
    -> candidate cells
    -> geometry/floor sampling
    -> clearance validation
    -> base cell classification
    -> adjacency construction
    -> authored link integration
    -> modifier application
    -> validation
    -> immutable chunk/snapshot publication
```

## 8.1 Layout phase

Calculate:

- grid origin and transform;
- cell size;
- layer spacing;
- coordinate range intersecting the bounds;
- chunk range;
- agent-specific generation settings.

Cell size must be a grid design parameter, not inferred from the mesh triangle size.

## 8.2 Geometry sampling phase

The first implementation may use controlled collision queries per candidate cell because this is easier to validate than directly consuming low-level navigation-octree geometry.

The geometry sampler should evaluate at least:

- whether a valid floor exists;
- floor height;
- floor slope;
- agent capsule clearance;
- configured step height and drop limits;
- collision profile or object channels selected for grid generation;
- whether multiple valid floors exist for separate layers.

Gather UObject and physics-scene information on the correct thread. Convert it into pure data before off-thread processing.

Do not permanently commit to per-cell traces if profiling shows they are too expensive. Keep the geometry source behind `FGridGeometrySampler` so a later implementation can consume navigation-octree or cached geometry without rewriting pathfinding and storage.

## 8.3 Adjacency phase

Adjacency is not implied only by neighboring coordinates.

Validate each connection using:

- source and destination traversability;
- height difference;
- step/drop rules;
- diagonal corner-cutting rules;
- agent dimensions;
- active authored links;
- static traversal restrictions.

Store adjacency compactly. For regular grids, prefer a direction mask plus link references rather than a heap allocation per cell.

## 8.4 Publication phase

Never expose a half-built grid.

Build replacement chunks or a complete replacement snapshot, validate them, and publish them atomically on the owning Navigation Data.

Increment revisions only when publication succeeds.

Emit a concise change set describing affected chunks and cells so rendering, observed paths, and consumer systems can react without rescanning the entire grid.

---

# 9. Pathfinding and navigation queries

## 9.1 A* implementation

Use a dedicated non-UObject algorithm implementation.

Working type:

```text
FGridAStar
```

Requirements:

- no UObject access inside the core search loop;
- operates on a retained immutable snapshot and query filter;
- deterministic neighbor order and tie-breaking;
- supports complete and partial paths;
- supports cancellation if used asynchronously;
- reports visited-node count for diagnostics;
- returns explicit failure reasons internally;
- avoids per-node heap allocation;
- reuses or preallocates search memory where safe;
- supports path cost independently from geometric length.

Start with a flat A* search. Do not implement hierarchical pathfinding until representative levels demonstrate a need.

## 9.2 Projection

World-to-grid projection must be a first-class operation, not a simple rounding function.

Projection should:

1. transform the point into grid-local space;
2. determine candidate coordinates inside the requested extent;
3. select cells compatible with the query filter and agent;
4. rank candidates by a documented deterministic rule;
5. return an `FNavLocation` whose node reference identifies the selected cell.

Define how projection behaves across multiple layers and equal-distance candidates.

## 9.3 Navigation node references

Encode or map grid cell identity to `NavNodeRef` safely.

The encoding must:

- reject invalid or stale values;
- preserve enough information to resolve a cell within the owning Navigation Data;
- not depend on a raw pointer;
- account for chunk and cell indices or use a stable indirection table;
- document bit allocation if packed;
- remain compatible with the target engine's `NavNodeRef` width.

Do not expose packed bit details as the public grid identity API.

## 9.4 Navigation raycast

Define grid raycast semantics explicitly.

The expected meaning is a line-of-travel test through grid connectivity and filters, not a physics trace and not a visual line-of-sight test.

Return the first blocked transition or nearest reachable point according to engine expectations.

## 9.5 Reachability

Provide a grid-native reachable-area query in addition to standard test-path functions.

Inputs should support:

- start cell or world location;
- filter;
- maximum traversal cost;
- optional maximum number of cells;
- optional early-stop predicate only in C++ extension points.

Outputs should be value data suitable for tactical visualization and gameplay validation.

Do not require one A* call per candidate cell.

---

# 10. Dynamic modifiers

## 10.1 Generic modifier component

Working type:

```text
UGridNavigationModifierComponent
```

The component describes a footprint and a generic navigation effect.

Supported effect categories should include:

- block traversal;
- unblock authored traversal where permitted;
- add cost;
- multiply cost;
- override area/category;
- enable or disable a link;
- apply traversal-channel flags.

A modifier must identify its affected cells from bounds or an explicit cell footprint.

Changing a modifier should update only affected overlay data or dirty chunks. Do not rebuild sampled geometry for a door-like state change when a traversal overlay is sufficient.

## 10.2 Navigation contributor interface

Working interface:

```text
IGridNavigationContributorInterface
```

Use it when an Actor or Component must provide grid-specific authored data that does not fit a single modifier component.

Possible contributions:

- explicit cell flags;
- custom cell footprint;
- authored grid links;
- generation-time metadata;
- runtime overlay commands.

The interface must remain generic. It must not include methods named for doors, terminals, puzzles, or temporal entities.

## 10.3 Occupancy component

Working type:

```text
UGridOccupancyComponent
```

Responsibilities:

- determine the logical cells occupied by its owner;
- register and unregister occupancy safely;
- update occupancy only when the occupied cell set changes;
- expose occupancy channel or category;
- optionally request reservations;
- never alter base topology directly.

Occupancy changes should normally increment an occupancy revision, not a topology revision.

Movement agents must not automatically block one another. Blocking behavior belongs to the query policy.

An optional dynamic-agent policy may combine path-following yield with selective repathing. It must preserve the outer Unreal Move To lifecycle, ignore the requester's own occupancy identity, leave endpoint arbitration to the goal policy, and retain a safe waiting path when transient occupancy has no alternate route. It must not silently enable RVO or bypass precise center gates.

## 10.4 Query context

Standard Unreal navigation requests do not always carry arbitrary game data. When a query needs caller-specific generic policy, use a narrow interface implemented by the query owner, controller, pawn, or agent.

Working interface:

```text
IGridNavigationQueryContextInterface
```

It may provide:

- traversal channels;
- occupancy channels to ignore or avoid;
- reservation owner identifier;
- generic agent capabilities;
- a grid filter profile.

Do not cast to project-specific pawn or controller classes inside the plugin.

---

# 11. Revisions and path invalidation

Track distinct revisions so unrelated changes do not invalidate every path.

Recommended revisions:

```text
TopologyRevision
TraversalRevision
CostRevision
OccupancyRevision
LinkRevision
```

A path must record the revisions relevant to its calculation and the chunks or cells it traverses.

When changes occur:

- invalidate paths only when the changed data can affect them;
- use the engine's observed-path and repath mechanisms where compatible;
- distinguish a path becoming invalid from a path becoming more expensive;
- avoid replanning every active path after an unrelated occupancy update;
- preserve the reason for invalidation for diagnostics and higher-level systems.

The plugin reports generic reasons such as:

- cell blocked;
- connection removed;
- cost policy changed;
- destination invalidated;
- grid rebuilt;
- query filter no longer valid.

It must not report game-specific reasons.

---

# 12. Multi-agent support

Use Unreal Supported Agents and Navigation Data selection instead of building a private agent registry that bypasses the Navigation System.

Generation must consider the selected `FNavAgentProperties`, including the relevant radius and height.

Preferred model:

- one `AGridNavigationData` instance per compatible supported-agent configuration;
- shared implementation code;
- separately generated clearance and connectivity where agent dimensions differ materially;
- explicit validation when grid cell size is incompatible with the agent.

Do not assume every agent shares the same clearance merely because they use the same cell coordinates.

Keep agent-specific traversal capabilities in filters or query context when they do not require different geometry generation.

---

# 13. Editor visualization

## 13.1 Rendering architecture

The Navigation Data should construct a dedicated rendering component.

Working types:

```text
UGridNavigationRenderingComponent
FGridNavigationSceneProxy
```

Use batched rendering data. Do not create persistent primitive components per cell.

The renderer must read a prepared immutable debug snapshot or compact render buffers. It must not walk mutable gameplay objects from the render thread.

## 13.2 Required visualization modes

Provide filters for:

- traversable cells;
- blocked cells;
- absent/invalid candidate cells;
- base cost heatmap;
- dynamic cost overlay;
- occupied cells;
- reserved cells;
- cell coordinates;
- chunk boundaries;
- neighbor connections;
- authored links;
- dirty chunks or cells;
- generation errors;
- selected agent configuration;
- selected path;
- reachable-area result.

Navigation visibility should integrate with Unreal's navigation display flow when practical, so the user experience resembles toggling standard navigation visualization.

## 13.3 Editor commands

Useful editor commands:

- build all GridWorld navigation;
- rebuild selected bounds;
- clear generated GridWorld navigation;
- validate selected Grid Navigation Data;
- toggle coordinate labels;
- toggle costs;
- toggle occupancy;
- select displayed agent;
- inspect the cell under the cursor.

Do not require a custom editor window for the first milestone. Start with viewport visualization, actor details, and concise commands.

## 13.4 Validation

Editor validation should report actionable problems such as:

- no applicable bounds;
- zero or invalid cell size;
- unsupported bounds rotation;
- excessive candidate-cell count;
- no supported agent configuration;
- agent larger than the configured grid assumptions;
- duplicate stable grid identifiers;
- overlapping grids with ambiguous ownership;
- invalid layer spacing;
- unsupported runtime-generation setting;
- failed serialization version.

---

# 14. Data access and public API boundaries

Consumers need semantic cell information, but they must not mutate internal storage.

Expose read-only result structs such as:

```text
FGridCellSnapshot
FGridPathResult
FGridReachabilityResult
FGridProjectionResult
FGridChangeSet
```

Queries should return explicit status values rather than encoding failure as an arbitrary invalid coordinate.

Recommended result categories:

```text
Success
Partial
InvalidNavigationData
InvalidStart
InvalidGoal
ProjectionFailed
Unreachable
Cancelled
StaleData
UnsupportedQuery
```

Public mutation must occur through controlled operations:

- registered modifiers;
- registered occupancy sources;
- registered links;
- explicit rebuild requests;
- validated runtime overlay transactions.

Do not expose mutable cell arrays, chunk maps, open lists, or generator internals.

---

# 15. Serialization

Generated navigation data must use explicit versioning.

Working type:

```text
FGridNavigationDataVersion
```

Serialized data should include enough metadata to reject incompatible builds:

- plugin data version;
- engine compatibility information where required;
- generation-settings hash;
- agent-settings hash;
- grid transform and dimensions;
- chunk layout;
- topology and cell data;
- stable grid identifier.

Dynamic occupancy must not be serialized as generated navigation data.

Dynamic traversal state should be serialized only if a future explicit persistence requirement exists. Do not conflate generated navigation serialization with save-game state.

On incompatible data:

- reject it visibly;
- request or require a rebuild;
- do not attempt unsafe partial loading.

---

# 16. Threading model

Pathfinding should operate on immutable or otherwise safely synchronized pure data.

Rules specific to this plugin:

- do not access UObjects from the A* core;
- do not perform collision queries from arbitrary worker threads;
- gather geometry and modifier input on the correct thread;
- publish replacement snapshots atomically;
- retain snapshots for the duration of a query;
- make cancellation and world teardown explicit;
- never allow generator completion to publish into destroyed or unregistered Navigation Data;
- avoid holding a global write lock during a full build.

Start with synchronous generation and queries if necessary to establish correctness. Preserve the architecture required to move pure-data phases off-thread later.

Do not introduce asynchronous complexity before the synchronous integration passes functional tests.

---

# 17. Testing strategy

## 17.1 Pure-data automation tests

Implement focused tests for:

- world/grid conversion at positive and negative coordinates;
- conversion under translated and yaw-rotated grids;
- stable coordinate hashing and equality;
- node-reference encode/decode;
- deterministic A* tie-breaking;
- blocked cells;
- weighted costs;
- diagonal rules and corner cutting;
- partial paths;
- multiple layers;
- links;
- reachable-area queries;
- chunk-boundary paths;
- stale-handle detection;
- modifier composition;
- occupancy policy;
- serialization version rejection.

## 17.2 Engine integration tests

Create functional or automation tests that verify:

1. Grid Navigation Data registers with `UNavigationSystemV1`.
2. The correct Navigation Data is selected for the configured supported agent.
3. Point projection returns grid-backed `FNavLocation` values.
4. A standard navigation query returns a valid grid path.
5. `AIController::MoveTo` can move a Character using the standard Path Following Component and Character Movement Component.
6. Blocking a cell invalidates or replans an affected observed path.
7. An unrelated modifier does not invalidate an unaffected path.
8. Editor build produces visible grid navigation inside bounds.
9. Saved generated data reloads or visibly requests rebuild according to the selected generation mode.

Use small deterministic test maps generated from simple collision primitives.

---

# 18. Implementation milestones

Do not implement the entire plugin in one pass. Complete and validate each milestone before expanding the architecture.

## Milestone 0 — Engine integration spike

Goal: prove that a custom Navigation Data can participate in the engine pipeline.

Implement only enough to:

- define and spawn/register `AGridNavigationData`;
- associate it with one supported agent;
- return a minimal valid path for a controlled test case;
- confirm standard path following consumes the result;
- construct a minimal rendering component;
- identify all required engine overrides and function-pointer registrations for the project engine version.

Temporary simplifications are allowed inside the spike, but do not merge fabricated or unverified API assumptions into the final architecture.

Exit criteria:

- a Character controlled by an AIController receives and follows a path produced by `AGridNavigationData`;
- the custom Navigation Data is visible to `UNavigationSystemV1`;
- the custom debug renderer can draw at least one cell or path element.

## Milestone 1 — Core grid data

Implement:

- coordinate, identity, transform, cell, chunk, and snapshot types;
- world/grid conversion;
- stable revisions;
- a manually constructed test grid;
- pure deterministic A*;
- path result containing both cells and world points.

Exit criteria:

- all pure-data tests pass;
- standard engine path queries use the manually constructed grid.

## Milestone 2 — Bounds and geometry generation

Implement:

- bounds discovery;
- generation settings;
- collision-based floor and clearance sampling;
- adjacency generation;
- full build;
- editor visualization of generated cell states.

Exit criteria:

- placing bounds around a simple level and building navigation produces an accurate visible grid;
- standard MoveTo works on generated data.

## Milestone 3 — Query completeness

Implement and validate:

- projection;
- test path;
- path length and cost;
- navigation raycast;
- partial paths;
- reachable-area query;
- multi-layer selection rules.

Exit criteria:

- engine and grid-native queries return consistent deterministic results.

## Milestone 4 — Dynamic overlays

Implement:

- modifier component;
- occupancy component;
- traversal/cost/occupancy revisions;
- localized updates;
- affected-path invalidation and replanning.

Exit criteria:

- toggling a generic blocker changes the path without rebuilding unrelated chunks;
- occupancy can be visualized without blocking by default;
- query filters can opt into occupancy avoidance.

## Milestone 5 — Incremental generation and editor workflow

Implement:

- dirty-area to chunk mapping;
- localized chunk rebuild;
- editor build commands;
- validation;
- visualization filters;
- cell inspection.

Exit criteria:

- modifying geometry or a generation contributor rebuilds only affected chunks;
- the user can author and inspect the grid with a workflow comparable to standard navigation.

## Milestone 6 — Performance and advanced integration

Only after profiling representative levels, consider:

- asynchronous pure-data pathfinding;
- time-sliced or asynchronous generation;
- hierarchical pathfinding;
- advanced nav links;
- custom path following;
- navigation streaming;
- compressed cell storage;
- render-buffer caching.

Do not implement these based only on hypothetical scale.

---

# 19. Required investigation before coding

Codex must inspect the project engine source and current project configuration before finalizing the first implementation.

Specifically verify:

- exact `ANavigationData` pathfinding callback registration pattern;
- required static function signatures for find path, test path, and raycast;
- exact `FNavDataGenerator` virtual interface;
- how Navigation Data classes are selected and instantiated for Supported Agents;
- whether `ANavMeshBoundsVolume` is safe and useful as the base for a custom bounds volume;
- rendering component creation and navigation-show integration;
- observed-path registration and repath behavior;
- required semantics of `ProjectPoint` and `FindMoveAlongSurface` for normal walking and NavWalking;
- path-point and node-reference expectations of `UPathFollowingComponent`;
- thread-safety requirements of synchronous and asynchronous navigation queries;
- serialization behavior for custom Navigation Data in the target engine version;
- the minimum module dependencies needed by the public headers.

Record engine-version-specific discoveries in local `CODEX` documentation without copying generic rules from `AGENTS.md`.

---

# 20. Architectural constraints for future work

Future changes must preserve these invariants unless this document is intentionally revised:

1. `AGridNavigationData` is the authoritative navigable grid owner.
2. Engine navigation requests go through `UNavigationSystemV1`.
3. Recast is not required and is not the source of grid data.
4. Logical cell identity is stable and independent from world-space floating-point values.
5. Paths retain ordered cells as well as world-space points.
6. Base topology, dynamic traversal state, and occupancy are separate concerns.
7. Occupancy does not block navigation by default.
8. Pathfinding is deterministic for identical inputs.
9. Generation publishes complete immutable data, never partial state.
10. Runtime and editor responsibilities remain separated.
11. Consumers cannot mutate internal cell storage directly.
12. The plugin remains generic and contains no Paradox-specific classes or rules.
13. Standard Character Movement and Path Following are used until a verified limitation requires specialization.
14. Dynamic changes update affected overlays or chunks rather than rebuilding the whole world without need.
15. Advanced optimization follows profiling and does not precede correctness.

---

# 21. Initial definition of success

The first production-usable version is successful when a developer can:

1. enable the plugin;
2. configure a supported grid-navigation agent;
3. place navigation bounds around a ship or level mesh;
4. build navigation;
5. toggle navigation display and see generated grid cells;
6. select or provide a destination world point or cell;
7. obtain a deterministic grid path through Unreal's Navigation System;
8. move a Character along that path using standard AI path following and Character Movement;
9. toggle a generic runtime blocker and observe valid replanning;
10. query the ordered logical cells for recording, reachability, and tactical visualization;
11. inspect blocked cells, costs, links, occupancy, and dirty regions in the viewport.
