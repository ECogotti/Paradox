# Paradox Puzzle Circuit Overlay Renderer — Codex Specification

## Purpose

Implement the **Paradox project-level puzzle circuit renderer** that visualizes the puzzle relationships of the currently selected Actor.

This system consumes:

```text
Paradox Selection System
PuzzleSystem Graph Query API
GridWorld spatial/reference-frame APIs
```

and renders a temporary world-space circuit overlay made of orthogonal 3D segments.

The visual language is:

```text
INPUT relationships
    -> one configurable visual/material family

OUTPUT relationships
    -> another configurable visual/material family

runtime signal state
    -> changes brightness/emissive/state parameters

topology / geometry changes
    -> may rebuild routing

signal-only changes
    -> must NOT rebuild routing
```

The intended appearance is a schematic circuit laid mainly along the ground, with:

```text
straight segments
90-degree corners only
parallel lanes
vertical risers when floors differ
small bridge elevations when a wire crossing cannot otherwise be avoided
```

The renderer must be able to visualize a puzzle connection even when:

```text
there is no valid navigation path
there are blocked GridWorld cells
there is a wall between endpoints
there is empty space / a chasm
the endpoints are on different vertical levels
```

A puzzle relationship is logical.

Navigation reachability must never determine whether that relationship can be rendered.

---

# 1. Scope

This task belongs to the project-specific gameplay module, conceptually:

```text
ParadoxGameplay
```

Do not create a new plugin for this renderer.

The generic `PuzzleSystem` plugin owns graph knowledge.

`ParadoxGameplay` owns how that graph is rendered for this game.

Required dependency direction:

```text
ParadoxGameplay
    -> PuzzleSystem
    -> GridWorld
```

Never introduce:

```text
PuzzleSystem -> ParadoxGameplay
GridWorld -> ParadoxGameplay
```

---

# 2. Mandatory preliminary workflow

Before implementing anything, Codex must:

1. read repository root `AGENTS.md`;
2. identify the real Paradox gameplay module;
3. read every relevant local `CODEX` file;
4. read the relevant `Docs`;
5. inspect the current Selection System implementation;
6. inspect the completed PuzzleSystem Graph Query API;
7. inspect GridWorld public APIs and documentation;
8. inspect the existing GridWorld coordinate/reference-frame and runtime presentation concepts;
9. inspect current WorldState reset integration used by the Selection System;
10. inspect project logging/debug conventions;
11. inspect existing Custom Depth usage relevant to world overlays;
12. inspect the engine version's async/task APIs before using them;
13. compile the appropriate target before structural changes;
14. implement the smallest requested architecture;
15. compile after every meaningful change;
16. update user-facing documentation;
17. finish only when the affected target compiles.

Do not invent Unreal APIs from memory.

Working type names in this document may adapt to existing project naming conventions.

---

# 3. Explicit non-goals

Do not implement or modify:

```text
PuzzleSystem signal propagation
PuzzleSystem Controller logic
PuzzleSystem Graph Query semantics
GridWorld navigation
GridWorld pathfinding
GridWorld occupancy
GridWorld reservation
GOAP
IntentReplay
Smart Object interaction
selection input semantics
hover input semantics
interaction widget behavior
movement
AI movement
PCG
world reset logic outside the existing Paradox integration boundary
```

Do not add routing functionality to `PuzzleSystem`.

Do not add Paradox rendering concepts to `GridWorld`.

---

# 4. Authoritative input data

The renderer must use the completed PuzzleSystem Graph Query extension.

Do not reconstruct puzzle topology by scanning `APuzzleController` directly.

Do not duplicate graph semantics.

Expected graph concepts include:

```text
FPuzzleActorGraphView
FPuzzleGraphLink
FPuzzleGraphLinkHandle
FPuzzleGraphLinkState

PrimarySignal
GateInfluence

IncomingPrimaryLinks
IncomingGateLinks
OutgoingPrimaryLinks

RawPrimaryActive
Gate state
EffectivePrimaryActive
Controller context
Receiver context
```

The renderer is a consumer only.

---

# 5. Selection integration

The renderer visualizes only the graph of the **currently selected Actor**.

It must listen to the existing selection lifecycle rather than own selection itself.

Conceptual flow:

```text
No selected Actor
    -> no puzzle circuit overlay

Actor selected
    -> query PuzzleSystem graph
    -> build circuit routes
    -> render

Selected Actor changes
    -> clear old overlay
    -> query new Actor
    -> rebuild

Deselection
    -> clear overlay
```

Do not render the entire level graph by default.

Do not introduce a second selected-Actor source of truth.

---

# 6. Designer-facing enable/disable policy

Expose an optional selection-facing/project setting equivalent to:

```text
bShowPuzzleConnectionsWhenSelected
```

or use the closest existing selectable-component configuration pattern.

Default may be chosen according to current project convention, but must be documented.

If false:

```text
Actor can still be selected
outline still works
widget still works
puzzle circuit overlay remains hidden
```

Do not couple puzzle graph rendering to basic selection eligibility.

---

# 7. Core visual semantics

For the selected Actor:

## Incoming Primary links

If the Actor owns Receiver components:

```text
Emitter Actor
    -> Selected Actor
```

Render as an **Input** wire.

## Incoming Gate links

If the Actor owns Emitter components and those Emitters are gated:

```text
Gate Emitter Actor
    -> Selected Actor
```

Render as an **Input** wire.

The underlying PuzzleSystem link remains `GateInfluence`.

The renderer may group it visually with inputs while retaining the semantic link kind for state/debug/material decisions.

## Outgoing Primary links

If the selected Actor owns Emitter components:

```text
Selected Actor
    -> downstream Receiver Actor
```

Render as an **Output** wire.

---

# 8. Visual direction enum

Working concept:

```text
EParadoxPuzzleWireDirection
```

Initial values:

```text
Input
Output
```

This is a Paradox rendering concept.

Do not add it to PuzzleSystem.

Map:

```text
IncomingPrimary -> Input
IncomingGate    -> Input
OutgoingPrimary -> Output
```

---

# 9. State-driven brightness

Color/family identifies direction.

Brightness/emissive identifies runtime state.

