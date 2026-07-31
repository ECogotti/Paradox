# GridWorld runtime module

`InvalidStart + RecalculateToOriginalGoal` emits a contextual warning before its existing
controller-aware recalculation. All non-start exact-path failures remain strict.

`GridWorld` contains the navigation data, deterministic graph, generation, queries, runtime contributors, Blueprint facade and debug renderer.

## Ownership and lifetime

`AGridNavigationData` owns a base topology snapshot and a composed immutable snapshot. A short read/write lock is used only to copy or exchange the shared snapshot pointer. Query code retains a thread-safe `TSharedPtr<const FGridWorldSnapshot>` and never reads mutable UObject state during graph traversal.

Topology changes increment the topology generation encoded into `NavNodeRef`. Traversal and occupancy use independent revisions. A persistent `FGridCellId` is `{ GridId, X, Y, Layer }`; a `FGridCellHandle` is transient and must be rejected after its topology generation changes.

## Coordinate rules

Horizontal coordinates use floor division, including negative values. `FGridTransform` retains half-cell local X/Y centers, while `AGridNavigationBoundsVolume` offsets that transform so cell `(0,0,0)` is centered at world origin. Moving or scaling the Actor changes only the covered coordinate range; it does not translate the lattice. The complete Actor rotation defines the rigid grid orientation about world origin. By default, layers are rounded every 50 cm, and logical cell dimensions remain 100 x 100 x 50 cm.

`NavNodeRef` is encoded as:

```text
high 32 bits: non-zero topology generation
low 32 bits:  dense cell index + 1
```

Zero is always invalid.

## Generation

`FGridNavDataGenerator` runs collision sampling synchronously on the Game Thread. It traces along grid-local negative Z through each horizontal sample, validates the floor normal against Unreal world up, stores that normal as a normalized `FVector3f`, quantizes the local layer and tests a world-up 42 x 192 cm capsule for clearance. Successive floor traces may restart inside a thick or inclined collision solid; Chaos reports those results with `bStartPenetrating`, and GridWorld rejects them before layer quantization. The sampler continues below the solid, so a real separate floor remains available as another layer while the interior of one platform cannot create stacked cells. On an inclined plane, the upright capsule center uses `HalfHeight - Radius + Radius / UpDot + 1 cm`, keeping its lower hemisphere tangent to the floor instead of intersecting the ramp. When that base pose overlaps geometry, the sampler checks deterministic lifted poses up to `MaxStepHeight`. This mirrors UE's low-obstacle/step semantics without shrinking the capsule: a low curb may remain navigable, while a wall or overhead obstruction blocks every legal pose.

Adjacency is slope-aware and deterministic. The normals at both endpoints predict the continuous height change along the horizontal segment; their average is subtracted from the real world-Z delta. `MaxStepHeight` and `MaxDropHeight` apply only to the remaining discontinuity. This connects continuous ramps even when their per-cell rise exceeds the step limit while preserving actual stairs and ledges. Adjacency never crosses a `GridId` without an explicit link.

Collision queries ignore every `APawn` in the world. Pawns consume navigation and may move at runtime, so baking them into floor or clearance sampling would invalidate their own start cells. Dynamic blocking and reservation remain the responsibility of occupancy/modifier overlays.

Dirty navigation areas map to 16 x 16 chunks with a one-chunk horizontal halo. Navigation-bounds changes and explicit builds always apply. Native geometry dirty areas apply only to intersecting regions whose bounds volume has **Auto Rebuild On Geometry Changes** enabled; the default is enabled. This covers navigation-relevant geometry addition, removal, Transform, and collision changes when the Editor's global **Update Navigation Automatically** preference is active. A candidate topology is validated before dirty chunks are merged, so a failed build preserves the published snapshot.

`AGridNavigationData` is configured with `RuntimeGeneration = Dynamic`, so the same native dirty-area path also rebuilds Game/PIE topology for Movable geometry. The moved component must be navigation-relevant, block the bounds collision profile, intersect an enabled `GridNavigationBoundsVolume`, and that volume must allow automatic geometry rebuilds. A platform contributes cells only after generation has sampled its current pose; no cell frame follows it continuously while it moves. Prefer `UGridNavigationModifierComponent` when an authored obstacle only needs to change existing cells and does not introduce a new walkable surface.

Successful editor topology publication and clear operations mark the package containing `AGridNavigationData` dirty. Saving that package persists the versioned base snapshot across editor restarts and level unload/reload. Runtime overlays, occupancy, and reservations never dirty or serialize generated topology.

## Query behavior

`FGridAStar` is a non-UObject algorithm using integer costs, preallocated node memory, deterministic neighbor order and deterministic tie breaks. Orthogonal movement costs 1000 and diagonal movement costs 1414 before cell, area, modifier or occupancy costs. The ordinary heuristic ignores `Layer`: changing quantized layer while walking a ramp is not an additional graph step. Diagonal no-corner-cutting resolves the two actually published orthogonal neighbors by horizontal coordinate, so those neighbors may occupy different layers on an incline.

