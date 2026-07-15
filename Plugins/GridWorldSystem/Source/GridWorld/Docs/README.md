# GridWorld runtime module

`GridWorld` contains the navigation data, deterministic graph, generation, queries, runtime contributors, Blueprint facade and debug renderer.

## Ownership and lifetime

`AGridNavigationData` owns a base topology snapshot and a composed immutable snapshot. A short read/write lock is used only to copy or exchange the shared snapshot pointer. Query code retains a thread-safe `TSharedPtr<const FGridWorldSnapshot>` and never reads mutable UObject state during graph traversal.

Topology changes increment the topology generation encoded into `NavNodeRef`. Traversal and occupancy use independent revisions. A persistent `FGridCellId` is `{ GridId, X, Y, Layer }`; a `FGridCellHandle` is transient and must be rejected after its topology generation changes.

## Coordinate rules

Horizontal coordinates use floor division, including negative values. Cell centers are offset by half a cell in local X/Y. By default, layers are rounded relative to each grid origin every 50 cm; adding or removing a different surface does not renumber existing layers. The complete Actor rotation defines the rigid grid frame. Actor scale changes only bounds extents and never the default 100 x 100 x 50 cm logical cell dimensions.

`NavNodeRef` is encoded as:

```text
high 32 bits: non-zero topology generation
low 32 bits:  dense cell index + 1
```

Zero is always invalid.

## Generation

`FGridNavDataGenerator` runs collision sampling synchronously on the Game Thread. It traces along grid-local negative Z through each horizontal sample, validates the floor normal against Unreal world up, stores that normal as a normalized `FVector3f`, quantizes the local layer and tests a world-up 42 x 192 cm capsule for clearance. On an inclined plane, the upright capsule center uses `HalfHeight - Radius + Radius / UpDot + 1 cm`, keeping its lower hemisphere tangent to the floor instead of intersecting the ramp.

Adjacency is slope-aware and deterministic. The normals at both endpoints predict the continuous height change along the horizontal segment; their average is subtracted from the real world-Z delta. `MaxStepHeight` and `MaxDropHeight` apply only to the remaining discontinuity. This connects continuous ramps even when their per-cell rise exceeds the step limit while preserving actual stairs and ledges. Adjacency never crosses a `GridId` without an explicit link.

Collision queries ignore every `APawn` in the world. Pawns consume navigation and may move at runtime, so baking them into floor or clearance sampling would invalidate their own start cells. Dynamic blocking and reservation remain the responsibility of occupancy/modifier overlays.

Dirty navigation areas map to 16 x 16 chunks with a one-chunk horizontal halo. Navigation-bounds changes and explicit builds always apply. Native geometry dirty areas apply only to intersecting regions whose bounds volume has **Auto Rebuild On Geometry Changes** enabled; the default is enabled. This covers navigation-relevant geometry addition, removal, Transform, and collision changes when the Editor's global **Update Navigation Automatically** preference is active. A candidate topology is validated before dirty chunks are merged, so a failed build preserves the published snapshot.

Successful editor topology publication and clear operations mark the package containing `AGridNavigationData` dirty. Saving that package persists the versioned base snapshot across editor restarts and level unload/reload. Runtime overlays, occupancy, and reservations never dirty or serialize generated topology.

## Query behavior

`FGridAStar` is a non-UObject algorithm using integer costs, preallocated node memory, deterministic neighbor order and deterministic tie breaks. Orthogonal movement costs 1000 and diagonal movement costs 1414 before cell, area, modifier or occupancy costs. The ordinary heuristic ignores `Layer`: changing quantized layer while walking a ramp is not an additional graph step. Diagonal no-corner-cutting resolves the two actually published orthogonal neighbors by horizontal coordinate, so those neighbors may occupy different layers on an incline.