Do not rebuild geometry when only a puzzle signal changes.

Preferred visual model:

```text
Input:
    fixed input color family
    low emissive when inactive
    high emissive when active

Output:
    fixed output color family
    low emissive when inactive
    high emissive when active
```

The actual colors and shader appearance must remain material-configurable.

Do not hardcode final art colors in C++.

---

# 10. Effective state drives PrimarySignal wires

For a `PrimarySignal` link, brightness must represent:

```text
EffectivePrimaryActive
```

not merely:

```text
RawPrimaryActive
```

This is mandatory.

Example:

```text
Emitter raw = true
Controller-local gate = closed
EffectivePrimaryActive = false
```

Expected:

```text
wire remains visually inactive/dim
```

This allows the renderer to correctly display:

```text
same source Emitter
Controller A -> bright
Controller B -> dim
```

when the two Controller contexts have different gate results.

Do not derive output brightness globally from the Emitter.

---

# 11. GateInfluence brightness

For `GateInfluence`, the first implementation should use the raw observed state of that specific gate input:

```text
GateInputActive
```

for the gate wire's base brightness.

The renderer must still retain access to:

```text
OwningGateMode
OwningGateAllowsSignal
OwningEffectivePrimaryActive
```

so future visual styles can distinguish:

```text
gate emitter active
aggregate gate condition passes
aggregate gate condition fails
invalid gate
```

Do not flatten individual gate state and aggregate gate result.

---

# 12. Invalid / blocked states

The initial renderer must support at least the distinction between:

```text
Active
Inactive
```

The architecture must preserve enough graph state for future material variants such as:

```text
Blocked
Invalid
Bypassed
```

Do not hardcode a giant visual enum unless the first implementation actually needs it.

A reasonable internal visual state may be:

```text
EParadoxPuzzleWireVisualState
Inactive
Active
Blocked
Invalid
```

only if this simplifies the renderer and maps unambiguously from PuzzleSystem state.

Document the mapping.

---

# 13. Event-driven state updates

Listen to the PuzzleSystem Graph Query state event equivalent to:

```text
OnPuzzleGraphLinkStateChanged
```

When a currently rendered link changes state:

```text
update material / instance custom data
do not reroute
do not rebuild topology
do not run surface detection
```

This is mandatory for performance and architecture.

---

# 14. Topology event

Listen to the PuzzleSystem topology event equivalent to:

```text
OnPuzzleGraphTopologyChanged
```

If the event affects the selected Actor or one of its rendered relationships:

```text
re-query selected Actor graph
rebuild relevant routes
```

Do not rebuild every visible route on unrelated topology changes if affected-endpoint data allows narrower invalidation.

A full selected-subgraph rebuild is acceptable as the initial simple implementation because only one Actor is selected, but the code structure should permit targeted invalidation later.

---

# 15. WorldState reset lifecycle

This renderer is transient selection presentation.

It must integrate with the already-established selection reset lifecycle.

On WorldState reset start:

```text
Selection System clears selection
    ↓
Puzzle Circuit Renderer clears all visual routes
```

Prefer clearing as a consequence of selection reset rather than independently making WorldState own this renderer.

If an additional explicit reset binding is required for safety, it belongs in `ParadoxGameplay`.

Do not make WorldState depend on renderer classes.

---

# 16. Geometry model: orthogonal circuit

Every wire is a polyline composed exclusively of axis-aligned segments in the routing frame.

Allowed transitions:

```text
X
Y
Z
```

Every corner must be 90 degrees.

Do not use Bézier curves.

Do not generate smooth spline tangents.

Do not make decorative curved cables in this core system.

---

# 17. GridWorld is a reference frame, not a pathfinder

GridWorld is used to derive:

```text
grid origin/reference transform
grid orientation
cell spacing / routing pitch
world <-> grid-aligned coordinate conversions
known surface/cell information where available
```

The renderer must not call GridWorld navigation pathfinding to generate a puzzle wire.

Never require:

```text
FindPath()
Reachability
Traversal validity
Occupancy
Reservation
Agent radius
navigation cost
```

to render a puzzle relationship.

A puzzle wire is not a movement path.

---

# 18. No-path case

If two Actors have no valid GridWorld path:

```text
render anyway
```

The route is purely visual/logical.

Blocked cells do not invalidate a wire.

Closed doors do not invalidate a wire.

Opening/closing a door must not make the wire suddenly use a different navigation route.

---

# 19. Visual Routing Lattice

Create a project-specific mathematical routing lattice.

Working concept:

```text
FParadoxPuzzleRoutingLattice
```

This is not a second gameplay GridWorld.

It is a transient mathematical structure derived from GridWorld's reference frame.

It may represent routing coordinates even where no actual GridWorld cell exists.

Conceptually:

```text
(X, Y, ZLayer / WorldZ)
```

Do not use `FGridCellId` as the authoritative route coordinate for all routing points because some visual lattice points may exist:

```text
outside generated GridWorld cells
over empty space
between floors
in visually unsupported regions
```

---

# 20. Routing coordinate

Working value type:

```text
FParadoxPuzzleRoutingCoord
```

Conceptual fields:

```text
int32 X
int32 Y
float Z
```

or an equivalent representation compatible with the discovered GridWorld frame.

X/Y should be quantized to the routing lattice.

Z represents actual visual elevation, not a fake navigation floor index.

Do not require equally spaced floors.

---

# 21. Multi-level support

Endpoints may exist on different vertical levels.

Example:

```text
Emitter at Z0
Receiver at Z1
```

The route must insert a vertical riser when required.

Conceptual route:

```text
Source
───────┐
       │
       │ vertical structural riser
       │
       └──────── Target
```

All transitions remain 90 degrees.

Do not search for:

```text
stairs
ramps
elevators
navigation links
holes in ceilings
```

The wire is a logical circuit.

Custom Depth/material presentation may make it visible through intervening geometry.

---

# 22. Structural Z vs routing bridge Z

Distinguish semantically between two kinds of vertical movement.

## Structural vertical transition

Used because endpoints/surfaces are actually at different world heights.

Working semantic:

```text
FloorTransition
```

## Routing bridge transition

Used only to pass one visual wire over another when crossing cannot reasonably be avoided.

