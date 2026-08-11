# Puzzle Circuit Overlay

The puzzle circuit overlay renders the read-only PuzzleSystem relationships of the currently
selected Actor. `UParadoxPuzzleCircuitRendererComponent` is a native default subobject of
`AParadoxPlayerController`; it observes `UParadoxSelectionComponent` and never creates a second
selection authority or changes puzzle state.

## Opt-in and graph semantics

Enable `bShowPuzzleConnectionsWhenSelected` on an Actor's `UParadoxSelectableComponent`. It is
enabled natively on Pressure Plate and Vertical Barrier and disabled by default, including on
Chrono Spawn.

The renderer queries only `UPuzzleGraphSubsystem::QueryActorGraph`:

- `IncomingPrimaryLinks` and `IncomingGateLinks` are Input wires;
- `OutgoingPrimaryLinks` and `OutgoingGateLinks` are Output wires.

An outgoing gate wire connects the selected gate emitter to the primary emitter Actor whose input
the gate controls. It retains `GateInfluence` identity and gate-input signal state; it is not
duplicated once per Receiver because PuzzleSystem models that relationship per primary binding.

Primary wires use `bEffectivePrimaryActive`. Gate wires use the exact gate input's
`bGateInputActive`; their public route metadata also retains gate mode, validity, admission result,
and Controller result. A graph state notification changes only `PerInstanceCustomData[0]` on the
instances belonging to that link. It does not query surfaces or reroute.

## Routing and surfaces

The UObject-free `FParadoxPuzzleWireRouter` consumes a copied snapshot and produces deterministic
X/Y/Z-only routes. `FParadoxPuzzleRoutingSettings::Algorithm` selects the strategy:

- `DistributedRepulsive` is the default network-wide strategy and negotiates distance between wires;
- `OrderedBundles` remains available for layouts that benefit from intentional shared corridors;
- `LegacyIndependent` preserves the previous per-wire router for comparison and rollback and is
  deprecated.

`OrderedBundles` runs the bounded pipeline `route -> bundle -> order -> lane -> nudge`. It first
generates Manhattan/3D candidates for all 36 source/target combinations of the six endpoint faces.
Each face pair retains a direct candidate and an alternate corridor before the remaining
`MaxOrderedBundleCandidatesPerLink` budget is filled by deterministic cost. The initial independent
minimum is refined by at most `MaxBundleOptimizationPasses` global passes. Cost includes length,
bends, unsupported surface, vertical travel, crossings, congestion and bridge pressure, while
`BundleReuseBonus` attracts compatible wires onto a shared directed unit edge only once per edge.
An over-capacity edge receives congestion cost and no reuse bonus.

Shared directed edges become maximal bundles. A bounded adjacent-swap pass minimizes lane-order
inversions at merge and split points. Lanes are placed symmetrically around the bundle centerline,
then maximal corridors are nudged while preserving 90-degree geometry. Port coordinates are
resolved after lane ordering so endpoint order agrees with the adjacent bundle. Crossing checks,
bounded reroute and a local Z bridge run only after ordering and nudging. Ordinary elevation
changes remain `StructuralVertical`; bridge verticals have a separate segment kind.

`DistributedRepulsive` preserves Wire Boxes, N+1 ports and terminal composition, but deliberately
does not reward bundles. It ranks all 36 face pairs at the unsubdivided GridWorld resolution, then
generates fine candidates only for the best bounded subset. A single-link graph refines up to 8
pairs, a subdivided multi-link graph up to 12, and an unsubdivided multi-link graph up to 18. If
that subset is not routable, generation falls back to the complete face-pair set. It first selects
each wire by geometric cost, then performs at most `MaxNegotiationPasses` deterministic
rip-up/reroute passes. One global occupancy/proximity context is built for the generation.
Candidate evaluation ignores the current wire inside that context, while accepted changes update
its exact occupancy and spatial indices incrementally. Passes rotate the stable evaluation order
and charge exact sharing, crossings, nearby parallel/perpendicular edges and congestion history
accumulated during the current solve. The best complete generation is retained
lexicographically by crossings, shared length, proximity, bridge estimate, bends, unsupported
surface and length. History never survives the routing generation or a later selection.

