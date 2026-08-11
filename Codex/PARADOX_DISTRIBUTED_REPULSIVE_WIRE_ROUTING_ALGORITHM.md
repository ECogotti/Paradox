# Paradox Puzzle Wiring — Distributed / Repulsive Wire Routing Algorithm Patch

## Purpose

Add a **new selectable wire-generation algorithm** to the already implemented Paradox Puzzle Wiring system.

This task concerns **only the algorithm used to generate wire routes**.

Do not rewrite or replace:

```text
PuzzleSystem Graph Query
Selection integration
wire rendering
cube segment rendering
materials
brightness/emissive updates
surface detection implementation
3D lattice implementation
GridWorld integration
multi-level support
structural Z transitions
void handling
Z bridge rendering
async/task infrastructure
WorldState/reset behavior
debug framework
existing routing algorithms
```

The new algorithm must coexist with the routing algorithms already implemented.

The designer/user must be able to choose the routing algorithm from the existing engine-facing configuration mechanism.

Working name for the new mode:

```text
DistributedRepulsive
```

or:

```text
NegotiatedCongestion
```

Use the naming style already present in the project.

Do not rename existing modes.

---

# 1. High-level goal

The existing Ordered-Bundle mode intentionally encourages several wires to share a common routing corridor.

This new algorithm must do the opposite.

Its visual objective is:

> Spread wires across the available routing space as much as reasonably possible.

The router must discourage:

```text
wire crossings
exactly shared routing edges
long clusters of close parallel wires
several wires using adjacent corridors for long distances
unnecessarily congested routing regions
```

while still respecting:

```text
orthogonal 90-degree routing
existing 3D lattice
existing surface/void behavior
existing multi-level behavior
Wire Box endpoint constraints
reasonable wire length
reasonable bend count
bounded runtime cost
```

The result should tend toward:

```text
Wire A  ───────────────

              Wire B  ───────────────

      Wire C  ───────────────
```

rather than:

```text
Wire A  ───────────────
Wire B  ───────────────
Wire C  ───────────────
```

when sufficient routing space exists.

---

# 2. Required algorithm option

Inspect the currently implemented routing-algorithm selector.

There may already be an enum/settings type equivalent to:

```text
EParadoxPuzzleWireRoutingAlgorithm
```

Do not invent a parallel selection system if one already exists.

Add one new selectable value equivalent to:

```text
DistributedRepulsive
```

Existing values and existing behavior must remain unchanged.

Conceptual engine-facing configuration:

```text
Wire Routing Algorithm
├── Existing Algorithm A
├── Existing Ordered Bundle Algorithm
└── Distributed Repulsive
```

Only the selected algorithm is used for route generation.

The new mode may reuse existing shared infrastructure:

```text
routing snapshots
lattice representation
surface data
Wire Box resolution
candidate generation utilities
segment simplification
bridge fallback
render result types
```

but its congestion/cost logic must remain isolated enough that selecting another algorithm reproduces that algorithm's previous behavior.

---

# 3. Algorithmic basis

The new mode combines four established routing/graph-drawing ideas:

```text
1. PathFinder negotiated-congestion routing
2. Edge-edge repulsion adapted as a lattice proximity field
3. Edge-disjoint / congestion-limited routing as a cost principle
4. Orthogonal edge nudging as a final spacing pass
```

Do not blindly copy algorithms intended for FPGA electrical design or force-directed free-space graph layouts.

Adapt their **routing principles** to the existing Paradox orthogonal 3D lattice.

---

# 4. Reference 1 — PathFinder / Negotiated Congestion

Primary paper:

**Larry McMurchie, Carl Ebeling — "PathFinder: A Negotiation-Based Performance-Driven Router for FPGAs"**

Sources:

```text
ACM:
https://dl.acm.org/doi/10.1145/201310.201328

PDF:
https://www.gstitt.ece.ufl.edu/courses/eel4720_5721/reading/pathfinder.pdf
```

Algorithmic idea to apply:

> Routing resources become increasingly expensive when several routes compete for them. Routing is repeated iteratively so wires negotiate for limited routing space instead of allowing early-routed wires to permanently dominate the solution.

For Paradox, the "routing resources" are the existing:

```text
3D lattice edges / corridors
```

not FPGA wires/switches.

