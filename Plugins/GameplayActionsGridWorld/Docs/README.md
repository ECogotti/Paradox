# GameplayActionsGridWorld

When an exact action resumes from another cell under `RecalculateToOriginalGoal`, it logs
controller, recorded cell, current cell, and goal, then performs its controller-aware query.
Filter, topology, traversal, link, and goal validation are not downgraded.

`GameplayActionsGridWorld` is an optional bridge depending on `GameplayActions` and
`GridWorldSystem`. Neither core plugin depends on the bridge.

## Ready-to-use action

The asset `/GameplayActionsGridWorld/Definitions/DA_GameplayAction_MoveToGridCell` uses:

- `UGridMoveToCellAction`;
- action tag `GameplayAction.Type.GridWorld.MoveToGridCell`;
- exact lock `GameplayAction.Lock.Movement`;
- blocked policy `Queue`;
- optional journaling;
- `LockAILogic = false`;
- `BP_GridQueryFilter_Balanced`, whose Pawn occupancy policy is **Ignore** and dynamic-agent policy is **Reserved Corridor**;
- goal contention **Stop Before Occupied**.

The false AI-logic-lock default prevents a BT or StateTree task waiting for `Ended` from locking the
same AI logic that must continue processing the node.

## Parameters

The fixed schema starts with `PathSource`:

- `Destination` (default) mirrors `UGridMoveToCellTask::MoveToGridCell`: Actor/location goal,
  acceptance and overlap options, partial path, pathfinding, AI logic lock, moving-goal tracking,
  navigable end, navigation filter, strafe, and all GridWorld goal-contention settings.
  `GoalActor` takes precedence.
- `ExactInjectedPath` requires a valid `FGridInjectedPath` exported by GridWorld prediction or its
  navigation authority. Agent, filter, revisions, original goal and ordered cells come from that
  payload; arbitrary world-point arrays are not accepted.

Existing Definition assets are migrated in `PostLoad` to add the two new fields while preserving
authored values from the older destination-only schema. The plugin-owned ready-to-use asset also
migrates its former `RejectOccupied` value to the new `StopBeforeOccupied` default; project-owned
assets keep explicit authored policies.

## Lifecycle

`Action Init` validates and caches the immutable snapshot; no movement task exists while queued.
`Action Start` resolves the Controller again and begins movement only after the Movement lock is
acquired:

- an `AAIController` creates `UGridMoveToCellTask`, retaining moving-goal and GridWorld contention
  behavior, or `MoveToGridExactPath` for an injected request;
- another `AController` projects the location, or the current `GoalActor` location, to a walkable
  GridWorld cell and sends the path to its existing `UPathFollowingComponent`. Exact requests are
  authoritatively revalidated/materialized instead of running a second A* query.

If an exact payload becomes stale, **Fail on Invalidation** rejects/aborts it. **Recalculate to
Original Goal** performs a normal GridWorld query to the retained goal and records the path as a
recalculated child of the injected path. The same policy controls native path invalidation after
the request starts.

Both destination and exact paths apply the same final-cell contention contract. The default
**Stop Before Occupied** computes the complete path to the requested cell, but if that final cell is
foreign-owned it removes only the final cell and atomically claims the immediate predecessor. Exact
payloads retain both `RequestedGoalCell` and the effective `OriginalGoalCell`, so preview and
execution use the same prefix. If the goal becomes occupied between prediction and commit, an
unadjusted path is shortened once at commit. If the predecessor is unavailable, the request fails.
**Reject Occupied** remains the no-movement alternative. Failure, cancellation, replacement and
cleanup release the claim; successful non-partial completion converts it to Pawn parking.

The non-AI path is intended for player-controlled pawns. It requires a possessed Pawn and a
`UPathFollowingComponent` on the Controller. Player controllers that require Center-Constrained or
Cell-by-Cell movement should install `UGridWorldPathFollowingComponent`, as
`AParadoxPlayerController` does. That component automatically publishes a transient, non-blocking
occupancy identity for player and AI Pawns. Both exact occupied-goal policies are supported by both controller paths;
moving-goal tracking and the redirect/wait alternative-goal policies remain AI-task features.

Pause/resume forward to the active task or path-following request. Cleanup unbinds first, then
cancels unfinished work.

Success uses `GameplayAction.Result.Success`. `Blocked`, `OffPath`, `Aborted`, and `Invalid` map to
matching tags under `GameplayAction.Result.Failure.GridWorld.MoveToGridCell`.

The `GameplayActionsGridWorld.*` suite verifies the asset, schema/defaults, absence of premature
tasks during queue residence, automatic start after the Movement lock is released, result mapping,
late-callback protection, and the PlayerController path-following entry path. The underlying
`GridWorld.*` suite remains the authority for navigation queries and the shared movement task.