After negotiation, the strategy applies the existing N+1 face ports, bounded orthogonal nudging,
lanes only for unavoidable exact sharing, and finally the existing Z bridge fallback. A nudge is
accepted only when it lowers proximity without introducing crossings, extra sharing, bends,
unsupported distance, endpoint penetration, oblique segments or a new Z transition. Signal-only
changes still update instance custom data without invoking either solver.

### Runtime optimization pipeline

Fine subdivision increases path coordinates, but it no longer multiplies every expensive phase
equally. `DistributedRepulsive` applies these bounded optimizations:

- candidate length, bend, vertical, unsupported, bounds and spatial-cell metrics are calculated
  once and reused throughout negotiation;
- coarse face ranking and fine subset generation discard low-value face/corridor work before the
  solve, with the deterministic full-set fallback described above;
- exact sharing uses an edge-occupancy map; proximity and candidate-crossing discovery use spatial
  buckets instead of scanning every selected edge or route;
- the global context is updated incrementally and rebuilt at most once more when restoring a best
  pass that differs from the final working pass;
- single-wire graphs and already conflict-free initial generations skip negotiation and repulsive
  nudging;
- repeated geometry notifications in one frame are coalesced into one next-tick subgraph rebuild,
  while identical topology/geometry/tuning snapshots can reuse a cached routing result;
- Input and Output transforms are submitted in one `AddInstances` batch per persistent ISM;
  custom data is written without per-instance render-state invalidation, then each ISM is dirtied
  once.

The router consumes only copied value data and has no UObject or World access. `ExecutionMode`
selects `Standard` or `Multi-Thread`; Multi-Thread is the default and runs the same public router
dispatcher through `UE::Tasks`, so every current and future strategy shares the execution policy.
Puzzle Graph queries, Actor/Wire Box resolution, GridWorld reads, fallback traces and ISM
publication remain on the Game Thread. A component runs at most one solve at a time: newer requests
cooperatively cancel and supersede the active solve, repeated requests coalesce to the latest world
state, and generation/request identity rejects every stale completion. Deselection clears the
presentation immediately even while the worker is exiting.

The component reports preparation, queue, solve, apply and total time independently. This makes a
remaining Game Thread surface-preparation hitch distinguishable from router cost without moving
World or physics access onto a worker. Endpoint reroutes for the same selected Actor keep the old
wires until the replacement is ready; selection replacement never displays the previous Actor's
overlay. Immediately before apply, current Puzzle Graph signal state is reconciled onto the new
geometry so signal changes during a solve cannot publish stale custom data.

Only the source and target Wire Boxes of each link are protected from penetration. Other Actors in
the selected network are deliberately not routing obstacles. Endpoint bounds, terminal
orthogonality and source/target volume exclusion are hard invariants for all strategies. The
renderer receives final continuous segments and never adds an unscored connector or an oblique
segment.

`BendPenalty`, `BundleReuseBonus`, `CrossingPenalty`, `LanePenalty`, and the bounded pass counts are
the relevant Ordered Bundles tuning controls. `CornerPenalty`, the compact-route settings,
`PortSpacing`, and `SelectedPortRadius` remain serialized only for compatibility with the legacy
implementation or older assets. `RejectedNetworkBoundsCandidateCount` and
`NetworkBoundsFallbackCount` are legacy-only diagnostics.

### Tuning reference

The Details-panel tooltip is the authoritative short description. As a practical guide:

The Details panel uses `EditConditionHides`: selecting an algorithm shows only its private tuning.
Shared geometry, surface, crossing, vertical, bridge, detour and port controls remain visible.
`BendPenalty` is shared by Ordered and Distributed; `LanePenalty` is legacy/Ordered only.

