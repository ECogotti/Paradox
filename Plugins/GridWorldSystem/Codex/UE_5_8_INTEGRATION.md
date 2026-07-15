# Unreal Engine 5.8 integration notes

These notes record the engine-source contracts verified against UE 5.8.0 at `D:\Giochi\UE_5.8` before implementing GridWorldSystem.

## Navigation callbacks

`ANavigationData` stores function pointers with these UE 5.8 forms:

- `FFindPathPtr`: `FPathFindingResult (*)(const FNavAgentProperties&, const FPathFindingQuery&)`;
- `FTestPathPtr`: `bool (*)(const FNavAgentProperties&, const FPathFindingQuery&, int32*)`;
- raycast with additional results: `bool (*)(const ANavigationData*, const FVector&, const FVector&, FVector&, FNavigationRaycastAdditionalResults*, FSharedConstNavQueryFilter, const UObject*)`.

The non-CDO `AGridNavigationData` constructor assigns regular and hierarchical find/test callbacks plus `RaycastImplementationWithAdditionalResults`.

`FNavigationPath` is thread-safe shared data registered through `ANavigationData::CreatePathInstance`. `FGridNavigationPath` assigns its own `FNavPathType`, implements reset/cost/node lookup and supplies ordinary `FNavPathPoint` values to standard path following.

`UNavigationQueryFilter::GetQueryFilter` initializes the native `FNavigationQueryFilter`, whose implementation remains available through `GetImplementation()`. GridWorld installs `FGridNavigationQueryFilterImpl`, copies the designer-facing optimization mode and balanced turn penalty during `InitializeFilter`, and reads that backend inside `AGridNavigationData::FindPath`. This preserves filter-class propagation through native `FPathFindingQuery`, `AIController::MoveTo`, Behavior Tree, Gameplay StateTree and `UAITask_MoveTo` in UE 5.8 without adding task-specific search settings.

Shortest Path continues to use the original cell-state A*. Fewest Turns and Balanced use nine states per cell (no direction plus eight local XY directions) and a zero heuristic. Their open ordering is respectively lexicographic `(TurnCount, TraversalCost)` or `(TraversalCost + TurnCount * TurnPenalty, TurnCount, TraversalCost)`, followed by stable cell coordinates and direction. Layer is excluded from direction, and an explicit link publishes the target with no incoming direction. The actual `FNavigationPath::GetCost()` remains traversal-only; strategy and turn count are stored separately on `FGridNavigationPath`.

UE 5.8 initializes `FNavigationQueryFilter::DefaultMaxSearchNodes` to `MAX_NAV_SEARCH_NODES`, which is 2048. `UNavigationQueryFilter::GetQueryFilter` clones the NavData default before calling `InitializeFilter`, so an otherwise custom GridWorld filter inherited that 2048 value. This is too small for the directional graph because visited nodes are `(Cell, IncomingDirection)` states and zero-heuristic search may legitimately exceed 2048 states on a modest grid. `UGridNavigationQueryFilter::InitializeFilter` now calls `FNavigationQueryFilter::SetMaxSearchNodes` with `MaxSearchStates` (default 65536) for Fewest Turns and Balanced, while Shortest Path preserves the cloned native value. Search exhaustion is stored through `FNavigationPath::SetSearchReachedLimit` independently from ordinary unreachable partial paths.

## Supported Agent selection

UE 5.8 spawns NavData from each `FNavDataConfig::NavDataClass`. With this project's single Supported Agent, `GetNavDataForProps` resolves the main NavData. The project config points both `NavDataClass` and `PreferredNavData` at `/Script/GridWorld.GridNavigationData` with radius 42 and height 192.

## Generation and bounds

`FNavDataGenerator` is declared in `Engine/Public/AI/NavDataGenerator.h`. GridWorld overrides `RebuildAll`, `RebuildDirtyAreas`, `OnNavigationBoundsChanged` and build-progress reporting. Collision sampling uses the verified UE 5.8 `UWorld` line trace and capsule overlap profile APIs on the Game Thread. UE 5.8 `FHitResult::ImpactNormal` supplies the world-space floor normal stored per cell. Upright capsule clearance offsets its center by `HalfHeight - Radius + Radius / UpDot + 1 cm`, where `UpDot` is the normal's world-up component.

GridWorld compares the normalized hit against `MaxSlopeDegrees`, but physical walking remains owned by `UCharacterMovementComponent::GetWalkableFloorAngle()`. When a path's maximum stored cell slope exceeds that value, GridWorld warns once for the owning `AAIController` and leaves the Character untouched.

`AGridNavigationBoundsVolume` derives from `ANavMeshBoundsVolume` only for native Navigation System bounds registration. No `ARecastNavMesh` data or Recast module is used.