`UGridNavigationQueryFilter::PathOptimizationMode` selects the objective per query and defaults to **Balanced**. **Shortest Path** preserves the original cell-state A* path, heuristic, performance and tie-break behavior. **Fewest Turns** and **Balanced** expand search state to `(Cell, IncomingDirection)`, allocating eight local horizontal directions plus an initial/no-direction state only for those modes. Their zero-heuristic search is correct with arbitrary area, modifier, occupancy, reservation and link costs. `VisitedNodes` therefore counts directional states and may exceed the number of cells.

Unreal's generic `FNavigationQueryFilter` starts with a 2048-node budget. `UGridNavigationQueryFilter` overrides it only for directional optimization using the advanced `MaxSearchStates` property, default **65536**, and copies the effective value into both the native filter and GridWorld backend. Shortest Path deliberately retains the native budget. A directional search that exhausts its budget sets both `IsPartial()` and `DidSearchReachedLimit()` when partial paths are allowed. `AGridNavigationData` emits one contextual `LogGridWorld` warning per AI controller, including the mode, limit, goal and partial endpoint.

Fewest Turns orders states lexicographically by turn count and then real traversal cost. Balanced orders by `TraversalCost + TurnCount * BalancedTurnPenalty * 1000`, then fewer turns and lower real cost. The default penalty is 2.0 equivalent orthogonal cells. Direction is derived from the sign of local X/Y delta, ignoring `Layer`; the first segment is free, direction changes and reversals add one turn, and explicit links reset direction. Partial results first choose the reached cell with the smallest geometric distance to the goal, then apply the selected objective. Returned path cost excludes the synthetic turn penalty, while `FGridNavigationPath` and `FGridPathQueryResult` retain both the selected mode and final turn count.

The filter class flows through ordinary Unreal Move To requests, all three **Move To Grid Cell** integrations, and subsystem/Blueprint queries. An AI controller's Default Navigation Filter Class can apply it to all of that controller's native moves. It is query metadata only and never changes topology, generated-data hashing or serialization.

`DynamicAgentPolicy` is another per-query setting. **Yield** keeps the selected corridor and lets `UGridWorldPathFollowingComponent` wait before an occupied intermediate cell. **Yield Then Repath** temporarily excludes other non-reservation occupancy owners from A* after a configurable stationary delay. **Reserved Corridor** additionally uses the NavData-owned `FGridTrafficReservationManager` to protect a rolling capsule-aware prefix, including cells, swept transitions, opposite traversal, diagonal crossing, vertical overlap, explicit links, goal claims and parking. Mutations stay on the Game Thread; A*, debug and waiting tasks retain an immutable revisioned snapshot. None of this runtime state is serialized or requires a topology rebuild.

The plugin `BP_GridQueryFilter_Balanced` asset deliberately combines **Occupancy Policy = Ignore** with **Dynamic Agent Policy = Reserved Corridor**. Automatic Pawn occupancy is identity publication, not an ordinary A* blocker; the corridor manager remains the owner-aware authority for intermediate cells. Preview, exact injected movement and the final executed request should use this same filter class so they evaluate one search policy.

`ReservedLookAheadCells` is the designer minimum and defaults to 1. The path follower extends it until the protected distance covers braking plus both agent radii and separation, then releases the previous logical cell immediately after the next center gate is crossed. Granted reservations are never subtracted. Conflicts use continuous wait age and stable identity for deterministic priority. `DynamicAgentRepathDelay` defaults to 0.1 seconds; a delayed repath avoids other reservations when a detour exists, otherwise the original path remains valid and the agent waits without repeated invalidation.

The region's Movement Mode is the maximum traversal policy. Four Directions regions reject ordinary diagonal adjacency even if a query requests Eight Directions, including when an old or manually assembled snapshot contains a diagonal neighbor. Corner cutting requires permission from both the region and query filter. Explicit links are evaluated separately and may connect non-adjacent cells.

Standard `ANavigationData` callbacks implement projection, find/test path, raycast, movement along surface, length/cost and random point queries. `FGridNavigationPath` stores cell IDs, node refs, world points, segment cost, total length, traversed links, revisions and the maximum floor slope crossed. On the Game Thread, an AI-controlled Character receives one `LogGridWorld` warning when that path slope is higher than `UCharacterMovementComponent::WalkableFloorAngle`; GridWorld does not mutate the Character setting.

The Blueprint facade is intentionally read-only. Use validated contributor components to mutate runtime traversal state.

## Runtime cell presentation

`UGridRuntimeVisualizationSubsystem` is the local gameplay presentation layer for published cells. It is separate from `UGridNavigationRenderingComponent`: Show Navigation, editor debug filters and `GridWorld.Debug.Visual` do not enable it. The subsystem exists only in Game and PIE worlds, is not created on a dedicated server, is disabled by default, never Ticks and never writes to `AGridNavigationData`.

### Blueprint setup and lifecycle

At Begin Play, get the **Grid Runtime Visualization Subsystem** from the World and call **Enable Visualization**. Leaving **Style** unset loads the plugin default:

```text
/GridWorldSystem/Presentation/DA_GridRuntimeCellStyle_Default
```

The default uses `SM_GridRuntimeCell` and `M_GridRuntimeCell`: a collisionless runtime plane with an Unlit, Translucent, two-sided, depth-tested material. Traversable cells are translucent green, hover is yellow, selection is cyan and blocked cells have zero alpha. Presentation components always force `NoCollision`, disable overlaps, and set `Can Ever Affect Navigation` false regardless of the selected mesh.

Use the lifecycle operations intentionally:

- **Enable Visualization** lazily spawns one transient, non-replicated Actor and builds one HISM per existing 16 x 16 GridWorld chunk.
- **Set Visualization Visible** hides or shows that Actor without releasing its mappings or render resources.
- **Rebuild Visualization** recreates structural mappings from the current snapshot and invalidates all previous internal handles.
- **Disable Visualization** destroys the Actor and every HISM but retains hover, selection, and path-session semantics.
- **Clear Hovered Cells**, **Clear Selected Cells**, or **Clear Interaction States** explicitly remove semantic interaction state.

Every gameplay operation addresses cells exclusively through `FGridCellId`: **Set Cell Hovered**, **Set Cell Selected**, their clear operations, and **Get Cell Visual State**. HISM components and instance indices are private implementation details. Hover and selection are stored independently; selection resolves above hover, so clearing hover never removes a prior selection.

Equivalent C++ setup is:

```cpp
#include "Presentation/GridRuntimeVisualizationSubsystem.h"

if (UGridRuntimeVisualizationSubsystem* Presentation =
	World->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
{
	Presentation->EnableVisualization();
	Presentation->SetCellSelected(CellId, true);
}
```

Topology publication and `ClearGridWorld` now broadcast `UGridWorldSubsystem::OnGridWorldChanged`, matching traversal and occupancy publication. A topology change causes a complete presentation rebuild. Traversal/occupancy-only changes update custom data only for `FGridChangeSet::ChangedCells`. Neither path changes navigation snapshots or their topology, traversal, or occupancy revisions.

### Custom style and material contract

Create a **Grid Cell Visual Style** Data Asset to replace the mesh, material, colors, per-edge inset, floor-normal offset, culling distances, or shadow setting. Assign the cell material to the style: it is an explicit HISM slot override, so a material assigned only to the Static Mesh asset is not used. The material must enable **Usage > Used with Instanced Static Meshes** or Unreal will render its default fallback material. The supplied `M_GridRuntimeCell` already enables this usage. The mesh must have finite non-zero X/Y bounds and should be an XY plane with +Z normal. Each instance is centered on `FGridCellData::WorldCenter`, oriented to `FloorNormal` with the region X axis projected into the floor plane, inset within the logical cell, and offset along the normal to avoid z-fighting.

A custom material receives ten Per Instance Custom Data floats:

| Index | Channel | Meaning |
|---:|---|---|
| 0 | Interaction | `Unselected`, `Hovered`, or `Selected` enum value |
| 1 | Path | Independent preview/active/current/traversed/destination/invalid path state |
| 2 | NavigationFlags | Bit mask for traversable, blocked, high cost, occupied, and reserved |
| 3 | Emphasis | General semantic emphasis value |
| 4–7 | ResolvedRGBA | Final style color; the default material reads these channels |
| 8 | PathProgress | Normalized position of the winning path occurrence from 0 to 1 |
| 9 | CustomValue | Style-specific scalar |

Materials that only need the final appearance can read indices 4–7. More advanced materials may combine the semantic layers, but must preserve the ten-float layout defined by `FGridCellMaterialDataLayout`. Do not present a path by setting cells to `Selected`.

### Path presentation sessions

`UGridPathPresentationSubsystem` is created beside the cell and path-line visualization subsystems only in Game/PIE render-capable Worlds. It has no Tick, owns no renderer object, and stores local non-replicated session snapshots. A session retains semantic path state even while either renderer is disabled; enabling a backend later renders the current session state.

In Blueprint, construct `FGridPathPresentationRequest` with ordered `FGridCellId` values and call **Create Path Presentation**, or reuse the `Cells` and `Revisions` from a successful/partial `FGridPathQueryResult` through **Create Path Presentation From Query Result**. Store the returned handle and use:

- **Update Path Presentation** and **Update Path Presentation Progress**;
- **Set Path Presentation Visible**, **Priority**, or **Mode**;
- **Set Path Presentation Renderers** to select cell overlay, strict line, both, or neither;
- **Mark Path Presentation Invalid**;
- **Clear Path Presentation** to retain a reusable handle;
- **Release Path Presentation** to invalidate every copy of that handle.

Every create/update validates all cells against the current topology before mutating the session. Manual sessions require explicit release. **Owner Lifetime** requires a valid UObject, retains it weakly, and removes the session after owner collection. Handles are opaque, world-local IDs; HISM components and indices remain private.

Active progress modes resolve as follows:

- **All Cells** uses the active-remaining state for the complete path and a distinct destination;
- **Remaining Only** hides the current and traversed prefix;
- **Traversed and Remaining** distinguishes traversed, current, remaining, and destination;
- **Current and Remaining** hides only the traversed prefix;
- **Destination Only** contributes only the final cell;
- **Endpoints and Turns** contributes start, local-coordinate direction changes, grid/layer boundaries, and destination.

Preview contributions always use `Preview`. Invalid sessions show `Invalid` on every cell still present in topology. Overlap resolution is larger explicit priority, `Active` over `Preview`, then `Invalid > Destination > ActiveCurrent > ActiveRemaining > ActiveTraversed > Preview`, followed by the older immutable session sequence. A repeated cell within one session first uses that semantic state order and then its latest occurrence. `Replace Immediately` clears obsolete cells; `Preserve Traversed` retains only the already traversed prefix and only with **Traversed and Remaining**.

Equivalent C++ preview setup is:

```cpp
#include "Presentation/GridPathPresentationSubsystem.h"

FGridPathPresentationRequest Request;
Request.Cells = PathResult.Cells;
Request.SourceRevisions = PathResult.Revisions;
Request.Purpose = EGridPathPresentationPurpose::Preview;

FGridPathPresentationHandle Handle;
if (UGridPathPresentationSubsystem* Paths = World->GetSubsystem<UGridPathPresentationSubsystem>())
{
	Paths->CreatePathPresentation(Request, Handle);
	// Later: Paths->ReleasePathPresentation(Handle);
}
```

### Optional strict-line renderer

`UGridPathLineVisualizationSubsystem` is the independent Milestone 3 backend. Call **Enable Line Visualization** in Begin Play; leaving **Style** unset loads:

```text
/GridWorldSystem/Presentation/DA_GridRuntimePathLineStyle_Default
```

It lazily creates one transient, non-replicated, no-Tick Actor with collisionless HISM components for strict path segments and optional point markers. **Set Line Visualization Visible** preserves those resources, while **Disable Line Visualization** destroys them without releasing path sessions. It is absent on dedicated servers and never changes a navigation snapshot or revision.

Renderer selection is per session and independent from global backend lifecycle:

| Render Cell Overlay | Render Line | Result when corresponding backends are enabled |
|---|---|---|
| true | false | Cell highlights only |
| false | true | Strict line and optional markers only |
| true | true | Cell highlights and line together |
| false | false | No rendering; the reusable session and its progress remain alive |

Create a **Grid Path Line Visual Style** Data Asset to edit line geometry. `SegmentMesh` is stretched between displayed path points along its local X axis. `MarkerMesh` is optional; setting it to null removes point markers. Segment and marker materials, semantic colors, width, thickness, marker size, floor-normal offset, culling, and shadow behavior all belong to this line style, not to `GridCellVisualStyle`. Every assigned material must enable **Used with Instanced Static Meshes**.

The line material receives six Per Instance Custom Data floats:

| Index | Channel | Meaning |
|---:|---|---|
| 0 | PathState | Preview/current/remaining/traversed/destination/invalid enum value |
| 1 | PathProgress | Normalized logical position from 0 to 1 |
| 2–5 | ResolvedRGBA | Color resolved by `UGridPathLineVisualStyle` |

`AllCells` and `TraversedAndRemaining` draw the full strict polyline. Remaining modes start at the current logical point, `DestinationOnly` draws only its marker, and `EndpointsAndTurns` joins the logical endpoints and turns without changing the authoritative path. No smoothing is performed and the line is never movement input.

Example C++ setup:

```cpp
#include "Presentation/GridPathLineVisualizationSubsystem.h"
#include "Presentation/GridPathPresentationSubsystem.h"

UGridPathLineVisualizationSubsystem* Lines =
	World->GetSubsystem<UGridPathLineVisualizationSubsystem>();
Lines->EnableLineVisualization(CustomLineStyle);

FGridPathPresentationRequest Request;
Request.Cells = Cells;
Request.bRenderCellOverlay = false;
Request.bRenderLine = true;
Presentation->CreatePathPresentation(Request, Handle);
```

For AI-controlled active paths, enable **Present Active Path** on `UGridWorldPathFollowingComponent` or call **Set Active Path Presentation Enabled**. The default mode is **Traversed and Remaining**. **Present Active Path As Cell Overlay** defaults true and **Present Active Path As Line** defaults false; select any of the four renderer combinations before or during movement through **Set Active Path Presentation Renderers**. **Set Active Path Presentation Settings** changes mode and overlap priority. The component snapshots `FGridNavigationPath::CellPath`, reuses its handle after recalculation, observes native invalidation and repath failure, and releases symmetrically on finish, abort, Reset, Pawn/movement-interface replacement, or Cleanup. Its Blueprint delegates report path changes, deduplicated logical progress, invalidation, and finish result independently from whether automatic visualization is enabled.

### Path prediction and exact commit