| Setting | Default | Increase it when... | Main trade-off |
| --- | ---: | --- | --- |
| `LaneSpacing` | `8 cm` | bundled wires are visually too close | wider bends and more terminal room |
| `MaxLanesPerEdge` | `4` | more wires should share one corridor | denser bundles and harder ordering |
| `BridgeHeightOffset` | `25 cm` | bridges do not clear the crossed wire visually | taller Z excursions |
| `MaxRerouteAttempts` | `4` | the router needs more alternate corridors or crossing retries | higher synchronous CPU cost |
| `MaxOrderedBundleCandidatesPerLink` | `128` | difficult layouts need more face or corridor alternatives | CPU and temporary memory scale with link count |
| `MaxBundleOptimizationPasses` | `4` | global bundle choices stop before stabilizing | higher synchronous CPU cost |
| `MaxMetroOrderingPasses` | `8` | merge or split lane inversions remain reducible | more ordering work |
| `BendPenalty` | `100` | wires contain unnecessary corners | routes may become longer |
| `BundleReuseBonus` | `35` | nearby compatible wires fail to form bundles | excessive values can justify detours |
| `MaxDistributedCandidatesPerLink` | `128` | the repulsive solver needs more face/corridor alternatives | CPU and temporary memory increase per wire |
| `MaxNegotiationPasses` | `6` | congestion has not stabilized within the current solve | higher synchronous CPU cost |
| `LengthWeight` | `1` | distributed routes become too long relative to spacing benefits | higher values reduce willingness to detour |
| `SharedEdgePenalty` | `10000` | distributed wires still overlap exact lattice edges | narrow corridors may need fallback lanes more often |
| `HistoricalCongestionWeight` | `2000` | the same shared corridor survives several passes | excessive values can move several wires at once |
| `ProximityRadius` | `3 cells` | parallel wires remain visually clustered | a larger neighborhood increases solve work |
| `ProximityPenalty` | `250` | nearby wires do not separate enough | may trade length for spacing |
| `ProximityFalloffExponent` | `1.5` | tune whether repulsion is concentrated close to the wire | low values spread influence across the radius |
| `ParallelRunPenalty` | `25` | long parallel clusters remain preferable to brief close approaches | may encourage larger corridor separation |
| `PerpendicularProximityScale` | `0.25` | brief perpendicular near-misses need more/less influence | values near one treat them like parallel runs |
| `EndpointEscapeDistance` | `2 edges` | shared endpoints cannot fan in/out cleanly | larger values weaken repulsion near common endpoints |
| `VerticalProximityThreshold` | `50 cm` | different floors affect each other or low bridges are ignored | controls when Z-separated edges interact |
| `bEnableHierarchicalFacePairPruning` | `true` | disable it to retain all 36 fine face pairs | substantially higher solve CPU and temporary memory |
| `SingleLinkFineFacePairLimit` | `8` | a single wire needs more face alternatives | values up to 36 improve choice quality at additional cost |
| `SubdividedFineFacePairLimit` | `12` | subdivided layouts lose a useful face/corridor | the most sensitive quality/performance control at 2x2 or finer |
| `BaseResolutionFineFacePairLimit` | `18` | the unsubdivided lattice loses a useful face/corridor | more fine candidate generation |
| `bEnableSingleLinkFastPath` | `true` | disable it to retain detour alternatives for a single wire | slower isolated-wire solve |
| `bEnableConflictFreeNegotiationSkip` | `true` | disable only when diagnosing negotiation | normally result-preserving extra passes |
| `SpatialIndexLinkThreshold` | `8` | profiling shows bucket overhead on small graphs | performance-only; geometry must remain identical |
| `SpatialIndexEdgeThreshold` | `64` | profiling shows bucket overhead on small edge sets | performance-only; geometry must remain identical |
| `UnsupportedPenalty` | `4` per cm | wires should prefer sampled floor support | overly high values can produce long routes |
| `LanePenalty` | `10` | too many wires congest the same edges | weaker bundle reuse |
| `CrossingPenalty` | `100000` | avoidable planar crossings remain | can favor longer same-budget candidates |
| `VerticalPenalty` | `2` per cm | routes change elevation too readily | less flexibility in multi-level layouts |
| `BridgePenalty` | `5000` | routes rely too readily on the final bridge fallback | more detours before accepting a bridge |
| `EndpointClearance` | `25 cm` | a first or last bend crowds an Actor face | terminals extend farther into the scene |
| `MultiPortFanoutLength` | `75 cm` | several ports collapse together immediately | longer parallel terminal sections |
| `PortEdgeInset` | `8 cm` | outer ports sit too close to face corners | less usable span for N+1 distribution |

