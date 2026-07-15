# Grid World System

Grid World System provides deterministic, cell-based navigation through Unreal Engine's native Navigation System. `AGridNavigationData` is the single authority: `UNavigationSystemV1`, `AIController::MoveTo`, `UPathFollowingComponent`, C++ and Blueprint queries all consume the same immutable grid snapshots.

The plugin does not use Recast. Its runtime and editor code are separated into the `GridWorld` and `GridWorldEditor` modules.

## Project defaults

- Supported Agent radius: **42 cm**.
- Supported Agent total height: **192 cm**.
- Horizontal cell size: **100 x 100 cm**.
- Vertical layer spacing: **50 cm**.
- Debug cell half extent: **50 x 50 x 25 cm**.
- Chunk size: **16 x 16 cells**.
- Movement: four directions.
- Maximum floor slope: **45 degrees**.
- Maximum step and drop: **45 cm**.
- Runtime generation mode: `DynamicModifiersOnly`.

The logical cell dimensions do not represent the Character capsule. A 42 cm radius agent is deliberately tested for clearance at each 100 cm cell center.

Placed and runtime Pawns are ignored while sampling floors and capsule clearance. They are navigation consumers and must not carve permanent holes beneath their own start positions; use `GridNavigationOccupancyComponent` when an Actor should affect traversal dynamically.

GridWorld is generated for ordinary world-up walking. Floor normals are stored per cell. On a ramp, capsule clearance is placed tangent to the sampled plane and adjacency subtracts the continuous rise predicted by the neighboring floor normals before applying step/drop limits. A 60-degree ramp can therefore rise more than 45 cm per cell without being mistaken for a stair, while a real 46 cm discontinuity on a flat floor is still rejected by the default step limit.

`Max Slope Degrees` controls what GridWorld can generate; `CharacterMovement.WalkableFloorAngle` controls what a particular Character can physically walk. Set the Character value at least as high as the steepest generated ramp. GridWorld logs one contextual warning per AI controller when a path exceeds its current Character setting, but never changes movement configuration automatically.

## Setup

1. Enable the `GridWorldSystem` plugin.
2. Under **Project Settings > Navigation System > Supported Agents**, configure one `GridWorld` agent with radius **42 cm**, height **192 cm**, and both **Navigation Data Class** and **Preferred Nav Data** set to `GridNavigationData`. Paradox stores this in `Config/DefaultEngine.ini`. Without it, Unreal falls back to `RecastNavMesh-Default` and GridWorld movement tasks cannot resolve their Navigation Data.
3. Add a `GridNavigationBoundsVolume`, then position, rotate, and scale the Actor to cover the desired area. Scaling changes the bounds extents; cells remain 100 x 100 x 50 cm unless explicitly configured otherwise.
4. Ensure navigable floors block the bounds volume's collision profile, `Pawn` by default.
5. Select **Build > Grid World > Build Grid World** for the first build. Later Transform edits and changes to generation properties such as Movement Mode rebuild automatically when the edit is committed.
6. Leave **Auto Rebuild On Geometry Changes** enabled on a bounds volume when adding, removing, moving, or changing collision on navigation-relevant meshes should rebuild its affected chunks automatically.
7. Enable **Show > Navigation** in the viewport.
8. For **Center-Constrained** or **Cell-by-Cell** movement, use `AGridWorldAIController` (or the included `BP_GridAiController`) so the move is handled by `UGridWorldPathFollowingComponent`.
9. When using ramps above the Character Movement default, set **Walkable Floor Angle** to at least the bounds volume's required slope.

Every bounds volume has a persistent `GridId` and describes an independent grid. Two grids never acquire implicit adjacency, even if they are close. Add a `GridNavigationLinkComponent` for an intentional connection.

Invalid/zero scale, non-box brushes, duplicated GUIDs or ambiguous bounds overlaps reject the build. The last valid snapshot remains available. Pitch and roll rotate the sampling frame, but walkability still follows Unreal world up because standard Character Movement does not use local gravity.

## Designer workflow

Completed Transform edits on a `GridNavigationBoundsVolume` rebuild automatically through Unreal's native navigation-bounds notification. The Level Editor Build menu also contains actions to:

- build all Grid World bounds;
- rebuild chunks touched by selected Actors, including a one-chunk halo;
- clear generated data;
- validate bounds and GUIDs;
- inspect the projected cell at the selected Actor;
- select the configured GridWorld Supported Agent;
- toggle cells, costs, occupancy, links, chunks and dirty-region visualization.

**Auto Rebuild On Geometry Changes** is enabled by default per volume. UE's native navigation dirty areas detect navigation-relevant Static Mesh collision when an Actor or component is added, removed, moved, or changes collision. GridWorld rebuilds only enabled intersecting regions and their chunk halo. Disabled volumes retain their last built topology until a bounds Transform/property change, **Build Grid World**, or **Rebuild Grid World for Selection**. The project-wide Editor preference **Update Navigation Automatically** must also be enabled; the per-volume option filters the native pipeline and does not override that global preference.

Runtime changes use components:

- `GridNavigationModifierComponent` blocks/unblocks authored cells and composes additive, multiplicative or priority-based override costs;
- `GridNavigationOccupancyComponent` contributes optional cost, blocking and reservation information;
- `GridNavigationLinkComponent` creates directed or bidirectional explicit links, including links between different grids.

These components listen to `TransformUpdated`; they do not Tick. Use their Blueprint refresh functions after changing several exposed fields at runtime. Occupancy is ignored by navigation filters unless `AddCost` or `Block` is selected.

## Public queries

`UGridWorldSubsystem` and `UGridWorldBlueprintLibrary` expose projection, cell inspection, pathfinding and reachability. Results use `EGridQueryStatus` and return persistent `FGridCellId` values in addition to world points.

Cell inspection also returns the immutable world-space **Floor Normal** sampled for that cell. Native `FGridNavigationPath` instances retain `MaximumFloorSlopeDegrees`, the steepest cell normal crossed by the path.

`UGridNavigationQueryFilter` supports Unreal area costs and flags plus:

- four/eight direction movement;
- path optimization through **Shortest Path**, **Fewest Turns**, or **Balanced**;
- corner cutting;
- authored links;
- traversal channel;
- occupancy policy;
- reservation ownership through `IGridNavigationQueryContext`.

**Balanced** is the default and minimizes `TraversalCost + TurnCount x BalancedTurnPenalty x 1000`; its default penalty of `2.0` makes one turn equivalent to two ordinary cell steps. **Shortest Path** preserves the minimum traversal-cost search. **Fewest Turns** first minimizes direction changes and uses real traversal cost only as a secondary criterion, so it may choose a substantially longer route to remove one turn. The returned `Cost` is always the real traversal cost and never includes the synthetic Balanced penalty.

A turn is a change between consecutive ordinary segment directions in the grid's local horizontal axes. The first segment does not count, layer changes on ramps do not count, and diagonal/orthogonal changes or reversals count once. Explicit links reset incoming direction, so GridWorld does not compare direction across rotated grids or special traversal. `FGridNavigationPath` and Blueprint `FGridPathQueryResult` expose the selected optimization mode and resulting turn count.

Create a Blueprint subclass of `GridNavigationQueryFilter`, configure **Path Optimization Mode**, and assign that class to the normal Move To filter, **Move To Grid Cell** BT/Blueprint/StateTree filter, or the subsystem/Blueprint `Find Path` filter input. To make the policy the default for every move from one controller, assign that class as the AI controller's **Default Navigation Filter Class**. This is a per-query policy: changing it does not rebuild or reserialize GridWorld.

Directional modes can visit up to nine search states per cell, so their `VisitedNodes` count may exceed the number of cells. They use a zero heuristic to remain correct with areas, modifiers, occupancy, reservations and explicit links. **Shortest Path** retains the original A* allocation, heuristic and tie-break behavior.

For **Fewest Turns** and **Balanced**, the filter also exposes the advanced **Max Search States** setting. Its default is **65536**, replacing Unreal's generic 2048-node budget only for these directional strategies; **Shortest Path** keeps its existing native budget and behavior. If the directional search reaches this limit while partial paths are allowed, the result is intentionally marked partial and ends at the best reached cell. GridWorld logs one contextual warning per controller so this cannot look like an unexplained successful arrival. Raise the budget for larger grids, or disable **Accept Partial Path** when truncated progress must fail instead of completing a Move To.

