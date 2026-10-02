# Hacking terminal

Place `AParadoxHackingTerminal` in the normal GridWorld/SmartObject setup. Assign its TerminalMesh
and `Selectable.SelectionWidgetClass`. Native assets are `DA_ParadoxHackTerminal` and
`SOD_ParadoxHackingTerminalSmartObject` (standard Item Slot approach layout). The terminal owns an
Emitter and has no Receiver. Route OutputSignalTag through existing Puzzle controllers.

## Configuration

Defaults: Password HEART, TimeLimitSeconds 30 s, LetterRefreshInterval 0.5 s, LettersPerButton 3,
bAsynchronousRefresh false, MaxAsyncRefreshOffset 0.5 s, and signal
`Puzzle.Signal.Paradox.HackingTerminal.Hacked`. Password is nonempty ASCII A-Z, normalized to
uppercase; spaces, accents and digits are rejected. Times must be finite and positive; K is
3-26, offset finite and nonnegative when async is enabled. ValidateConfiguration reports errors.
Native components, the SmartObject Definition, the HackTerminal catalog entry and WorldState's
PersistentState capture are required. Editor validation also checks the interaction catalog's
Definition/schema. Keep the native persistent-state capture when customizing the participant.

Attempts snapshot configuration, pools and offsets. Every pool has K unique letters including
its target. Sync refresh shares phase; async refresh starts at each sampled offset, then repeats
at LetterRefreshInterval. Offset zero is deferred safely. Timers follow game pause and dilation.
The terminal uses one scheduler and no Actor Tick.
Each refresh samples uniformly from the slot's pool excluding its current letter, so every
unfrozen button visibly changes. Initial letters still sample the full pool. Error reshuffles
also exclude each slot's previous display, including slots that have just been unfrozen.

## Blueprint binding and composition

Create a Widget Blueprint derived from `UParadoxHackingTerminalWidget`. Required BindWidget names:

| Name | Type |
| --- | --- |
| StartHackingButton | Concrete Blueprint subclass of CommonButtonBase |
| PasswordText | CommonTextBlock |
| TimeRemainingProgress | ProgressBar |
| LetterContainer | Any multi-child PanelWidget subclass, including HorizontalBox, VerticalBox, WrapBox |
| HackingStateSwitcher | WidgetSwitcher with at least three child pages |

Create a second Blueprint derived from `UParadoxHackingLetterWidget`, with `LetterButton` (a concrete
CommonButtonBase Blueprint) and `LetterText` (CommonTextBlock). Compose the text inside a named slot
exposed by the Common button, so LetterText belongs to the letter Blueprint's binding hierarchy.
A text hidden in the Common button's own private WidgetTree cannot satisfy the outer BindWidget.
Assign the letter Blueprint as LetterWidgetClass
on the terminal widget. Native logic adds one child per password slot. OnLetterWidgetsRebuilt
lets Blueprint configure the panel slots, sizes and alignment. All fonts, colors, images,
animations, padding and layout belong to Blueprint. There is no native fallback WidgetTree.

C++ updates text, Common button interaction availability, remaining-time progress and click bindings.
Assign Common UI styles in the button/text Blueprints; native logic does not replace those styles.
PasswordText shows
the target. GetViewedAttempt provides remaining seconds and frozen-letter progress for custom UI.
Presentation hooks: OnFrozenStateChanged on letters; OnHackingError, OnHackingSucceeded,
OnHackingTimedOut, OnHackingFailed, OnLetterWidgetsRebuilt and OnHackingViewUnavailable on the terminal view.
No presentation override is necessary for authoritative behavior.

Compose HackingStateSwitcher pages in this order: **0 ready/retry**, **1 hacking**, **2 hacked**.
Place StartHackingButton on page 0 and the minigame controls on page 1 as appropriate for your layout.
An accepted Start click selects page 1 immediately, including a queued action or spatial approach.
Rejected requests stay on page 0. Timeout, cancellation and approach failure return to page 0;
an incorrect letter only resets progress and keeps page 1 because the attempt remains active.
Persistent Hacked always selects page 2, including another instigator's success and reselection.
The view reconstructs accepted approach/active attempt state and never cancels gameplay on removal.
Do not drive the switcher's active index in Blueprint; native logic derives it from gameplay state.
All three page compositions remain designer-owned. Existing widget Blueprints must replace ordinary
Button/TextBlock bindings with Common types and add the named switcher; no native fallback is created.

