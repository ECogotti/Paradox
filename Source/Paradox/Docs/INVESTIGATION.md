# Investigation and replay continuity

## Single objective and retarget

`UParadoxCloneInvestigationComponent` executes movement and inspection exclusively through
GameplayActions. It owns at most one authoritative action handle.

Sight resolves the source Actor's inspection location when available. Hearing uses the observation
world position. Movement uses `UGridMoveToCellActionDefinition`, a scheduling priority derived from
the stimulus priority, and `RedirectOnCompletion`. A destination reserved by another moving agent
does not reject or shorten the request before the clone starts: GridWorld's rolling
`ReservedCorridor` arbitrates the next protected cells as the agents approach. If the endpoint is
still genuinely unavailable near completion, the goal-contention policy may redirect locally.
`MovementGoalContentionPolicy` exposes this clone-specific behavior for investigation and replay
recovery positioning. After arrival the clone faces the target and runs a GameplayActions wait;
`InspectionDuration` defaults to 2 seconds.

For a higher-priority target the component first builds and preflights the replacement request.
Commit then:

1. advances `InvestigationRevision`;
2. changes callback correlation before ending the old handle;
3. interrupts only the investigation-owned action with
   `GameplayAction.Result.Interrupted.InvestigationSuperseded`;
4. replaces the context and Blackboard mirror;
5. submits the new action;
6. broadcasts the exact new revision to the still-running BT task.

Late handle, correlation, or revision completions are ignored. The replay resume context is
captured only on the first Replay-to-Investigating transition and is never recaptured by retarget.

## Recovery decisions

`UParadoxReplayRecoveryPolicy` evaluates `ParadoxReplayRecovery` metadata and produces:

- `ReissueNow`: submit the immutable prepared intent again;
- `MoveThenReissue`: reposition through GameplayActions, then reissue;
- `AlreadySatisfied`: append reconciliation to the Journal without reissue;
- `CannotRestore`: remain `Investigating` with replay paused.

On failure, `OnReplayContinuityCannotBeRestored` supplies the authoritative context and diagnostic.
Fix the world/configuration and call `RetryReplayContinuity`; no automatic GOAP fallback occurs.

A higher-priority observation arriving during a recovery reposition interrupts that reposition and
starts the new investigation. Pending intent index and original resume snapshot are preserved.

## Movement from a different cell

Clone replay never weakens generic exact-path validation. If, and only if, validation fails with
`InvalidStart` under `RecalculateToOriginalGoal`, the clone execution strategy:

1. logs one warning per intent with clone, intent, recorded/current cells, and semantic goal;
2. discards the stale runtime path;
3. runs a controller-aware GridWorld query from the current cell;
4. stamps a new `ExactInjectedPath`;
5. preserves filter, requested goal, preview correlation, partial policy, dynamic-conflict policy,
   invalidation policy, and goal-contention policy.

Topology, filter, link, traversal, and goal mismatches remain errors. The underlying AI task and
non-AI Gameplay Action also warn before their existing InvalidStart recalculation fallback.
