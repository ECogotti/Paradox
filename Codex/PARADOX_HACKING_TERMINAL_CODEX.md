# `AParadoxHackingTerminal` — Codex Implementation Specification

## Purpose

Implement a new Paradox gameplay Actor named:

```cpp
AParadoxHackingTerminal
```

The Actor represents an interactable hacking terminal used as an environmental puzzle **Emitter only**.

It must integrate with the existing:

- PuzzleSystem;
- selection / hover system;
- interaction system;
- IntentReplay;
- GOAP / GoalAgents integration used by Paradox;
- WorldState reset/snapshot system.

Do **not** redesign those systems to fit this Actor. Inspect the existing implementation and extend the smallest appropriate integration points.

---

# Mandatory preliminary investigation

Before changing code:

1. Read the repository root `AGENTS.md`.
2. Identify the module that should own `AParadoxHackingTerminal`.
3. Search that module and every affected plugin/module for relevant `CODEX` folders and read the applicable instructions.
4. Read the relevant `Docs` folders.
5. Inspect the current APIs and implementations of:
   - `UPuzzleEmitterComponent`;
   - Selection / Hover;
   - Interaction;
   - IntentReplay;
   - WorldState;
   - GOAP / GoalAgents;
   - existing Paradox gameplay actions;
   - existing puzzle Actor integrations.
6. Find existing patterns for:
   - interactable/selectable Actors;
   - interaction actions exposed in the interaction widget;
   - IntentReplay recording and replay;
   - non-blocking/background gameplay actions, if already supported;
   - GOAP actions with delayed completion;
   - WorldState participants;
   - runtime UMG creation / restoration.
7. Reuse existing interfaces, components, Gameplay Tags, action types, state serialization patterns, timers, delegates, logging and debug conventions whenever possible.
8. Do not invent Unreal APIs or project APIs.
9. Avoid creating dependencies from generic plugins back into Paradox-specific code.

`AParadoxHackingTerminal` is project-specific and should normally live in the Paradox integration/gameplay layer because it coordinates several generic systems. Do not move IntentReplay-, GOAP-, selection-, interaction-, or Paradox-specific behavior into the generic PuzzleSystem plugin.

---

# Architectural role

The terminal is always an **Emitter**, never a Receiver.

Required conceptual composition:

```text
AParadoxHackingTerminal
├── SceneRoot
├── UPuzzleEmitterComponent
└── existing Paradox selection / interaction capabilities as required
```

The puzzle flow remains:

```text
AParadoxHackingTerminal
        ↓ owns/drives
UPuzzleEmitterComponent
        ↓
APuzzleController
        ↓
UPuzzleReceiverComponent
```

The terminal must never:

- directly activate Receivers;
- evaluate Controller conditions;
- bypass `APuzzleController`;
- become a new PuzzleSystem architectural role.

On successful hacking it publishes one persistent configured signal through its owned `UPuzzleEmitterComponent`.

---

# Designer-facing configuration

Expose the following tuning on `AParadoxHackingTerminal`, using the existing project style for reflected properties, categories, tooltips, clamps and edit conditions.

Required configuration:

```text
Password
TimeLimitSeconds
LetterRefreshInterval
LettersPerButton
bAsynchronousRefresh
MaxAsyncRefreshOffset
OutputSignalTag
```

## `Password`

The password the player must complete.

Initial implementation should use the English alphabet only.

Normalize or validate according to the existing project conventions, but do not silently accept unsupported characters if they cannot be represented by the minigame.

Example:

```text
HEART
```

Prefer case-insensitive designer input but use a normalized uppercase runtime representation.

An empty password is invalid configuration and must fail predictably.

## `TimeLimitSeconds`

Total time available for one hacking attempt.

Must be greater than zero.

This timer starts when hacking begins and continues independently of:

- selection state;
- whether the hacking widget is currently visible;
- player movement;
- distance from the terminal after the attempt has started;
- other gameplay actions performed by the same pawn.

## `LetterRefreshInterval`

Base interval between character changes for non-frozen buttons.

Must be greater than zero.