`AActor::PostEditMove(true)` ends by calling `FNavigationSystem::OnPostEditActorMove` in UE 5.8. This updates native navigation bounds and reaches `FGridNavDataGenerator::OnNavigationBoundsChanged`, which rebuilds GridWorld without a duplicate editor movement delegate. Full Actor rotation supplies the rigid grid frame; Actor scale is applied only to the brush bounds extents, leaving the configured logical cell dimensions unchanged. New bounds default to 100 x 100 x 50 cm cells, Four Directions, and Direct Velocity for precise path styles.

UE 5.8 reports navigation-relevant Static Mesh additions, removals, Transform changes and collision changes through `FNavigationDirtyArea`. Geometry updates carry `ENavigationDirtyFlag::Geometry` (ordinary octree geometry updates use `All`, which combines Geometry and DynamicModifier); bounds updates additionally carry `NavigationBounds`. `FGridNavDataGenerator::RebuildDirtyAreas` opts into the per-volume geometry policy, while `OnNavigationBoundsChanged` and editor commands bypass it. This lets **Auto Rebuild On Geometry Changes** suppress only automatic obstacle updates for a region. Native dirty-area accumulation still follows the Level Editor's global `bNavigationAutoUpdate` preference.

## Async safety

UE 5.8 async navigation queries can invoke NavData callbacks from worker threads. GridWorld callbacks first retain an immutable thread-safe snapshot. The read/write lock protects pointer exchange only; A*, projection scans and reachability do not hold it.

`USceneComponent::TransformUpdated` is a public three-parameter event in UE 5.8. Modifier, link and occupancy components bind during registration, unbind during unregistration and never enable Tick.

`UAITask_MoveTo::PerformMove`, `ResetObservers` and `ResetTimers` are exported virtual hooks in UE 5.8. `UGridMoveToCellTask` overrides them to project the source goal before each native request and to bind/unbind Actor transform observation symmetrically. The original request is restored after `AIController::MoveTo` copies the projected request so the native StateTree Vector tracking still compares its binding with the unprojected source value.

UE 5.8 also exports virtual `UAITask_MoveTo::OnRequestFinished` and `OnDestroy`. GridWorld uses those hooks for optional endpoint contention without completing and recreating the outer gameplay task. On a contested Success or endpoint-related Blocked result, it invalidates the completed request ID, removes the old observers, claims another candidate and schedules the inherited `ConditionalPerformMove` for the next frame. This transition can repeat for every newly contested fallback. All other results continue through the native implementation; task destruction releases any outstanding claim before the inherited abort/cleanup.

Endpoint claims and rolling corridors are owned by `AGridNavigationData` through a plain C++ manager. Mutations and UObject weak-lifetime pruning execute only on the Game Thread. Every mutation deterministically rebuilds a UObject-free immutable snapshot and swaps its shared pointer under a short lock, matching the existing async topology contract. `UGridMoveToCellTask` binds the NavData traffic-change delegate only while waiting for a goal; the binding, warning/timeout timer, next-tick retry and temporary claims are all removed symmetrically from inherited reset/destroy paths. A successful claim is converted to parking protection tied to the Pawn rather than to the completed task.

Runtime occupancy creation follows the verified `NewObject<UGridNavigationOccupancyComponent>`, `AActor::AddInstanceComponent`, `UActorComponent::OnComponentCreated` and `RegisterComponent` lifecycle. The component attaches before registration, is transient, binds `USceneComponent::TransformUpdated` during registration and unbinds symmetrically. Its cached affected-cell set prevents high-frequency transforms inside one cell footprint from publishing redundant occupancy revisions.

`UBTTask_MoveTo::PrepareMoveTask` and `FStateTreeMoveToTask::PrepareMoveToTask` are exported virtual hooks. The GridWorld variants override only these factories, preserving the engine implementations for Blackboard observation, task reuse, aborts, StateTree binding ticks and completion callbacks. The required runtime modules are `AIModule`, `GameplayTasks`, `GameplayStateTreeModule` and `StateTreeModule`.

## Path following and rendering

`UPathFollowingComponent` consumes path point locations and node refs. `UCharacterMovementComponent` uses `FindMoveAlongSurface` specifically for NavWalking, while standard walking follows path points normally.

UE 5.8 exports the `UPathFollowingComponent` overrides used by `UGridWorldPathFollowingComponent`: `OnPathUpdated`, `SetMoveSegment`, `FollowPathSegment`, `UpdatePathSegment`, `HasReachedDestination`, `HasReachedCurrentTarget`, `DetermineStartingPathPoint`, `Reset`, `Cleanup`, `OnNewPawn` and `SetNavMovementInterface`. `AAIController` creates its path follower through the optional default subobject named `PathFollowingComponent`; `AGridWorldAIController` replaces that class in its object initializer.