`UGridNavigationQueryFilter::PathOptimizationMode` selects the objective per query and defaults to **Balanced**. **Shortest Path** preserves the original cell-state A* path, heuristic, performance and tie-break behavior. **Fewest Turns** and **Balanced** expand search state to `(Cell, IncomingDirection)`, allocating eight local horizontal directions plus an initial/no-direction state only for those modes. Their zero-heuristic search is correct with arbitrary area, modifier, occupancy, reservation and link costs. `VisitedNodes` therefore counts directional states and may exceed the number of cells.

Unreal's generic `FNavigationQueryFilter` starts with a 2048-node budget. `UGridNavigationQueryFilter` overrides it only for directional optimization using the advanced `MaxSearchStates` property, default **65536**, and copies the effective value into both the native filter and GridWorld backend. Shortest Path deliberately retains the native budget. A directional search that exhausts its budget sets both `IsPartial()` and `DidSearchReachedLimit()` when partial paths are allowed. `AGridNavigationData` emits one contextual `LogGridWorld` warning per AI controller, including the mode, limit, goal and partial endpoint.

Fewest Turns orders states lexicographically by turn count and then real traversal cost. Balanced orders by `TraversalCost + TurnCount * BalancedTurnPenalty * 1000`, then fewer turns and lower real cost. The default penalty is 2.0 equivalent orthogonal cells. Direction is derived from the sign of local X/Y delta, ignoring `Layer`; the first segment is free, direction changes and reversals add one turn, and explicit links reset direction. Partial results first choose the reached cell with the smallest geometric distance to the goal, then apply the selected objective. Returned path cost excludes the synthetic turn penalty, while `FGridNavigationPath` and `FGridPathQueryResult` retain both the selected mode and final turn count.

The filter class flows through ordinary Unreal Move To requests, all three **Move To Grid Cell** integrations, and subsystem/Blueprint queries. An AI controller's Default Navigation Filter Class can apply it to all of that controller's native moves. It is query metadata only and never changes topology, generated-data hashing or serialization.

`DynamicAgentPolicy` is another per-query setting. **Yield** keeps the selected corridor and lets `UGridWorldPathFollowingComponent` wait before an occupied intermediate cell. **Yield Then Repath** temporarily excludes other non-reservation occupancy owners from A* after a configurable stationary delay. **Reserved Corridor** additionally uses the NavData-owned `FGridTrafficReservationManager` to protect a rolling capsule-aware prefix, including cells, swept transitions, opposite traversal, diagonal crossing, vertical overlap, explicit links, goal claims and parking. Mutations stay on the Game Thread; A*, debug and waiting tasks retain an immutable revisioned snapshot. None of this runtime state is serialized or requires a topology rebuild.

`ReservedLookAheadCells` is the designer minimum and defaults to 1. The path follower extends it until the protected distance covers braking plus both agent radii and separation, then releases the previous logical cell immediately after the next center gate is crossed. Granted reservations are never subtracted. Conflicts use continuous wait age and stable identity for deterministic priority. `DynamicAgentRepathDelay` defaults to 0.1 seconds; a delayed repath avoids other reservations when a detour exists, otherwise the original path remains valid and the agent waits without repeated invalidation.

The region's Movement Mode is the maximum traversal policy. Four Directions regions reject ordinary diagonal adjacency even if a query requests Eight Directions, including when an old or manually assembled snapshot contains a diagonal neighbor. Corner cutting requires permission from both the region and query filter. Explicit links are evaluated separately and may connect non-adjacent cells.

Standard `ANavigationData` callbacks implement projection, find/test path, raycast, movement along surface, length/cost and random point queries. `FGridNavigationPath` stores cell IDs, node refs, world points, segment cost, total length, traversed links, revisions and the maximum floor slope crossed. On the Game Thread, an AI-controlled Character receives one `LogGridWorld` warning when that path slope is higher than `UCharacterMovementComponent::WalkableFloorAngle`; GridWorld does not mutate the Character setting.

The Blueprint facade is intentionally read-only. Use validated contributor components to mutate runtime traversal state.

## AI movement integration

