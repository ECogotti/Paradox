# Puzzle Emitter Actor Templates — Codex Specification

This file defines reusable gameplay Actor templates built on top of the existing environmental puzzle system.

It supplements:

- the root `AGENTS.md`;
- `PUZZLE_SYSTEM_ARCHITECTURE.md`.

Do not repeat or reimplement rules already defined there.

The purpose of this file is to define ready-to-use **Emitter Actor bases** that designers can place in the world or subclass in Blueprint.

These Actors are not replacements for `UPuzzleEmitterComponent`.

The layering is:

```text
Reusable gameplay Actor
        ↓
owns and drives
        ↓
UPuzzleEmitterComponent
        ↓
publishes puzzle signal state
```

Each template in this file must define a reusable gameplay concept, not merely wrap one call to the Emitter component.

---

# TEMPLATE DESIGN PRINCIPLES

## 1. Templates are world objects, not new puzzle-system roles

An Emitter template is a gameplay Actor that uses `UPuzzleEmitterComponent`.

It may provide:

- gameplay state;
- reusable state-transition rules;
- timers;
- designer-facing configuration;
- Blueprint hooks;
- a minimal public API for concrete gameplay input.

It must not bypass the existing puzzle flow.

The final output remains:

```text
Template Actor
    ↓
UPuzzleEmitterComponent
    ↓
APuzzleController
```

---

## 2. Separate input detection from state policy when they are not the same concern

A generic template should not accumulate every possible way of detecting gameplay input.

For example, a reusable switch Actor may understand:

```text
Press
Release
```

without knowing whether those calls came from:

```text
player interaction
overlap
weight detection
animation
AI
a scripted event
another gameplay system
```

Concrete Blueprint children may provide the physical or interaction-specific detection.

---

## 3. Prefer a small configurable state machine over many near-identical Actor classes

When several classic puzzle objects differ mainly in how input changes output state, use one reusable Actor with explicit modes.

Do not create separate native classes merely for:

```text
Button
Lever
Pressure Plate
Pedal
Toggle Switch
One-Shot Switch
Timed Button
```

when the difference can be expressed by one stable state policy.

---

## 4. Template Actors publish observed state; they do not contain puzzle conditions

Emitter templates may decide their own local state.

They must not decide whether doors, traps, bridges, or other Receivers should activate.

That remains Controller responsibility.

---

## 5. Template Actors must be useful as Blueprint parents

A designer should be able to create a Blueprint child and focus on:

```text
visuals
interaction
collision
audio
animation
```

without rebuilding the core state behavior.

---

# TEMPLATE 01 — `APuzzleSwitch`

## Purpose

`APuzzleSwitch` is the generic base Actor for puzzle controls whose input can be reduced to two conceptual events:

```text
Press
Release
```

It converts those input events into one persistent puzzle signal according to a configurable switch mode.

It is intended to serve as the native base for designer-facing Blueprint Actors such as:

```text
BP_PressurePlate
BP_Button
BP_Lever
BP_Pedal
BP_WallSwitch
BP_RuneSwitch
BP_PullChain
```

The concrete child decides how `Press` and `Release` are triggered.

`APuzzleSwitch` decides how those calls affect switch state.

`UPuzzleEmitterComponent` publishes the resulting signal.

---

# ARCHITECTURE

Required conceptual composition:

```text
APuzzleSwitch
├── Scene Root
└── UPuzzleEmitterComponent
```

Do not add mandatory mesh, collision, interaction, trigger, or weight-detection components to the native base class.

Those belong to concrete children unless a future shared requirement clearly justifies a separate reusable template.

The state flow is:

```text
Gameplay input
    ↓
Press / Release requests
    ↓
Input transition state machine
    ↓
optional PressDelay / ReleaseDelay
    ↓
Confirmed Press / Release edge
    ↓
APuzzleSwitch mode policy
    ↓
Switch output state changes
    ↓
UPuzzleEmitterComponent publishes configured signal
```

The input transition state machine and the switch output policy are separate responsibilities.