Working semantic:

```text
WireBridge
```

They are both geometric Z segments but must remain distinguishable in route metadata.

This is useful for:

```text
debug
future materials
future tuning
cost scoring
```

---

# 23. Surface detection

The intended normal appearance is that horizontal wires lie just above a physical surface.

Introduce a configurable offset:

```text
GroundWireHeightOffset
```

The wire surface point is:

```text
SurfaceZ + GroundWireHeightOffset
```

The exact default must be designer-configurable.

Choose a safe small positive default consistent with project scale.

---

# 24. Surface resolution hierarchy

For a visual routing sample:

```text
1. use GridWorld surface/cell data when valid and suitable
2. otherwise perform world surface detection
3. if no supporting surface exists, mark the segment/sample unsupported
```

Use the real GridWorld public API.

Do not inspect private GridWorld data.

---

# 25. World surface detection fallback

Where GridWorld has no usable cell/surface data:

```text
trace/probe downward
```

using an appropriate project collision/query channel discovered by Codex.

Do not invent a permanent expensive trace pipeline without checking existing project conventions.

Surface detection is for visual placement only.

It does not determine puzzle validity.

---

# 26. Empty-space / chasm support

A missing surface must never make a puzzle link disappear.

If there is empty space:

```text
supported surface
──────────────┐

wire ─────────────────────────

                     ┌──────────── supported surface
```

the wire may bridge horizontally over the void at its current routing elevation.

This is a valid `Unsupported` horizontal segment.

Do not:

```text
drop the wire to the bottom of the chasm
fail the route
hide the link
require navigation
```

---

# 27. Prefer supported routes when reasonable

Surface support may influence route scoring.

A route that stays on surfaces is preferable to an equally reasonable route that crosses a large void.

But unsupported space is a **penalty**, not an invalid state.

Conceptual scoring term:

```text
UnsupportedLength * UnsupportedPenalty
```

If no supported alternative exists:

```text
use the unsupported bridge segment
```

The link must still render.

---

# 28. Surface cache

Do not perform duplicate surface traces for the same lattice coordinate.

Create a transient cache conceptually like:

```text
FParadoxPuzzleSurfaceSample
```

with data such as:

```text
bHasSurface
SurfaceZ
SurfaceNormal, if useful
source of data: GridWorld / trace
revision/generation
```

Key it by appropriate lattice/sample identity.

If several candidate routes sample the same coordinate:

```text
resolve once
reuse
```

---

# 29. Surface sampling density

Do not trace every few centimeters along every candidate.

Sample at routing-lattice meaningful intervals.

The routing pitch should naturally bound surface-query density.

Avoid thousands of traces during one selection rebuild.

---

# 30. Route representation

Working concept:

```text
FParadoxPuzzleWireRoute
```

Contains:

```text
PuzzleGraphLinkHandle
Direction
RoutePoints[]
RouteSegments[]
RoutingGeneration
```

Each segment should identify:

```text
Start
End
Axis
SegmentKind
Lane
```

---

# 31. Segment kinds

Working enum:

```text
EParadoxPuzzleWireSegmentKind
```

Suggested initial values:

```text
GroundSupported
GroundUnsupported
StructuralVertical
BridgeHorizontal
BridgeVertical
```

Simplify if fewer kinds are sufficient, but preserve the semantic distinction between:

```text
normal supported horizontal
unsupported horizontal
real level transition
anti-crossing bridge
```

---

# 32. Connection endpoints

Do not route every wire to the exact center of an Actor.

Create visual routing ports.

Ports are renderer concepts only.

They are not PuzzleSystem components.

They do not change gameplay interaction points.

---

# 33. Endpoint projection

For each source/target Actor:

1. determine an appropriate world anchor;
2. project/quantize it into the routing frame;
3. determine nearby surface/elevation;
4. create one or more visual ports around the Actor footprint/reference point.

Fallback anchor:

```text
relevant puzzle component location
then Actor location/bounds origin
```

If the project later exposes explicit puzzle visualization anchors, support them without making them mandatory.

---

# 34. Selected Actor ports

The selected Actor may have multiple incoming/outgoing links.

Do not collapse every route into one identical endpoint.

Generate distinct ports around the selected Actor.

This greatly reduces crossings.

---

# 35. Port side assignment

Classify external endpoints relative to the selected Actor.

Possible sides in the local routing frame:

```text
North
South
East
West
```

For multi-level endpoints, side classification remains primarily X/Y.

Assign each link to a suitable side based on:

```text
relative direction
input/output grouping
current congestion
```

Do not hardcode all input wires to one side if that would cause unnecessary long routes.

---

# 36. Spatial port ordering

Within one selected-Actor side:

```text
sort external endpoints spatially
assign ports in the same order
```

This is mandatory as a first anti-crossing heuristic.

Avoid:

```text
top endpoint -> bottom port
bottom endpoint -> top port
```

when equivalent ordering can prevent crossings.

The sort must be deterministic.

---

# 37. Remote endpoint ports

If several visible links terminate at the same remote Actor, allocate distinct nearby visual ports/lanes there too.

Do not force all segments into one exact target point if that creates overlap.

---

# 38. Router works on all links together

Do not route each connection in complete isolation.

When an Actor is selected:

```text
query all relevant links
    ↓
create one routing problem
    ↓
assign ports
    ↓
route links with awareness of already reserved lattice edges/lanes
```

The anti-crossing solution requires shared transient routing occupancy.

---

# 39. Lattice edge occupancy

Track visual occupancy at the edge level.

Working concepts:

```text
FParadoxPuzzleLatticeEdge
FParadoxPuzzleEdgeOccupancy
```

An edge knows:

```text
axis
endpoints
occupied lanes
wire handles using it
perpendicular crossing constraints
vertical riser presence
```

Do not model anti-crossing only as Actor/cell occupancy.

Wires occupy edges.

---

# 40. Parallel lanes

Parallel wires may share the same routing corridor using different lanes.

Example:

```text
────────────
────────────
────────────
```

Expose tuning:

```text
LaneSpacing
MaxLanesPerEdge
```

Lane offsets are visual.

Do not alter PuzzleSystem topology.