The important PathFinder concepts to adapt are:

```text
current congestion cost
historical congestion cost
iterative rip-up and reroute
bounded negotiation passes
```

The new mode should therefore not route every wire once and permanently accept the result.

It should iteratively improve the complete selected-Actor wire set.

---

# 5. Reference 2 — Edge-Edge Repulsion

Reference:

**Chun-Cheng Lin, Hsu-Chun Yen — "A New Force-Directed Graph Drawing Method Based on Edge-Edge Repulsion"**

Sources:

```text
DOI / ACM:
https://dl.acm.org/doi/10.1109/IV.2005.10

Full text:
https://scholars.lib.ntu.edu.tw/bitstreams/792933ee-7fee-452b-af93-745f7977cafd/download
```

The original work uses edge-edge repulsion in a force-directed graph drawing context.

Do **not** implement the original force-directed geometry solver.

Paradox must preserve:

```text
orthogonal X/Y/Z segments
90-degree corners
routing lattice
Wire Box face normals
surface/void semantics
```

Instead adapt only the visual principle:

> Existing wire segments create a repulsive field around themselves.

In Paradox this becomes a discrete:

```text
Proximity Congestion Field
```

over the routing lattice.

---

# 6. Reference 3 — Edge-Disjoint Paths with Congestion

Reference:

**Julia Chuzhoy, Shi Li — "A Polylogarithmic Approximation Algorithm for Edge-Disjoint Paths with Congestion 2"**

Sources:

```text
arXiv:
https://arxiv.org/abs/1208.1272

ACM/JACM:
https://dl.acm.org/doi/10.1145/2893472
```

Relevant concept:

> The amount of path sharing on an edge can be treated as a bounded congestion resource.

Do not implement this paper's approximation algorithm.

Use it only as design guidance for the cost model:

```text
exact edge sharing
    -> highly undesirable

limited sharing
    -> possible only when geometry forces it

fully separated paths
    -> preferred
```

Paradox must **not** make edge-disjointness an absolute hard constraint because some layouts may not have enough space.

Use very high congestion cost instead of guaranteed failure.

---

# 7. Reference 4 — Orthogonal Edge Nudging

Reference:

**Tim Hegemann, Alexander Wolff — "A Simple Pipeline for Orthogonal Graph Drawing"**

Sources:

```text
arXiv:
https://arxiv.org/abs/2309.01671

PDF:
https://arxiv.org/pdf/2309.01671
```

Relevant pipeline:

```text
route
    ↓
order
    ↓
nudge
```

The paper uses nudging to move parallel edge segments and balance inter-edge distances while preserving orthogonal structure.

For Paradox, apply a final nudging/spacing pass after negotiated routing.

Do not replace the existing route topology with the paper's full graph-drawing system.

Use the principle:

> Once valid routes exist, use remaining free lateral space to increase and regularize separation between parallel wires without introducing diagonals or unnecessary bends.

---

# 8. Existing Wire Box concept must remain

The new routing mode must preserve the existing Wire Box endpoint architecture.

Every source/destination Actor resolves a Wire Box using the already implemented logic.

Expected existing behavior:

```text
custom WireTarget box if present
otherwise automatic visual box
```

Do not replace this logic.

---

# 9. Custom WireTarget support remains mandatory

Preserve the current override convention:

```text
Actor Tag = "WireTarget"
```

When the existing Wire Box resolver finds the valid custom `UBoxComponent` associated with this Actor override, that box remains authoritative.

If no custom box is resolved, keep the existing automatic box behavior based on visible:

```text
UStaticMeshComponent
USkeletalMeshComponent
```

This task does not redesign Wire Box resolution.

---

# 10. Source/target face constraint

The new algorithm must enforce exactly the same hard endpoint constraint as the other current routing algorithms.

Every wire:

```text
starts on a Source Wire Box face
ends on a Target Wire Box face
```

The first segment must be orthogonal to the chosen source face.

The final segment must be orthogonal to the chosen target face.

Example valid:

```text
┌──────────────┐
│ Source       ●──────────
└──────────────┘
```

Example invalid:

```text
┌──────────────┐
│ Source       ●
└──────────────┘\
                 \
```

Do not weaken this constraint to improve separation.

---

# 11. Face selection

Reuse the existing face-candidate system.