The input state machine decides when a raw input request becomes a confirmed logical `Press` or `Release` edge.

The switch mode decides how a confirmed edge changes the puzzle output.

---

# SWITCH MODE

Use one explicit enum equivalent to:

```text
EPuzzleSwitchMode
```

Required modes:

```text
Hold
Toggle
Latch
Pulse
```

Do not create separate native classes for these behaviors.

Switch modes react only to confirmed logical input edges produced by the input transition state machine.

Raw `Press()` and `Release()` requests must not bypass pending delay state.

---

## `Hold`

Output follows the confirmed logical input state.

```text
Confirmed Press   -> Active
Confirmed Release -> Inactive
```

Primary use cases:

```text
pressure plate
hold button
pedal
momentary physical switch
```

Repeated `Press` while already pressed must not create duplicate state transitions.

Repeated `Release` while already released must not create duplicate state transitions.

---

## `Toggle`

Each confirmed `Press` edge inverts the output state.

```text
Inactive + Confirmed Press -> Active
Active   + Confirmed Press -> Inactive
```

`Release` does not change output state.

Primary use cases:

```text
lever
on/off button
wall switch
valve with two logical positions
```

A continuously held input must not repeatedly toggle.

One physical press cycle must produce at most one toggle until a confirmed `Release` edge has rearmed the input state.

This means the Actor must track input-transition state separately from active output state.

---

## `Latch`

The first confirmed `Press` edge activates the switch and normal input cannot deactivate it.

```text
Inactive + Confirmed Press -> Active
Active + Confirmed Press   -> no state change
Confirmed Release          -> no output state change
```

Primary use cases:

```text
button that stays pressed
one-way lever
permanent mechanism trigger
one-shot puzzle control
```

`ResetSwitch()` may explicitly restore the configured initial state.

Do not automatically reset Latch mode through ordinary `Release` input.

---

## `Pulse`

A confirmed `Press` edge activates the output temporarily.

```text
Confirmed Press
  ↓
Active
  ↓
PulseDuration
  ↓
Inactive
```

`Release` does not end the pulse early.

Primary use cases:

```text
timed button
bell-like trigger
temporary door control
short activation impulse
```

`PulseDuration` is only relevant in this mode.

---

# PULSE RETRIGGER POLICY

Pulse mode must explicitly define what happens when a new confirmed `Press` edge occurs while a pulse is already active.

Use a small enum equivalent to:

```text
EPuzzlePulseRetriggerMode
```

Required initial policies:

```text
Ignore
Restart
```

Do not add additional policies until a real gameplay need exists.

---

## `Ignore`

While the pulse is active:

```text
new Confirmed Press -> ignored
```

The existing pulse continues to its original end time.

---

## `Restart`

While the pulse is active:

```text
new Confirmed Press -> restart pulse timer from full PulseDuration
```

The output remains active; no duplicate Active transition is published.

Only the timer is restarted.

---

# ZERO-DURATION PULSE

A zero-duration pulse must still produce an observable Active state before returning to Inactive.

Do not publish:

```text
Active
Inactive
```

back-to-back in the same synchronous call stack.

Required semantics:

```text
PulseDuration <= 0
    ↓
activate now
    ↓
deactivate on the next deferred gameplay opportunity
```

Use the simplest lifecycle-safe Unreal mechanism already appropriate for the project and engine version.

The exact implementation must preserve one observable Active transition followed by one Inactive transition.

---

# STATE MODEL

`APuzzleSwitch` must represent input transitions with one explicit enum equivalent to:

```text
EPuzzleSwitchInputState
```

Required states:

```text
Released
PressPending
Pressed
ReleasePending
```

Do not represent these mutually exclusive states with independent booleans such as:

```text
bIsInputPressed
bIsPressed
bIsPressDelayPending
bIsReleaseDelayPending
```

The enum is the single authoritative owner of input-transition state.

The switch output remains a separate authoritative boolean:

```text
bool bIsActive
```

Input state and output state are orthogonal.