`UGridPathPreviewComponent` is the input-independent Milestone 4 owner. Add it to a Controller, Pawn, or interaction Actor, then call **Update Preview for Controller** with a controller and an `FGridCellId`. Mouse, gamepad, tactical UI, AI planning, and replay tools all use the same entry point; the component never reads input itself and never commands movement.

Prediction uses the normal `AGridNavigationData` query, `UGridNavigationQueryFilter`, supported-agent properties, `UPathFollowingComponent::OnPathfindingQuery` context, immutable snapshot and `FGridNavigationPath`. It is synchronous and has no Tick. Repeating the same semantic request reuses the last result; the signature changes for navigation-data identity, start/goal cell, agent, initialized filter context, topology/traversal revisions, relevant occupancy/traffic revisions, partial policy, or injected invalidation policy.

The component exposes `FGridPathPreviewResult`, including status, ordered path data, start/goal, opaque query signature, request generation, stale state and commit eligibility. `StalePolicy` selects **Keep but Mark Stale**, **Clear Immediately**, or **Recalculate Automatically**. Partial results independently select **Show and Allow Commit** (default), **Show but Block Commit**, or **Hide and Block Commit**. A stale preview is never exported as executable: **Prepare Preview for Commit** synchronously refreshes when the current start or revisions changed and returns false when the current policy blocks commit.

**Goal Contention Policy** defaults to `StopBeforeOccupied`. Prediction resolves the requester's occupancy identity and asks `AGridNavigationData` to inspect both final-cell `OccupancyOwners` and traffic claims. When the requested goal is foreign-owned, A* still computes the complete route to it; prediction then removes exactly that final cell. `FGridPathPreviewResult::RequestedGoalCell` retains the input, `GoalCell` becomes the immediate predecessor, and `bGoalAdjustedForContention` is true. Both presentation backends and the injected payload use this exact prefix.

The predecessor must itself be claimable. Execution repeats the check atomically and also handles occupancy that changes after preview by shortening an unadjusted injected path once. An already shortened path is never shortened again. `RejectOccupied` remains the strict failure policy and produces `GoalOccupied` without a fallback.

Automatic presentation is optional. `bRenderCellOverlay` and `bRenderLine` are independent, so preview can use cells, line, both, or neither. `CellVisualStyle` and `LineVisualStyle` remain separate assets. Disabling either renderer retains the semantic preview. The component owns its presentation session with **Owner Lifetime** and releases it at End Play or **Clear Preview**.

Blueprint flow:

```text
Grid Cell Pointer target changed
    -> Update Preview for Controller(Controller, CellId)
click/confirm
    -> Prepare Preview for Commit
    -> Move Along Exact Grid Path (AI task), or pass InjectedPath to the gameplay action bridge
```

Equivalent C++ setup is:

```cpp
#include "Prediction/GridPathPreviewComponent.h"

const FGridPathPreviewResult Preview = PreviewComponent->UpdatePreviewForController(Controller, GoalCell);
FGridInjectedPath ExactPath;
FGridPathPreviewResult RefreshedPreview;
if (PreviewComponent->PreparePreviewForCommit(ExactPath, RefreshedPreview))
{
	UGridMoveToCellTask* Task = UGridMoveToCellTask::MoveToGridExactPath(Controller, ExactPath);
	Task->ReadyForActivation();
}
```

Milestone 5 exact injection is represented by `FGridInjectedPath`, never by arbitrary world points. It retains ordered cells, original goal, runtime NavData identity, agent/filter signatures, topology and relevant dynamic revisions, partial state, an opaque path identity and preview correlation. `AGridNavigationData::CreateExactInjectedPath`, `ValidateInjectedPath`, and `MaterializeInjectedPath` are the authority. Validation checks current start, every cell, every ordinary or authored-link transition, filter/link permissions, revisions and goal consistency by reusing the same `FGridAStar` traversal rules.

Native replay adapters can ask `AGridNavigationData::CreateExactInjectedPath` to stamp `bAllowDynamicAgentConflictsDuringValidation`. This is intentionally narrow: it ignores transient live-agent and traffic-revision conflicts while creating, revalidating, and materializing the recorded sequence. Topology, traversal, area/filter, link, start, partial-path, and live ordinary blocking-occupancy checks remain authoritative. The resulting `FGridNavigationPath` retains both the flag and `ReservedCorridor`. Path following waits without abandoning recorded intermediate cells; if the final cell remains reserved after the clone reaches its predecessor, it reports `Blocked` to the owning goal-contention policy so `RedirectOnCompletion` can select a nearby endpoint. The Blueprint `UGridWorldSubsystem::CreateExactInjectedPath` API remains strict by default.

`UGridMoveToCellTask::MoveToGridExactPath` materializes a valid payload into the normal `FGridNavigationPath`, submits it through `AAIController::RequestMove`, and retains native request IDs, pause/resume, abort, completion and path observers. **Fail on Invalidation** aborts a changed exact route. **Recalculate to Original Goal** (default) enters the existing path recalculation pipeline; if the payload is already stale when a queued action starts, it immediately performs a normal query to the retained original goal. Path metadata distinguishes `Computed`, `Preview`, `Injected`, and `Recalculated` origins and retains parent/preview correlation without becoming a validity authority.

