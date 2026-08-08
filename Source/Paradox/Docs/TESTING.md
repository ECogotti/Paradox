# Clone behavior verification

## Automation

Build `ParadoxEditor`, then run:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -DDC-ForceMemoryCache -ExecCmds="Automation RunTests StartsWith:GameplayActions+StartsWith:GameplayActionsGridWorld+StartsWith:IntentReplay+StartsWith:IntentReplayPerception+StartsWith:PerceptionKnowledge+StartsWith:GridWorld+StartsWith:PuzzleSystem.TransformMover+StartsWith:Paradox.Interaction+StartsWith:Paradox.Selection+StartsWith:Paradox.VerticalBarrier+StartsWith:Paradox.Camera+StartsWith:Paradox.CloneBehavior+StartsWith:Paradox.Crouch+StartsWith:Paradox.Perception+StartsWith:Paradox.TimeLoop+StartsWith:Paradox.TimeTravel; Quit" -TestExit="Automation Test Queue Empty" -log
```

Coverage includes interruption terminal reasons, pending-recovery resume rejection, immutable
snapshots, immediate reissue/already-satisfied resolution, default policy priorities,
equal/lower-priority arbitration, higher-priority single-action retargeting, stale completion
rejection, native composition, node instancing, fresh exact paths from a different start cell,
instant crouch/uncrouch alongside a running Movement lock, stance replay, and stable temporal
Perception Entity IDs. `Paradox.Perception.PlayerSightUsesPawnFacing` deliberately separates
Player Controller `ControlRotation` from Pawn rotation and verifies that both gameplay eyes and the
native listener direction follow the Pawn immediately after it turns.

`Paradox.Camera.*` covers configuration validation and asset wiring, exact left/right quarter turns,
ignored concurrent requests, held-input trigger semantics, drift resistance, screen-relative pan at
all four orientations, no-Pawn operation, real-delta advancement while paused, continuous corner
containment, and dynamic rotation-safe zoom limits after current Camera Volume bounds change.

`PuzzleSystem.TransformMover.RequestGateAndRuntimeState` covers dependency-free defer semantics and
event-free whole-state reconstruction. `GameplayActions.Locks.SourceOwnedExternalAuthority` covers
conflicting-action interruption, new-request rejection, and independent lock owners.
`Paradox.VerticalBarrier.*` covers native composition, shared bounds, distinct-Actor occupancy,
safe defer/retry, safety return, navigation ordering, attachment persistence after EndOverlap,
Paradox Character Movement-lock ownership, world-delta transport, 60 Hz PIE moving-base transport,
and endpoint cleanup.

`Paradox.Selection.*` covers hover/selected stencil transitions, exact restoration of existing
Custom Depth/Stencil/write-mask state, RMB toggle/replacement and empty-world deselection,
dynamic/destroyed mesh cleanup, idempotent reset, World State reset-start cleanup, lazy world-widget
creation/context/teardown, selected interaction-cell refresh/cleanup, and native composition on
Pressure Plate, Vertical Barrier, and Chrono Spawn.

`Paradox.Interaction.*` covers several slots, several definitions on the same slot, Activity Tag and
interaction-tag filtering, runtime slot transforms, deterministic ordering, GridWorld projection,
Smart Object claims, ordinary occupancy/reservations, requester-owned state, traffic reservations,
failure results, and the shared GridWorld style asset. Its `Action`, `Replay`, and `Hardening`
groups additionally cover exact-tag submission, required Property Bag schema, native standard
Definition defaults, standard Receiver/Emitter effects, reachability/effect preflight,
claim acquisition only at start, every terminal release path, selection-reset separation,
requester/target teardown, the non-implemented base fallback, one semantic recorded intent, fresh
replay resolution/claim, immutable source tracks, replay origin/journal, and moved-requester
failure without movement. `GridWorld.Presentation.CellOverlay.*` covers owner-lifetime sessions,
priority, Primary/Secondary resolution, path coexistence, navigation immutability, and renderer
reuse.

`GameplayActionsGridWorld.Execution.*` verifies the reusable non-journaled executor contract; the
existing runtime tests continue to cover the Grid Move adapter's AI and Player path-following
behavior.

Run the focused suites after building `ParadoxEditor`:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -DDC-ForceMemoryCache -ExecCmds="Automation RunTests Paradox.Interaction; Quit" -TestExit="Automation Test Queue Empty" -log
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -DDC-ForceMemoryCache -ExecCmds="Automation RunTests Paradox.Selection; Quit" -TestExit="Automation Test Queue Empty" -log
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -DDC-ForceMemoryCache -ExecCmds="Automation RunTests GridWorld.Presentation.CellOverlay; Quit" -TestExit="Automation Test Queue Empty" -log
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -DDC-ForceMemoryCache -ExecCmds="Automation RunTests Paradox.PressurePlate.Architecture; Quit" -TestExit="Automation Test Queue Empty" -log
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -DDC-ForceMemoryCache -ExecCmds="Automation RunTests Paradox.VerticalBarrier.Architecture; Quit" -TestExit="Automation Test Queue Empty" -log
```