---

# 41. Overlap vs crossing

Treat separately.

## Parallel overlap

May be allowed through lane assignment.

## Perpendicular crossing

Strongly penalize/avoid.

Concept:

```text
CrossingPenalty >> ParallelLanePenalty
```

The renderer should prefer:

```text
slightly longer clean route
```

over:

```text
short route that crosses another wire
```

---

# 42. Candidate route generation

Do not search an unbounded 3D space for a mathematically optimal path.

Generate a bounded set of reasonable orthogonal candidates.

At minimum consider axis-order permutations where applicable:

```text
X -> Y -> Z
Y -> X -> Z
X -> Z -> Y
Y -> Z -> X
Z -> X -> Y
Z -> Y -> X
```

Not every candidate is required when source/target share coordinates.

Also allow limited lateral detour variants around the direct Manhattan corridor.

Expose a bounded parameter such as:

```text
MaxCandidatesPerLink
```

Do not allow combinatorial explosion.

---

# 43. Candidate route scoring

Score routes using visual cost, not navigation cost.

Conceptual terms:

```text
TotalLength
CornerCount
UnsupportedLength
ExistingWireConflict
ParallelLaneUse
PerpendicularCrossingCount
VerticalLength
BridgeUse
PortSideCost
```

A conceptual weighted cost:

```text
Cost =
    LengthWeight * TotalLength
  + CornerPenalty * CornerCount
  + UnsupportedPenalty * UnsupportedLength
  + LanePenalty * ParallelLaneUse
  + CrossingPenalty * PerpendicularCrossingCount
  + VerticalPenalty * VerticalLength
  + BridgePenalty * BridgeUse
```

Exact values are tunable.

Do not hardcode art tuning throughout the algorithm.

---

# 44. Routing priorities

Preferred qualitative order:

```text
1. no crossing
2. supported ground route when reasonable
3. short route
4. low corner count
5. parallel lane
6. modest detour
7. unsupported horizontal bridge over empty space
8. anti-crossing Z bridge when unavoidable
```

The exact scoring may cause supported/unsupported ordering to vary.

The key invariant is:

```text
crossings are strongly avoided
but route generation never fails only because a clean 2D route is unavailable
```

---

# 45. Greedy routing

Initial implementation may route links sequentially.

For each link:

```text
generate candidates
score against current routing occupancy
choose best
reserve edges/lanes
```

The ordering of links must be deterministic.

Prefer routing the most constrained links first when a useful constraint metric exists.

Otherwise use a stable deterministic ordering derived from graph/query order plus endpoint geometry.

---

# 46. Conflict detection

After the first routing pass, detect:

```text
perpendicular crossings
invalid lane overflow
vertical-riser collisions
unwanted duplicate geometry
excessive bridge use
```

Do not assume greedy routing always produces a clean solution.

---

# 47. Rip-up and reroute

Support bounded conflict resolution.

When two routes conflict:

```text
temporarily release one or a small set
reroute with updated occupancy
```

Working tuning:

```text
MaxRerouteAttempts
```

Do not perform unbounded optimization.

If the limit is reached, fall back to a bridge.

---

# 48. Anti-crossing bridge fallback

If a crossing cannot reasonably be eliminated in the ground plane:

```text
raise one wire to a temporary bridge layer
cross above the other wire
return to normal layer
```

Geometry remains 90-degree only.

Conceptual side view:

```text
      ┌────────────┐
──────┘            └──────
──────── other wire ───────
```

Expose:

```text
BridgeHeightOffset
BridgeApproachLength
```

or equivalent minimal tuning.

Use bridge only as a fallback.

Do not make every crossing a bridge without attempting clean routing.

---

# 49. Bridge routing Z is local

An anti-crossing bridge must not be confused with a floor transition.

It should:

```text
rise locally
cross
descend locally
```

Do not permanently move the remainder of the route onto the bridge layer.

---

# 50. Multi-floor structural transition placement

When endpoints differ in Z, generate candidate positions for the structural riser.

Do not hardcode only:

```text
always rise at source
```

or:

```text
always rise at target
```

Candidate axis ordering naturally provides several possibilities.

The scoring system chooses the cleaner route.

---

# 51. Walls and geometry occlusion

The renderer is allowed to draw wires through walls/solid geometry as a schematic.

Do not add physical obstacle avoidance based on walls unless explicitly required later.

The visual material/Custom Depth setup can make the wire readable through occluding geometry.

This is intentional.

A wall is not the same problem as a missing surface.

---

# 52. Default rendering primitive

Use the Unreal Engine built-in cube Static Mesh as the default/fallback wire primitive:

```text
/Engine/BasicShapes/Cube.Cube
```

Codex must verify the exact asset path/API in the project engine version rather than inventing it.

The cube is scaled and transformed to represent straight rectangular wire segments.

Do not require a user-authored mesh for the core implementation.

Do not use a cylinder as the default.

---

# 53. Why cube segments

The visual language is orthogonal and circuit-like.

A cube/rectangular prism:

```text
scales cleanly along X/Y/Z
matches 90-degree routing
works for ground and vertical segments
avoids spline deformation requirements
supports instancing efficiently
```

The renderer should therefore not require `USplineMeshComponent` for the first implementation.

A spline/polyline data representation may exist logically, but rendering can use straight cube instances/components.

---

# 54. Rendering implementation preference

Prefer an instanced solution where practical:

```text
UInstancedStaticMeshComponent
```

or `UHierarchicalInstancedStaticMeshComponent` only if scale justifies it.

Because the system usually renders only the selected Actor's local graph, plain ISM is likely sufficient.

Separate render batches may be required for:

```text
Input material
Output material
different visual states/material families if material architecture requires it
```

Do not optimize blindly.

Profile before introducing unnecessary renderer complexity.

---

# 55. Alternative component pool

If per-link independent dynamic material/state makes ISM impractical with current project material setup, a pooled `UStaticMeshComponent` solution is acceptable for the first implementation.

Rules:

```text
pool
reuse
disable when unused
avoid create/destroy churn
```

Do not spawn one Actor per segment.

Do not create permanent world Actors for wire pieces.

---

# 56. Material data

Expose configurable material references for:

```text
InputWireMaterial
OutputWireMaterial
```

Do not embed final colors in code.

The material should support at least a scalar/state parameter equivalent to:

```text
SignalStrength
```

where:

```text
0 = inactive
1 = active
```

If ISM is used, prefer Per Instance Custom Data when that produces a simpler/cheaper runtime update.

If component/MID rendering is used, set scalar parameters on the appropriate MID.

Choose based on actual engine/project patterns.

---

# 57. Default material fallback

The core system must not fail because custom art materials are absent.

If no project wire material is assigned:

```text
use a safe engine/default material fallback
```

and still render geometry.

Do not fabricate binary `.uasset` materials.

Document the properties designers can later assign.

---

# 58. Custom Depth support

Expose a renderer option enabling Custom Depth for wire geometry when required by the final material/post-process presentation.

Working setting:

```text
bRenderWiresInCustomDepth
```

and configurable stencil if needed.

Do not reuse the Selection Hover/Selected stencil ranges automatically unless the project explicitly decides to share them.

Puzzle wires are a separate presentation system.

Before assigning a stencil value, inspect the project's current stencil registry/ranges.

If no dedicated puzzle-wire stencil is required by the current material, do not invent one.

---

# 59. Segment transform

For each straight segment:

```text
Midpoint = (Start + End) / 2
Length = distance(Start, End)
Axis = X / Y / Z
```

Scale the cube so it forms a thin rectangular prism.

Expose:

```text
WireThickness
```

Use consistent world-space thickness.

Corner continuity must avoid visible gaps.

A small overlap or dedicated corner treatment is allowed if required.

---

# 60. Corner rendering

The simplest initial implementation may allow adjacent rectangular segments to overlap slightly at 90-degree corners.

This is acceptable if visually clean.

Do not require a separate corner mesh.

If future art requires dedicated elbows, keep route data extensible.

Default remains built-in cube only.

---

# 61. Input/output visual batching

At minimum maintain separate rendering ownership for:

```text
Input wires
Output wires
```

because they use different material/color families.

A `GateInfluence` rendered as Input remains semantically tagged in metadata.

Do not discard the originating graph link kind.

---

# 62. Runtime link-to-render mapping

Maintain a mapping:

```text
FPuzzleGraphLinkHandle
    -> rendered route / segment instance identities
```

This allows:

```text
LinkStateChanged
    -> update only that wire's instances/material state
```

without rebuilding unrelated geometry.

Opaque graph handles must be revalidated on topology changes.

---

# 63. Geometry recalculation triggers

Full/partial routing recalculation occurs only when geometry/topology meaningfully changes.

Examples:

```text
selected Actor changes
PuzzleSystem topology affecting selected graph changes
source/target Actor crosses routing-lattice threshold
source/target relevant Z changes significantly
surface cache invalidated for relevant region
renderer tuning requiring geometry changes
```

Do not reroute because:

```text
Emitter active changes
Gate opens/closes
EffectivePrimaryActive changes
Receiver active changes
```

Those are visual-state updates only.

---

# 64. Movable endpoints

Do not full-reroute every frame for small Actor motion.

Track endpoint routing coordinates.

Conceptual rule:

```text
Actor moves inside same routing coordinate
    -> optionally adjust local endpoint segment only

Actor crosses routing lattice coordinate
    -> reroute affected link(s)

Actor changes structural Z layer/height meaningfully
    -> reroute affected link(s)
```

Exact threshold must be configurable/derived from lattice pitch.

---

# 65. No permanent Tick for topology

The system may use a lightweight Tick while the overlay is visible if required to detect movable endpoint transform changes.

If used:

```text
disabled when no selected overlay
no graph scanning
no route rebuild unless threshold crossed
```

Prefer transform-change events/delegates where practical.

Do not add a global always-on Tick.

---

# 66. Routing must be pure-data friendly

Architect the expensive routing calculation so it can operate on plain C++ snapshot data.

Working pipeline:

```text
Game Thread
    BuildRoutingSnapshot

Pure calculation
    CalculateRoutes(Snapshot)

Game Thread
    ApplyRoutingResult
```

The core calculation must not require:

```text
AActor access
UActorComponent access
UWorld access
GridWorld UObject queries
PuzzleSystem UObject queries
render component mutation
```

during candidate generation/scoring/rerouting.

This makes it deterministic, testable, and thread-compatible.

---

# 67. Routing snapshot

Working concept:

```text
FParadoxPuzzleRoutingSnapshot
```

Contains copied/value data such as:

```text
RoutingGeneration
selected Actor anchor
link endpoint positions
link direction/kind
grid reference transform
routing pitch
surface samples/cache subset
routing settings
previous occupancy when useful
```

Use value types.

Do not store unsafe raw UObject access requirements inside worker calculation.

---

# 68. Routing result

Working concept:

```text
FParadoxPuzzleRoutingResult
```

Contains:

```text
RoutingGeneration
routes[]
diagnostics
```

No render component pointers.

No direct Actor mutation.

---

# 69. Multithreading policy

The router must be **thread-compatible by design**.

Actual async execution may be enabled if profiling shows meaningful Game Thread cost.

If implemented async:

```text
Game Thread:
    gather UObject/world data
    resolve surface data
    build snapshot

Worker thread:
    candidate generation
    route scoring
    edge/lane occupancy
    crossing detection
    rip-up/reroute
    bridge choice

Game Thread:
    validate generation
    apply result to renderer
```

Do not access gameplay UObjects from arbitrary workers.

---

# 70. Async generation/revision

Every routing request must have a monotonically increasing generation/revision.

Working concept:

```text
uint64 RoutingGeneration
```

Case:

```text
Actor A selected
Generation 10 starts

Actor B selected
Generation 11 starts

Generation 10 completes later
```

Expected:

```text
discard Generation 10 result
```

Never apply stale async routes.

---

# 71. Cancellation policy

Do not require blocking worker cancellation for correctness.

It is acceptable to let an obsolete pure-data task finish and discard its result using `RoutingGeneration`.

Use cancellation only if the chosen UE task API supports it cleanly and it materially reduces cost.

Do not introduce complicated synchronization merely to cancel tiny tasks.