`GridCellSubdivision` controls the wire lattice resolution relative to the selected GridWorld
region. `None` (the default) uses one routing cell per GridWorld cell. `2 x 2` divides each cell
twice per axis, so a 100 x 100 cm GridWorld cell becomes four 50 x 50 cm routing cells. `4 x 4`
and `8 x 8` provide progressively finer presets. This subdivision is visual-routing data only: it
does not create GridWorld cells and never changes navigation, occupancy, reservations, or GridWorld
presentation. The effective X/Y pitch is derived at runtime from `GridTransform.CellSize` and is no
longer exposed as editable tuning. When no GridWorld region is available, the same subdivision is
applied to the world-aligned 100 cm fallback lattice.

Higher subdivisions increase the number of candidate coordinates and surface samples. Tuning
expressed in lattice cells, including `ProximityRadius` and `EndpointEscapeDistance`, consequently
covers a smaller world-space distance at finer subdivisions. Rendering-only controls such as `WireThickness` and
`GroundWireHeightOffset` never affect routing. Likewise, signal strengths affect only material
custom data. Deprecated properties under the `Legacy` category should not be used to tune
`OrderedBundles`.

### Endpoint bounds and `WireTarget`

Every wire starts on the source boundary and ends on the target boundary. The preferred lateral
face is the face directed toward the other Actor; X wins exact directional ties. The first segment
follows the source face's outward normal and the last approaches opposite the target face's outward
normal. Candidate routes are checked once before lane assignment and again after terminal
composition, lane offsets and normalization. Any final segment that enters either open endpoint
volume is rejected. These invariants outrank compactness, corner tier, lane, crossing, bridge,
surface, and length scoring.

By default, endpoint bounds are the union of direct Actor-owned, visible, non-hidden
`UStaticMeshComponent` and `USkeletalMeshComponent` bounds whose mesh asset is valid. Widgets,
billboards, collision-only volumes, child-Actor geometry, hidden meshes, and renderer/debug
components cannot enlarge this automatic boundary.

For explicit authoring, add one `UBoxComponent` to the endpoint Actor and add the ordinary
Component Tag `WireTarget` (this is an `FName`, not a Gameplay Tag). Collision, visibility, and
overlap settings on that box do not affect routing. A rotated box is projected into the selected
GridWorld routing frame and represented by its axis-aligned X/Y/Z bounds, so all terminal segments
remain orthogonal. Prefer exactly one tagged box. If several valid boxes are tagged, Data
Validation and runtime emit a warning and the component with the lexicographically first stable
component path is used; Unreal enumeration order is never semantic. If that selected box has
invalid extent, automatic visible-mesh bounds are used. If neither source is usable, the renderer
logs a diagnostic and falls back to the Actor Location as a point endpoint. Bounds metadata reports
`CustomWireTarget`, `VisibleMeshes`, or `PointFallback`.

Ports sharing one Actor face follow the adjacent bundle lane order, using remote tangential order
as the deterministic fallback. A face carrying exactly one link always uses its exact centre at
both source and target. With two or more links, the ports use the `N+1` rule over the complete face:
`EqualGap = FaceSpan / (N + 1)`, and port `i` is placed at
`FaceMin + (i + 1) * EqualGap`. Edge margins and inter-port gaps are therefore equal. Odd groups
retain one exact centre port and even groups straddle the centre symmetrically. `PortEdgeInset=8 cm`
is only a minimum safety margin: when it exceeds `EqualGap`, it clamps the two outer margins and
the remaining ports redistribute uniformly between them. The default
`EndpointClearance=25 cm`. A multi-link face also keeps its parallel terminal fan-out for
`MultiPortFanoutLength=75 cm` before connecting to the lattice, preventing distinct centre-out
ports from immediately collapsing into one visual bottleneck. A single-link face never receives
this extra fan-out.