For PIE acceptance, assign valid Smart Object Definitions and interaction catalogs to Blueprint
children of Pressure Plate and Vertical Barrier. RMB selection must show green Primary cells when
at least one option is free and orange Secondary cells otherwise. LMB navigation and its path
preview must remain active while the Actor outline is visible. Deselect, target destruction, and
World State restore-start must remove the interaction cells immediately. Chrono Spawn must retain
selection behavior without Smart Object or Paradox interaction components.

For execution acceptance, give the possessed requester a `UGameplayActionComponent` and choose an
exact catalog tag from the widget or call `RequestInteraction` directly. Confirm the action chooses
the lowest-cost complete path, acquires one claim before movement, reaches the exact green cell,
and revalidates before applying the effect. Exercise
success, explicit failure, cancel, interrupt, and time-loop abort; each must release that claim.
Block every path between presentation and submission/start and verify the request fails without
rotation or teleport. Recording one accepted player
request must add one semantic intent containing Definition identity, soft `Target`, and
`InteractionTag`; replay must create a new action and claim while leaving the source track
unchanged.

## Idle tail and recorded Time Travel

1. In T0 move to a destination, remain still for ten seconds, then trigger Time Travel.
2. In T1 enter T0's Hearing range during that final standstill. T0 must enter `Investigating`;
   replay/comparison must not complete when the movement action ends.
3. Assign a finite Niagara System to the inherited `TimeTravelNiagaraComponent`. Trigger rewind
   while moving: movement must be interrupted, the VFX must play, and reset must occur only after
   `OnSystemFinished`.
4. Remove the Niagara System and repeat. The player must rewind immediately.
5. Let T0 replay the recorded Time Travel in T1. Its VFX must play and, at completion, T0 must be
   hidden, non-collidable, absent from GridWorld occupancy, and unregistered as perception Source.
6. Run `IntentReplay.Playback.PreservesRecordedIdleTail`,
   `Paradox.TimeTravel.RecordedActionPreemptsMovementAndSupportsNoVfx`, and
   `Paradox.TimeLoop.CloneTimeTravelDepartureRetiresInPlace`.

## Crouch and perceptual identity

`Paradox.Crouch.ActionReplayAndMovementConcurrency` starts a long Movement-lock action, records
rapid absolute crouch and uncrouch requests, and verifies both stance actions start immediately
while movement remains `Running`. It then replays the track and verifies the final stance.

`PerceptionKnowledge.Core.ValueIdentityAndRegistration` covers the controlled reassignment API:
registered mutation, invalid ID, and live collision all preserve the previous identity; a unique ID
can be assigned only while disabled and is registered on re-enable.

`Paradox.TimeLoop.ConsolidationResetAndCloneReconstruction` verifies the original player ID is
stored in T0, every T0 reconstruction inherits it, the next player receives a fresh ID, and T0/T1
remain distinct.
`IntentReplayPerception.EndToEnd.ReplaySourceCausalEventSurvivesReconstructionDrift` reconstructs
T0 with the same Entity ID and action track, records its event in T1, then re-emits it over two
seconds late, 1400 cm away, and with different strength/loudness. The verified causal occurrence
must match exactly once. A second occurrence and the same semantic event from a new Source must
remain unexpected even when the new Source is itself replay-owned and therefore carries
`CorrelatedReplayIntent + Verified`.
`Paradox.CloneBehavior.Policy.DefaultPrioritiesAndFilters` verifies that this external correlated
noise receives Hearing priority 100, while only `ObserverCaused + Verified` is ignored. It also
verifies that the project coordinator enables the causal-identity opt-in while the generic plugin
default remains strict.

`IntentReplayPerception.EndToEnd.PersistentStateReacquisitionDetectsLateValueMismatch` records an
initial Sight State and a second identical snapshot after reacquisition. During comparison it
reacquires the State three seconds beyond the recorded timestamp with the opposite bool value.
The result must be `UnexpectedStateValue/StateValueMismatch`, not
`NoCandidateInTimeWindow`. `Paradox.CloneBehavior.NativeCompositionAndInactiveGoap` also loads
`BP_CloneAstronaut` and verifies that its coordinator inherits the ordered-State option.

PIE verification:

1. start standing, begin a long move, then press crouch twice rapidly;
2. verify the movement does not pause or terminate and both stance transitions are replayed;
3. record T0 footsteps while playing T1, then consolidate through T2;
4. inspect the two reconstructed Sources: T0 must keep its original ID, T1 must have a different
   saved ID, and the current player must have a third ID;
