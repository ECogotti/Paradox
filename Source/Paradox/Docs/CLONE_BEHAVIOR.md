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
Replay/Investigating -- explicit future handoff --> Goap (terminal)
```

Entering `Investigating` atomically pauses the playback clock, captures every replay-owned active
intent as an immutable `FIntentReplaySuspendedIntent`, registers the expected interruption reason,
and interrupts those actions with
`GameplayAction.Result.Interrupted.ByInvestigation`. These interruptions remain visible in the
Intent Replay Execution Journal and are not replay fractures.

`RequestEnterGoapMode` is intentionally not called by Milestone 3 gameplay. If a future authority
calls it, the coordinator stops replay and investigation, commits terminal `Goap`, safely stops the
Behavior Tree, and broadcasts the external handoff. The transition cannot be reversed for that
run.

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