Examples:

```text
Toggle:
    InputState = Released
    bIsActive = true

Pulse after release but before timeout:
    InputState = Released
    bIsActive = true

Latch after release:
    InputState = Released
    bIsActive = true
```

Do not collapse input-transition state and puzzle output state into one value.

---

## `Released`

Semantics:

```text
raw input is released
logical input is released
no input delay is pending
```

`Press()` from this state either:

```text
PressDelay <= 0 -> Pressed
PressDelay > 0  -> PressPending
```

---

## `PressPending`

Semantics:

```text
raw input is pressed
logical input is still released
PressDelay is pending
```

If the delay completes while this is still the current state:

```text
PressPending
    ↓
Pressed
    ↓
process one Confirmed Press edge
```

If `Release()` arrives first:

```text
PressPending
    ↓
cancel PressDelay
    ↓
Released
```

No Confirmed Press or Confirmed Release edge is produced in that cancellation path.

---

## `Pressed`

Semantics:

```text
raw input is pressed
logical input is pressed
no input delay is pending
```

`Release()` from this state either:

```text
ReleaseDelay <= 0 -> Released
ReleaseDelay > 0  -> ReleasePending
```

---

## `ReleasePending`

Semantics:

```text
raw input is released
logical input is still pressed
ReleaseDelay is pending
```

If the delay completes while this is still the current state:

```text
ReleasePending
    ↓
Released
    ↓
process one Confirmed Release edge
```

If `Press()` arrives first:

```text
ReleasePending
    ↓
cancel ReleaseDelay
    ↓
Pressed
```

No new Confirmed Press edge is produced because the previous logical press was never released.

This is required for cases such as a pressure plate with:

```text
PressDelay = 0
ReleaseDelay = 5
```

If the occupant leaves, the switch enters `ReleasePending` and remains logically pressed.

If the occupant returns before the delay completes, the release is cancelled and the switch returns to `Pressed` without reactivating or producing another press edge.

---

# INPUT DELAYS

Input delays are part of the generic switch input state machine, not special behavior implemented separately by each switch mode.

Required configuration:

```text
PressDelay
ReleaseDelay
```

Their universal meanings are:

```text
PressDelay
= time the raw input must remain pressed before Confirmed Press is produced

ReleaseDelay
= time the raw input must remain released before Confirmed Release is produced
```

This keeps the same semantics across `Hold`, `Toggle`, `Latch`, and `Pulse`.

---

## `PressDelay`

When `Press()` is received from `Released`:

```text
PressDelay <= 0
    ↓
transition directly to Pressed
    ↓
process Confirmed Press
```

Otherwise:

```text
Released
    ↓
PressPending
    ↓
start PressDelay
```

If `Release()` arrives before completion:

```text
cancel PressDelay
PressPending -> Released
```

The input must remain continuously pressed for the entire delay.

---

## `ReleaseDelay`

When `Release()` is received from `Pressed`:

```text
ReleaseDelay <= 0
    ↓
transition directly to Released
    ↓
process Confirmed Release
```

Otherwise:

```text
Pressed
    ↓
ReleasePending
    ↓
start ReleaseDelay
```

If `Press()` arrives before completion:

```text
cancel ReleaseDelay
ReleasePending -> Pressed
```

The input must remain continuously released for the entire delay.

---

# DELAY SEMANTICS BY SWITCH MODE

The delay mechanism is shared by all modes.

The modes do not own separate delay implementations.

They receive only Confirmed Press and Confirmed Release edges.

## `Hold`

Both delays directly affect output timing.

```text
Confirmed Press   -> Active
Confirmed Release -> Inactive
```

Example:

```text
PressDelay = 0
ReleaseDelay = 5
```

A pressure plate activates immediately and deactivates only after remaining released for five seconds.

Returning during `ReleasePending` cancels the release delay and keeps the output active.

## `Toggle`

`PressDelay` delays the toggle edge.

`ReleaseDelay` delays logical rearming because a new Confirmed Press cannot occur until the previous Confirmed Release has completed.

