# Paradox Puzzle Wiring — Routing Criteria Patch for Existing Implementation

## Purpose

This document is a **targeted correction of the cable-generation/routing criteria** for the already implemented Paradox Puzzle Wiring system.

The existing system is already implemented.

Do **not** rewrite or replace the whole renderer.

Do **not** reimplement features that already exist, including:

```text
PuzzleSystem Graph Query integration
Selection integration
3D routing lattice
GridWorld reference-frame integration
surface detection
void / unsupported-surface handling
multi-level Z routing
structural vertical segments
anti-crossing Z bridge fallback
cube-based segment rendering
Input / Output rendering
runtime brightness/emissive state updates
event-driven PuzzleSystem state updates
async/thread-compatible routing architecture
routing generation/revision handling
debug/lifecycle/reset behavior
```

Modify only the criteria and passes used to generate and organize the cable routes.

The new routing goal is:

> Prefer globally ordered bundles of orthogonal wires instead of trying to keep every wire spatially independent.

The only new hard geometric constraint introduced by this patch is:

> Every wire must start on a face of the source Actor's Wire Box and terminate on a face of the destination Actor's Wire Box. The first and last route segments must be orthogonal to those faces.

Everything between the endpoint boxes remains controlled by the existing 3D routing system and the ordered-bundle criteria described below.

---

# 1. Mandatory preliminary work

Before editing the router:

1. read repository root `AGENTS.md`;
2. read every relevant `CODEX` file;
3. inspect the current implemented Puzzle Wiring renderer/router;
4. identify the exact classes/functions responsible for:
   - endpoint generation;
   - candidate-route generation;
   - route cost;
   - lane allocation;
   - crossing detection;
   - rerouting;
   - bridge fallback;
   - surface projection;
5. inspect the current automated routing tests;
6. compile a baseline;
7. make the smallest modification necessary;
8. do not rewrite unrelated rendering/state/lifecycle code;
9. preserve current public behavior outside cable generation criteria;
10. compile and test the final result.

---

# 2. Algorithms/reference techniques to apply

The implementation must explicitly take its routing criteria from the following established graph-drawing techniques.

These references are architectural/algorithmic guidance.

Do not vendor or add third-party libraries merely to satisfy this task.

Adapt the algorithms to the existing Paradox 3D lattice and current pure-data router.

---

## 2.1 Edge Routing with Ordered Bundles

Paper:

**Sergey Pupyrev, Lev Nachmanson, Sergey Bereg, Alexander E. Holroyd — "Edge Routing with Ordered Bundles"**

Links:

```text
https://arxiv.org/abs/1209.4227
https://doi.org/10.1016/j.comgeo.2015.10.005
```

Core idea to apply:

> Wires that travel through similar regions should be encouraged to share routing corridors, while every wire remains visually and semantically separate inside the bundle.

The paper's central objective is not merely to collapse edges together. It keeps edges individually drawable inside ordered bundles and optimizes routing using costs related to length, required drawing ink, width/separation, congestion and crossings.

For Paradox, adapt this concept to the existing routing lattice:

```text
compatible existing corridor
    -> can lower route cost

unnecessary isolated parallel corridor
    -> less desirable

shared corridor
    -> forms a logical bundle

individual PuzzleGraphLink
    -> always remains an individual wire/lane
```

This replaces the previous mindset:

```text
"existing wire = obstacle/penalty"
```

with:

```text
"compatible existing corridor = potential bundling benefit"
```

Do not merge the semantic identities of wires.

---

## 2.2 Metro-Line Crossing Minimization

Reference:

**Martin Fink, Sergey Pupyrev — "Metro-Line Crossing Minimization: Hardness, Approximations, and Tractable Cases"**

Link:

```text
https://arxiv.org/abs/1306.2079
```

Related ordered-bundle work also uses a metro-line ordering formulation:

```text
https://arxiv.org/abs/1209.4227
```

Core idea to apply:

> Once several wires share a corridor, choose and propagate their lane order so that the wires exchange positions as little as possible at corners, merges, splits and junctions.

For Paradox this means:

```text
Bundle corridor
├── Wire A
├── Wire B
├── Wire C
└── Wire D
```

must not independently decide its lane on every segment.

Instead compute a stable ordering:

```text
Lane 0 -> Wire A
Lane 1 -> Wire C
Lane 2 -> Wire D
Lane 3 -> Wire B
```

and preserve that ordering for as long as the wires share the same corridor.

At:

```text
corner
merge
split
junction
```

choose the permutation that minimizes crossing/order inversions.

The general Metro-Line Crossing Minimization problem is computationally difficult, so **do not implement an unbounded exact global solver**.