Only exact injection is implemented. Waypoints, prefixes, repair/rejoin, and mandatory-waypoint recalculation belong to Milestone 6 and are not exposed as placeholder modes.

### Pointer integration

Add `UGridCellPointerComponent` to a Pawn, Player Controller, or another input owner. It has no Tick and no Enhanced Input dependency. Call one of these functions from the project's existing input/UI flow:

- **Update From Screen Position** deprojects through the supplied Player Controller and performs the configured trace;
- **Update From World Ray** traces from an explicit origin and direction;
- **Update From Hit Result** reuses a blocking hit already produced by gameplay/UI code.

The default trace uses `Visibility`, simple collision, and a maximum distance of 100,000 cm. Runtime cell HISMs cannot intercept it because they have no collision. **Navigable Only** uses ordinary GridWorld projection and is the default. **Existing Cells** can target a published blocked cell. The component emits **On Target Cell Changed** only when the persistent `FGridCellId` changes, can clear on miss, and optionally mirrors only hover into the visualization subsystem. Selection remains an explicit gameplay/UI decision.

## AI movement integration

`UGridMoveToCellTask` derives from `UAITask_MoveTo` and is the shared implementation used by `UBTTask_MoveToGridCell`, the asynchronous Blueprint **Move To Grid Cell** node and `FStateTreeMoveToGridCellTask`. It selects the NavData through the controller's Supported Agent, projects Actor or Vector input, and sends the resulting cell center through the native `AIController::MoveTo` and path-following lifecycle.

The optional `GameplayActionsGridWorld` bridge also calls this exact task. `PauseGridMove()` and
`ResumeGridMove()` are narrow lifecycle wrappers for external owners such as Gameplay Action
instances; they do not add a dependency from GridWorld back to GameplayActions.

Actor destinations use `INavAgentInterface::GetNavAgentLocation` and `GetMoveGoalOffset` when implemented. Tracking binds to the Actor root component's `TransformUpdated` delegate and reuses the active task only when the projected `FGridCellId` changes. Target destruction and projection/NavData failures finish observably through the inherited async delegates. StateTree Vector bindings continue to use the native tolerance-based tracking; Blueprint Vector values are captured at activation.

The debug renderer stores active path data per controller. It observes path creation, update, invalidation and repath failure, then reconciles completion on the next frame so the abort emitted by replacing a request cannot clear the replacement path.

`AGridWorldAIController` replaces the native default path-following subobject with `UGridWorldPathFollowingComponent`. `FGridNavigationPath` carries immutable per-point region rotation, style, drive mode, tolerances, cell identity and stop classification. Standard points delegate entirely to the engine component. Precise points use local-grid horizontal position and speed tests; the starting world position is retained so an off-center Pawn first visits its current cell center.

Both precise styles replace intermediate point acceptance with monotonic local-grid gates. A gate is a plane through the cell center, normal to the incoming segment, and may be crossed only in the forward direction. Its half-width is `max(CellCenterTolerance, min(CellSize.X, CellSize.Y) * 0.25)`, which is 12.5 cm for the default cells. The sweep between consecutive feet locations evaluates the exact plane intersection. Crossing inside the strip advances without a speed requirement; crossing outside finishes `Blocked` rather than steering back toward a waypoint that is now behind the Pawn.

**Center-Constrained** targets the initial center, turns and both sides of explicit links while skipping collinear centers. **Cell-by-Cell** retains every intermediate center as a gate. The independent **Path Drive Mode** is applied only to those precise styles. **Accelerated** forces `FNavMovementProperties::bUseAccelerationForPaths` while active and uses the movement interface's braking-distance contract at the final center. **Direct Velocity** forces that flag off, also forces `UCharacterMovementComponent::bRequestedMoveUseAcceleration` off when applicable, and calls `RequestDirectMove` at constant maximum navigation speed on intermediate segments. Its final segment uses `min(MaxSpeed, Distance / DeltaTime)` and calls `StopMovementKeepPathing` inside the center tolerance. **Use Accelerated Final Approach** switches only that last segment back to Accelerated.

Both changed movement flags are saved before mutation and restored on every completion, failure, abort, repath, Pawn or movement-interface replacement, Reset and Cleanup. Standard paths do not mutate them. Final precise completion ignores task acceptance/overlap by disabling direct movement to a tracked goal and requiring both configured tolerances. GridWorld does not alter Actor rotation, collision, RVO, root motion or replication.

Active path debug data carries the same immutable gate geometry and drive policy consumed by movement. The scene proxy draws each plane as a wire rectangle plus a forward arrow, so the rendered width and direction are not an approximation of the runtime rule, and labels each active precise path `ACCELERATED`, `DIRECT VELOCITY`, or `DIRECT VELOCITY / ACCELERATED FINAL`.

