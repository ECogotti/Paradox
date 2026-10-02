# Hacking terminal validation

Validated on 1 October 2026 with installed Unreal Engine 5.8.3, Win64.

The dedicated run passed all 36 tests (30 scenarios, spatial action/replay/AI integration,
configuration and command validation, unavailable UI, real PIE clock/view, and the shared
background-lock regression, plus Common UI/state-switcher validation). Nine tests report warnings because the editor factory initially
compiles empty temporary Widget Blueprints before the required controls are composed. Their final
compiled classes have the required bindings, which are checked at runtime.

The additional regression run passed all 74 tests across GameplayActions, IntentReplay,
Paradox.Interaction, Selection, CloneBehavior, PressurePlate and PuzzleOverlay.Renderer.
The background-lock test appears in both runs; the counts are not counts of unique tests.

The widget-change regression run passed 45 of 46 tests across GameplayHUD, Inventory,
Interaction and Selection (five passing tests include warnings). The remaining failure is
`Paradox.Inventory.25.WorldPresence.KeyCardDoesNotPublishOccupancy`: the existing BP_KeyCard
currently enables authored world collision and does not ignore Pawn collision, while that test
expects non-physical defaults. Its navigation/occupancy checks pass. The hacking changes do not
modify this asset or its collision settings; this asset/test-contract mismatch remains observable.

## Coverage of the specification

`Paradox.Hacking.Scenarios` supplies numbered cases except that scenario 12's non-blocking action
behavior is tested by `SpatialActionRewindReplayAndInvestigation`; numbered case 12 additionally
tests concurrent instigators. This table follows the original specification's numbering.

| Scenario | Verification |
| --- | --- |
| 1 | HEART completes through correct current letters, Hacked and active signal |
| 2 | Compiled Widget Blueprint, CommonTextBlock inside each CommonButtonBase named slot, dynamic children and actual click bindings |
| 3 | Three unique candidates with the target included |
| 4 | All 26 unique alphabet candidates |
| 5 | Pool and offset stability across refreshes |
| 6 | Correct click freezes a slot and preserves its current letter |
| 7 | Wrong click clears progress and retains pools, deadline and refresh phase |
| 8 | Deadline failure without publication |
| 9 | Selection context removal preserves the attempt |
| 10 | Reconstruction retains GUID, generation and advancing countdown |
| 11 | Leaving interaction range preserves the attempt |
| 12 | Released SmartObject claim/locks permit another action while hacking remains Running |
| 13 | Synchronous phase equality |
| 14 | Stable asynchronous offsets and advancing phases |
| 15 | Semantic replay success preserves recorded duration and publishes |
| 16 | Semantic replay failure retains Failed in the journal and completes the timeline |
| 17 | Easy simulation sampled with deterministic random streams |
| 18 | Hard simulation is less likely to succeed over the same deterministic seed set |
| 19 | Success at the precomputed solve duration |
| 20 | Failure at the configured deadline |
| 21 | Duplicate start cannot reroll the result or duration |
| 22 | Player attempt invalidated by restore; stale commands/completion rejected |
| 23 | Replay completion invalidated by restore |
| 24 | Simulated completion invalidated by restore |
| 25 | Actual WorldState Hacked snapshot restores state and signal |
| 26 | Actual Ready baseline clears Hacked and deactivates signal |
| 27 | Duplicate completion leaves emitter revision unchanged |
| 28 | Real native view destruction removes bindings while hacking stays active |
| 29 | Terminal destruction clears pending work safely |
| 30 | Blueprint children with no presentation overrides can complete hacking |

Additional checks cover concurrent instigators and superseding, invalid command ownership/index/
display/generation, snapshot isolation, lowercase normalization and invalid passwords, missing
persistent capture, the initial simulation opportunity at t=0, missing LetterWidgetClass, immediate
queue reevaluation, opt-in rejection, immutable declared locks, and outcome copying before cleanup.
The Common UI/state-switcher check covers ready (0), active (1), timeout/cancellation (0), wrong
letters retaining page 1, peer success and Hacked reselection (2), and an incomplete switcher
disabling input without altering gameplay. The spatial test starts via the actual Common Start
button, reconstructs page 1 while the action is queued, and observes success/retry after completion.