Path results retain topology, traversal and occupancy revisions. Only paths intersecting a changed cell or link are invalidated, and occupancy-only changes do not invalidate paths whose filter ignores occupancy.

The bounds volume is authoritative for movement topology. A Four Directions region cannot be made diagonal by an Eight Directions query filter, and corner cutting is enabled only when both the bounds and filter allow it. Explicit authored links remain independent and may connect non-adjacent cells.

## Move To Grid Cell

The runtime module provides the same grid-aware move through three designer surfaces:

- **Move To Grid Cell** Behavior Tree task, with the native Move To Blackboard Actor/Vector options and abort behavior;
- asynchronous Blueprint **Move To Grid Cell** task node, exposing the native completion and request-failed pins;
- StateTree **Move To Grid Cell** task, using the native Move To instance data, bindings and completion lifecycle.

All three resolve the destination through the Supported Agent's `AGridNavigationData` and move to the center of the nearest navigable cell. Actor targets honor `INavAgentInterface` goal offsets. When Actor tracking is enabled, `TransformUpdated` causes a new move only after the Actor crosses into a different `FGridCellId`; no Tick is added. Blueprint Vector inputs are captured when the task starts, while Blackboard and StateTree bindings retain their native observation behavior.

Failure to resolve the controller, target, GridWorld NavData or projected cell completes through the native failure path and writes a contextual `LogGridWorld` warning. Destroying an Actor target also fails the active task.

### Multiple agents targeting one cell

**Move To Grid Cell** exposes an optional **Goal Contention Policy** on its Behavior Tree, Blueprint and StateTree versions:

- **Ignore** is the compatibility default and keeps the previous behavior.
- **Redirect on Completion** initially follows the requested path. When that move would complete on a cell owned by another agent, the same asynchronous task selects and moves to a separated nearby cell instead.
- **Reserve Before Move** atomically claims the requested cell before starting. If it is occupied or already claimed, an alternative is selected immediately, reducing visible crowding at the original goal.

Redirects are not limited to one retry. Every internal move completion checks the newly selected cell again. If another AI occupied that fallback in the meantime, GridWorld rejects that cell, claims another candidate and starts another internal path without completing the outer Behavior Tree, Blueprint or StateTree task. Previously rejected cells remain excluded for the lifetime of that task, preventing ping-pong.

When no separated destination is currently available, the task remains `Running` and retries immediately after occupancy or traffic reservations change. **Goal Wait Warning Interval** rate-limits contextual warnings (1 second by default). After one continuous **Goal Availability Timeout** episode (5 seconds by default), it releases its claims and finishes `Blocked`. Acquiring a cell or tracking an Actor into a different requested cell resets the episode. If the Pawn already occupies the requested cell, or its current cell is already one of the nearest valid alternatives, the task succeeds without issuing another MoveTo.

Candidates are reached through ordinary same-grid adjacency, not explicit links, and are ordered by graph distance plus stable cell coordinates. **Additional Goal Separation** is added to the sum of the agent radii. With the project defaults, two 42 cm agents require 89 cm between centers, so a directly adjacent 50 cm cell is intentionally rejected; the default search radius of three cells reaches a suitable second-ring destination.

**Auto Register Pawn Occupancy** is enabled by default whenever a contention policy is active. It adds a transient, no-Tick `GridNavigationOccupancyComponent` to a Pawn that has no active one. This tracker publishes only occupant identity: it adds no path cost and does not block A*. If a Pawn already owns an active non-reservation occupancy component, that component is reused. Disable automatic registration only when the project manages agent occupancy itself. Goal claims are transient, Game-Thread-only state in `AGridNavigationData` and are released on failure, abort, repath to a different tracked goal, Pawn/task teardown or target destruction. A successful claim becomes parking protection tied to the Pawn while it remains on that destination.

This policy belongs to **Move To Grid Cell**, not to the bounds volume or path optimization filter: it changes the requested destination and task lifecycle, not grid topology or the A* objective. Native **Move To** remains unchanged.

### Dynamic agents along the path