Do not implement the minigame through Actor or Widget Tick if the same behavior can be implemented with the project's normal timer/event mechanisms.

## `LettersPerButton`

Number of possible characters assigned to each password button.

Required range:

```text
Minimum = 3
Maximum = 26
```

Use the English alphabet:

```text
ABCDEFGHIJKLMNOPQRSTUVWXYZ
```

For each button:

```text
1 candidate = correct target character
LettersPerButton - 1 candidates = unique incorrect characters
```

Incorrect candidates must:

- be selected from the English alphabet;
- never equal the target character;
- not contain duplicates inside the same button candidate pool.

Example:

```text
Target = H
LettersPerButton = 3

Candidates:
H
M
Q
```

A value of `26` means that button can cycle through the full English alphabet.

Candidate pools are generated once when that hacking attempt starts and remain stable for the lifetime of the attempt.

A new attempt may generate new incorrect candidates.

## `bAsynchronousRefresh`

When false, all non-frozen buttons refresh in sync.

When true, each button uses an initial random phase offset.

## `MaxAsyncRefreshOffset`

Relevant only when `bAsynchronousRefresh == true`.

Each button receives an offset equivalent to:

```text
Random(0, MaxAsyncRefreshOffset)
```

The offset is generated once when the attempt begins and remains stable for that attempt.

After the initial offset, the button continues to refresh every `LetterRefreshInterval`.

Use edit conditions so this property is clearly irrelevant in synchronous mode where practical.

## `OutputSignalTag`

Designer-configurable puzzle signal emitted by the terminal.

The signal is initially inactive unless an existing save/snapshot state says otherwise.

Successful hacking activates it persistently.

Use the normal `UPuzzleEmitterComponent` signal API.

Do not hard-code direct Receiver behavior.

---

# Terminal authoritative state

The Widget must **not** own the hacking state.

The terminal, or a terminal-owned runtime object/struct following existing project ownership conventions, is authoritative.

At minimum separate persistent terminal state from individual attempt state.

Conceptual terminal state:

```text
Ready
Hacked
```

Conceptual attempt state:

```text
Active
Success
TimedOut
Cancelled
```

Use explicit enums or the closest established project pattern rather than several unrelated authoritative booleans.

---

# Hacking attempt model

A hacking attempt must contain enough authoritative state to reconstruct the UI at any time.

Conceptually:

```text
HackingAttempt
├── AttemptId / equivalent stable identity if required
├── Instigator
├── StartTime
├── ExpirationTime
├── State
└── Buttons[]
     ├── TargetCharacter
     ├── CandidateCharacters[]
     ├── CurrentCharacter
     ├── bFrozen
     └── RefreshOffset
```

Exact data types must follow existing project conventions.

Do not expose mutable runtime internals directly to Blueprint if controlled query/command APIs are more appropriate.

---

# Hacking button UI model

Correction: there is **not** a separate letter display above a second lock button.

Each password position is represented by one UI button whose own text displays the currently active letter.

Conceptually:

```text
HackingLetterButton
└── Button
    └── Text
```

Example:

```text
PASSWORD: HEART

[ H ] [ X ] [ A ] [ Q ] [ T ]
```

Clicking a button evaluates the letter currently displayed **inside that same button**.

The UI should dynamically create or configure one hacking button for each password character.

The exact UMG class names should follow existing project naming and UI architecture. Do not create unnecessary Widget classes if the current framework already provides an appropriate reusable pattern.

---

# Candidate generation

When an attempt begins, generate one stable candidate pool for each password slot.

For target character `T`:

```text
Candidates[slot] =
    T
    + LettersPerButton - 1 unique incorrect characters
```

Candidate order may be randomized.

The candidate pool itself must not be regenerated on every refresh.

Only the currently displayed character changes during refresh.

---

# Character refresh behavior

For each non-frozen button:

```text
CurrentCharacter =
    RandomElement(CandidateCharacters)
```

A frozen button must not refresh.

## Synchronous mode

Conceptually:

```text
every LetterRefreshInterval
    refresh every non-frozen button
```

Buttons refresh at the same time.

## Asynchronous mode

Conceptually:

```text
for each button:
    InitialOffset = Random(0, MaxAsyncRefreshOffset)

    after InitialOffset:
        refresh

    then:
        refresh every LetterRefreshInterval
```

The exact implementation should minimize timer proliferation if the existing codebase has a cleaner scheduler/timer pattern, but the observable behavior must remain equivalent.

No refresh timing may depend on the Widget remaining alive.

---

# Correct button click

When the player clicks slot `i`:

```text
CurrentCharacter[i] == TargetCharacter[i]
```

then:

```text
Buttons[i].bFrozen = true
```

The button:

- keeps displaying the correct target character;
- stops randomizing;
- visually reflects the frozen state through the Widget.

If the button is already frozen, repeated clicks must not produce duplicate state transitions.

After every successful freeze, check completion.

The hacking attempt succeeds when:

```text
for every button:
    bFrozen == true
```

---

# Incorrect button click

When:

```text
CurrentCharacter[i] != TargetCharacter[i]
```

the attempt does **not** end.

Instead:

```text
all buttons -> bFrozen = false
all buttons resume refreshing
reroll the current displayed characters immediately
global attempt timer continues unchanged
candidate pools remain unchanged
```

The player must rebuild the password from zero.

Do not:

- reset `TimeLimitSeconds`;
- regenerate candidate pools;
- create a new hacking attempt;
- mark the attempt failed immediately.

---

# Timeout

When the attempt reaches its expiration time before completion:

```text
Attempt.State = TimedOut
```

For that attempt:

- stop its refresh scheduling;
- invalidate stale callbacks/timers;
- close or update its UI as appropriate;
- do not activate the terminal output signal.

If the terminal is still `Ready`, another hacking attempt may be started later.

---

# Successful completion

All successful paths must converge on one authoritative terminal operation equivalent in responsibility to:

```text
CompleteHackingAttempt(...)
```

Do not duplicate terminal activation logic inside:

- player UI code;
- IntentReplay code;
- GOAP code.

On the first valid success:

```text
TerminalState = Hacked
OutputSignalTag = active
```

Publish through `UPuzzleEmitterComponent`.

The signal remains active until the terminal state is restored/reset by the appropriate game/world state flow.

Repeated success requests after the terminal is already hacked must be deduplicated.

Any still-active attempts that no longer have gameplay meaning after the terminal becomes hacked must be completed/cancelled safely according to the chosen concurrency policy.

---

# Selection and interaction integration

`AParadoxHackingTerminal` must use the selection / hover and interaction systems that already exist in Paradox.

Do not create a parallel interaction framework.

Required player-facing interaction:

```text
Start Hacking
```

The interaction system must enforce its existing normal requirements to **start** the action, including adjacency/range if that is already part of the project contract.

After an attempt has started, moving away from the terminal must **not** cancel it.

The hacking action is non-blocking.

The player may:

- start hacking;
- move away;
- perform other gameplay actions;
- select other Actors;
- later reselect the terminal.

Do not keep the player movement/action state locked simply because a hacking attempt exists.

---

# Widget lifetime and state restoration

The Widget is a view/controller for player input, not the authoritative minigame state.

Required behavior:

```text
player selects terminal
    ↓
Widget shows active hacking state

player deselects terminal
    ↓
Widget disappears / is removed according to existing selection architecture
    ↓
hacking continues on terminal

player selects terminal again while attempt is still active
    ↓
Widget reconstructs from authoritative attempt state
    ↓
same remaining time
same candidate pools
same current displayed characters
same frozen buttons
same async offsets / refresh phase
```

Do not restart the attempt when the Widget is reconstructed.

Do not regenerate random state merely because a Widget is recreated.

The progress bar must be derived from authoritative attempt timing.

Conceptually:

```text
RemainingTime / TimeLimitSeconds
```

Use the current project's preferred UI update mechanism.

---

# Concurrent attempts

Preserve support for multiple logical actors interacting with the same terminal unless the existing interaction architecture imposes a stronger project rule.

The runtime model should not assume that the Widget itself represents "the one global attempt".

Potential participants include:

```text
Player
IntentReplay clone
GOAP clone
```