`INavMovementInterface` exposes the mutable `FNavMovementProperties`, current feet location/velocity, maximum navigation speed, braking distance, `RequestPathMove`, `RequestDirectMove` and `StopMovementKeepPathing` used by precise following. GridWorld saves `bUseAccelerationForPaths` before the first precise segment and restores it before interface replacement, Reset or Cleanup. Accelerated drive forces it on. Direct Velocity forces it off and, for `UCharacterMovementComponent`, separately saves and disables `bRequestedMoveUseAcceleration`; both are restored symmetrically. Direct intermediate segments request maximum speed, while the logical last segment is detected with `MoveSegmentEndIndex` and limited to `Distance / DeltaTime` to avoid intentional overshoot. Optional accelerated final approach applies the existing braking contract only to that final segment.

`UPathFollowingComponent::UpdateBlockDetection`, `ResetBlockDetectionData`, `FNavigationPath::Invalidate`, `IsWaitingForRepath` and `ANavigationData::RequestRePath` are exported in UE 5.8. GridWorld uses those contracts for optional dynamic-agent yielding without completing the outer Move To request. While yielding, the custom component suppresses native blocked samples; Accelerated movement receives zero path input early enough to brake, while Direct Velocity calls `StopMovementKeepPathing`. A stationary blocker invalidates the existing observed path, allowing native `UAITask_MoveTo::ConditionalUpdatePath` or path auto-recalculation to preserve their respective lifecycle.

The query backend copies a requester-owned non-reservation `OccupantId`. A* closes other occupied intermediate cells for **Yield Then Repath** and tests the immutable cell/segment reservation snapshot for **Reserved Corridor**. When no route avoids the dynamic state, `AGridNavigationData::FindPath` immediately retries with the same filter semantics except agent closure and marks the path as a waiting fallback. This avoids `RePathFailed` in one-cell corridors and remains independent from RVO, which GridWorld neither enables nor modifies.

Reserved Corridor advances from center-gate progress, not raw path-point skipping. Its designer minimum is extended by `INavMovementInterface::GetPathFollowingBrakingDistance`, current speed and the two capsule radii. The previous logical cell is removed in the same published revision after the next gate is swept. Direct Velocity calls `StopMovementKeepPathing` before an ungranted gate; Accelerated supplies zero path input inside the protected stopping distance. Standard remains best effort and warns once because native point acceptance cannot provide the same gate-to-gate guarantee.

Intermediate centers use one-way local-grid gate planes without a speed requirement or custom braking. `DetermineCurrentTargetPathPoint` is overridden so Center-Constrained can skip collinear centers while Cell-by-Cell retains every gate. `UpdatePathSegment` may finish `Blocked` through the exported protected `OnPathFinished` when a Pawn crosses outside the gate, preventing native steering from reversing toward an expired point. The native goal-actor shortcut and collision-success flag are disabled for precise final segments so configured center position and speed remain the only success conditions.

`FNavigationPath::AddObserver` reports `NewPath`, runtime update, invalidation, clearing and repath failure events. `UPathFollowingComponent::OnRequestFinished` is broadcast after the component has cleared its current path; GridWorld therefore reconciles the controller on the next frame. This prevents the abort of a replaced request from removing a newer debug path while still clearing completed, failed and aborted requests.

`ANavigationData::ConstructRenderingComponent` returns a transient `UDebugDrawComponent`. GridWorld's scene proxy batches cells, links, chunks, costs, overlay state, immutable center-gate rectangles/arrows, drive-mode labels and traffic-reservation snapshots, and checks `EngineShowFlags.Navigation` plus `GridWorld.Debug.Visual`. Reservation owner locations are copied on the Game Thread; no UObject is dereferenced by the render thread. Turquoise indicates granted future cells, orange requested cells, red the first active conflict, and a line/short GUID identifies the owner. Current/parking shapes are deliberately omitted from this layer and remain occupancy debug.

UE 5.8 `FDebugRenderSceneProxy::FMesh` accepts one `FDynamicMeshVertex`/index batch and selects the translucent debug material when `FMesh::Color.A < 255`. GridWorld uses that contract for all light-green floor quads. Their corners are analytically intersected with each cell's tangent plane along the same sampling axis, avoiding extra collision traces; dark-green borders and cell boxes remain debug lines.

## Serialization

Generated topology is serialized after `ANavigationData::Serialize` with magic `GWRD` and format version 5. Version 5 appends the normalized compact floor normal after each cell world center; region drive mode and accelerated-final policy remain present from version 4. Versions 2–4 are still decoded according to their original field layouts and read to their ends, then rejected visibly. This prevents an Unreal export serial-size mismatch while ensuring topology without reliable floor normals is never published. Runtime overlays, occupancy and reservation owners are deliberately omitted.

`ANavigationData::RebuildAll` normally marks its package dirty in editor builds, but UE's direct `OnNavigationBoundsChanged -> FNavDataGenerator::OnNavigationBoundsChanged` path does not pass through that method. GridWorld therefore marks the non-transient package containing `AGridNavigationData` dirty immediately after every successful editor topology publication and after clear. This covers bounds-driven, manual, and incremental builds, including External Actor packages, without dirtying game worlds or runtime overlay-only updates.