5. verify the recorded T0 footstep is `Matched` for T1 and does not enter `Investigating`;
6. emit the same semantic noise from a new Source ID and verify it remains unexpected and does
   enter `Investigating`.

## PIE scenarios A-G

### A — matched replay

Record and replay an unchanged state/event. Verify a matched Journal entry, mode stays `Replay`,
and no investigation Gameplay Action exists.

### B — unexpected Hearing

Place `AParadoxSemanticNoiseSphere`, record without its event, then emit during clone replay.
Verify priority 100, one Hearing target, move/orient/two-second inspection, and replay resume.

### C — unexpected Sight state

Place `AParadoxSemanticStateCube`, record powered off, then set powered on for clone replay. Verify
the Sight mismatch creates priority 300 through `Sight.ComputerPowered.High`; AI Sight itself does
not report a temporal paradox. For the delayed-reacquisition case, see the cube, turn away, wait
about 20 seconds, then return and record the second view. After rewind, change `Powered` while the
clone is turned away. Its later reacquisition must produce `UnexpectedStateValue` and
`Investigating` even if replay movement arrives outside the default 0.25-second State window.
If a Blueprint drives the cube from overlap, filter `Other Actor` to the current player. An
unfiltered `FlipFlop` allows the replay clone to toggle the cube back before observing it; use the
one-shot `LogParadox` State transition from `SetPowered` to verify the actual value sequence.

### D — movement recovery from another cell

Interrupt a replay movement with B or C. After investigation, verify one warning shows recorded
cell, current cell, and goal, the old path identity is discarded, and movement resumes toward the
same semantic goal on a fresh exact path.

Also record a move to cell A, rewind, then send the new player toward A before clone replay begins.
The player's transient destination reservation must not make investigation or recovery movement
fail with `EPathFollowingResult::Invalid`. The clone starts toward its semantic goal and
`ReservedCorridor` arbitrates the rolling look-ahead cells; if A remains unavailable near arrival,
`RedirectOnCompletion` performs endpoint resolution.

### E — non-movement recovery outcomes

Exercise a still-valid target (`ReissueNow`), a target needing a position (`MoveThenReissue`), an
already achieved state (`AlreadySatisfied`), and a destroyed/invalid target (`CannotRestore`).
The last case must remain `Investigating` until explicit retry.

### F — stale completion

Retarget an active investigation and force/observe the old action completion. Verify its handle,
correlation, and revision cannot complete the current BT task or resume replay.

### G — Hearing replaced by high-priority Sight

Start B, then power the PC from C while the clone is moving or inspecting. Verify priority
100 -> 300, revision increments once, the old investigation action ends with
`InvestigationSuperseded`, no second target is queued, and replay resumes only after the PC
investigation and all original intents are reconciled.

For diagnostics enable `Paradox.CloneBehavior.Debug 1`, `PerceptionKnowledge.Debug 1`, and the
corresponding local component flags. Disable the global flags to confirm output stops immediately.
For the player listener, rotate the astronaut while leaving the free camera fixed: both debug FOV
edges must rotate with the astronaut. `Attach to Pawn` is not the fix for direction; it follows
Controller position only.

## Controller Profile persistence

Assign a non-default `PerceptionKnowledgeProfile` to the Listener component on
`BP_CloneController`, start PIE, record a short player run, and perform a rewind. Inspect the
runtime clone controller after possession. The Listener must still reference the authored Data
Asset; `GetEffectiveSightRadius`, `GetEffectiveLoseSightRadius`, `GetEffectiveHearingRange`, and
the Hearing renderer must all use its values rather than the native 1500/1800/3000 fallback.

`Paradox.CloneBehavior.Assets.CloneControllerPreservesAuthoredPerceptionProfile` automates the
same late-spawn sequence with the project clone Controller Blueprint and
`DA_ClonePerceptiopnProfile`. `Paradox.TimeLoop.ConsolidationResetAndCloneReconstruction` verifies
the same behavior through actual clone reconstruction.

## Footstep automation

After building `ParadoxEditor`, run:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -ExecCmds="Automation RunTests StartsWith:Paradox.Footsteps+StartsWith:FootstepSystem; Quit" -TestExit="Automation Test Queue Empty" -log
```

`Paradox.Footsteps.*` covers profile lookup, fallback and disabled surfaces, standing/crouched
policy, emitted payloads, PerceptionKnowledge failures, lifecycle binding, Tick/debug gates, CDO
component setup, the Left Control mapping, both astronaut profiles, and walk/run notify authoring.

The exact standing, suppressed-crouch, unsuppressed-crouch, listener-range, and observation replay
procedure is documented in [Footsteps, semantic Hearing, and crouch](FOOTSTEPS.md).