The new routing cost may cause a different face pair to win because it reduces congestion.

This is allowed.

For example:

```text
Source East -> Target West
```

may be individually shortest, but:

```text
Source North -> Target North
```

may produce much greater separation from all other wires.

The Distributed/Repulsive mode may choose the latter if its total negotiated cost is lower.

Do not hardcode specific face pairs.

---

# 12. Core difference from Ordered Bundles

The new algorithm is intentionally the inverse of bundle attraction.

Ordered-Bundle philosophy:

```text
used compatible corridor
    -> cost decreases / bundle bonus
```

Distributed-Repulsive philosophy:

```text
used corridor
    -> cost increases strongly

nearby corridor
    -> cost also increases

far corridor
    -> no proximity cost
```

There must be **no BundleReuseBonus** in this mode.

Do not accidentally share Ordered-Bundle cost weights with this mode if they reverse the intended behavior.

---

# 13. Routing resource occupancy

Reuse or extend the existing lattice-edge occupancy representation.

Each lattice edge must expose enough temporary routing information for the current negotiation pass:

```text
CurrentUsageCount
CurrentWireIds / handles when useful
HistoricalCongestion
```

Do not modify gameplay GridWorld occupancy.

This occupancy belongs only to the temporary wire router.

---

# 14. Exact shared-edge penalty

If another wire already uses the same lattice edge, apply a strong cost.

Conceptually:

```text
SharedEdgePenalty
```

Possible model:

```text
if UsageCount == 0:
    cost += 0

if UsageCount == 1:
    cost += SharedEdgePenalty

if UsageCount > 1:
    cost += SharedEdgePenalty * UsageCountMultiplier
```

Exact formula is implementation-specific.

Important behavior:

> Cost should rise progressively with congestion.

Do not make exact sharing impossible in all cases.

When no valid practical alternative exists, sharing may still be preferable to route failure or pathological geometry.

---

# 15. Proximity Congestion Field

This is the key feature distinguishing this mode from simple edge-disjoint routing.

A wire must penalize not only its exact occupied edge, but nearby **parallel routing edges**.

Example conceptual field around a horizontal wire:

```text
distance 3 lattice units    Low penalty

distance 2 lattice units    Medium penalty

distance 1 lattice unit     High penalty

WIRE EDGE                   Very high occupancy penalty

distance 1 lattice unit     High penalty

distance 2 lattice units    Medium penalty

distance 3 lattice units    Low penalty
```

At or beyond a configurable radius:

```text
0 proximity penalty
```

Working setting:

```text
ProximityRadius
```

---

# 16. Proximity penalty falloff

Do not require a physically based continuous force equation.

A discrete lattice falloff is preferred for predictable art direction.

Example conceptual weights:

```text
distance 0:
ExactEdgeCongestion

distance 1:
ProximityWeight * 1.00

distance 2:
ProximityWeight * 0.50

distance 3:
ProximityWeight * 0.20

distance >= 4:
0
```

The exact curve should be configurable or represented by a small tunable policy.

Possible engine-facing options:

```text
ProximityRadius
ProximityPenalty
ProximityFalloffExponent
```

Avoid exposing dozens of micro-parameters.

---

# 17. Parallelism matters more than isolated proximity

The main aesthetic problem is long parallel cable clusters.

Therefore proximity cost should be especially strong when:

```text
candidate segment
and
existing nearby segment
```

are parallel for multiple consecutive lattice edges.

Conceptual:

```text
ParallelProximityCost =
    proximity penalty
    *
    shared parallel run length
```

or an equivalent accumulated edge-by-edge cost.

Thus:

```text
two wires 1 cell apart for 20 cells
```

must cost substantially more than:

```text
two wires briefly pass 1 cell apart for 1 cell
```

This is important.

Do not treat every nearby point as equally problematic regardless of duration.

---

# 18. Perpendicular proximity

A wire passing close to another wire only briefly at a perpendicular approach is less visually problematic than a long parallel cluster.

Therefore:

```text
ParallelNearPenalty > PerpendicularNearPenalty
```

The exact ratio is tunable.

Actual geometric crossing remains handled separately and must be much more expensive.

---

# 19. Crossing penalty remains strongest

Conceptual priority:

```text
actual crossing
    -> extremely high cost

exact corridor sharing
    -> very high cost

long close parallel run
    -> high cost

short close pass
    -> moderate cost

well-separated routing
    -> low/no cost
```

Do not solve proximity by allowing more crossings.

Set:

```text
CrossingPenalty >> ProximityPenalty
```

---

# 20. Historical congestion

Adapt PathFinder's historical-cost idea.

Each routing edge/region may accumulate:

```text
HistoricalCongestion
```

when repeated negotiation passes keep producing undesirable congestion there.

Conceptual update:

```text
if edge/corridor remains over-congested:
    HistoricalCongestion += HistoryIncrement
```

During later negotiation passes:

```text
RouteCost += HistoricalCongestion * HistoryWeight
```

This prevents all wires from simply moving together from one congested corridor to another on every pass.

---

# 21. Proximity history

Optionally extend historical cost beyond exact occupied edges to repeatedly congested proximity regions.

Keep the first implementation simple.

Recommended initial behavior:

```text
history on exact routing edges
current pass computes proximity dynamically
```

Only add persistent proximity history if profiling/testing shows wires oscillating between equivalent nearby corridors.

Do not overcomplicate the first implementation.

---

# 22. Initial routing pass

Pass 0/1 should produce a valid initial orthogonal route for every wire using:

```text
existing face constraints
existing lattice
existing surface/void costs
existing multi-level logic
LengthCost
BendCost
CrossingCost
```

Congestion/proximity may initially be empty or built incrementally.

Do not expect the first pass to be aesthetically final.

---

# 23. Negotiation passes

After the initial pass:

```text
1. calculate edge occupancy
2. calculate proximity congestion
3. identify worst conflicts/congested wires
4. rip up routes selected for rerouting
5. reroute them using current + historical congestion cost
6. update congestion
7. repeat
```

Working limit:

```text
MaxNegotiationPasses
```

A small bounded number is expected.

Do not hardcode a pass count without profiling.

Expose a reasonable setting/default.

---

# 24. Rip-up strategy

Do not give the first-routed wire permanent priority.

Possible deterministic strategies:

```text
reroute all wires each negotiation pass
```

or preferably, if current architecture supports it efficiently:

```text
reroute only wires with the highest congestion/proximity contribution
```

A route's conflict score may include:

```text
shared-edge count
crossing count
parallel-near length
proximity integral
```

Use the smallest implementation that avoids order bias.

---

# 25. Routing order must vary deterministically

If wires are always routed in the same order:

```text
Wire A
Wire B
Wire C
```

A may consistently receive ideal space before B/C.

Avoid permanent first-wire privilege.

Possible deterministic approaches:

```text
rotate routing priority by negotiation pass
sort by current congestion severity
sort most constrained wires first
```

Do not use nondeterministic random ordering.

The same input snapshot must produce the same output.

---

# 26. Candidate route cost

The new mode's conceptual cost becomes:

```text
Cost =
      LengthWeight * RouteLength
    + BendPenalty * BendCount
    + UnsupportedPenalty * UnsupportedLength
    + CrossingPenalty * CrossingCount
    + SharedEdgePenalty * SharedUsage
    + CurrentCongestionPenalty
    + HistoricalCongestionPenalty
    + ProximityPenalty
    + ParallelRunPenalty
    + BridgePenalty
```

There is deliberately:

```text
NO BundleReuseBonus
```

in this mode.

Use the existing route-cost framework where possible.

Do not duplicate the entire router just because the cost function differs.

---

# 27. Cost hierarchy

Tune defaults so the qualitative order is:

```text
1. preserve hard endpoint/face constraints
2. avoid crossings
3. avoid exact shared edges
4. avoid long close parallel runs
5. spread wires into uncongested space
6. keep bend count reasonable
7. keep route length reasonable
8. avoid unsupported/bridge segments according to existing project priorities
```

Do not let separation create absurd routes with dozens of corners or extreme length.

All costs remain tradeoffs except the existing hard geometric constraints.

---

# 28. Separation saturation

The router should not chase infinite separation.

Once another wire is beyond:

```text
ProximityRadius
```

there should normally be no additional reward for moving even farther away.

This keeps routing bounded and prevents unnecessary detours.

Objective:

```text
far enough
```

not:

```text
maximize Euclidean distance at any cost
```

---

# 29. Same-endpoint fan-out / fan-in