`ReleaseDelay` does not directly deactivate the output.

## `Latch`

`PressDelay` may require sustained input before the latch activates.

`ReleaseDelay` still keeps generic input semantics but does not change the latched output.

The property may be hidden or marked advanced for this mode when it provides little designer value.

## `Pulse`

`PressDelay` delays pulse start.

`ReleaseDelay` delays logical rearming for the next accepted press cycle.

`ReleaseDelay` does not end an active pulse early.

Pulse expiration remains controlled only by `PulseDuration`.

---

# INPUT DELAY EVENTS

Expose Blueprint-observable events or delegates equivalent to:

```text
OnPressDelayStarted
OnPressDelayCancelled
OnPressDelayCompleted

OnReleaseDelayStarted
OnReleaseDelayCancelled
OnReleaseDelayCompleted
```

These events are for presentation and external observation.

They must not be required to keep the native input state machine functional.

Required event semantics:

```text
OnPressDelayStarted
-> emitted when entering PressPending and the delay timer begins

OnPressDelayCancelled
-> emitted when PressPending is cancelled before confirmation

OnPressDelayCompleted
-> emitted after PressPending successfully becomes Pressed and the Confirmed Press edge has been processed

OnReleaseDelayStarted
-> emitted when entering ReleasePending and the delay timer begins

OnReleaseDelayCancelled
-> emitted when ReleasePending is cancelled before confirmation

OnReleaseDelayCompleted
-> emitted after ReleasePending successfully becomes Released and the Confirmed Release edge has been processed
```

A delay of zero does not enter a pending state and must not emit Started, Cancelled, or Completed delay events.

---

# INPUT DELAY CONTROL API

Provide controlled operations equivalent to:

```text
RestartPressDelay()
RestartReleaseDelay()

CancelPendingPress()
CancelPendingRelease()

IsPressDelayPending()
IsReleaseDelayPending()

GetPressDelayRemaining()
GetReleaseDelayRemaining()
```

Required semantics:

```text
RestartPressDelay()
```

is valid only while:

```text
InputState == PressPending
```

It restarts the pending timer from the configured full `PressDelay` without leaving `PressPending` and without emitting another `OnPressDelayStarted` event.

```text
RestartReleaseDelay()
```

is valid only while:

```text
InputState == ReleasePending
```

It restarts the pending timer from the configured full `ReleaseDelay` without leaving `ReleasePending` and without emitting another `OnReleaseDelayStarted` event.

```text
CancelPendingPress()
```

performs the same state result as the opposite raw input arriving during `PressPending`:

```text
PressPending -> Released
```

It emits `OnPressDelayCancelled` and produces no confirmed logical edge.

```text
CancelPendingRelease()
```

performs the same state result as the opposite raw input arriving during `ReleasePending`:

```text
ReleasePending -> Pressed
```

It emits `OnReleaseDelayCancelled` and produces no confirmed logical edge.

Calling a restart or cancellation operation while its required pending state is not active must fail predictably and must not manufacture a new pending transition.

Do not expose mutable timer handles.

---

# CONFIGURATION

The native template should expose only configuration that is broadly useful to the switch concept.

Required configuration:

```text
SwitchMode
OutputSignalTag
bStartActive
PressDelay
ReleaseDelay
PulseDuration
PulseRetriggerMode
```

`PressDelay` and `ReleaseDelay` belong to the generic input state machine and are available across all switch modes.

Properties that are mode-specific should be hidden or disabled in the editor when they are irrelevant, where practical.

Examples:

```text
PulseDuration only relevant for Pulse
PulseRetriggerMode only relevant for Pulse
ReleaseDelay may be advanced or hidden for Latch when it provides no useful designer behavior
```

---

# OUTPUT SIGNAL

`APuzzleSwitch` publishes exactly one primary switch-state signal through its owned `UPuzzleEmitterComponent`.

The signal tag is designer-configurable:

```text
OutputSignalTag
```

The emitted active state must always match the switch's authoritative `bIsActive` state.