The spacing therefore adapts to both face length and incident-wire count. The same rule applies to
source/output and target/input faces.
`PortSpacing` and `SelectedPortRadius` are retained solely for serialized compatibility and are no
longer read.
`FParadoxPuzzleWireRoute` exposes `SourcePort` and `TargetPort`, including bounds, side, position,
normal, clearance point, face-slot index/count, normalized distance from the face centre, and
whether the port used `WireTarget`.

The selected Actor's GridWorld region supplies the yaw frame and base X/Y cell size. If no region is
available, the overlay uses a world-aligned 100 cm base lattice. GridWorld is never asked to find a
path. On the Game Thread, every routing sub-cell sample is mapped back to the containing cell in
GridWorld's immutable snapshot, including blocked cells. Missing samples fall back to a downward
Visibility trace that ignores the endpoint Actors and renderer. A surface farther than
`MaxSurfaceSnapDistance` is rejected; the wire keeps its current height and is marked
`GroundUnsupported` rather than falling to a distant floor.

Endpoint root transforms and the selected `WireTarget` component transform are observed without
Tick. Transform notifications produced in one frame are deduplicated by Actor and processed in a
single next-tick batch. Because face choice and wire-to-wire cost are global, that batch reroutes the whole
selected subgraph under `OrderedBundles` and `DistributedRepulsive`. `LegacyIndependent` retains localized incident-face
rerouting. A structural graph event is first compared against the selected subgraph signature and
rebuilds only when that signature changes. Signal-only changes still update instance custom data
without routing.

## Rendering and materials

The component owns two persistent `UInstancedStaticMeshComponent`s, one for Input and one for
Output. Their visible owner is a transient presentation Actor because Unreal Controller Actors are
hidden in game; selection and renderer authority remain on `AParadoxPlayerController`. The
presentation Actor and its components have no collision, overlaps, navigation relevance, decals,
or shadows. The default mesh is `/Engine/BasicShapes/Cube.Cube`; optional Input and Output
materials may replace the Engine default material. Route transforms are added in one batch for
each direction, signal-strength custom data is assigned without per-instance render invalidation,
and the ISM render state is dirtied once after the batch.

Each instance has one custom-data float named by contract as `SignalStrength`:

- inactive: `0.15`;
- active: `1.0`.

A custom surface material can read `PerInstanceCustomData[0]` to control emissive intensity.
Custom Depth is enabled by default. Input wires use stencil `210`; Output wires use `220`. The
reserved ranges are Input `210-219`, Output `220-229`, Hover `230-239`, and Selection `240-249`.
Data Validation rejects category overlap or values outside the wire ranges.

The post-process asset remains level-authored:

`/Game/Vfx/VfxMasterMaterials/MM_PP_Outline`

The runtime renderer never modifies this asset or adds it to a camera. The `Paradox Outline`
material expression includes Input/Output in `CombinedMask` and `OutlineColor`; a material that
manually combines only the legacy masks must connect `PuzzleInputMask` and `PuzzleOutputMask`.

## API and lifecycle