`UGridMoveToCellTask` derives from `UAITask_MoveTo` and is the shared implementation used by `UBTTask_MoveToGridCell`, the asynchronous Blueprint **Move To Grid Cell** node and `FStateTreeMoveToGridCellTask`. It selects the NavData through the controller's Supported Agent, projects Actor or Vector input, and sends the resulting cell center through the native `AIController::MoveTo` and path-following lifecycle.

Actor destinations use `INavAgentInterface::GetNavAgentLocation` and `GetMoveGoalOffset` when implemented. Tracking binds to the Actor root component's `TransformUpdated` delegate and reuses the active task only when the projected `FGridCellId` changes. Target destruction and projection/NavData failures finish observably through the inherited async delegates. StateTree Vector bindings continue to use the native tolerance-based tracking; Blueprint Vector values are captured at activation.

The debug renderer stores active path data per controller. It observes path creation, update, invalidation and repath failure, then reconciles completion on the next frame so the abort emitted by replacing a request cannot clear the replacement path.

`AGridWorldAIController` replaces the native default path-following subobject with `UGridWorldPathFollowingComponent`. `FGridNavigationPath` carries immutable per-point region rotation, style, drive mode, tolerances, cell identity and stop classification. Standard points delegate entirely to the engine component. Precise points use local-grid horizontal position and speed tests; the starting world position is retained so an off-center Pawn first visits its current cell center.

Both precise styles replace intermediate point acceptance with monotonic local-grid gates. A gate is a plane through the cell center, normal to the incoming segment, and may be crossed only in the forward direction. Its half-width is `max(CellCenterTolerance, min(CellSize.X, CellSize.Y) * 0.25)`, which is 12.5 cm for the default cells. The sweep between consecutive feet locations evaluates the exact plane intersection. Crossing inside the strip advances without a speed requirement; crossing outside finishes `Blocked` rather than steering back toward a waypoint that is now behind the Pawn.

**Center-Constrained** targets the initial center, turns and both sides of explicit links while skipping collinear centers. **Cell-by-Cell** retains every intermediate center as a gate. The independent **Path Drive Mode** is applied only to those precise styles. **Accelerated** forces `FNavMovementProperties::bUseAccelerationForPaths` while active and uses the movement interface's braking-distance contract at the final center. **Direct Velocity** forces that flag off, also forces `UCharacterMovementComponent::bRequestedMoveUseAcceleration` off when applicable, and calls `RequestDirectMove` at constant maximum navigation speed on intermediate segments. Its final segment uses `min(MaxSpeed, Distance / DeltaTime)` and calls `StopMovementKeepPathing` inside the center tolerance. **Use Accelerated Final Approach** switches only that last segment back to Accelerated.

Both changed movement flags are saved before mutation and restored on every completion, failure, abort, repath, Pawn or movement-interface replacement, Reset and Cleanup. Standard paths do not mutate them. Final precise completion ignores task acceptance/overlap by disabling direct movement to a tracked goal and requiring both configured tolerances. GridWorld does not alter Actor rotation, collision, RVO, root motion or replication.

Active path debug data carries the same immutable gate geometry and drive policy consumed by movement. The scene proxy draws each plane as a wire rectangle plus a forward arrow, so the rendered width and direction are not an approximation of the runtime rule, and labels each active precise path `ACCELERATED`, `DIRECT VELOCITY`, or `DIRECT VELOCITY / ACCELERATED FINAL`.

The GridWorld path follower also performs capsule-aware dynamic-agent look-ahead when selected by the query filter. Reactive Yield policies inspect occupancy; Reserved Corridor requests an exclusive rolling prefix from the NavData traffic authority. The effective distance combines the designer minimum cells, Character braking distance, both capsule radii and additional separation. Accelerated paths request braking without changing direction; Direct Velocity stops through `StopMovementKeepPathing` before an ungranted gate. It suppresses native block detection while intentionally yielding, preserves the current center gate, and invalidates the same observed path only after the configured stationary delay. Repath fallback memory prevents repeatedly recalculating the same blocked one-cell corridor, while a different blocker can trigger another repath. RVO remains untouched.