Conceptually:

```text
SetSwitchActive(NewState)
    ↓
update authoritative switch state
    ↓
publish OutputSignalTag through UPuzzleEmitterComponent
```

Do not let Blueprint children manually duplicate signal publication for normal switch-state changes.

The base Actor owns this invariant.

No payload is required for the initial implementation.

Do not create a switch payload type unless a concrete future condition needs additional switch-specific data.

---

# PUBLIC GAMEPLAY API

Required conceptual public operations:

```text
Press()
Release()
ResetSwitch()

GetInputState()
IsInputPressed()
IsPressed()
IsSwitchActive()

RestartPressDelay()
RestartReleaseDelay()
CancelPendingPress()
CancelPendingRelease()
IsPressDelayPending()
IsReleaseDelayPending()
GetPressDelayRemaining()
GetReleaseDelayRemaining()
```

Exact Unreal declarations should follow the established module API style.

The important semantics are fixed below.

---

## `Press()`

`Press()` is a raw input request.

It does not directly mean that a Confirmed Press edge has occurred.

State behavior:

```text
Released
    -> Pressed immediately when PressDelay <= 0
    -> otherwise PressPending

PressPending
    -> no change

Pressed
    -> no change

ReleasePending
    -> cancel ReleaseDelay
    -> return to Pressed
    -> no new Confirmed Press edge
```

Repeated `Press()` while the raw input is already represented as pressed must not create duplicate edges.

---

## `Release()`

`Release()` is a raw input request.

It does not directly mean that a Confirmed Release edge has occurred.

State behavior:

```text
Pressed
    -> Released immediately when ReleaseDelay <= 0
    -> otherwise ReleasePending

ReleasePending
    -> no change

Released
    -> no change

PressPending
    -> cancel PressDelay
    -> return to Released
    -> no Confirmed Release edge
```

Repeated `Release()` while the raw input is already represented as released must not create duplicate edges.

---

## Input queries

Required semantics:

```text
IsInputPressed()
```

returns true when the raw input is currently represented as pressed:

```text
PressPending
Pressed
```

```text
IsPressed()
```

returns true when the logical input is currently confirmed as pressed:

```text
Pressed
ReleasePending
```

```text
GetInputState()
```

returns the authoritative `EPuzzleSwitchInputState`.

These are queries only and must not expose mutable input state.

---

## `ResetSwitch()`

Reset is an explicit external operation, not ordinary user input.

It must:

```text
cancel PressDelay timer
cancel ReleaseDelay timer
cancel active Pulse timer or deferred pulse completion
invalidate stale switch-owned timer callbacks
restore InputState to Released
restore active output to bStartActive
publish only if the resulting active state actually changes
emit the normal reset hook
```

Reset must not synthesize Confirmed Press or Confirmed Release edges.

Reset must make the Actor usable again after Latch or Pulse state.

---

# INTERNAL STATE TRANSITION BOUNDARIES

The implementation must keep input confirmation and puzzle output changes separate.

## Confirmed input edge boundary

Use internal operations equivalent to:

```text
HandleConfirmedPress()
HandleConfirmedRelease()
```

Only transitions into `Pressed` from a truly released logical state may call `HandleConfirmedPress()`.

Only transitions into `Released` from a truly pressed logical state may call `HandleConfirmedRelease()`.

Pending-state cancellation must not call either operation.

Mode behavior is applied only inside these confirmed edge handlers.

Conceptually:

```text
HandleConfirmedPress()
    ↓
switch SwitchMode
    ↓
Hold   -> SetSwitchActive(true)
Toggle -> SetSwitchActive(!bIsActive)
Latch  -> SetSwitchActive(true)
Pulse  -> start/retrigger Pulse behavior
```

```text
HandleConfirmedRelease()
    ↓
switch SwitchMode
    ↓
Hold   -> SetSwitchActive(false)
Toggle -> no output change
Latch  -> no output change
Pulse  -> no output change
```

## Output state boundary