Attempts should therefore be associated with an Instigator or equivalent identity.

The first valid successful completion hacks the terminal.

Once the terminal is hacked, later callbacks from obsolete attempts must not republish success or corrupt state.

If the existing gameplay design already explicitly restricts a terminal to one active attempt, follow that existing rule instead and document the conflict rather than inventing a new concurrency layer.

---

# IntentReplay integration

The hacking action must be recorded by IntentReplay.

## Player recording

The action begins when the player executes `Start Hacking`.

The action ends when either:

```text
Success
Timeout / Failure
```

Record only the semantic result needed by replay.

Conceptually:

```text
HackTerminal
Target = AParadoxHackingTerminal
Duration = actual elapsed attempt duration
Result = Success | Failure
```

Do **not** record:

```text
candidate pools
current letter sequence
individual refreshes
button clicks
wrong clicks
frozen slot history
async offsets
random seeds
```

unless the existing IntentReplay architecture requires additional generic metadata for normal action identity/lifecycle.

## Clone replay

A replay clone must not play the UI minigame.

It replays the recorded semantic outcome.

Conceptually:

```text
Start replay hacking
    ↓
schedule RecordedDuration
    ↓
clone remains free to perform other gameplay behavior
    ↓
RecordedDuration elapsed
    ↓
if recorded Result == Success:
    request authoritative terminal hacking success
else:
    finish without activating terminal
```

This is a **non-blocking/background action**.

Inspect IntentReplay before implementing this. If the current action model assumes every action is blocking/sequential, extend it using the smallest generic mechanism necessary rather than special-casing the entire replay system around this terminal.

Do not fake non-blocking behavior by immediately completing the recorded action if doing so would change its temporal meaning.

Stale replay completion callbacks after:

- WorldState reset;
- terminal destruction;
- terminal already hacked;
- attempt cancellation;

must not mutate restored/current state.

---

# GOAP integration

GOAP clones do not play the UI minigame.

They simulate its difficulty and determine:

```text
Success / Failure
SimulatedSolveTime
```

once when the hacking action begins.

The result must remain stable for that attempt.

Do not reroll simply because the GOAP agent replans or some unrelated event occurs.

---

# GOAP difficulty parameters

Difficulty must be based on at least:

```text
L = Password length
K = LettersPerButton
T = TimeLimitSeconds
R = LetterRefreshInterval
asynchronous refresh offsets when enabled
```

Larger `L` increases difficulty.

Larger `K` increases difficulty.

Larger `T` decreases difficulty.

Larger `R` generally increases difficulty because correct letters appear less frequently.

For a button with `K` candidates:

```text
P(correct on one random refresh) = 1 / K
```

After `N` independent opportunities:

```text
P(target appeared at least once)
    = 1 - ((K - 1) / K)^N
```

For multiple independent slots, a useful analytical reference is:

```text
P(success)
    = product over slots of:
      [1 - ((K - 1) / K)^Ni]
```

Do not necessarily implement the GOAP solver as one direct closed-form roll if the first-hit simulation below produces the required duration more naturally.

---

# GOAP solve-time simulation

Preferred behavior:

For every password slot, simulate when its target character first appears using the same effective probability:

```text
1 / LettersPerButton
```

Include:

- `LetterRefreshInterval`;
- initial asynchronous offset when enabled.

Conceptually:

```text
slot H -> first correct appearance at 2.1 s
slot E -> first correct appearance at 5.4 s
slot A -> first correct appearance at 1.2 s
slot R -> first correct appearance at 8.7 s
slot T -> first correct appearance at 4.3 s

SimulatedSolveTime = max(slot first-hit times)
```

Then:

```text
if SimulatedSolveTime <= TimeLimitSeconds:
    Result = Success
    Duration = SimulatedSolveTime
else:
    Result = Failure
    Duration = TimeLimitSeconds
```

This represents an abstract idealized hacking performance and is intentionally not a literal replay of UI clicks.

If the current GOAP architecture has an established stochastic-cost/action-result abstraction, integrate this calculation there rather than creating a terminal-owned GOAP framework.