Cell visualization adds one batched translucent mesh. Each walkable cell's four horizontal corners are projected along the floor-sampling axis onto the tangent plane through `WorldCenter` and `FloorNormal`, then offset 2 cm along the normal to avoid z-fighting. Light-green fill communicates walkable topology and dark-green edges delimit cells. The box remains available for cost and occupancy coloring. Cells made non-walkable by authored or runtime blockers are omitted from all three cell layers.

When `AGridNavigationData::bDrawTrafficReservations` is enabled, the same scene proxy draws every owner's granted future cells in turquoise, waiting requests in orange, and the first conflict in red. A line and shortened occupancy GUID connect each future cell to the copied Pawn location. Current occupancy and parking do not appear in this layer. `WAITING RESERVATION` changes independently from the blue path; only a real replacement query changes that path line.

## Runtime contributors

Cost composition is deterministic:

1. sum additive costs;
2. apply multiplicative costs;
3. resolve overrides by highest priority and then stable contributor ID;
4. apply blockers, which always prevail;
5. authored unblock requests affect only authored, explicitly removable blocks.

For duplicate link IDs, any disabled contributor disables the link. Occupancy and reservations remain separate from traversal and are ignored by default.

Every composed occupied cell also retains its runtime-only `OccupancyOwners`. This lets goal arbitration ignore the moving Pawn's own footprint while detecting other agents. The owner list, reservations and all other occupancy data remain outside serialized topology.

`UGridMoveToCellTask` optionally arbitrates endpoint contention with `Ignore`, `RedirectOnCompletion` or `ReserveBeforeMove`. Claims use the same NavData traffic authority as Reserved Corridor, are mutated only on the Game Thread, and reference their task/Pawn weakly. A candidate claim is accepted atomically only when it respects claims, current/future corridor shapes, occupied cells, query area/channel rules and capsule separation. If no destination is available, the outer task stays running and wakes on traffic/occupancy changes; warnings are rate-limited and the continuous episode ends `Blocked` after `GoalAvailabilityTimeout`. Reaching a claim converts it into Pawn-lifetime parking protection.

`RedirectOnCompletion` intercepts `UAITask_MoveTo::OnRequestFinished` before the inherited task broadcasts completion. A contested destination is added to the task's rejected set, the old claim is released, a new candidate is claimed and `ConditionalPerformMove` runs on the next frame. The rejected set has no one-redirect limit, so the same outer task can repeat this transition for every fallback that becomes occupied. `Blocked` results redirect only when the endpoint is actually contested; unrelated movement failures preserve native failure behavior. If the Pawn already occupies the requested or nearest equally ranked available cell, the task succeeds without starting a redundant MoveTo.

When enabled, automatic Pawn tracking reuses an active non-reservation `UGridNavigationOccupancyComponent` or creates a transient attached one sized from the controller's nav-agent radius and height. The generated tracker has zero occupancy cost and is non-blocking so it cannot invalidate or obstruct its owner's path. Occupancy components cache their affected `FGridCellId` set and publish an overlay update only when that set changes; transform updates within the same footprint do not rebuild the overlay.

`AGridWorldAIController` performs that registration at possession time by default, so stopped Pawns remain visible to every native and task-based Move To. The query filter copies the requester's own `OccupantId` into immutable search state, preventing a Pawn's footprint from excluding itself. An in-memory per-world owner registry resolves blocker velocity without Actor scans; it does not own grid topology or duplicate cell state.

## Troubleshooting