The GridWorld path follower also performs capsule-aware dynamic-agent look-ahead when selected by the query filter. Reactive Yield policies inspect occupancy; Reserved Corridor requests an exclusive rolling prefix from the NavData traffic authority. The effective distance combines the designer minimum cells, Character braking distance, both capsule radii and additional separation. Accelerated paths request braking without changing direction; Direct Velocity stops through `StopMovementKeepPathing` before an ungranted gate. It suppresses native block detection while intentionally yielding and preserves the current center gate. Ordinary paths may invalidate after the configured stationary delay; replay exact paths stamped for dynamic-agent tolerance wait on intermediate conflicts instead of entering a per-frame revalidation/repath loop. Repath fallback memory prevents repeatedly recalculating the same blocked one-cell corridor, while a different blocker can trigger another repath. RVO remains untouched.

The debug renderer's cell visualization adds one batched translucent mesh. Each walkable cell's four horizontal corners are projected along the floor-sampling axis onto the tangent plane through `WorldCenter` and `FloorNormal`, then offset 2 cm along the normal to avoid z-fighting. Light-green fill communicates walkable topology and dark-green edges delimit cells. The box remains available for cost and occupancy coloring. Cells made non-walkable by authored or runtime blockers are omitted from all three debug layers. This remains independent from the runtime presentation subsystem described above.

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

`UGridMoveToCellTask` arbitrates endpoint contention with `StopBeforeOccupied` by default, plus `Ignore`, `RejectOccupied`, `RedirectOnCompletion` and `ReserveBeforeMove`. Claims use the same NavData traffic authority as Reserved Corridor, are mutated only on the Game Thread, and reference their task/Pawn weakly. `StopBeforeOccupied` computes a complete path to the requested final cell, removes only that cell when it is foreign-owned, and atomically claims the immediate predecessor. `RejectOccupied` instead fails the request. Both ignore the requesting Pawn's own owner ID. The redirect policies also consider capsule separation, query area/channel rules and nearby candidate cells. Failure, abort, replacement and teardown release a claim; a successful non-partial completion converts it into Pawn-lifetime parking protection.

`RedirectOnCompletion` intercepts `UAITask_MoveTo::OnRequestFinished` before the inherited task broadcasts completion. A contested destination is added to the task's rejected set, the old claim is released, a new candidate is claimed and `ConditionalPerformMove` runs on the next frame. The rejected set has no one-redirect limit, so the same outer task can repeat this transition for every fallback that becomes occupied. `Blocked` results redirect only when the endpoint is actually contested; unrelated movement failures preserve native failure behavior. If the Pawn already occupies the requested or nearest equally ranked available cell, the task succeeds without starting a redundant MoveTo.

Goal contention is owner-aware. A temporary goal claim created by a gameplay action and the move task that executes it may be different `UObject` instances, but they do not contend when both carry the same occupancy owner ID. Exact-path tasks acquire or resolve the same occupancy component used by their path follower before completion arbitration; they therefore cannot mistake their own destination footprint for a foreign occupant and enter a delayed false `Blocked` timeout.

Automatic Pawn tracking reuses an active non-reservation `UGridNavigationOccupancyComponent` or creates a transient attached one sized from the controller's nav-agent radius and height. `UGridWorldPathFollowingComponent` enables this for every controlled Pawn by default, including player Pawns; move tasks call the same creation helper. The generated tracker has zero occupancy cost and is non-blocking so it cannot invalidate or obstruct its owner's path. In addition to its physical footprint, a Pawn tracker always publishes the nearest walkable cell within the NavData projection extent around `APawn::GetNavAgentLocation`; floor-distance maintenance, an off-center capsule, or a cell boundary therefore cannot make the Pawn's logical current cell look unoccupied. Occupancy components cache their affected `FGridCellId` set and publish an overlay update only when that set changes; transform updates within the same footprint do not rebuild the overlay.

`FGridChangeSet` classifies traversal, blocking occupancy/reservations, occupancy cost, and owner-identity changes separately while retaining the aggregate `ChangedCells` list. Active path invalidation consumes only the semantics used by its query: moving a zero-cost, non-blocking Pawn changes presentation and ownership state but does not masquerade as a blocking navigation edit.

The query filter resolves every `AController` type, not only AI controllers, and copies the requester's own `OccupantId` into immutable search state. This prevents the Pawn's footprint, corridor, destination claim, or parking record from excluding its next preview/exact path. The legacy `AGridWorldAIController` opt-out remains supported in addition to the path follower setting. An in-memory per-world owner registry resolves blocker velocity without Actor scans; it does not own grid topology or duplicate cell state.

## Troubleshooting