Create or edit a `GridNavigationQueryFilter` Blueprint to configure **Dynamic Agent Policy** for any normal Move To, BT/Blueprint/StateTree **Move To Grid Cell**, or subsystem query:

- **Ignore** preserves the previous behavior and is the compatibility default.
- **Yield** inspects the upcoming logical corridor and stops before another GridWorld-controlled Pawn without changing the path.
- **Yield Then Repath** first yields, then invalidates and recalculates the same active path after the blocker remains below **Stationary Agent Speed Threshold** for **Dynamic Agent Repath Delay** seconds.
- **Reserved Corridor** asks the authoritative NavData for exclusive protection of the current cell, upcoming cells, swept segments, diagonal crossings, explicit links and final parking. Already granted reservations are never stolen; conflicting agents wait by stable wait age and occupancy identity.

Assign the filter to an individual Move To or set it as the controller's **Default Navigation Filter Class**. This is independent from path optimization: Shortest Path, Fewest Turns, and Balanced all treat occupied intermediate cells as unavailable while searching for a detour. If no detour exists, GridWorld deliberately keeps the original corridor as a waiting path instead of failing the Behavior Tree, Blueprint, StateTree, or native Move To task. When that Pawn clears the corridor, movement resumes. If a later path is occupied by a different Pawn, the same yield/repath cycle may repeat without a one-redirect limit.

For Reserved Corridor, **Reserved Look Ahead Cells** is the designer minimum (1 by default). Runtime extends the prefix until it covers braking distance, both capsule radii and **Additional Agent Separation**. The old logical cell is released in the same publication that observes the next center gate, and a new tail cell is requested. Center-Constrained still reserves skipped collinear gates internally. Accelerated movement begins braking inside the protected envelope; Direct Velocity stops before an ungranted gate. Neither mode enables or modifies RVO, so the blue path and center-gate trajectory remain authoritative.

After **Dynamic Agent Repath Delay**, a waiting owner may seek a route around other reservations. If no safe detour exists, it keeps the original path and waits without repeatedly invalidating it. Native MoveTo can use the corridor policy but never chooses an alternative final goal; destination redirection remains specific to **Move To Grid Cell**. Standard path following is supported as best effort and emits one warning; the strict gate-to-gate guarantee requires Center-Constrained or Cell-by-Cell.

`GridWorldAIController` registers its possessed Pawn through a transient non-blocking occupancy component by default. Disable **Auto Register Pawn Occupancy** on the controller only when the project provides an equivalent active `GridNavigationOccupancyComponent`. The final path cell is intentionally excluded from corridor avoidance: **Goal Contention Policy** remains the authority for shared destinations.

### Recommended strict crowd setup

For the default 42 x 192 cm agents on 100 cm cells, use **Reserve Before Move**, alternative radius 8, 5 cm goal separation, automatic occupancy, 5 second availability timeout, and 1 second warning interval on **Move To Grid Cell**. Disable partial paths and Stop on Overlap; keep pathfinding enabled and track moving Actor goals when required.

On the assigned Grid Navigation Query Filter use **Balanced** with penalty 2.0, 65536 search states, **Reserved Corridor**, 1 reserved look-ahead cell, 5 cm agent separation, 5 cm/s stationary threshold, 0.1 second repath delay, Occupancy Policy **Ignore**, corner cutting disabled, and links enabled. For the strictest physical behavior, configure the bounds as **Cell-by-Cell**, **Direct Velocity**, 2 cm center tolerance, with accelerated final approach disabled.

**Occupancy Policy = Ignore** is intentional in this setup. Reserved Corridor already handles each participating Pawn's current cell, future cells, swept segments, crossing conflicts, goal claim and final parking through one owner-aware traffic authority. Making the ordinary occupancy overlay block those same cells would duplicate the decision, turn transient Pawn positions into hard graph obstacles, and can produce unnecessary no-path/partial-path results or repeated invalidation in narrow corridors. Ignore affects only the generic occupancy cost/block layer: it does not hide agents from Reserved Corridor or goal contention. Use **Block** for uncoordinated occupants that must be hard temporary obstacles, or **Add Cost** when non-reserving occupancy should merely discourage a route.

## Cell-centered path following

Each `GridNavigationBoundsVolume` exposes **Path Following Style**:

- **Standard** preserves Unreal's native path following, including task acceptance radius and goal overlap behavior.
- **Center-Constrained** uses one-way center gates at the initial cell, turns and explicit-link boundaries; collinear intermediate centers are skipped so straight runs remain continuous.
- **Cell-by-Cell** uses a one-way gate at every intermediate cell center, without braking there, and stops only at the final center.

An intermediate gate is the local-grid plane through its cell center, perpendicular to the incoming path segment. It is valid only from the incoming side toward the outgoing side. The Pawn must cross it inside a lateral half-width derived from the smallest horizontal cell dimension, never smaller than **Cell Center Tolerance**. With the default 100 x 100 cm cells this produces a 50 cm-wide gate, or 25 cm on either side of the center. A valid crossing advances immediately without braking. A crossing outside the gate fails the move as `Blocked`; the component never reverses to chase an expired waypoint, which prevents elastic oscillation.

**Cell Center Tolerance** (2 cm by default) remains the strict horizontal position test for the final center and also accepts an intermediate gate immediately when the Pawn passes directly through that central area. **Stop Speed Tolerance** (5 cm/s by default) applies only to the final center. Vertical placement remains governed by Character walking and the generated floor. Changing these bounds properties rebuilds the affected navigation data automatically.

For **Center-Constrained** and **Cell-by-Cell**, **Path Drive Mode** independently selects how the movement component receives each segment:

- **Accelerated** temporarily enables **Use Acceleration for Paths** and retains Character acceleration, braking and inertia.
- **Direct Velocity** is the native bounds default for precise path styles. It requests constant segment velocity and instantaneous direction changes without inertial oscillation.
- **Direct Velocity** requests the segment direction at `GetMaxSpeedForNavMovement()`, changes direction immediately at a gate and limits only the final frame so it cannot intentionally overshoot the destination center. It temporarily disables both **Use Acceleration for Paths** and Character Movement's **Requested Move Use Acceleration**.

With Direct Velocity, **Use Accelerated Final Approach** keeps constant direct velocity on intermediate segments but switches the final segment back to the accelerated braking behavior. It is disabled by default. This can make arrival look softer, but the purely direct setting is the one intended for rigid, constant-speed turns without inertial reversal.

Precise paths keep the Pawn's real starting position before the ordered centers. `UGridWorldPathFollowingComponent` saves and restores every movement property it changes on success, failure, abort, repath, Pawn replacement, movement-interface replacement and teardown. **Standard** never changes either property. GridWorld never teleports, snaps or rotates the Pawn and does not disable collision, RVO, root motion or replication. A collision or avoidance deviation that misses a gate can therefore still produce the normal `Blocked` result.

At a precise final center, task acceptance radius and overlap do not end the request early. Four- and Eight-Directions paths both use center-to-center segments; Character acceleration, braking, collision, replication and slight physical corner rounding remain native.

If a precise path is requested by an ordinary `AAIController`, navigation remains functional with Standard following and `LogGridWorld` emits one contextual warning for that controller. Reparent the controller Blueprint to `GridWorldAIController` to enforce the selected style.

## Debugging

Visual data is batched in one scene proxy and follows both controls:

`GridWorld.Debug.Visual != 0 AND local debug enabled`

`GridWorld.Debug.Visual 0` immediately disables all GridWorld visual output. Local filters live on `AGridNavigationData`; contributor components also expose `bEnableDebug` for instance-specific diagnostics.

Blue path lines are tracked per `AAIController`. A new or runtime-updated path replaces that controller's line immediately; invalidation, repath failure, abort, failure or successful completion clears it. Multiple AI paths can be shown at the same time.

With **Show Grid Cells** enabled, every navigable cell draws a translucent light-green quad projected onto its sampled tangent floor plane, dark-green borders, and the existing translucent green cell box. Non-walkable cells, including runtime blockers, draw neither quad nor box, so holes match the currently published traversable topology. Occupancy ignored by a query does not create a false hole: its existing indicator may recolor the box while the green floor quad remains visible. Cost coloring behaves the same way.