Use a deterministic bounded heuristic appropriate for the small selected-Actor subgraph.

The objective to preserve is:

```text
minimize lane-order inversions / crossings
```

not mathematical global optimality.

---

## 2.3 Orthogonal Edge Ordering + Nudging

Paper:

**Tim Hegemann, Alexander Wolff — "A Simple Pipeline for Orthogonal Graph Drawing"**

Links:

```text
https://arxiv.org/abs/2309.01671
https://arxiv.org/pdf/2309.01671
```

Core idea to apply:

The pipeline separates:

```text
route edges
    ↓
order edges
    ↓
nudge edges
```

The nudging pass moves already-routed parallel edge segments so that inter-edge distances are visually balanced while preserving the orthogonal structure.

Apply the same separation in Paradox:

```text
logical route topology
    ↓
bundle/line ordering
    ↓
lane placement
    ↓
orthogonal nudging
    ↓
final render coordinates
```

Do not bake final visual lane offsets into the initial route search.

Nudging is a **post-routing aesthetic pass**.

The existing Paradox implementation does not need to reproduce the paper's exact linear-programming formulation if that would require replacing the current router.

It must, however, implement the same objective:

```text
parallel wires remain ordered
spacing is balanced
segments remain orthogonal
unnecessary lateral jitter is removed
```

---

# 3. New endpoint concept: Wire Box

Every Actor used as a source or destination must resolve a routing box.

Working concept:

```text
FParadoxPuzzleWireBox
```

The Wire Box is used only to constrain:

```text
where a wire may start
where a wire may terminate
the normal direction of the first segment
the normal direction of the last segment
```

The Wire Box is **not automatically an obstacle for unrelated wires**.

Do not introduce general Actor-box obstacle avoidance as part of this patch.

---

# 4. Automatic Wire Box

If no custom override is provided, calculate the Actor Wire Box from visible visual components.

Include:

```text
UStaticMeshComponent
USkeletalMeshComponent
```

that are currently valid/visible according to the real Unreal APIs and current project conventions.

The Wire Box must be calculated in the routing/GridWorld reference frame so its routing faces correspond consistently to the routing axes.

Conceptually:

```text
visible mesh geometry/bounds
        ↓
transform into routing reference frame
        ↓
union
        ↓
routing-aligned Wire Box
```

Do not rely on an arbitrary world-axis AABB if the GridWorld/routing frame is rotated.

Inspect the real component bounds APIs and choose the smallest correct implementation.

---

# 5. Optional custom Wire Box — Actor Tag `WireTarget`

The user may override the automatically generated Wire Box.

The override is identified using:

```text
Actor Tag = "WireTarget"
```

If the puzzle Actor has the Actor Tag:

```text
WireTarget
```

and contains a valid `UBoxComponent` intended as its custom routing box:

```text
use that UBoxComponent as the authoritative Wire Box
```

The designer can therefore author:

```text
custom size
custom relative position
custom orientation
```

without changing the Actor's actual visible mesh bounds.

When a valid custom Wire Box is found:

```text
DO NOT also union the visible mesh bounds into it.
```

The custom box is the override.

---

## 5.1 Invalid custom override

If the Actor has:

```text
Actor Tag = WireTarget
```

but no valid `UBoxComponent` can be resolved:

```text
emit a warning
fallback to the automatic visible-mesh Wire Box
```

Do not make wiring fail.

If multiple candidate `UBoxComponent`s exist:

```text
use a deterministic documented resolution policy
emit a warning in debug/development
```

Do not choose based on unstable component iteration order.

Do not introduce new mandatory authoring assets just to disambiguate this case.

---

# 6. Wire Box faces

Each resolved Wire Box exposes routing faces corresponding to its local/reference-frame axes:

```text
+X
-X
+Y
-Y
+Z
-Z
```

Normally ground-level connections will naturally use lateral faces:

```text
+X
-X
+Y
-Y
```

but the system must not structurally forbid Z faces if the existing 3D routing architecture has a valid use for them.

---

# 7. Only hard routing constraint added by this patch

For every Puzzle wire:

```text
Source Wire Box
    ↓
chosen Source Face
    ↓
wire route
    ↓
chosen Target Face
    ↓
Target Wire Box
```

Required:

```text
wire starts on Source Face
wire ends on Target Face
```

and:

```text
first segment is parallel to Source Face normal
last segment is parallel to Target Face normal
```

Therefore a route must never visually leave or enter a face diagonally.

Example valid:

```text
┌──────────────┐
│ Source       ●────────
└──────────────┘
```

Example invalid:

```text
┌──────────────┐
│ Source       ●
└──────────────┘\
                \
```

The exact endpoint position on the face should not be fixed earlier than necessary.

---

# 8. Face selection must remain part of global route optimization

Do not impose new hard preferences such as:

```text
parallel source/target faces must win
nearest face centers must win
East must connect to West
North must connect to South
```

Those were considered previously and are **not required by this patch**.

For each link, generate reasonable source/target face-pair candidates.

The chosen pair should be whichever produces the best result under the **global route/bundle cost**.

Therefore a face pair that produces a slightly longer individual connection may still win if it:

```text
joins a clean shared corridor
reduces total bends
reduces bundle crossings
produces cleaner lane ordering
reduces overall clutter
```

Face selection is therefore part of the bundle-routing problem.

---

# 9. Delay exact endpoint placement

Do not permanently assign exact face coordinates before bundle ordering.

Preferred conceptual flow:

```text
select candidate source/target faces
        ↓
build logical routing/bundle skeleton
        ↓
determine line order
        ↓
assign lanes
        ↓
nudge parallel wires
        ↓
resolve final points on source/target faces
```

The router may use provisional face anchors during candidate generation, but final ports must be allowed to move along the usable face region.

This is important because several wires may choose the same face.

---

# 10. Multiple wires on one face

If several wires use the same Wire Box face:

```text
do not collapse them to one exact point
```

Their ordering on the face must be derived from the bundle/metro-line ordering.

Example:

```text
Wire A
Wire B
Wire C
```

approaching in that order should preferably terminate in the same order on the face.

This reduces immediate endpoint crossings.

Use the existing lane/nudging spacing system to place the final face ports cleanly.

Do not introduce an unrelated fixed hardcoded spacing system if the existing nudging pass already provides the necessary distribution.

---

# 11. Change the route-cost philosophy

The existing router must stop treating every occupied/used routing corridor as something to avoid.

Introduce the concept of:

```text
BundleReuseBonus
```

or an equivalent negative/discounted cost.

Conceptual cost:

```text
RouteCost =
      LengthCost
    + BendCost
    + UnsupportedSurfaceCost
    + CrossingCost
    + BridgeCost
    + CongestionCost
    - CompatibleBundleReuseBonus
```

Exact names/weights depend on current code.

The important behavioral rule is:

> Reusing a compatible corridor with separate lanes may be cheaper than creating an isolated nearby corridor.

---

# 12. Compatible bundle reuse

Do not blindly reward every occupied edge.

A corridor is reusable when the new wire can share it without causing pathological ordering.

Consider at least:

```text
same routing axis
sufficient lane capacity
compatible route direction/order
no unavoidable immediate inversion
no invalid geometry
```

If reuse would create more crossing/order inversions than it removes:

```text
do not apply the bundle bonus
```

---

# 13. Bundle extraction

After initial/global route selection, identify contiguous sequences of routing edges shared by multiple wires.

Working concept:

```text
FParadoxPuzzleWireBundle
```

Each bundle records:

```text
shared corridor edges
member wire handles
entry/exit points
lane order
```

Do not merge actual `FPuzzleGraphLinkHandle`s.

Each wire remains individually addressable for:

```text
runtime state
material brightness
debug
render-instance mapping
```

---

# 14. Metro-line ordering pass

For each shared corridor:

```text
collect participating wires
determine their incoming ordering
determine their outgoing ordering
choose lane order minimizing inversions
```

Propagate stable order through as much of the bundle as possible.

At splits and merges:

```text
prefer preserving existing relative order
```

Use deterministic local optimization.

Recommended bounded heuristic:

1. seed order from endpoint/previous-corridor spatial order;
2. propagate order through the shared corridor;
3. evaluate local inversions at junctions;
4. perform bounded adjacent swaps when they reduce crossings;
5. keep the best deterministic result.

Do not implement exponential permutation search.

---

# 15. Orthogonal nudging pass

After topology and order are established:

```text
do not reroute merely to create prettier spacing
```

Run a separate nudging pass.

The nudging pass adjusts the lateral position of parallel segments while preserving:

```text
segment axis
90-degree corners
wire ordering
bundle membership
endpoint-face constraint
surface/height semantics
```

Goals:

```text
balanced spacing
stable visual bundles
minimal lateral jitter
aligned parallel runs
clean merge/split appearance
```

This pass is directly inspired by the route -> order -> nudge pipeline from Hegemann/Wolff.

---

# 16. Bend minimization remains important

Ordered bundling must not create excessive cornering.

Keep/increase a meaningful:

```text
BendPenalty
```

Prefer:

```text
long straight bundle trunks
```

over:

```text
many short staggered bundle segments
```

when otherwise comparable.

After all ordering/nudging:

```text
remove redundant collinear points
remove zero-length segments
remove avoidable micro-doglegs
```

without violating face-normal endpoint constraints.

---

# 17. Crossing policy

Continue using the existing crossing-resolution architecture.

New priority should be:

```text
1. better face-pair/bundle route
2. metro-line ordering
3. lane assignment
4. orthogonal nudging
5. bounded reroute
6. existing Z bridge fallback
```

Do not use Z bridges before attempting to solve crossing/order problems through bundle organization.

Do not remove the existing bridge fallback.

---

# 18. Existing 3D routing behavior remains unchanged

Preserve the existing implementation for:

```text
multi-floor links
StructuralVertical
surface projection
GridWorld surface lookup
world surface fallback
unsupported void segments
bridge Z
```

The bundle algorithm must operate on the existing 3D route representation.

This patch does not re-specify those systems.

---

# 19. Surface/void cost remains secondary

Existing supported/unsupported-surface scoring remains valid.

However it must coexist with bundle reuse.

A slightly unsupported route may sometimes be acceptable if it creates a dramatically cleaner bundle, depending on configured weights.

Do not turn:

```text
Unsupported
```

into route invalidity.

Preserve existing behavior.

---

# 20. Determinism

Identical:

```text
Puzzle graph
Actor transforms
Wire Boxes
surface snapshot
routing settings
```

must produce identical:

```text
face selection
bundle selection
wire order
lane assignment
nudging result
```

Do not use pointer addresses or unstable container iteration as tie-breakers.

Use stable graph/query ordering and deterministic geometric tie-breaks.

---

# 21. Threading

Preserve the existing pure-data/thread-compatible routing architecture.

The new algorithmic data must be representable in the routing snapshot/result:

```text
Wire Box data
face candidates
provisional face portals
bundle membership
bundle order
lane assignment
nudged route geometry
```

Do not introduce worker-thread access to:

```text
AActor
UStaticMeshComponent
USkeletalMeshComponent
UBoxComponent
UWorld
PuzzleSystem UObjects
GridWorld UObjects
```

Resolve Wire Boxes on the Game Thread before launching the pure routing task.

---

# 22. Routing snapshot additions

Extend the existing routing snapshot only as needed with value data equivalent to:

```text
FResolvedWireBox
{
    routing-space transform/bounds
    face definitions
    source Actor stable runtime identity for diagnostics
}
```

The worker receives copies/value data.

Do not keep a dependency on component pointers for route calculation.

---

# 23. Do not modify runtime visual-state behavior

This patch must not affect the existing rule:

```text
PrimarySignal brightness
    -> EffectivePrimaryActive

GateInfluence brightness
    -> gate input state / current existing implementation
```

Signal-state changes continue to update visual state only.

They must not cause:

```text
face reselection
bundle rebuild
route rebuild
nudging
surface queries
```

---

# 24. Debug additions

Extend the existing routing debug only as needed.

Useful new debug layers:

```text
Auto Wire Box
Custom WireTarget Box
candidate faces
chosen source face
chosen target face
provisional face portal
final face port
bundle ID
bundle member count
bundle reuse cost/bonus
lane order
lane index
nudging offset
crossing/inversion count before ordering
crossing/inversion count after ordering
```

Keep debug disabled by default and under existing local/global debug controls.

---

# 25. Required focused tests

Do not rewrite unrelated test suites.

Add tests targeted at the changed criteria.

---

## Test 1 — Automatic Wire Box

Actor with visible Static/Skeletal Mesh components.

Expected:

```text
one resolved routing-space Wire Box
covering relevant visible components
```

---

## Test 2 — Custom WireTarget

Actor has:

```text
Actor Tag = WireTarget
valid UBoxComponent
```

Expected:

```text
custom box used
automatic visible-mesh box ignored
```

---

## Test 3 — Invalid WireTarget fallback

Actor tagged `WireTarget` but no valid box.

Expected:

```text
warning
automatic Wire Box used
routing still succeeds
```

---

## Test 4 — Endpoint on source face

Expected route starts exactly on selected source face.

---

## Test 5 — Endpoint on target face

Expected route ends exactly on selected target face.

---

## Test 6 — Orthogonal source departure

First segment direction must equal the chosen source-face outward normal axis.

---

## Test 7 — Orthogonal target arrival

Last segment must be aligned with the target-face normal axis.

---

## Test 8 — Face choice is not hardcoded

Construct geometry where a non-obvious face pair gives a lower global bundle cost.