---

# 72. Anti-crossing coordination and threading

Do not route every wire independently on separate workers without shared coordination.

The final occupancy/lane/crossing solution is globally coupled within the selected subgraph.

A safe first async model is:

```text
one worker task
    routes all wires sequentially/coordinately
```

This already removes Game Thread blocking.

Future optimization may parallelize candidate generation, but is not required.

---

# 73. Surface detection and threads

World surface traces and UObject/GridWorld queries should remain on the Game Thread unless the actual verified engine/project API supports a safe async query path.

Preferred first design:

```text
Game Thread
    resolve/cache surface samples

Worker
    consume immutable samples
```

Do not call ordinary UObject/world query APIs from worker code based on assumption.

---

# 74. Performance boundaries

Normal expected case:

```text
one selected Actor
small local set of graph links
bounded candidate count
bounded reroute attempts
cached surfaces
event-driven state updates
```

This should remain inexpensive.

Expose tuning such as:

```text
MaxCandidatesPerLink
MaxRerouteAttempts
MaxLanesPerEdge
```

to make worst-case cost bounded.

Do not perform unbounded global optimization.

---

# 75. Rendering cost

Avoid:

```text
one Actor per wire segment
component create/destroy churn
recreating all material instances on state events
rebuilding route on signal events
```

Prefer:

```text
ISM or pooled components
persistent route-to-render mapping
per-instance custom data or cached MID
```

---

# 76. Debugging

Follow root project debug rules.

Provide local debug enable:

```text
bEnableDebug
```

and the owning module's global debug kill switch.

Debug should make it possible to inspect:

```text
selected Actor
number of graph links
routing generation
route points
port assignment
segment kinds
supported/unsupported state
lanes
crossings detected
reroute attempts
bridges inserted
surface cache hits/misses
routing duration
discarded stale async results
```

Visual debug may draw:

```text
routing lattice points
ports
route points
segment categories
bridge transitions
```

but must be disabled by default.

Player-facing wires are not debug drawing.

---

# 77. Logging

Use the Paradox gameplay module's existing log category and macros.

No committed `LogTemp`.

Useful warnings:

```text
selected Actor graph query failed unexpectedly
graph handle stale during state update
surface resolution failed and unsupported fallback used
routing exceeded reroute budget and bridge fallback was used
render instance mapping inconsistent
async result discarded due to stale generation, only when verbose debug is enabled
```

Do not spam logs for normal signal state changes.

---

# 78. Unreal Insights

Make meaningful expensive paths measurable.

Useful scopes:

```text
ParadoxPuzzleOverlay_QuerySelectedGraph
ParadoxPuzzleOverlay_BuildRoutingSnapshot
ParadoxPuzzleOverlay_ResolveSurfaceSamples
ParadoxPuzzleOverlay_CalculateRoutes
ParadoxPuzzleOverlay_ApplyRoutingResult
```

Do not instrument trivial getters.

If async is added, keep event names stable and searchable.

---

# 79. Suggested project structure

Follow existing module organization first.

If no stronger pattern exists:

```text
Public/
└── PuzzleOverlay/
    ├── ParadoxPuzzleCircuitRendererComponent.h
    ├── ParadoxPuzzleCircuitTypes.h
    └── ParadoxPuzzleWireRouter.h

Private/
└── PuzzleOverlay/
    ├── ParadoxPuzzleCircuitRendererComponent.cpp
    ├── ParadoxPuzzleWireRouter.cpp
    ├── ParadoxPuzzleSurfaceResolver.cpp
    └── Tests/
        ├── ParadoxPuzzleWireRouterTests.cpp
        └── ParadoxPuzzleCircuitRendererTests.cpp
```

The pure router may be a non-UObject C++ type if that best supports testability/thread compatibility.

Do not create folders/classes that are not actually required.

---

# 80. User/designer configuration

Expose useful tuning, grouped clearly.

## Visibility

```text
bShowPuzzleConnectionsWhenSelected
```

## Geometry

```text
RoutingPitchOverride, only if needed
GroundWireHeightOffset
WireThickness
LaneSpacing
MaxLanesPerEdge
BridgeHeightOffset
```

## Routing

```text
MaxCandidatesPerLink
MaxRerouteAttempts
CornerPenalty
UnsupportedPenalty
LanePenalty
CrossingPenalty
VerticalPenalty
BridgePenalty
```

Do not expose every internal variable.

Provide safe defaults.

## Rendering

```text
WireMesh
InputWireMaterial
OutputWireMaterial
bRenderWiresInCustomDepth
```

Default `WireMesh`:

```text
/Engine/BasicShapes/Cube.Cube
```

If a property is left unset, resolve the engine cube as fallback rather than forcing designers to assign it manually.

---

# 81. Default cube requirement

The first implementation must work with **no custom mesh asset provided by the user**.

Use the engine cube as the default straight-segment primitive.

Do not generate a custom mesh asset.

Do not ask the user to author a cylinder.

Do not make a spline mesh asset mandatory.

---

# 82. Material asset policy

The user may later provide project-specific materials.

The code must expose material slots but remain functional with engine/default fallback material.

Do not create `.uasset` materials through source code.

Do not hardcode final input/output colors.

---

# 83. Renderer lifecycle

Required cleanup:

```text
selection changes
    -> clear old rendered routes

deselect
    -> clear rendered routes

selected Actor destroyed
    -> clear safely

graph topology invalidates selected graph
    -> rebuild/clear as appropriate

WorldState reset
    -> selection clears
    -> overlay clears

world teardown
    -> release delegates/tasks/render resources safely
```

Async callbacks must tolerate teardown.

---

# 84. Delegate cleanup

Every binding to:

```text
Selection events
PuzzleSystem Graph events
World lifecycle events, if any
```

must have symmetrical cleanup.

Do not leave callbacks bound after component/world destruction.

---

# 85. State-event update race with async routing

A graph link may change active state while an async route is being calculated.

Do not restart routing for this.

Preferred model:

```text
route snapshot captures topology/geometry
state updates continue to update latest state cache
async route returns
apply geometry
apply latest current state to newly created instances
```

Do not apply stale brightness from the route snapshot if a newer graph state exists.