Use `AParadoxPlayerController::GetPuzzleCircuitRendererComponent`, then
`GetDisplayedActor`, `GetRoutingGeneration`, `GetRenderedRoutes`,
`IsWireCalculationInProgress`, `GetActiveWireCalculationRequestId`, or
`GetActiveWireCalculationGeneration` for a read-only view.
`RefreshOverlay` explicitly revalidates the selected graph; `ClearOverlay` removes presentation
only. `OnWireCalculationStarted` and `OnWireCalculationFinished` are available as Blueprint and
native delegates and are always emitted on the Game Thread. A completion reports `Applied`,
`AppliedFromCache`, `Cancelled`, `Superseded`, or `Failed`, plus phase timings and whether geometry
was actually applied. Routes expose endpoint Wire Box source, chosen faces, bundle IDs, lane, nudge offset, and
topology/terminal/bridge/total corner counts. Result diagnostics expose bundle and reused-edge
counts, congestion, total reuse bonus, optimization/ordering pass counts, inversions before/after,
nudged segments, and crossings resolved by ordering, reroute, or bridge. Distributed diagnostics
add executed/best negotiation pass, per-wire reroute count, current/history edge usage, shared and
parallel-near lengths, proximity cost, coarse/pruned face-pair counts, fast-path wires, global
context builds, spatial queries/visits and the selected algorithm.

Deselect, selection replacement, selected-Actor destruction, selection reset at WorldState
restore start, component EndPlay, and world teardown clear instances, route mappings, endpoint
delegates, and surface generations symmetrically. They do not modify PuzzleSystem, cancel Gameplay
Actions, or affect GridWorld navigation/presentation layers.

Local debug is off by default and requires both the component flag and the module gate:

```text
Paradox.PuzzleOverlay.Debug 1
```

The bounded synchronous pure-router 32-link Ordered Bundles benchmark reports its current P95 through
`Paradox.PuzzleOverlay.OrderedBundles.Benchmark.ThirtyTwoLinks`. The Distributed suite also runs a
subdivided twelve-link fixture and verifies bounded candidate counts, coarse/fine pruning,
spatial-index use and at most two context builds. A separate test compares Standard and worker
geometry for all three strategies and deterministically cancels an active component worker through
deselection. Unreal Insights scopes cover graph query, preparation, queueing, worker/standard
solve, cancellation, surfaces, distributed solve/pass/proximity construction, reroute, and apply.

## Troubleshooting

- No wires: verify the selected Actor opts in and the relevant Puzzle Controller has initialized
  graph links. Empty/null interaction or Smart Object configuration does not create Puzzle links.
- Geometry but no circuit color: enable `r.CustomDepth=3`, configure the outline post process, and
  recompile/re-add the native material node if its cached shader predates the Input/Output pins.
- Active and inactive look identical: assign a wire material that reads
  `PerInstanceCustomData[0]`; the Engine fallback guarantees geometry, not emissive distinction.
- A wire is unsupported over a void: this is intentional. Increase `MaxSurfaceSnapDistance` only
  when the lower surface is genuinely part of the circuit presentation.
- A wire still uses the Actor Location: ensure exactly one directly owned `UBoxComponent` has the
  Component Tag `WireTarget`, or give the Actor a direct Static/Skeletal Mesh with valid bounds.
- Data Validation reports multiple `WireTarget` boxes: remove the tag from all but one for clear
  authoring. Runtime deterministically chooses the first component path and logs the choice.
- Increasing `CornerPenalty` has no effect under Ordered Bundles: use `BendPenalty`; use
  `BundleReuseBonus`, congestion/crossing penalties and the bounded pass counts for global tuning.
- Ordered and legacy routes differ: temporarily select `LegacyIndependent` for A/B comparison,
  but do not author new content around its obstacle or corner-tier heuristics.
- Distributed wires remain too close: start with `ProximityRadius` and `ProximityPenalty`; use
  `ParallelRunPenalty` for long side-by-side runs. Raise `SharedEdgePenalty` only for exact overlap.
- Distributed routes detour too much: increase `LengthWeight`, reduce the relevant proximity
  penalty, or reduce `ProximityRadius`. `EndpointEscapeDistance` affects only wires sharing an
  endpoint and is the preferred control for clean fan-in/fan-out.
- Fine subdivision is fast but lost a desirable face choice: raise
  `SubdividedFineFacePairLimit`, or disable `bEnableHierarchicalFacePairPruning` for the full 36
  face pairs. Use the calculation timings to compare preparation cost against worker solve cost.
- Several wires visually converge immediately outside one face: increase `MultiPortFanoutLength`.
  This does not move the centre-out boundary ports or affect faces carrying a single link.