The spatial integration test uses real GridWorld and SmartObject resolution, Required journaling,
rewind-style system abort with actual elapsed failure duration, replay investigation/recovery,
StopReplay cancellation, and replay on an already Hacked terminal without duplicate publication.
It also submits a non-player goal-derived request through the native action and checks background
execution, precomputed outcome, movement away and final duration. No GOAP planner is introduced.

## PIE and Blueprint composition

`Paradox.Hacking.PIE.PauseDilationAndBlueprintView` starts actual PIE on a temporary map. A compiled
Blueprint view binds to the attempt, game pause freezes the authoritative countdown, 0.5 global
dilation remains applied until completion, and both the view and terminal observe success at the
recorded game-time duration. The fixture uses a CommonUI-compatible viewport and restores the prior
viewport class after PIE.

Temporary Blueprint tests exercise Common buttons/text, three switcher pages, HorizontalBox and VerticalBox containers, dynamic letters,
deselection/reselection, destruction and absence of presentation overrides. They do not save a
production layout. Designers should compose the final two Widget Blueprints using the bindings in
[the usage guide](HACKING_TERMINAL.md), then assign SelectionWidgetClass and LetterWidgetClass.

The final designer-authored appearance, animations, mouse focus/hit geometry and other custom
panel layouts have not been manually exercised. Existing designer Blueprints require the current
Common UI bindings and HackingStateSwitcher contract before use. A cooked package
was not launched. Development and Shipping verification here means successful native compilation.

## Builds and reproducibility

Successful targets: ParadoxEditor Win64 Development, Paradox Win64 Development, and Paradox Win64
Shipping. Built DLLs and game binaries are retained. No installed Engine source was modified.

Run the dedicated suite with `Automation RunTests Paradox.Hacking+GameplayActions.Scheduling.Background`
using an offscreen editor for UMG and PIE. Run shared regressions in a separate editor process with
`Automation RunTests GameplayActions+IntentReplay+Paradox.Interaction+Paradox.Selection+Paradox.CloneBehavior+Paradox.PressurePlate+Paradox.PuzzleOverlay.Renderer`.

Latest widget-change report: `Saved/Automation/HackingCommonWidgets/index.json`;
log: `Saved/Logs/HackingCommonWidgetsTests.log`. Widget regression report/log:
`Saved/Automation/HackingCommonWidgetsRegressions/index.json` and
`Saved/Logs/HackingCommonWidgetsRegressions.log`. Original feature reports remain at
`Saved/Automation/Hacking/index.json` and `Saved/Automation/HackingRegressions/index.json`.
Reports/logs are generated outputs, not source deliverables.

Build validation also repaired two existing test guards: Blueprint wire-target validation now
requires WITH_EDITOR, and the PressurePlate test observer requires WITH_DEV_AUTOMATION_TESTS.
The existing Receiver interaction fixture now disables its uncontrolled-manual fallback so that
its assertion actually tests a Controller prerequisite transition. These changes do not alter
production Receiver, PressurePlate or circuit behavior.

The native default now resolves the designer-renamed `SOD_ParadoxHackingTerminalSmartObject`
asset. Inventory regression fixtures likewise resolve the existing `BP_KeyCard` in
`Blueprints/Access`; their former paths no longer exist. No authored asset composition was changed.

The subsequent letter-generation change excludes the current display from periodic refreshes
and error reshuffles, and aligns simulated first-hit probability with that exclusion.
ParadoxEditor Development, Paradox Development and Paradox Shipping compiled successfully after
this change. Automation and PIE were not rerun for this change; the reports above predate it.

The gate-admission update adds `Paradox.Hacking.GateInvalidationConcurrencyAndView` for blocked starts,
concurrent Player/AI/Replay failure, actual interruption duration, immediate switcher reset, stale clicks,
retry and persistent Hacked state. The spatial integration fixture also includes a gate-triggered failure,
the terminal Gameplay Action result, copied journal outcome and replay of that recorded failure.
`PuzzleSystem.Emitter` regressions cover initial/changed admission notifications, identical republishes,
raw signal preservation, gate destruction, Controller removal, exact-channel/multiple-consumer rules,
Graph-independent observation and reentrant effects. These regressions were compiled but not executed;
automation and PIE reports above predate this update.
ParadoxEditor Win64 Development, Paradox Win64 Development and Paradox Win64 Shipping compiled
successfully after this update. Updated project/plugin DLLs and game executables are retained.