All output state changes must pass through one internal operation equivalent to:

```text
SetSwitchActive(bool bNewActive)
```

This operation is the single authority for:

```text
state-change deduplication
updating bIsActive
publishing the signal
calling activation/deactivation extension hooks
```

Do not publish directly from input-state branches or multiple mode branches.

Repeated requests for the current output state must do nothing.

---

# BLUEPRINT EXTENSION HOOKS

Blueprint children need presentation hooks without owning core switch logic.

Provide state-transition hooks equivalent to:

```text
OnPressed
OnReleased

OnPressDelayStarted
OnPressDelayCancelled
OnPressDelayCompleted

OnReleaseDelayStarted
OnReleaseDelayCancelled
OnReleaseDelayCompleted

OnSwitchActivated
OnSwitchDeactivated
OnSwitchReset
```

Use the smallest Unreal mechanism consistent with the existing module style.

These hooks are intended for:

```text
animations
mesh movement
sounds
VFX
interaction feedback
```

`OnPressed` and `OnReleased` correspond to confirmed logical input edges, not raw `Press()` and `Release()` calls.

Delay hooks follow the event semantics defined in `INPUT DELAY EVENTS`.

They must not be required to keep the switch state machine functional.

The native switch behavior must remain complete even when no Blueprint hook is implemented.

---

# EXPECTED BLUEPRINT CHILD PATTERNS

## `BP_PressurePlate`

Recommended configuration:

```text
SwitchMode = Hold
```

Example delayed release configuration:

```text
PressDelay = 0
ReleaseDelay = 5
```

This activates immediately on the first accepted occupant and starts a five-second pending release when the last accepted occupant leaves.

If an occupant returns before the release delay completes, the pending release is cancelled and the switch remains active without producing another confirmed press edge.

Concrete child responsibility:

```text
first valid occupant enters -> Press()
last valid occupant leaves  -> Release()
```

The base `APuzzleSwitch` must not implement occupant counting, weight detection, or overlap filtering.

Those may later justify separate reusable detection helpers or a dedicated pressure-plate template.

---

## `BP_Button`

Possible configurations:

### Button that stays active forever

```text
SwitchMode = Latch
```

### Button that toggles on/off

```text
SwitchMode = Toggle
```

### Timed button

```text
SwitchMode = Pulse
PulseDuration = X
```

### Button active only while held

```text
SwitchMode = Hold
```

Concrete child responsibility:

```text
interaction starts -> Press()
interaction ends, when relevant -> Release()
```

---

## `BP_Lever`

Typical configuration:

```text
SwitchMode = Toggle
```

Possible one-way lever:

```text
SwitchMode = Latch
```

The difference between a Lever and a Button is normally presentation and input delivery, not puzzle-state logic.

Do not create a separate native Lever class unless a later requirement introduces genuine reusable behavior that does not belong in `APuzzleSwitch`.

---

# TIMER AND REENTRY SEMANTICS

Switch-owned timers may only control switch-owned delayed transitions.

The mandatory timer responsibilities are:

```text
PressDelay confirmation
ReleaseDelay confirmation
Pulse expiration
zero-duration Pulse deferred completion
```

The timers must not bypass the authoritative state machines.

## Press delay completion

A Press delay callback may only complete a transition when:

```text
InputState == PressPending
```

Then:

```text
PressPending -> Pressed
process Confirmed Press
emit OnPressDelayCompleted
```

Otherwise the callback is stale and must not change state.

## Release delay completion

A Release delay callback may only complete a transition when:

```text
InputState == ReleasePending
```

Then:

```text
ReleasePending -> Released
process Confirmed Release
emit OnReleaseDelayCompleted
```

Otherwise the callback is stale and must not change state.

## Pulse completion

When a Pulse timer expires:

```text
if current mode/state still expects pulse completion
    -> SetSwitchActive(false)
```

`Release()` does not end the Pulse early.

Reset must invalidate all pending switch-owned delayed completions.

Changing mode at runtime is not a required gameplay feature for the initial implementation.