---

# 86. Topology-event race with async routing

If topology changes during async routing:

```text
increment RoutingGeneration
rebuild snapshot
old result becomes stale
```

Do not merge stale topology results.

---

# 87. Actor-movement race with async routing

If endpoint movement crosses the reroute threshold during calculation:

```text
increment RoutingGeneration
start new route request
discard old result
```

Small motion inside the same routing coordinate may be handled locally without generation invalidation if implementation supports it safely.

---

# 88. Required routing tests

The pure router must have automated tests independent of rendering where practical.

## Test 1 — Simple same-level L route

```text
Source (0,0,Z)
Target (5,4,Z)
```

Expected:

```text
orthogonal route
90-degree corners only
no diagonal segment
```

## Test 2 — Straight X

Same Y/Z.

Expected one straight horizontal route.

## Test 3 — Straight Y

Same X/Z.

Expected one straight horizontal route.

## Test 4 — Multi-level

Different Z.

Expected:

```text
at least one StructuralVertical segment
90-degree transitions
```

## Test 5 — No navigation path

No GridWorld traversal/path exists.

Expected:

```text
route still generated
```

Router must not call navigation pathfinding.

## Test 6 — Missing GridWorld cells

Intermediate visual lattice coordinates have no real Grid cells.

Expected:

```text
route still generated
```

## Test 7 — Empty-space bridge

Surface absent along part of candidate.

Expected:

```text
GroundUnsupported segment
or equivalent unsupported horizontal metadata
route remains valid
```

## Test 8 — Supported detour preferred

Two comparable candidates:

```text
short candidate crosses large void
slightly longer candidate stays supported
```

With configured penalty:

```text
supported candidate wins
```

## Test 9 — Void unavoidable

All candidates require unsupported segment.

Expected:

```text
route still generated
```

## Test 10 — Two parallel wires

Expected:

```text
shared corridor allowed through separate lanes
no destructive overlap
```

## Test 11 — Perpendicular crossing avoidable

Expected:

```text
router chooses detour
no crossing
```

when within routing budget.

## Test 12 — Crossing unavoidable

Expected:

```text
WireBridge inserted
```

after bounded reroute attempts.

## Test 13 — Bridge semantics

Expected bridge contains:

```text
BridgeVertical up
BridgeHorizontal
BridgeVertical down
```

and is distinct from StructuralVertical.

## Test 14 — Port ordering

Three endpoints on same side.

Expected selected-Actor ports preserve spatial ordering and avoid unnecessary crossing.

## Test 15 — Deterministic routing

Same snapshot/settings.

Expected identical route result/order.

## Test 16 — Candidate bound

Large/problematic case.

Expected:

```text
MaxCandidatesPerLink respected
MaxRerouteAttempts respected
```

No unbounded execution.

---

# 89. Required renderer integration tests

## Test 17 — Selection builds overlay

Select Actor with graph links.

Expected:

```text
graph queried
routes rendered
```

## Test 18 — Deselection clears

Expected no remaining rendered wire geometry.

## Test 19 — Input/output separation

Incoming links use input render family.

Outgoing links use output render family.

## Test 20 — Primary effective active changes

```text
EffectivePrimaryActive false -> true
```

Expected:

```text
brightness/state changes
routing generation unchanged
geometry unchanged
```

## Test 21 — Gate closes

Primary raw remains active.

Gate closes.

Expected:

```text
PrimarySignal output becomes visually inactive
no reroute
```

## Test 22 — GateInfluence state

Gate input changes.

Expected:

```text
only affected gate wire visual state updates
```

unless aggregate state also changes downstream PrimarySignal state.

## Test 23 — Same Emitter / different Controllers

One outgoing link effective active, another inactive.

Expected two wires with different brightness despite same source Emitter.

## Test 24 — Topology change

Selected Actor graph topology changes.

Expected structural rebuild.

## Test 25 — Unrelated topology change

If affected data allows filtering:

Expected no unnecessary selected overlay rebuild.

A full selected-subgraph rebuild is acceptable only if implementation intentionally uses coarse invalidation and documents it.

## Test 26 — Actor moves within routing cell

Expected no full reroute.

## Test 27 — Actor crosses routing coordinate

Expected affected route reroute.

## Test 28 — WorldState reset

Expected overlay clears with selection.

## Test 29 — Default mesh

No user mesh assigned.

Expected engine cube renders wires.

## Test 30 — No custom materials

No project materials assigned.

Expected fallback material renders valid geometry.

## Test 31 — Async stale result

Generation A starts.

Generation B supersedes it.

A completes later.

Expected A discarded.

## Test 32 — State change during async route

Signal changes while route task runs.

Expected returned geometry uses latest signal state when applied.

---

# 90. Implementation milestones

Codex must implement incrementally.

Stop after the milestone explicitly requested by the user.

## Milestone 0 — Investigation

Inspect:

```text
Selection API
PuzzleSystem Graph Query API
GridWorld reference-frame APIs
GridWorld surface data APIs
WorldState selection reset flow
project collision channels
rendering conventions
module dependencies
current Custom Depth allocations
```

Determine exact type names and API contracts.

Compile baseline.

No feature implementation yet.

## Milestone 1 — Pure routing types and same-level orthogonal router

Implement:

```text
routing coordinate
wire route
route segments
candidate generation
90-degree X/Y routing
deterministic scoring
bounded candidates
```

No rendering.

No surface detection.

No async.

Automated pure C++ tests.

Compile.

## Milestone 2 — 3D/multi-level routing

Add:

```text
Z-aware candidates
StructuralVertical segments
axis-order candidate permutations
vertical cost
```

Still pure-data.

Tests.

Compile.

## Milestone 3 — Surface resolver and void support

Implement Game Thread surface snapshot/cache:

```text
GridWorld first
world surface detection fallback
unsupported samples
GroundSupported
GroundUnsupported
```

Feed immutable surface data into pure router.

Tests.

Compile.

## Milestone 4 — Anti-crossing routing

Implement:

```text
visual ports
port side assignment
spatial port ordering
lattice edge occupancy
parallel lanes
crossing detection
greedy routing
bounded rip-up/reroute
WireBridge fallback
```