Expected:

```text
router may select it
```

No forced East->West rule.

---

## Test 9 — Shared-corridor attraction

Several links have similar routes.

Expected:

```text
compatible corridor reuse is preferred
bundle forms
```

instead of several unnecessary isolated near-parallel corridors.

---

## Test 10 — Individual wire identity preserved

Multiple links in one bundle.

Expected each retains:

```text
unique PuzzleGraphLinkHandle
independent visual state
independent render mapping
```

---

## Test 11 — Stable metro ordering

Three or more wires share a corridor and split.

Expected:

```text
relative order preserved when possible
crossing/order inversions minimized
```

---

## Test 12 — Nudging

Parallel bundle members.

Expected:

```text
distinct parallel segments
balanced spacing
same orthogonal route topology
```

---

## Test 13 — Multiple wires on one face

Expected:

```text
distinct final ports
ordering coherent with bundle order
no unnecessary immediate endpoint crossing
```

---

## Test 14 — Bend minimization

Two candidate bundle routes have similar other cost but different bend count.

Expected fewer-bend route.

---

## Test 15 — Crossing resolution order

Crossing can be solved by line ordering/nudging.

Expected:

```text
no Z bridge
```

---

## Test 16 — Existing bridge fallback

Crossing remains unavoidable within routing budget.

Expected:

```text
existing Z bridge still works
```

---

## Test 17 — Multi-level regression

Existing Floor/StructuralVertical route still works after bundle changes.

---

## Test 18 — Void regression

Existing unsupported-surface route still works.

---

## Test 19 — State-change regression

Signal state changes.

Expected:

```text
no routing generation change
only visual state update
```

---

## Test 20 — Deterministic result

Same snapshot repeated.

Expected identical:

```text
faces
routes
bundles
orders
lanes
nudging
```

---

# 26. Forbidden changes

Do not:

```text
rewrite the entire renderer
rewrite PuzzleSystem Graph Query
rewrite Selection
rewrite GridWorld
replace existing surface detection
replace existing multi-level routing
remove current Z bridge fallback
change cube rendering
change signal brightness semantics
change WorldState/reset integration
add PCG
add a third-party routing library
introduce general Actor obstacle avoidance
force parallel source/target faces
force nearest face centers
force specific face pairs
fix exact face-port positions before bundle ordering
treat every occupied corridor as a penalty
merge bundled wires into one semantic link
perform exact exponential Metro-Line optimization
add unbounded rerouting
access UObject state from routing worker threads
use LogTemp
```

---

# 27. Required implementation order

Modify the existing router in this order:

```text
1. Add automatic/custom Wire Box resolution.

2. Convert endpoint generation from point-based endpoints
   to face-constrained endpoint candidates.

3. Enforce orthogonal source departure / target arrival.

4. Change route cost so compatible shared corridors
   receive a BundleReuseBonus.

5. Extract shared corridors into logical Ordered Bundles.

6. Add Metro-Line-style stable line ordering.

7. Adapt existing lane assignment to bundle order.

8. Add/adjust orthogonal nudging as a separate post-pass.

9. Resolve final endpoint locations on chosen box faces
   after ordering/nudging.

10. Run the existing bounded crossing reroute.

11. Preserve existing Z bridge fallback.

12. Preserve existing surface/multi-level/render/state systems.

13. Add focused tests and debug visualization.

14. Compile and profile.
```

---

# 28. Definition of Done for this patch

This routing correction is complete only when:

- the existing wiring system remains intact outside route-generation criteria;
- every source/destination Actor resolves a Wire Box;
- visible Static/Skeletal Mesh bounds provide the automatic box;
- Actor Tag `WireTarget` enables the custom `UBoxComponent` override;
- invalid custom override falls back safely;
- every wire starts and ends on chosen Wire Box faces;
- first and last segments are orthogonal to their corresponding faces;
- face selection is optimized as part of global routing rather than hardcoded;
- exact ports may be finalized after bundle ordering;
- compatible shared corridors are rewarded;
- Ordered Bundles are formed while individual link identities remain separate;
- Metro-Line-style ordering minimizes lane inversions/crossings;
- orthogonal nudging balances bundle spacing;
- bend count remains strongly penalized;
- crossing resolution attempts ordering/nudging/reroute before Z bridge;
- existing 3D lattice, surface, void, multi-level and bridge behavior still works;
- runtime signal changes still update only visual state;
- routing remains deterministic;
- routing remains pure-data/thread-compatible;
- focused tests pass;
- affected target compiles successfully;
- no unrelated system is rewritten.

If the target does not compile, the patch is not finished.