Expose calculation helpers only where useful for testing/debugging; do not expose mutable internals.

---

# GOAP action execution

The GOAP hacking execution is non-blocking.

Conceptually:

```text
GOAP starts HackTerminal
    ↓
calculate Result + Duration once
    ↓
register/schedule terminal attempt
    ↓
agent is free to continue with other behavior
    ↓
Duration elapsed
    ↓
Success -> authoritative terminal completion
Failure -> no puzzle signal
```

Do not require the clone to remain adjacent to the terminal once hacking has started.

As with IntentReplay, stale delayed completion must be safely invalidated by reset/destruction/current terminal state.

---

# WorldState integration

`AParadoxHackingTerminal` must participate correctly in the existing WorldState architecture.

Do not build a second custom snapshot/reset framework.

First inspect:

- how puzzle Actors currently participate;
- what interface/component owns snapshot capture;
- how persistent vs transient state is represented;
- reset ordering relative to PuzzleSystem Emitters and Controllers;
- how pending timers/actions are invalidated.

At minimum WorldState restoration/reset must correctly handle:

```text
TerminalState
Output signal state
active hacking attempts
pending refresh timers
timeout timers
IntentReplay delayed completions
GOAP delayed completions
Widget-visible runtime state
```

## Reset safety

When a world reset invalidates a currently running attempt:

```text
cancel/invalidate attempt timing
cancel/invalidate letter refresh callbacks
cancel/invalidate timeout callbacks
cancel/invalidate IntentReplay completion callbacks
cancel/invalidate GOAP completion callbacks
prevent stale callbacks from firing into restored state
```

Do not rely only on clearing a timer handle if another delayed system can still call the terminal later. Use the existing project identity/generation/cancellation mechanism where available.

## Restoring terminal state

WorldState is authoritative about the restored state.

Do not hard-code every restore to `Ready`.

For example, if the restored snapshot says the terminal was hacked:

```text
TerminalState = Hacked
OutputSignalTag = active
```

If restoring the initial baseline:

```text
TerminalState = Ready
OutputSignalTag = inactive
```

The final emitter state after restoration must match the restored terminal state so Controllers querying the Emitter receive the correct value.

For an in-progress hacking attempt, follow the actual WorldState contract discovered in the repository:

- if transient gameplay operations are intentionally discarded on restore, clear the attempt safely;
- if the system explicitly snapshots transient operations, restore the full authoritative attempt state including timing/random state as required.

Do not invent behavior that conflicts with the existing WorldState model.

---

# Initialization

At runtime initialization:

1. validate configuration;
2. initialize authoritative terminal state;
3. initialize the owned `UPuzzleEmitterComponent`;
4. publish/expose the correct initial signal state;
5. register with selection/interaction/WorldState using the established lifecycle;
6. ensure late-starting Puzzle Controllers can query the correct state.

Do not depend on Blueprint child event ordering to establish authoritative state.

---

# Shutdown / EndPlay

On shutdown:

- invalidate terminal-owned timers;
- invalidate active attempt callbacks;
- release/unregister interaction bindings where required;
- unregister WorldState participation if required;
- invalidate delayed replay/GOAP completion safely;
- remove Widget bindings/delegates;
- avoid callbacks into destroyed UObjects/Actors.

Follow existing module lifetime patterns.

---

# Suggested controlled API responsibilities

Exact API names must follow the repository after inspection. Do not blindly create these names if equivalent APIs already exist.

Conceptually the terminal needs operations equivalent to:

```text
StartHacking(Instigator)
SubmitLetter(Instigator, SlotIndex)
GetHackingAttemptState(Instigator)
HasActiveHackingAttempt(Instigator)

CompleteHackingAttempt(AttemptIdentity, Result)
CancelHackingAttempt(AttemptIdentity)

IsHacked()
GetRemainingHackTime(Instigator)
GetHackProgress(Instigator)
```

Internal responsibilities should be centralized around operations equivalent to:

```text
CreatePlayerAttempt(...)
GenerateCandidatePools(...)
RefreshButton(...)
HandleCorrectSelection(...)
HandleIncorrectSelection(...)
CheckPlayerAttemptCompletion(...)
SetTerminalHacked(...)
RestoreTerminalState(...)
```