Several wires may necessarily leave the same Actor face or converge on the same target face.

Close spacing immediately around the endpoint may be unavoidable.

Do not punish this as strongly as long-distance parallel clustering.

Introduce an endpoint exemption/ramp region if necessary.

Conceptually:

```text
EndpointEscapeDistance
```

Within this region:

```text
reduced ProximityPenalty
```

so wires can fan out cleanly from the same Wire Box.

After the escape region:

```text
full repulsion applies
```

Do not allow this exemption to extend across the entire route.

---

# 30. Face-port distribution

Preserve the existing Wire Box face-port logic.

When multiple wires use one face, they retain distinct ports.

The repulsive algorithm may influence:

```text
which face is chosen
which port order is most useful
```

but must keep:

```text
endpoint on face
orthogonal first/last segment
```

Do not move ports outside the box face to achieve separation.

---

# 31. Orthogonal nudging final pass

After the final negotiated routes are accepted:

```text
route topology
    ↓
final line order
    ↓
orthogonal nudging
```

Use the Hegemann/Wolff principle to exploit available free lateral room.

In this Distributed mode, nudging should bias toward **greater separation**, not compact bundles.

It must preserve:

```text
90-degree geometry
face-normal endpoints
no new crossings
reasonable bends
surface/height constraints
```

---

# 32. Nudging must not create new conflicts

After nudging, validate at least:

```text
no new crossings
no Wire Box endpoint violation
no invalid segment axis
no forbidden Z transition
```

If nudging cannot increase spacing safely:

```text
keep original position
```

Do not invalidate a correct negotiated route merely to achieve prettier spacing.

---

# 33. Existing Z bridge fallback remains

Do not rewrite the current bridge system.

The Distributed algorithm should first attempt to resolve conflicts through:

```text
negotiated congestion
proximity repulsion
rip-up/reroute
nudging
```

If an actual crossing remains unavoidable within routing bounds:

```text
use the existing Z bridge fallback
```

Do not use bridge Z merely because two wires are close but not crossing.

---

# 34. Existing multi-level behavior remains

Do not redesign:

```text
StructuralVertical
floor-to-floor transition
surface resolution
void support
```

The new congestion cost simply participates in choosing among valid existing 3D candidates.

A structural Z transition is not itself congestion.

---

# 35. 3D proximity

Proximity must consider the actual 3D route.

Two wires that overlap in X/Y but are on clearly separated structural floors should not necessarily repel each other as if they were adjacent.

Use a configurable/derived vertical relevance threshold.

Conceptual:

```text
if abs(Z1 - Z2) > VerticalProximityThreshold:
    little/no proximity cost
```

However wires separated only by the small existing anti-crossing bridge height should still be considered locally related/conflicting where appropriate.

Use existing route segment semantic types to distinguish:

```text
StructuralVertical/Floor difference
Bridge layer
normal ground layer
```

---

# 36. Sharing as last resort

If geometry makes separation impossible, exact/shared-edge routing remains allowed.

Examples:

```text
narrow corridor
same constrained face exit
limited lattice
```

When this happens, reuse the existing lane/rendering fallback if applicable.

Do not fail to render a valid PuzzleSystem relationship.

The rule remains:

> Every valid puzzle link must have a renderable route.

---

# 37. Determinism

For identical:

```text
Puzzle graph
Wire Boxes
Actor transforms
routing lattice
surface snapshot
algorithm settings
```

the Distributed mode must produce identical routes.

Historical congestion exists only within one routing solve/generation unless the current architecture explicitly caches deterministic routing history.

Do not let previous unrelated selections change the next result.

---

# 38. Thread compatibility

Preserve the existing pure-data router architecture.

Negotiation data must be ordinary routing snapshot/work-data:

```text
edge occupancy
historical congestion
proximity field
wire conflict score
negotiation pass index
```

Do not access:

```text
AActor
UActorComponent
UWorld
PuzzleSystem UObject
GridWorld UObject
```

from worker route computation.

No change to the existing Game Thread snapshot -> worker calculation -> Game Thread apply architecture.

---

# 39. Bounded complexity

The algorithm must have explicit limits:

```text
MaxCandidatesPerWire
MaxNegotiationPasses
MaxReroutesPerPass, if applicable
ProximityRadius
```

