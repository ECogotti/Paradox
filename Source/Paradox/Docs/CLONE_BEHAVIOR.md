# Clone behavior authority

## Runtime contract

`UParadoxCloneBehaviorCoordinatorComponent` is the only authority for a clone's
`Replay`, `Investigating`, and terminal `Goap` mode. Blackboard keys are a mirror for Behavior Tree
selection; changing a Blackboard value does not change authoritative behavior.

The time loop prepares Intent Replay and observation comparison before starting the authored
Behavior Tree. At the synchronized barrier it calls `AuthorizeReplayStart`; only
`UBTTask_ParadoxRunIntentReplay` calls `StartReplay` or `ResumeReplay`.

AI Sight is information input only. Temporal paradox authority remains
`UParadoxTemporalVisionComponent` plus Temporal Index.

The response policy ignores a verified observation only when its justification is
`ObserverCaused`. `CorrelatedReplayIntent` describes an external replay Source and still passes
through normal comparison rules: a matched historical occurrence stays in `Replay`, while an
unexpected noise from a newly introduced clone enters `Investigating`.

## State transitions

```text
Replay -- accepted comparison --> Investigating
Investigating -- recovery complete --> Replay
Replay/Investigating -- completed replay Time Travel --> Goap (terminal, default)
Replay/Investigating/Goap -- Health death --> Stopped (terminal)
```

Entering `Investigating` atomically pauses the playback clock, captures every replay-owned active
intent as an immutable `FIntentReplaySuspendedIntent`, registers the expected interruption reason,
and interrupts those actions with
`GameplayAction.Result.Interrupted.Paradox.Investigation.Started`. These interruptions remain visible in the
Intent Replay Execution Journal and are not replay fractures.

When a clone completes its recorded Time Travel, the time loop calls `RequestEnterGoapMode` on the
next tick by default. Deferral lets the Time Travel Gameplay Action complete before the coordinator
stops replay, investigation, Behavior Tree, Gameplay Actions and movement and commits terminal
`Goap`. The transition cannot be reversed for that run. This is currently a stationary GOAP
placeholder: the clone remains visible, collidable, GridWorld-occupied, semantically observable and
continues consuming Oxygen, while the Time Travel action has already disabled its perception
listener and Temporal Vision. A failed handoff leaves the clone stationary and reports playback
failure.

In `SharedGlobal` Oxygen mode, a terminal GOAP clone remains an active reservoir participant.
`RetireInPlace` deactivates its facade before hiding it, so it no longer contributes to either the
fixed World rate's participant gate or the `PerActiveAvatar` multiplier.

Set `UParadoxTimeLoopComponent::CloneTimeTravelCompletionBehavior` to `RetireInPlace` to preserve
the legacy hidden retirement behavior, including collision disable, GridWorld release and semantic
Source unregister.

`StopForDeath` is an idempotent terminal stop. It rejects every later replay, investigation, or
GOAP request; stops Intent Replay, observation comparison, investigation, Behavior Tree, Gameplay
Actions, and movement; and releases GridWorld traffic/occupancy. The dead Clone is disabled as a
temporal observer but keeps identity, Temporal Index, and temporal target registration. Its ragdoll
and capsule do not block Pawns or affect NavMesh. Time-loop reset destroys and reconstructs it
instead of reviving it in place. See [Paradox Health System](HEALTH_SYSTEM.md).

## Read-only diagnostics

Use `GetDebugSnapshot`, `GetCurrentInvestigation`, and `GetReplayResumeContext`. Enable detailed
logs only when both gates are true:

```text
Paradox.CloneBehavior.Debug 1
Coordinator.bEnableDebug = true
```

Logs are transition/event based and include policy rule, current/candidate priority, decision, and
investigation revision. They are never emitted per frame.

## Legacy timelines

Consolidated timelines retain the complete `UIntentReplayTimelineBundle` and keep `ReplayTrack` for
compatibility. A legacy action-only timeline can still replay, but logs one warning and cannot
produce perception comparisons or investigations.

See [PERCEPTION_INTEGRATION.md](PERCEPTION_INTEGRATION.md),
[INVESTIGATION.md](INVESTIGATION.md), and
[BEHAVIOR_TREE_SETUP.md](BEHAVIOR_TREE_SETUP.md).