The important requirement is single ownership of state transitions, not these exact function names.

---

# Blueprint / presentation hooks

Provide only useful presentation hooks consistent with existing project patterns.

Potential events/delegates:

```text
OnHackingStarted
OnHackingProgressChanged
OnHackingMistake
OnHackingSucceeded
OnHackingTimedOut
OnTerminalHacked
OnTerminalRestored
```

Do not make Blueprint responsible for:

- authoritative hacking state;
- timers;
- candidate generation;
- success validation;
- puzzle signal publication;
- IntentReplay outcome;
- GOAP simulation;
- WorldState restoration.

Blueprint/UMG should focus on visuals, audio and presentation.

---

# Validation

Validate at least:

```text
Password is not empty
Password contains only supported English alphabet characters
TimeLimitSeconds > 0
LetterRefreshInterval > 0
LettersPerButton >= 3
LettersPerButton <= 26
MaxAsyncRefreshOffset >= 0
OutputSignalTag is valid
owned UPuzzleEmitterComponent exists
required interaction/selectable configuration resolves
required WorldState integration is valid
```

Invalid configuration must fail predictably.

Do not publish an unrelated fallback signal.

---

# Debug information

Use the existing owning module's logging/debug infrastructure.

Do not introduce `LogTemp` in committed code.

Make the following state inspectable when debug is enabled:

```text
Actor name
TerminalState
OutputSignalTag
Output signal active/inactive
Password length
LettersPerButton
TimeLimitSeconds
LetterRefreshInterval
Sync / Async mode
MaxAsyncRefreshOffset
number of active attempts
```

For each active attempt where useful:

```text
Instigator
Attempt state
Elapsed time
Remaining time
Result if precomputed
Simulated duration if GOAP/replay
button target
button candidate pool
button current character
button frozen state
button refresh offset
```

Do not spam logs every refresh by default.

Use state-transition logging or explicit debug modes.

---

# Required behavior scenarios

Validate at minimum the following.

## 1. Basic player success

```text
Password = HEART
player starts hacking
all five correct letters are frozen before timeout
terminal becomes Hacked
OutputSignalTag becomes active
```

## 2. One button equals one visible/clickable letter

The displayed text is inside the clickable button.

There is no second lock-button row.

Clicking `[H]` evaluates the `H` currently shown by that button.

## 3. Candidate count = 3

For each slot:

```text
1 correct character
2 unique incorrect characters
```

No duplicate candidate and no incorrect candidate equal to the target.

## 4. Candidate count = 26

The candidate pool contains the full English alphabet exactly once.

## 5. Candidate pools remain stable

During refreshes:

```text
CandidateCharacters unchanged
CurrentCharacter changes
```

## 6. Correct click

Correct current character:

```text
button becomes frozen
display remains correct
refresh stops for that button
```

## 7. Incorrect click after partial progress

Example:

```text
3 buttons frozen
player clicks wrong character
```

Result:

```text
all 3 unfreeze
all slots resume randomization
current letters reroll
candidate pools stay unchanged
global timer does not restart
```

## 8. Timeout

No full password before deadline:

```text
attempt -> TimedOut
terminal remains Ready
emitter remains inactive
```

## 9. Deselect during hacking

```text
start hacking
freeze some buttons
deselect terminal
Widget disappears
attempt continues
```

## 10. Reselect during hacking

```text
reselect same terminal
Widget shows the authoritative current attempt
frozen states preserved
current letters preserved
remaining time preserved
```

No new attempt is created.

## 11. Move away

Leaving interaction range after `Start Hacking` does not cancel the attempt.

## 12. Non-blocking player behavior

The player can perform unrelated gameplay actions while the attempt continues.

## 13. Synchronous refresh

All non-frozen buttons refresh together at `LetterRefreshInterval`.

Frozen buttons remain unchanged.

## 14. Asynchronous refresh

Each button uses its stable initial offset and then its normal interval.

Widget recreation does not regenerate offsets.

## 15. IntentReplay success

Recorded:

```text
Duration = X
Result = Success
```

Clone starts semantic hacking, remains free to act, and after X seconds terminal success is applied if still valid.

## 16. IntentReplay failure

Recorded failure waits the recorded duration and never activates the terminal.

## 17. GOAP easy configuration

Short password, low `LettersPerButton`, fast refresh and generous time should produce higher success opportunity than an otherwise identical harder configuration.

Do not hard-code a designer-facing "easy" flag; validate the actual simulation inputs.

## 18. GOAP hard configuration

Increasing `Password.Length` or `LettersPerButton`, reducing available time, or increasing refresh interval must affect the simulated difficulty in the expected direction.

## 19. GOAP success duration

Successful simulated hacking completes after its precomputed `SimulatedSolveTime`, not automatically at the full timeout.

## 20. GOAP failure duration

A simulated failure completes at `TimeLimitSeconds`.

## 21. GOAP result stability

Once the action begins, replanning or unrelated updates do not reroll that attempt.

## 22. World reset while player hacking

Reset:

```text
invalidates attempt
invalidates timers/callbacks
restores terminal from WorldState
restores matching emitter signal
```

No stale callback later hacks the restored terminal.

## 23. World reset while replay completion is pending

The old replay callback must not mutate the restored state.

## 24. World reset while GOAP completion is pending

The old GOAP callback must not mutate the restored state.

## 25. Restore hacked snapshot

If the WorldState snapshot says hacked:

```text
TerminalState = Hacked
Emitter = active
```

## 26. Restore initial baseline

If the baseline says unhacked:

```text
TerminalState = Ready
Emitter = inactive
```

## 27. Duplicate completion

Multiple success callbacks after the terminal is already hacked produce no duplicate signal/state transition.

## 28. Widget destroyed

Destroying/removing the Widget alone never destroys or cancels the authoritative hacking attempt.

## 29. Terminal destruction

All timers/delegates/callbacks are safely invalidated.

## 30. Blueprint child with no presentation overrides

Core hacking logic, PuzzleSystem signal publication, replay integration, GOAP simulation and WorldState behavior remain functional without Blueprint presentation hooks.

---

# Forbidden shortcuts

Do not:

```text
store authoritative hacking state only in the Widget
restart hacking because the Widget was recreated
cancel hacking because the terminal was deselected
cancel hacking because the pawn moved away
make hacking a blocking player/replay/GOAP action
record individual UI button clicks into IntentReplay
make replay clones actually play the UI minigame
make GOAP clones actually play the UI minigame
reroll GOAP outcome during the same attempt
hard-code 26 candidate letters for every button
regenerate candidate pools every refresh
reset the global attempt timer after a wrong click
publish directly to Receivers
make AParadoxHackingTerminal a Puzzle Receiver
put Paradox-specific dependencies inside the generic PuzzleSystem plugin
use Tick by default for letter refresh/countdown
let stale timer/replay/GOAP callbacks mutate state after WorldState reset
hard-code WorldState restoration to Ready when a snapshot may contain Hacked
create a parallel selection or interaction framework
create a second WorldState/reset architecture
```

---

# Documentation

Update/create the relevant human-facing `Docs` for every affected module/plugin.

Document at least:

- how to place/configure `AParadoxHackingTerminal`;
- required designer properties;
- interaction flow;
- hacking UI behavior;
- `LettersPerButton`;
- synchronous vs asynchronous refresh;
- PuzzleSystem signal behavior;
- IntentReplay semantics;
- GOAP difficulty simulation;
- non-blocking behavior;
- WorldState/reset behavior;
- debugging/troubleshooting.

Do not put Codex workflow instructions in `Docs`.

---

# Compilation and verification

Compilation is part of the task.

After meaningful changes:

1. compile the affected target;
2. read complete relevant errors;
3. fix errors caused by the implementation;
4. recompile until successful.

Also validate the required runtime scenarios where practical.

If some runtime integration cannot be validated because of an external limitation, explicitly report:

- what was implemented;
- what was compiled;
- what could not be tested;
- why;
- the remaining risk.

Review the final diff and remove unrelated changes, temporary logging and generated files.