No unbounded convergence loop.

If the negotiation limit is reached:

```text
accept best valid result found
then use existing crossing/bridge fallback where necessary
```

Track the best result, not just the last result.

---

# 40. Best-result tracking

Because negotiation can oscillate:

```text
Pass 2 may be better than Pass 3
```

Maintain a deterministic quality score for the complete wire set.

Conceptual global score:

```text
TotalCrossings
TotalSharedEdgeLength
TotalParallelProximityCost
TotalLength
TotalBends
TotalUnsupportedLength
TotalBridgeCost
```

Store the best valid generation encountered.

At the end:

```text
return best generation
```

not automatically the final pass.

---

# 41. Global quality priority

When comparing complete routing generations, prefer lexicographically or with very strong weights:

```text
1. fewer crossings
2. less exact shared-edge length
3. less close-parallel/proximity cost
4. fewer pathological bridges
5. fewer bends
6. shorter total length
```

Use current project priorities for unsupported-surface cost.

The exact scoring implementation should be documented.

---

# 42. Engine-facing tuning

Expose only useful algorithm-specific settings under the new routing mode.

Recommended:

```text
MaxNegotiationPasses

SharedEdgePenalty

HistoricalCongestionWeight

ProximityRadius
ProximityPenalty
ProximityFalloff

ParallelRunPenalty

EndpointEscapeDistance
VerticalProximityThreshold
```

Reuse existing:

```text
LengthWeight
BendPenalty
UnsupportedPenalty
CrossingPenalty
BridgePenalty
```

where those settings are already shared across algorithms.

Do not duplicate existing settings with new names unnecessarily.

---

# 43. Debug data

Extend current routing debug for this mode with:

```text
Algorithm = DistributedRepulsive

NegotiationPass
EdgeUsageCount
HistoricalCongestion
ProximityField
WireConflictScore
SharedEdgeLength
ParallelNearLength
CrossingCount
best-generation score
rerouted wires per pass
```

Useful visualization:

```text
routing edges colored by congestion
proximity bands around current wires
```

Only when existing debug is enabled.

Do not add always-on visualization.

---

# 44. Profiling

Reuse existing router profiling/Unreal Insights infrastructure.

If useful, add scopes equivalent to:

```text
ParadoxPuzzleWire_DistributedRouting
ParadoxPuzzleWire_NegotiationPass
ParadoxPuzzleWire_BuildProximityField
```

Do not instrument every lattice-edge lookup.

The new mode is expected to cost more than a single greedy routing pass but remains bounded and occurs only on actual route recalculation.

Signal-state changes must still not invoke it.

---

# 45. Required tests

Add focused tests for the new algorithm only.

Do not rewrite existing routing tests.

## Test 1 — Selectable mode

Existing algorithm selector exposes the new mode.

Selecting previous modes preserves their previous behavior.

## Test 2 — Wire Box regression

New mode uses the already-resolved Wire Boxes.

## Test 3 — Custom WireTarget regression

Actor with:

```text
Actor Tag = WireTarget
```

uses the current custom Wire Box implementation.

## Test 4 — Orthogonal source departure

First segment remains orthogonal to source Wire Box face.

## Test 5 — Orthogonal target arrival

Last segment remains orthogonal to target Wire Box face.

## Test 6 — Exact shared edge discouraged

Two wires with multiple reasonable corridors.

Expected:

```text
different corridors
```

when cost allows.

## Test 7 — Parallel proximity discouraged

Two route solutions:

```text
A:
wires run one lattice unit apart for long distance

B:
wires remain several lattice units apart
```

with comparable length/bends.

Expected:

```text
B wins
```

## Test 8 — Brief proximity acceptable

A short close pass must cost less than a long close parallel run.

## Test 9 — Crossing remains worse

A separated route with no crossing must beat a route that crosses merely to increase distance.

## Test 10 — Narrow corridor fallback

Only one practical corridor exists.

Expected:

```text
all links still route successfully
```

even if sharing/proximity is unavoidable.

## Test 11 — Negotiation reduces congestion

Initial pass clusters several wires.

After negotiation:

```text
global shared/proximity score decreases
```

without increasing crossings.

## Test 12 — No first-wire privilege

Negotiation must prevent one wire from permanently monopolizing the best corridor solely because it routed first.