- No cells: confirm the floor blocks the selected collision profile and the capsule has clearance.
- A generated ramp is disconnected: verify `Max Slope Degrees` accepts its floor normal. Continuous ramp rise is slope-aware; `Max Step Height` applies only to residual discontinuities.
- A path exists but the Character stops on a ramp: set Character Movement **Walkable Floor Angle** at least as high as the path slope reported by `LogGridWorld`. GridWorld intentionally does not override it.
- Build rejected: run **Build > Grid World > Validate Grid World** and inspect `LogGridWorld`; scale components must be finite and non-zero.
- `MoveTo` selects another NavData: verify the `SupportedAgents` entry and `PreferredNavData` in `DefaultEngine.ini`.
- **Move To Grid Cell** fails immediately: verify the controller resolves `AGridNavigationData` for its Supported Agent and the destination lies within projection extent of a navigable cell.
- A Move To task repeatedly fails without moving: rebuild Grid World after upgrading. Pawns are ignored by current generation, but an older serialized snapshot may still contain a hole under a placed AI.
- A Four Directions Character rounds a corner: inspect the logical blue path points; standard path following and Character Movement may smooth the physical turn, but consecutive cell IDs change only one local horizontal axis.
- A precise path follows centers only approximately or behaves like Standard: the AI controller must derive from `AGridWorldAIController`; the log names controllers using an incompatible path-following component.
- A precise AI is blocked before completion: verify that the final center tolerance is reachable with the Pawn capsule and collision geometry. No position snap is applied.
- A precise move reports a missed center gate: inspect the orange/violet gate rectangle and its arrow. The Pawn crossed the plane more than one quarter-cell from the center; collision, excessive lateral velocity or a path-incompatible movement modifier pushed it outside the permitted strip.
- Direct Velocity still deviates from the blue segment: GridWorld does not disable collision, RVO, root motion or replication. Inspect the gate and movement modifiers; a deviation outside the gate intentionally ends as `Blocked` rather than reversing.
- A path optimization mode appears unchanged: ensure the intended `UGridNavigationQueryFilter` subclass is assigned to the request or AI controller. If both objectives select the same route, raise **Balanced Turn Penalty** or use **Fewest Turns** to enforce turn priority.
- A Fewest Turns or Balanced Move To stops before a reachable target: raise the filter's advanced **Max Search States** value. The default 65536 replaces UE's 2048-node budget for directional searches. Turning off **Accept Partial Path** makes a truncated request fail instead of treating its best progress as a completed move.
- A newly placed obstacle does not update the grid: enable **Auto Rebuild On Geometry Changes** on the intersecting bounds, enable the Editor preference **Update Navigation Automatically**, and confirm the mesh collision is navigation-relevant and blocks the selected collision profile.
- Multiple agents stop on the same goal: set **Goal Contention Policy** on **Move To Grid Cell**. `RedirectOnCompletion` preserves the requested destination until completion; `ReserveBeforeMove` avoids competing for it before movement begins. Keep the alternative radius large enough for the agent diameter, not merely one adjacent cell.
- A following agent contacts a stopped Character: use `GridWorldAIController`, select **Reserved Corridor** on the assigned Grid Navigation Query Filter, keep controller auto-registration enabled, and use Center-Constrained or Cell-by-Cell for strict gate enforcement. It waits in a one-cell corridor and detours only when another valid route exists; it never enables RVO or changes the center-gate path.
- Traffic reservations are not visible: enable Show Navigation, `GridWorld.Debug.Visual 1`, path/cell debug, and **Draw Traffic Reservations** on `GridNavigationData`. Turquoise is granted future corridor, orange is waiting, red is the active conflict; current/parked cells remain under occupancy debug.
- Generated cells disappear after reopening a level: rebuild once, then save the level or use **Save All** so the `GridNavigationData` package (including an External Actor package when applicable) is written.
- No visualization: enable Show Navigation, set `GridWorld.Debug.Visual 1`, and enable the desired local draw filters.
- Stale handle/node ref: resolve the persistent `FGridCellId` again after a topology rebuild.
- Serialized version 2–4 rejected after upgrading: rebuild Grid World once and save the level. Version 5 is required because it persists per-cell floor normals used by ramps and floor-projected debug.