Yellow wire markers on the blue path identify the final center where the selected style requires a full stop. Intermediate gate planes are drawn as vertical wire rectangles with an arrow showing the only valid crossing direction: orange for **Center-Constrained** and violet for **Cell-by-Cell**. Their width is the exact lateral gate tolerance used by path following. Near the beginning of each active precise path, the label reads `ACCELERATED`, `DIRECT VELOCITY`, or `DIRECT VELOCITY / ACCELERATED FINAL`.

Enable **Draw Traffic Reservations** on `GridNavigationData` to inspect Reserved Corridor. Granted future cells use translucent turquoise floor quads with dark turquoise borders/boxes; each draws a line to its owning Pawn and a short `OccupantId`. Requested but ungranted cells are orange and the active conflict is red. Current occupancy and final parking remain in the ordinary occupancy visualization, so the turquoise layer shows only the short future corridor. `WAITING RESERVATION` appears without changing the blue line; the blue line changes only for a real repath. All owners are rendered together and every grant, release, gate advance, and repath invalidates the scene proxy immediately.

For the older Yield policies, cyan markers show the reactive look-ahead corridor. `YIELDING` and a red blocker marker identify a Pawn being waited on; `REPATHING` appears while the existing request is recalculated. These markers use the same global and local path-debug controls as the blue path and center gates.

The module uses the single `LogGridWorld` category. Validation errors identify the offending bounds or contributor.

A build that produces no navigable cells is rejected instead of replacing the previous valid snapshot. The log reports the affected volume and suggests checking placement, collision profile, slope, and capsule clearance.

If a custom path strategy appears to have no effect, verify that the configured `GridNavigationQueryFilter` subclass is assigned to the actual Move To or query. Two strategies may also choose the same path when the graph offers no meaningful distance-versus-turn tradeoff; increase **Balanced Turn Penalty** or use **Fewest Turns** to make turn preference more visible.

If a directional Move To stops short and a second request reaches the goal, inspect `LogGridWorld` for a **Max Search States** warning. Increase that advanced setting on the assigned filter. Disabling **Accept Partial Path** prevents the truncated path from completing successfully, but does not increase the available search budget.

If multiple agents still overlap at a **Move To Grid Cell** destination, enable **Redirect on Completion** or **Reserve Before Move** on that task and leave **Auto Register Pawn Occupancy** enabled. For 42 cm agents on 50 cm cells, keep **Max Alternative Search Radius** at two or more; a one-cell radius cannot provide capsule separation. Occupied cells appear through the existing occupancy visualization when **Show Navigation** and **Show Occupancy** are enabled.

If a following agent reaches the Pawn in front, verify that both use `GridWorldAIController`, assign a `GridNavigationQueryFilter` with **Dynamic Agent Policy** set to **Reserved Corridor**, and leave controller occupancy registration enabled. Use Center-Constrained or Cell-by-Cell for the strict gate contract. In a single-cell corridor with no alternate route, `WAITING RESERVATION` is expected; GridWorld does not push, teleport, enable RVO, or steer an agent off its center-gate trajectory.

## Persistence

Serialization has an explicit format magic and version. It stores generated topology, agent/settings hash, regions, chunks and cell adjacency. Runtime modifier overlays, occupancy and reservations are recomposed and are never serialized.

Every successful editor topology build, incremental geometry rebuild, and clear operation marks the package containing `AGridNavigationData` dirty. Save the level (or use **Save All** when External Actors/sublevels are involved) after the build. The versioned topology then reloads when the editor restarts or the level is closed and reopened; no automatic rebuild is required. Closing without saving still discards the build, like any other level change.

Format version 5 stores the compact floor normal of every generated cell. Version 2–4 payloads are consumed to their correct serialized end but deliberately rejected, because missing normals cannot safely reconstruct ramp clearance or adjacency. The level therefore loads without stale navigation and logs that a rebuild is required. Run **Build Grid World** once and save the level to persist version 5.

## Verification

Run the automated suite headlessly with:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests GridWorld.; Quit" -TestExit="Automation Test Queue Empty" -log
```

For manual acceptance, build a bounds volume over flat and inclined collision geometry, set both GridWorld and Character Movement to the required maximum slope, enable Show Navigation, issue **Move To Grid Cell**, move its target across cell boundaries, toggle a blocker and inspect the projected floor quads, affected path, cell, cost, occupancy and dirty chunk.