If runtime mode changes are exposed later, stale timers must not apply old-mode transitions.

---

# INITIALIZATION

At runtime startup, the switch must initialize its internal authoritative state before exposing the signal to dependent Controllers.

The final initialized state must be queryable from the owned `UPuzzleEmitterComponent` so Controllers starting later receive the correct current state.

Required initial semantics:

```text
InputState = Released
bIsActive = bStartActive
published OutputSignalTag state = bStartActive
no PressDelay timer pending
no ReleaseDelay timer pending
no Pulse timer pending
```

Do not depend on Blueprint child event ordering to establish the initial signal state.

---

# SPECIFIC VALIDATION

Validate at least:

```text
owned UPuzzleEmitterComponent exists
OutputSignalTag is valid
PressDelay is not negative
ReleaseDelay is not negative
PulseDuration is not negative, unless the implementation intentionally normalizes <= 0 to deferred zero-duration pulse behavior
```

Configuration should make obvious which properties are irrelevant for the current mode.

A broken switch configuration must not publish an unrelated fallback tag.

---

# DEBUG STATE FOR `APuzzleSwitch`

When puzzle debug is enabled through the module's established debug system, the switch should make these values inspectable:

```text
Actor name
SwitchMode
InputState
IsInputPressed result
IsPressed result
bIsActive
OutputSignalTag
PressDelay
Press delay pending / time remaining when relevant
ReleaseDelay
Release delay pending / time remaining when relevant
Pulse active/inactive
Pulse time remaining when relevant
PulseRetriggerMode when relevant
```

The goal is to answer quickly:

```text
Did this switch receive raw Press or Release input?
Which input-transition state is it in?
Is Press or Release waiting on a delay?
Why is it still logically pressed after raw release?
Why is its output still active?
Is a Pulse timer running?
Which signal is it publishing?
```

---

# REQUIRED BEHAVIOR SCENARIOS

Validate the following template-specific cases.

## 1. Hold basic cycle with zero delays

```text
Press   -> Confirmed Press -> Active
Release -> Confirmed Release -> Inactive
```

## 2. Hold duplicate raw input

```text
Press
Press again
```

Only one Confirmed Press and one activation transition are produced.

## 3. Press delay completion

With:

```text
PressDelay > 0
```

```text
Press
-> PressPending
-> delay completes
-> Pressed
-> one Confirmed Press
```

## 4. Press delay cancellation

```text
Press
-> PressPending
Release before completion
-> OnPressDelayCancelled
-> Released
```

No Confirmed Press or Confirmed Release is produced.

## 5. Press delay restart

While `PressPending`, `RestartPressDelay()` restarts the remaining delay from the configured full duration without leaving the state or emitting another Started event.

## 6. Hold delayed release

With:

```text
SwitchMode = Hold
PressDelay = 0
ReleaseDelay = 5
```

```text
Press
-> Active
Release
-> ReleasePending
-> remain Active
wait 5 seconds
-> Released
-> Inactive
```

## 7. Release delay cancellation by repress

During `ReleasePending`:

```text
Press
-> OnReleaseDelayCancelled
-> Pressed
```

The output remains active and no new Confirmed Press is produced.

## 8. Release delay restart

While `ReleasePending`, `RestartReleaseDelay()` restarts the remaining delay from the configured full duration without leaving the state or emitting another Started event.

## 9. Explicit pending transition cancellation

```text
CancelPendingPress()
```

must move `PressPending -> Released` with one cancellation event and no confirmed edge.

```text
CancelPendingRelease()
```

must move `ReleasePending -> Pressed` with one cancellation event and no confirmed edge.

## 10. Toggle full cycle

With zero delays:

```text
Press   -> Active
Release -> still Active
Press   -> Inactive
```

## 11. Toggle held input

Repeated `Press()` calls without a Confirmed Release must not toggle repeatedly.

## 12. Toggle delayed rearm

With `ReleaseDelay > 0`, a new press during `ReleasePending` cancels the pending release and does not toggle again.