- No cells: confirm the floor blocks the selected collision profile and the capsule has clearance.
- Duplicate cells inside a thick or inclined platform: rebuild Grid World with the current plugin version. Only non-penetrating floor hits may publish layers; real vertically separated floors remain supported.
- A low step still removes a cell: verify both GridWorld **Max Step Height** and the Character Movement value accept that rise, then rebuild and save format version 7 data. Full-height collision or insufficient headroom is intentionally still blocking.
- A generated ramp is disconnected: verify `Max Slope Degrees` accepts its floor normal. Continuous ramp rise is slope-aware; `Max Step Height` applies only to residual discontinuities.
- A path exists but the Character stops on a ramp: set Character Movement **Walkable Floor Angle** at least as high as the path slope reported by `LogGridWorld`. GridWorld intentionally does not override it.
- Build rejected: run **Build > Grid World > Validate Grid World** and inspect `LogGridWorld`; scale components must be finite and non-zero.
- `MoveTo` selects another NavData: verify the `SupportedAgents` entry and `PreferredNavData` in `DefaultEngine.ini`.
- **Move To Grid Cell** fails immediately: verify the controller resolves `AGridNavigationData` for its Supported Agent and the destination lies within projection extent of a navigable cell.
- A valid preview becomes only a destination marker or will not commit after the first player move: verify the player owns `UGridWorldPathFollowingComponent` and inspect `LogGridWorld` for the exact-injection diagnostic. Current versions initialize the query's ignored owner from every `AController`, so the player's own parking record must not invalidate its next path.
- A Move To task repeatedly fails without moving: rebuild Grid World after upgrading. Pawns are ignored by current generation, but an older serialized snapshot may still contain a hole under a placed AI.
- A Four Directions Character rounds a corner: inspect the logical blue path points; standard path following and Character Movement may smooth the physical turn, but consecutive cell IDs change only one local horizontal axis.
- A precise path follows centers only approximately or behaves like Standard: the AI controller must derive from `AGridWorldAIController`; the log names controllers using an incompatible path-following component.
- A precise AI is blocked before completion: verify that the final center tolerance is reachable with the Pawn capsule and collision geometry. No position snap is applied.
- A precise move reports a missed center gate: inspect the orange/violet gate rectangle and its arrow. The Pawn crossed the plane more than one quarter-cell from the center; collision, excessive lateral velocity or a path-incompatible movement modifier pushed it outside the permitted strip.
- Direct Velocity still deviates from the blue segment: GridWorld does not disable collision, RVO, root motion or replication. Inspect the gate and movement modifiers; a deviation outside the gate intentionally ends as `Blocked` rather than reversing.
- A path optimization mode appears unchanged: ensure the intended `UGridNavigationQueryFilter` subclass is assigned to the request or AI controller. If both objectives select the same route, raise **Balanced Turn Penalty** or use **Fewest Turns** to enforce turn priority.
- A Fewest Turns or Balanced Move To stops before a reachable target: raise the filter's advanced **Max Search States** value. The default 65536 replaces UE's 2048-node budget for directional searches. Turning off **Accept Partial Path** makes a truncated request fail instead of treating its best progress as a completed move.
- A newly placed or runtime-moved obstacle does not update the grid: confirm `AGridNavigationData` is **Dynamic**, enable **Auto Rebuild On Geometry Changes** on the intersecting bounds, and verify the component is navigation-relevant and blocks the selected collision profile. In the editor, also enable **Update Navigation Automatically**. Moving platforms publish only after a rebuild at their current pose.
- Multiple agents target the same goal: use the default **Stop Before Occupied** to retain the requested route and stop at its predecessor, or **Reject Occupied** when the entire request must fail. Use `RedirectOnCompletion` or `ReserveBeforeMove` only when unrelated alternatives are allowed.
- A following agent contacts a stopped Character: use `GridWorldAIController`, select **Reserved Corridor** on the assigned Grid Navigation Query Filter, keep controller auto-registration enabled, and use Center-Constrained or Cell-by-Cell for strict gate enforcement. It waits in a one-cell corridor and detours only when another valid route exists; it never enables RVO or changes the center-gate path.
- Traffic reservations are not visible: enable Show Navigation, `GridWorld.Debug.Visual 1`, path/cell debug, and **Draw Traffic Reservations** on `GridNavigationData`. Turquoise is granted future corridor, orange is waiting, red is the active conflict; current/parked cells remain under occupancy debug.
- Generated cells disappear after reopening a level: rebuild once, then save the level or use **Save All** so the `GridNavigationData` package (including an External Actor package when applicable) is written.
- No visualization: enable Show Navigation, set `GridWorld.Debug.Visual 1`, and enable the desired local draw filters.
- Runtime presentation is not visible or uses Unreal's default material: call **Enable Visualization** in a Game/PIE world. Show Navigation and the debug CVar do not control gameplay presentation. Verify the selected `Grid Cell Visual Style` has both mesh and material assigned, assign the material on the style rather than only on the mesh, and enable **Used with Instanced Static Meshes** on custom materials. Blocked cells are intentionally transparent in the default style.
- Pointer does not find a cell: confirm world geometry blocks the component's trace channel. The runtime HISM deliberately has no collision; pointer projection starts from the geometry hit, not from the visual plane.
- Stale handle/node ref: resolve the persistent `FGridCellId` again after a topology rebuild.
- Serialized version 2–6 rejected after upgrading: rebuild Grid World once and save the level. Version 7 is required for world-anchored coordinates, step-adjusted clearance and penetration-safe surface sampling.