Missing bindings/classes disable controls and produce LogParadox diagnostics. They do not cancel
or restart attempts. The view timer exists only with a selection context. Deselect/removal clears
bindings and presentation only; reselection reconstructs the same attempt.

## Actions and attempts

Submit through Interaction.RequestInteraction or StartHacking. The requester needs an initialized
IntentReplay journal sink; HackTerminal uses Required journaling and normal spatial preflight.
Approach owns Movement/Interaction locks and a SmartObject claim. Actual attempt start releases
them through background execution, retaining handle/lifecycle until completion. Other actions,
movement away and deselection do not interrupt hacking.

The terminal owns attempts with GUID, generation, weak instigator, mode, timings and letter state.
Duplicate active starts do not create a new attempt; different instigators can hack concurrently.
Correct clicks freeze a letter. Wrong clicks unfreeze all slots and reroll current letters while
preserving pools, deadline and phase. SubmitLetter/CancelAttempt validate instigator and identity;
clicks also validate index and current display. Invalid/stale commands return false. Queries copy
snapshots, preventing mutation of authoritative state.

Controller gates on OutputSignalTag also govern hacking availability. Start/preflight and actual attempt
creation reject a blocked emitter, including Player, AI and Replay requests. The rule matches emitter
activation interactions: at least one Open/Bypassed primary consumer permits hacking; all Closed/Invalid
consumers block it; no consumers permits it. This does not alter the Controller-local raw signal.

If admission is lost during hacking, every active attempt immediately becomes Failed. Actions report
semantic failure with actual elapsed game time, including simulated/replay attempts, rather than their
planned completion time. The selected view returns to switcher page 0, disables Start while blocked,
disables letter interaction, and calls OnHackingFailed. Reopening gates enables a fresh request; it never
resumes a failed attempt. Queued/approaching requests revalidate before attempt creation. Already Hacked
persistent state stays Hacked and page 2 stays selected when gates subsequently close.

For Actor effects, bind Emitter.OnGateInvalidationChanged and filter OutputSignalTag; bIsInvalidated
selects blocked/admitted visuals. Query Emitter.IsInvalidated(OutputSignalTag) for initial state.

First success sets Hacked and calls Emitter.SetSignalState once. Peers become Superseded and their
actions cancel without publication. Timeout leaves Ready and permits retry. Explicit cancellation,
death, StopReplay and teardown cancel owned attempts without success signals.

## Replay, AI and WorldState

One semantic action records Target and standard interaction parameters. OutcomeParameters contains
only DurationSeconds (double) and bSucceeded (bool); no clicks, pools, offsets or seeds. Rewind
aborts before track finalization and captures failure after actual elapsed hacking time.

Clone replay uses UParadoxCloneReplayExecutionStrategy and the source outcome, waiting its duration
without UI. Expected failure remains Failed in the journal and does not stop the timeline. Other
target/configuration errors retain normal failure handling. Recoverable investigation preserves
background attempts; StopReplay still cancels owned handles. An already Hacked terminal preserves
recorded replay duration/outcome without another signal publication; ordinary interaction is already
satisfied.

AI/GoalDerived and other non-player, non-replay origins simulate once. Initial display gives an
opportunity at t=0 with probability 1/K; unsolved slots sample geometric first-hit times with
probability 1/(K-1), matching refreshes that exclude the current wrong letter. The
maximum is solve duration. Beyond TimeLimitSeconds, failure occurs at the deadline. Replanning does not
reroll. The GOAP planner is outside this feature.

WorldState captures only persistent Ready/Hacked. PreRestore cancels attempts, clears timers and
advances generation. PropertiesRestored rebuilds the signal from restored state: Hacked snapshots
activate it; Ready baseline deactivates it. Transient attempts never resume. Stale callbacks and
clicks cannot hack the restored terminal.

## Debugging and tests

Use GetAttemptSnapshot, GetAttemptForInstigator, GetDiagnosticSnapshots and ValidateConfiguration.
Snapshots identify the weak instigator and expose bHasPlannedOutcome, bPlannedSuccess and
PlannedDurationSeconds for simulated/replay attempts. Interactive attempts have no planned outcome.
Enable local bEnableDebug plus `Paradox.Hacking.Debug 1` for transition diagnostics, without refresh
spam. Automation: `Paradox.Hacking` and
`GameplayActions.Scheduling.BackgroundLocksQueueAndCopiedOutcome`. Blueprint test trees are
editor-only temporary fixtures; final UI composition remains designer-authored.

See [validation coverage](HACKING_TERMINAL_VALIDATION.md) for the tested scenarios and runtime limits.