The switch can toggle again only after a Confirmed Release has occurred and a later new Confirmed Press is produced.

## 13. Latch

```text
Confirmed Press   -> Active
Confirmed Release -> still Active
later Confirmed Press -> still Active
```

## 14. Latch with press delay

The latch must not activate until `PressDelay` completes successfully.

Cancelling `PressPending` must leave it inactive.

## 15. Latch reset

`ResetSwitch()` restores `bStartActive`, restores `InputState = Released`, cancels all pending timers, and allows future use.

## 16. Pulse normal completion

```text
Confirmed Press -> Active
wait PulseDuration
-> Inactive
```

## 17. Pulse release

A Confirmed Release before pulse timeout must not end the pulse.

## 18. Pulse press delay

Pulse activation starts only after PressDelay successfully produces a Confirmed Press.

## 19. Pulse retrigger ignore

A second accepted press cycle during an active pulse must not change the original pulse end time.

## 20. Pulse retrigger restart

A second accepted press cycle during an active pulse restarts the pulse duration without republishing an Active transition.

## 21. Zero-duration pulse

Must produce one observable Active transition and one later Inactive transition, not two synchronous back-to-back publications.

## 22. Start active

With:

```text
bStartActive = true
```

late-starting Controllers must observe the signal as active while `InputState` still initializes as `Released`.

## 23. Reset during input delay

Reset during `PressPending` or `ReleasePending` cancels the timer, restores `InputState = Released`, and prevents stale callbacks from changing state later.

## 24. Reset during pulse

Reset cancels pending pulse completion and restores the configured initial output state without a stale timer changing it later.

## 25. Blueprint child without overrides

The native input state machine, delay logic, mode policy, and signal publication must function even when no Blueprint presentation hooks are implemented.

---

# FORBIDDEN SHORTCUTS FOR `APuzzleSwitch`

Do not:

```text
create separate native classes for Hold, Toggle, Latch, and Pulse
represent the four input-transition states with several independent authoritative booleans
implement separate PressDelay / ReleaseDelay state machines inside each switch mode
let raw Press() or Release() bypass pending input-delay state
produce Confirmed Press when cancelling ReleasePending
produce Confirmed Release when cancelling PressPending
let stale timer callbacks change current state
put Controller condition logic inside the switch
activate Receivers directly
add mandatory interaction logic to the base class
add mandatory overlap logic to the base class
add weight detection to the base class
let Blueprint children manually maintain the authoritative InputState or bIsActive state
publish the same output state repeatedly
use Tick for input delays or Pulse timing
make Release cancel Pulse by default
make Toggle react repeatedly to a held or not-yet-rearmed Press
expose mutable timer handles
```

---

# FUTURE EMITTER TEMPLATE ENTRY FORMAT

When adding another reusable Emitter Actor template to this file, define only template-specific architecture using this structure:

```text
Template name
Purpose
Required composition
Input model
Authoritative state
Configuration
Public gameplay API
Signal output
Blueprint extension hooks
Lifecycle-specific behavior
Template-specific validation
Debug state
Required behavior scenarios
Forbidden shortcuts
```

Do not repeat the global puzzle architecture or root project rules inside every template entry.

---

# IMPLEMENTATION SCOPE FOR THE FIRST TASK

When Codex is explicitly asked to implement the first template from this file, the task scope is:

```text
APuzzleSwitch
EPuzzleSwitchMode
EPuzzleSwitchInputState
EPuzzlePulseRetriggerMode
native input transition state machine
PressDelay / ReleaseDelay timers
input delay events and controlled timer API
native switch mode policy
owned UPuzzleEmitterComponent integration
Press / Release / ResetSwitch API
Pulse timer behavior
Blueprint presentation hooks
switch-specific validation and debug visibility
relevant user documentation
```

Do not implement yet:

```text
BP_PressurePlate
BP_Button
BP_Lever
weight detection
interaction framework integration
occupant filtering
additional payload types
additional Emitter templates
```

Those are separate tasks.