## Test 13 — Historical congestion

Repeatedly congested corridor becomes progressively less attractive across passes.

## Test 14 — Best-pass retention

Later pass is worse than earlier pass.

Expected:

```text
best earlier valid generation returned
```

## Test 15 — Proximity radius saturation

Wires farther apart than configured `ProximityRadius`.

Expected:

```text
no additional repulsion cost
```

## Test 16 — Endpoint escape

Several wires leave the same box face.

Expected:

```text
they can leave valid ports
then fan outward
```

without pathological immediate detours.

## Test 17 — Nudging increases separation

Final pass has free lateral space.

Expected increased spacing without new crossings.

## Test 18 — 3D floor separation

Two wires overlap in XY but are on substantially different structural floors.

Expected reduced/no proximity penalty according to vertical threshold.

## Test 19 — Existing bridge fallback

Crossing remains unavoidable.

Expected existing bridge system still resolves it.

## Test 20 — Signal state regression

Emitter/gate state changes.

Expected:

```text
no negotiation/routing recalculation
```

## Test 21 — Deterministic result

Same snapshot/settings.

Expected identical result and best-pass selection.

## Test 22 — Bounded runtime

Pathological setup.

Expected:

```text
MaxNegotiationPasses respected
MaxCandidates respected
valid best route returned
```

---

# 46. Implementation order

Modify only the existing algorithm-selection/routing layer.

Recommended order:

```text
1. Inspect current routing enum/strategy selection.

2. Add new selectable DistributedRepulsive mode.

3. Reuse existing Wire Box + face endpoint constraints.

4. Add per-edge CurrentUsage and HistoricalCongestion
   to this mode's working data.

5. Add exact SharedEdgePenalty.

6. Add discrete Proximity Congestion Field.

7. Add stronger accumulated cost for long nearby parallel runs.

8. Implement bounded PathFinder-style negotiation passes.

9. Remove/reroute congested wires between passes.

10. Track best global valid routing generation.

11. Add final repulsive orthogonal nudging pass.

12. Keep existing bridge fallback.

13. Add mode-specific debug/profiling.

14. Add focused tests.

15. Compile and compare against existing modes.
```

---

# 47. Forbidden changes

Do not:

```text
replace existing algorithms
remove Ordered Bundle mode
make this new algorithm the only mode
rewrite the renderer
rewrite surface detection
rewrite lattice generation
rewrite multi-level behavior
rewrite Wire Box resolution
remove WireTarget support
change PuzzleSystem Graph Query
change signal brightness rules
change cube rendering
add PCG
add a third-party FPGA/router library
implement force-directed diagonal geometry
introduce non-orthogonal cable segments
drop the face-normal endpoint constraint
make edge-disjointness a hard requirement
allow failure when congestion is unavoidable
use unbounded negotiation passes
use nondeterministic random routing
access UObjects from worker routing code
reroute on signal-state changes
use LogTemp
```

---

# 48. Definition of Done

The patch is complete only when:

- a new routing mode is selectable alongside the algorithms already implemented;
- existing algorithms retain their previous behavior;
- the new mode is based on PathFinder-style negotiated congestion;
- exact shared lattice edges are strongly penalized;
- nearby parallel wires generate a proximity penalty;
- long close parallel runs cost more than brief proximity;
- repeated congestion accumulates historical cost between negotiation passes;
- routes can be ripped up and rerouted through bounded negotiation;
- first-routed wires do not receive permanent priority;
- the best valid negotiation pass is retained;
- edge sharing remains possible when separation is physically impractical;
- proximity cost saturates beyond a configurable radius;
- final orthogonal nudging attempts to exploit free space for greater separation;
- crossings remain more expensive than mere proximity;
- the existing Wire Box architecture remains unchanged;
- custom `WireTarget` boxes remain supported;
- every wire still starts/ends on Wire Box faces;
- first and last segments remain orthogonal to the corresponding box faces;
- existing 3D lattice/surface/void/multi-level/bridge behavior remains untouched;
- runtime signal state does not invoke this router;
- calculation remains deterministic;
- calculation remains pure-data/thread-compatible;
- worst-case iteration count is bounded;
- focused automated tests pass;
- the affected target compiles;
- no unrelated subsystem is rewritten.

If the affected target does not compile, the task is not finished.