Tests for clean routing and unavoidable crossing.

Compile.

## Milestone 5 — Renderer core

Implement renderer using default:

```text
/Engine/BasicShapes/Cube.Cube
```

Prefer:

```text
ISM
```

or pooled StaticMeshComponents if required by state/material architecture.

Implement:

```text
segment transforms
input/output render groups
fallback material
route-to-render mapping
selection lifecycle
cleanup
```

No final art assets required.

Compile.

## Milestone 6 — Puzzle runtime state visualization

Bind:

```text
LinkStateChanged
TopologyChanged
```

Implement:

```text
PrimarySignal brightness from EffectivePrimaryActive
GateInfluence brightness from GateInputActive
per-link state update without reroute
latest-state application after geometry rebuild
```

Compile and test.

## Milestone 7 — Movable endpoint invalidation

Implement:

```text
endpoint coordinate tracking
reroute threshold
same-cell/local update policy
affected-link reroute
```

No always-on expensive scan.

Compile.

## Milestone 8 — Async-ready / optional async routing

First profile synchronous router with Unreal Insights.

The pure-data router must already be thread-compatible.

If cost justifies async or the user explicitly requests it:

```text
Build snapshot on Game Thread
Run CalculateRoutes on UE worker task
Apply result on Game Thread
RoutingGeneration stale-result protection
```

Do not move unsafe UObject/world queries to workers.

Compile and test race cases.

## Milestone 9 — Custom Depth and presentation options

Inspect project stencil allocation.

Implement optional wire Custom Depth only if required by current presentation.

Do not collide with selection stencil ranges.

Expose final rendering/tuning settings.

Compile.

## Milestone 10 — Lifecycle hardening, docs, final profiling

Validate:

```text
selection switch
deselect
selected Actor destruction
linked Actor destruction
graph topology invalidation
WorldState reset
world teardown
async stale tasks
material fallback
mesh fallback
surface cache cleanup
delegate cleanup
```

Update Docs.

Profile representative heavy selected graph.

Review diff.

Final compile.

---

# 91. User-authored assets

The core task requires **no user-authored mesh**.

Default wire mesh:

```text
/Engine/BasicShapes/Cube.Cube
```

Optional future assets the user may provide:

```text
Input wire material
Output wire material
custom straight-segment mesh
custom corner/elbow art
custom bridge material/style
```

None are mandatory for first functional implementation.

Codex must not block implementation waiting for these assets.

---

# 92. Documentation

Add/update ParadoxGameplay `Docs`.

Explain:

```text
purpose
dependency on PuzzleSystem Graph Query
selection lifecycle
Input vs Output semantics
EffectivePrimaryActive brightness rule
GateInfluence brightness rule
GridWorld as reference frame only
Visual Routing Lattice
no navigation/pathfinding dependency
surface detection
void behavior
multi-level Z routing
ports
lanes
crossing avoidance
rip-up/reroute
bridge fallback
default cube renderer
material configuration
state events vs rerouting
movable endpoint policy
thread-compatible snapshot architecture
optional async routing
debugging
performance tuning
```

Do not place Codex instructions inside user docs.

---

# 93. Forbidden shortcuts

Do not:

```text
scan APuzzleController directly instead of Graph Query
recompute PuzzleSystem semantics
route based on raw Emitter state for PrimarySignal brightness
use GridWorld FindPath
require a valid nav path
treat blocked cells as route failure
treat missing Grid cells as route failure
treat missing surface as route failure
drop void wires to the bottom of a chasm
use diagonal segments
use smooth spline curves
route every link independently with no shared occupancy
allow arbitrary crossings when a clean bounded alternative exists
use bridge as first choice
reroute on every signal state change
full-reroute every frame for movable Actors
spawn one Actor per segment
require custom mesh assets
use a cylinder as default
depend on PCG
perform unsafe UObject access on worker threads
apply stale async results
use unbounded candidate search
use unbounded reroute loops
add Paradox wire concepts to PuzzleSystem
add Paradox wire concepts to GridWorld
make WorldState depend on renderer classes
use LogTemp in committed code
```

---

# 94. Definition of Done

The complete system is established only when:

- it lives in the Paradox project gameplay layer;
- it consumes PuzzleSystem Graph Query rather than Controller internals;
- only the selected Actor's relevant graph is rendered by default;
- incoming Primary and Gate relationships render as Input;
- outgoing Primary relationships render as Output;
- PrimarySignal brightness uses `EffectivePrimaryActive`;
- GateInfluence preserves its individual gate state;
- signal-state changes update visuals without rerouting;
- topology/geometry changes can reroute;
- GridWorld is used as a coordinate/reference-frame source, not as navigation pathfinding;
- no valid navigation path is required;
- visual lattice coordinates may exist without real GridWorld cells;
- surface data uses GridWorld first and world detection fallback;
- missing surface produces supported logical rendering through an unsupported horizontal segment;
- wires do not drop to chasm bottoms by default;
- endpoints on different vertical levels create structural Z transitions;
- all geometry is orthogonal with 90-degree corners;
- selected Actor ports are distinct and spatially ordered;
- parallel wires can use lanes;
- perpendicular crossings are strongly avoided;
- conflict routing is bounded;
- rip-up/reroute is bounded;
- unavoidable crossings use a local Z bridge fallback;
- bridge Z and structural Z are semantically distinct;
- the default wire primitive is the engine cube;
- no custom wire mesh is mandatory;
- rendering does not require one Actor per segment;
- route-to-graph-link mapping supports targeted state updates;
- movable endpoints do not force full rerouting every frame;
- pure routing is implemented with value data and is thread-compatible;
- optional async routing uses Game Thread snapshot -> worker calculation -> Game Thread apply;
- stale async routing results are discarded by generation;
- world/surface/UObject queries remain on safe threads;
- WorldState reset clears the overlay through selection lifecycle;
- debug and logging follow project rules;
- tests cover routing, voids, multi-level, crossings, bridges, state updates, and lifecycle;
- Docs are updated;
- representative performance is measurable in Unreal Insights;
- the affected target compiles successfully;
- the final diff has no unrelated changes.

If the affected target does not compile, the task is not finished.
