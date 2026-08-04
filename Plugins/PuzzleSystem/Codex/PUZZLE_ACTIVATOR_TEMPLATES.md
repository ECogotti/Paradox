# Puzzle Activator Actor Templates — Codex Specification

This file defines reusable gameplay Actor templates that react to the existing environmental puzzle system.

It supplements:

- the root `AGENTS.md`;
- `PUZZLE_SYSTEM_ARCHITECTURE.md`;
- the existing `PuzzleSystem` user documentation.

Do not repeat or reimplement rules already defined there.

The word **Activator** is used here as a designer-facing category for world objects that perform a gameplay action when a puzzle Receiver changes state.

Architecturally, these Actors remain ordinary gameplay Actors that own and react to a `UPuzzleReceiverComponent`:

```text
UPuzzleEmitterComponent
    -> APuzzleController
    -> UPuzzleReceiverComponent
    -> Activator Actor gameplay behavior
```

Do not introduce a fourth mandatory PuzzleSystem role.

---

# ACTIVATOR TEMPLATE DESIGN PRINCIPLES

## 1. Activator templates are Receiver-driven gameplay Actors

An Activator template owns a `UPuzzleReceiverComponent` and reacts to its effective activation and deactivation transitions.

It must not:

- subscribe directly to Emitters;
- inspect Controller conditions;
- activate or deactivate other Receivers directly;
- bypass `APuzzleController` for ordinary puzzle wiring;
- contain project-specific gameplay rules.

The Receiver remains the authoritative owner of requested/effective puzzle activation state.

The Activator Actor owns only the concrete reusable gameplay reaction.

---

## 2. Templates must remain generic and reusable

Activator templates belong to the existing `PuzzleSystem` runtime plugin/module.

They must not depend on:

- Paradox project modules;
- Temporal Index logic;
- WorldState implementations specific to Paradox;
- PerceptionKnowledge;
- GridWorld;
- IntentReplay;
- project-specific audio, Niagara, navigation, or reset systems.

External projects may integrate those systems through subclassing, delegates, components, or adapters outside the generic PuzzleSystem plugin.

---

## 3. The native base must provide complete behavior

A Blueprint child should be able to add:

- meshes;
- collision;
- audio;
- VFX;
- custom presentation;
- optional integration components;

without rebuilding the movement state machine.

The native implementation must remain functional even when no Blueprint hook is implemented.

---

## 4. Templates may use Tick only while actively required

PuzzleSystem evaluation remains event-driven.

A movement template may use Actor Tick while interpolation is actively progressing, because continuous spatial interpolation is the gameplay effect itself.

Tick must be disabled when:

- the Actor is at an endpoint;
- movement is paused by the `Stop` deactivation behavior;
- no valid moved component exists;
- the Actor is being torn down.

Do not leave Tick permanently enabled.

---

# TEMPLATE 01 — `APuzzleTransformMover`

## Purpose

`APuzzleTransformMover` is an abstract, Blueprintable gameplay Actor base that moves one selected `USceneComponent` between two authored transforms in response to its owned `UPuzzleReceiverComponent`.

It is intended as the native base for reusable puzzle objects such as:

```text
BP_PuzzleDoor
BP_MovingPlatform
BP_RotatingBridge
BP_RaisingWall
BP_Piston
BP_ElevatorSegment
BP_SlidingBarrier
BP_TrapMechanism
```

The class owns:

- Receiver integration;
- endpoint authoring markers;
- movement state;
- activation mode policy;
- deactivation-during-movement policy;
- timing;
- easing;
- interpolation;
- reversal;
- pause and resume behavior;
- runtime replacement of the moved component;
- Blueprint presentation hooks;
- validation and debug state.

The class does not create the concrete component that it moves.

A native or Blueprint subclass supplies one or more scene components and chooses which component is controlled.

---

# REQUIRED CLASS DECLARATION

Create an abstract Actor equivalent to:

```text
APuzzleTransformMover
```

Use the correct Unreal declarations for the engine version and existing module conventions.

The class must be suitable as a Blueprint parent and must not be directly placeable as a finished gameplay object unless the existing project convention intentionally allows abstract native Actor bases to appear in the class picker.

---

# REQUIRED COMPOSITION

Required conceptual composition:

```text
APuzzleTransformMover
├── UBillboardComponent       BillboardRoot
├── UArrowComponent           StartArrow
├── UArrowComponent           EndArrow
└── UPuzzleReceiverComponent  PuzzleReceiver

Runtime/configured reference:
└── USceneComponent*          MovedComponent
```

## `BillboardRoot`

Use a `UBillboardComponent` as the root component so the abstract template is easy to identify and select in editor viewports.

The billboard is an editor aid and must be hidden in game.

Reuse an existing appropriate PuzzleSystem icon when available. Do not create an unrelated project-specific asset dependency merely for this template.

## `StartArrow`

`StartArrow` defines the canonical `Start` transform.

Required editor meaning:

```text
MovementAlpha = 0
State = AtStart
MovedComponent transform = StartArrow transform
```

Use a clearly recognizable default color, preferably green.

## `EndArrow`

`EndArrow` defines the canonical `End` transform.

Required editor meaning:

```text
MovementAlpha = 1
State = AtEnd
MovedComponent transform = EndArrow transform
```

Use a clearly different default color, preferably red.

## Arrow component rules

Both arrows must:

- be attached to `BillboardRoot`;
- be visible as editor authoring helpers;
- be hidden in game;
- have collision disabled;
- not affect navigation;
- never be selected as the moved component;
- remain independently editable in the Blueprint and level viewport;
- use visibly different colors.

Do not replace the arrows with plain `USceneComponent` markers.

## `PuzzleReceiver`

The Actor owns one `UPuzzleReceiverComponent`.

Its effective state transitions drive the mover according to the configured movement mode.

The mover must react only to effective Receiver transitions, not to individual Controller request changes.

## `MovedComponent`

The native Actor must not create a mandatory mesh or other concrete movable visual component.

The moved object is a selected `USceneComponent` supplied by a subclass or assigned at runtime.

The architecture should support:

```text
StaticMeshComponent
SkeletalMeshComponent
SceneComponent hierarchy root
custom USceneComponent subclasses
```

provided the selected component supports ordinary transform updates and satisfies validation requirements.

---

# MOVEMENT SPACE AND ENDPOINT MODEL

The two arrow transforms are authoritative endpoints expressed relative to `BillboardRoot`.

At runtime, derive the corresponding world transforms from the root and arrow transforms, then apply the interpolated world transform to `MovedComponent`.

This allows the entire mover Actor to be repositioned in the level without rewriting Start and End coordinates.

The canonical movement path is always represented by one normalized value:

```text
MovementAlpha = 0.0 -> Start
MovementAlpha = 1.0 -> End
```

`MovementAlpha` is the authoritative spatial progress value.

The actual component transform is derived from:

```text
StartArrow transform
EndArrow transform
MovementAlpha
configured easing
```

Do not repeatedly use the moved component's current transform as a new interpolation origin. That would introduce drift and make reversal nondeterministic.

The implementation must support interpolation of:

- translation;
- rotation;
- scale.

Use a rotation interpolation method appropriate for transforms and the shortest stable rotation path. Do not rely on naive component-wise Euler interpolation when it can produce discontinuities.

---

# MOVEMENT STATE

Use one explicit enum equivalent to:

```text
EPuzzleTransformMoverState
```

Required states:

```text
AtStart
MovingTowardEnd
AtEnd
MovingTowardStart
```

These states are the authoritative movement direction/end-position state.

Do not use one generic `Moving` state plus a separate target enum.

## `AtStart`

Required semantics:

```text
MovementAlpha = 0
movement Tick disabled
component is at Start transform
```

## `MovingTowardEnd`

Required semantics:

```text
target endpoint = End
MovementAlpha may increase while not paused
```

## `AtEnd`

Required semantics:

```text
MovementAlpha = 1
movement Tick disabled
component is at End transform
```

## `MovingTowardStart`

Required semantics:

```text
target endpoint = Start
MovementAlpha may decrease while not paused
```

---

# PAUSED MOVEMENT STATE

The `Stop` deactivation behavior requires the mover to preserve both its directional state and current progress while spatial movement is paused.

Use a separate authoritative flag equivalent to:

```text
bool bIsMovementPaused
```

When paused:

```text
State remains MovingTowardEnd or MovingTowardStart
MovementAlpha remains unchanged
movement Tick is disabled
```

This is intentional.

The directional enum preserves which endpoint must be resumed later, while `bIsMovementPaused` distinguishes active interpolation from a stopped in-progress traversal.

Do not introduce an ambiguous `Stopped` enum value that loses the pending direction.

---

# MOVEMENT MODE

Use one enum equivalent to:

```text
EPuzzleTransformMoverMode
```

Required modes:

```text
Latch
FlipFlop
PingPong
```

Movement mode determines how Receiver activation/deactivation edges request endpoint movement.

It does not determine interpolation speed or easing.

---

## `Latch`

The first successful activation cycle moves the object from Start toward End.

Once End is actually reached, the latch is completed and later Receiver activations have no effect.

Required semantics:

```text
not completed + Receiver Activated
    -> request movement toward End

AtEnd reached
    -> mark latch completed

latch completed + later Receiver Activated
    -> no movement
```

The latch must be considered consumed only when the mover reaches `AtEnd`.

Do not consume the latch merely because activation was requested.

This distinction is required because the movement may be stopped or returned before reaching End.

If movement returns to Start before completion, a later activation may attempt the Latch movement again.

Receiver deactivation after the latch has reached End has no effect.

An explicit `ResetMover()` operation clears the completed latch state.

---

## `FlipFlop`

Each accepted Receiver activation requests movement toward the endpoint opposite the mover's current directional/end state.

Required target selection:

```text
AtStart             + Activated -> End
AtEnd               + Activated -> Start
MovingTowardEnd     + Activated -> Start
MovingTowardStart   + Activated -> End
```

Exception for paused movement created by the `Stop` deactivation behavior:

```text
bIsMovementPaused + Activated
    -> resume the preserved direction
    -> do not flip target
```

This exception is mandatory because `Stop` is defined as pause-and-resume behavior.

FlipFlop reacts to Receiver activation edges only.

Receiver deactivation does not request the opposite endpoint by itself; it only applies the configured in-progress deactivation behavior when the mover is currently moving.

---

## `PingPong`

Receiver active state conceptually commands End, and Receiver inactive state conceptually commands Start, subject to the configured behavior when deactivation occurs during movement.

Required activation behavior:

```text
Receiver Activated
    -> request End
```

Required ordinary deactivation behavior at stable endpoints:

```text
AtEnd + Receiver Deactivated
    -> request Start

AtStart + Receiver Deactivated
    -> no movement
```

When deactivation occurs while moving, apply `EPuzzleTransformMoverDeactivationBehavior` exactly as defined below.

If the mover is already `MovingTowardStart`, another deactivation notification must not restart or duplicate the same movement.

---

# RECEIVER DEACTIVATION DURING MOVEMENT

Use one enum equivalent to:

```text
EPuzzleTransformMoverDeactivationBehavior
```

Required values:

```text
Stop
Return
Continue
```

This policy applies only when the Receiver becomes inactive while the mover is currently in one of these states:

```text
MovingTowardEnd
MovingTowardStart
```

It does not replace the normal endpoint behavior of the selected movement mode.

---

## `Stop`

The mover stops at its current interpolated transform and preserves its current directional state.

Required result:

```text
MovementAlpha unchanged
State unchanged
bIsMovementPaused = true
movement Tick disabled
```

When the Receiver is activated again:

```text
bIsMovementPaused = false
resume movement in the preserved direction
```

Reactivation after `Stop` must resume movement.

It must not:

- restart from an endpoint;
- select a new FlipFlop target;
- lose progress;
- replay a fresh movement-start transition as though no traversal existed.

Expose a distinct resumed hook/event so presentation can distinguish a new movement from a resumed movement when useful.

---

## `Return`

The mover immediately reverses toward the logical origin endpoint of its current direction.

Because the state enum already contains direction, the origin is unambiguous:

```text
MovingTowardEnd   + Deactivated -> request Start
MovingTowardStart + Deactivated -> request End
```

Required behavior:

```text
preserve current MovementAlpha
change directional state
continue interpolation without positional snap
```

The return target is the opposite endpoint, not the exact world-space transform at which the current traversal originally started.

This keeps behavior deterministic even after previous reversals.

A later Receiver activation is processed normally by the selected movement mode.

Examples:

```text
Latch:
Start -> moving toward End -> Deactivated/Return -> move toward Start
later Activated before latch completion -> move toward End again

FlipFlop:
Start -> moving toward End -> Deactivated/Return -> move toward Start
later Activated while returning -> FlipFlop requests End

PingPong:
Start -> moving toward End -> Deactivated/Return -> move toward Start
later Activated while returning -> request End
```

---

## `Continue`

The deactivation is ignored for the currently active traversal.

Required behavior:

```text
State unchanged
MovementAlpha continues toward the existing target
movement Tick remains active
```

The mover completes the current traversal and stops at that endpoint.

Do not queue the ignored deactivation to execute automatically after the endpoint is reached.

For example, in PingPong mode:

```text
moving toward End
Receiver becomes inactive
DeactivationBehavior = Continue
    -> finish at End
    -> remain at End
```

A later Receiver activation/deactivation cycle may issue new movement requests normally.

This rule prevents `Continue` from degenerating into "finish and immediately reverse."

---

# ACTIVATION AND DEACTIVATION PROCESSING ORDER

Receiver state handlers must use the following conceptual order.

## On Receiver Activated

```text
if movement is paused by Stop:
    resume preserved direction
    return

switch MovementMode:
    Latch   -> request End if latch not completed
    FlipFlop -> request endpoint opposite current directional/end state
    PingPong -> request End
```

## On Receiver Deactivated

```text
if State is MovingTowardStart or MovingTowardEnd:
    apply DeactivationBehavior
    return

switch MovementMode:
    Latch   -> no effect
    FlipFlop -> no effect
    PingPong -> if AtEnd, request Start; if AtStart, no effect
```

Repeated identical requests must be deduplicated.

Do not restart timers, reset easing progress, or rebroadcast transitions when the mover is already travelling toward the requested target.

---

# MOVEMENT TIMING MODE

Use one enum equivalent to:

```text
EPuzzleTransformMoverTimingMode
```

Required modes:

```text
Speed
MovementTime
```

The designer selects exactly one authoritative timing model.

Do not keep both active simultaneously.

Use editor conditions to show or enable only the properties relevant to the selected timing mode where practical.

---

## `Speed`

The designer configures a direct movement speed.

Required conceptual properties:

```text
ForwardSpeed
ReturnSpeed
bUseSeparateReturnTiming
```

When `bUseSeparateReturnTiming` is false, both directions use `ForwardSpeed`.

When true:

```text
MovingTowardEnd   -> ForwardSpeed
MovingTowardStart -> ReturnSpeed
```

Interpret speed as translation distance per second using Unreal world units.

Conceptually:

```text
FullTranslationDistance = distance(StartLocation, EndLocation)
AlphaPerSecond = Speed / FullTranslationDistance
```

The linear `MovementAlpha` changes by this normalized amount over time.

Rotation and scale follow the same normalized alpha.

### Speed-mode limitation

A transform path with zero translation distance but different rotation or scale has no meaningful translation speed.

For the initial implementation:

```text
zero translation distance + non-identical transforms + TimingMode = Speed
    -> invalid configuration
```

The designer must use `MovementTime` for pure rotation or pure scale movement.

Do not silently invent an angular or scale speed conversion.

---

## `MovementTime`

The designer specifies how long a complete endpoint-to-endpoint traversal takes.

Required conceptual properties:

```text
ForwardMovementTime
ReturnMovementTime
bUseSeparateReturnTiming
```

When `bUseSeparateReturnTiming` is false, both directions use `ForwardMovementTime`.

When true:

```text
MovingTowardEnd   -> ForwardMovementTime
MovingTowardStart -> ReturnMovementTime
```

The configured value represents a full traversal from one endpoint to the other.

Movement from a partial alpha uses the proportional remaining time automatically.

Example:

```text
MovementAlpha = 0.25
moving toward End
ForwardMovementTime = 4 seconds
remaining time = 3 seconds
```

After reversal:

```text
MovementAlpha = 0.25
moving toward Start
ReturnMovementTime = 2 seconds
remaining time = 0.5 seconds
```

Do not restart a full duration from the current intermediate transform.

---

# LINEAR PROGRESS AND EASED TRANSFORM ALPHA

Keep two concepts separate:

```text
MovementAlpha
EasedAlpha
```

## `MovementAlpha`

- authoritative normalized time/spatial progress;
- always clamped to `[0, 1]`;
- advances linearly according to speed or movement time;
- determines remaining traversal time;
- survives pause and reversal;
- determines endpoint state transitions.

## `EasedAlpha`

- derived from `MovementAlpha`;
- used only to calculate the visible interpolated transform;
- may use built-in Unreal easing or a custom curve;
- must not become the authoritative progress value.

This separation is mandatory so reversal and remaining-time calculations remain deterministic under non-linear easing.

---

# INTERPOLATION SOURCE

Use one enum equivalent to:

```text
EPuzzleTransformMoverInterpolationSource
```

Required values:

```text
BuiltInEasing
CustomCurve
```

---

## Built-in Unreal easing

Expose Unreal's established easing modes through the actual engine API available in the project version.

Codex must inspect the installed engine headers and use the real supported enum/function rather than inventing a parallel easing library.

Expected categories include the built-in equivalents of:

```text
Linear
Step
Sinusoidal In
Sinusoidal Out
Sinusoidal In Out
Ease In
Ease Out
Ease In Out
Exponential In
Exponential Out
Exponential In Out
Circular In
Circular Out
Circular In Out
```

Only expose parameters that are genuinely required by the chosen Unreal easing function, such as blend exponent or step count.

Do not recreate built-in easing algorithms manually when Unreal already provides the verified implementation.

---

## Custom curve

Allow an optional `UCurveFloat` equivalent to:

```text
MovementCurve
```

The expected normalized curve domain is:

```text
X = MovementAlpha in [0, 1]
Y = transformed interpolation alpha
```

For the initial implementation, clamp the evaluated output to `[0, 1]`.

Do not allow curve overshoot to move beyond Start or End until a concrete requirement explicitly introduces overshoot behavior.

A missing curve while `CustomCurve` is selected is invalid configuration and must fail predictably.

---

# COMPONENT SELECTION

The class must support both designer configuration and runtime replacement.

## Designer-configured default component

Use an Unreal-supported component reference mechanism appropriate for selecting a component supplied by the Actor or a Blueprint child.

Prefer a configured component reference rather than a hard-coded component name search.

Resolve the configured default component during controlled initialization.

Do not perform repeated world searches.

## Runtime component API

Provide controlled public operations equivalent to:

```text
SetMovedComponent(USceneComponent* NewComponent) -> bool
GetMovedComponent() -> USceneComponent*
HasValidMovedComponent() -> bool
RestoreDefaultMovedComponent() -> bool
```

Do not expose the cached pointer as freely mutable Blueprint state.

## Validation rules

A valid moved component must:

- be non-null;
- be registered when runtime movement begins;
- use movable mobility;
- not be `BillboardRoot`;
- not be `StartArrow`;
- not be `EndArrow`;
- not be the Receiver component;
- not simulate physics under the base implementation;
- normally belong to the same Actor.

The initial implementation should reject components owned by another Actor.

Cross-Actor component control introduces different ownership, lifetime, attachment, and serialization concerns and is outside this template's first scope.

## Runtime replacement semantics

When `SetMovedComponent()` succeeds:

```text
old component is no longer controlled
new component becomes authoritative moved component
new component is immediately synchronized to current EasedAlpha transform
current state, direction, pause state, and MovementAlpha are preserved
```

Replacing the component must not restart movement.

If replacement occurs while moving:

```text
new component snaps to the current path position
movement continues toward the existing target
```

If replacement occurs while paused:

```text
new component snaps to the paused path position
movement remains paused
```

The old component remains at its current transform; the mover does not restore it automatically.

## Invalid or destroyed component

If the moved component becomes invalid during movement:

- disable movement Tick;
- preserve state, direction, pause state, and `MovementAlpha`;
- emit a useful warning once per invalidation event;
- do not falsely report an endpoint reached;
- allow a later valid `SetMovedComponent()` call to synchronize and continue.

---

# REQUESTING ENDPOINT MOVEMENT

All target changes must pass through controlled internal operations equivalent to:

```text
RequestMoveTowardStart()
RequestMoveTowardEnd()
```

These operations are the single authority for:

- request deduplication;
- direction changes;
- reversal detection;
- pause clearing where appropriate;
- Tick activation;
- movement-start or movement-reversed hooks;
- endpoint short-circuiting.

Do not change `State` or movement direction independently in Receiver callbacks.

## Request End

Conceptual behavior:

```text
AtEnd
    -> no movement

MovingTowardEnd and not paused
    -> no change

MovingTowardEnd and paused
    -> resume

AtStart
    -> State = MovingTowardEnd

MovingTowardStart
    -> preserve MovementAlpha
    -> State = MovingTowardEnd
    -> reversal event
```

## Request Start

Conceptual behavior:

```text
AtStart
    -> no movement

MovingTowardStart and not paused
    -> no change

MovingTowardStart and paused
    -> resume

AtEnd
    -> State = MovingTowardStart

MovingTowardEnd
    -> preserve MovementAlpha
    -> State = MovingTowardStart
    -> reversal event
```

---

# MOVEMENT UPDATE

While actively moving and not paused:

1. validate the moved component;
2. calculate delta progress from the selected timing mode and direction;
3. update and clamp `MovementAlpha`;
4. calculate `EasedAlpha`;
5. interpolate Start and End transforms;
6. apply the transform to the moved component;
7. detect exact endpoint arrival;
8. transition to `AtStart` or `AtEnd`;
9. disable Tick;
10. emit endpoint hooks/events exactly once.

Do not determine endpoint arrival only through approximate transform comparison.

Use canonical alpha boundaries:

```text
MovementAlpha <= 0 -> AtStart
MovementAlpha >= 1 -> AtEnd
```

Snap to the exact endpoint transform when a boundary is reached.

---

# COLLISION AND SWEEP

The base class should support a small, explicit collision movement option without becoming a full obstacle-resolution system.

Use an option equivalent to:

```text
bSweepMovement
```

Default it to false unless the existing module has a stronger convention.

## Sweep disabled

The component follows the authored transform deterministically and reaches the requested endpoint regardless of blocking collision.

## Sweep enabled

Use the verified Unreal component movement API that supports sweeping for the selected component and transform update.

Document actual engine limitations, especially for:

- non-root components;
- rotation;
- scale;
- complex collision.

If sweep blocks translation:

- do not report endpoint completion;
- do not advance authoritative progress beyond the actually accepted movement without reconciliation;
- expose a movement-blocked hook/event;
- avoid repeating the same blocked notification every Tick;
- allow movement to continue when the obstruction clears.

Do not add pathfinding, pushing, crushing, damage, or obstacle avoidance to the generic template.

If robust transform reconciliation cannot be implemented safely for the engine API in use, keep sweep out of the first implementation rather than shipping a misleading partial behavior. Document the limitation.

---

# INITIAL POSITION

Use one enum equivalent to:

```text
EPuzzleTransformMoverInitialPosition
```

Required values:

```text
Start
End
```

At initialization:

```text
InitialPosition = Start
    -> MovementAlpha = 0
    -> State = AtStart

InitialPosition = End
    -> MovementAlpha = 1
    -> State = AtEnd
```

The moved component must be synchronized to the exact selected endpoint before normal gameplay transitions are processed.

For Latch mode:

```text
InitialPosition = End
    -> latch begins completed
```

An explicit reset returns the mover to its configured initial position and resets Latch completion consistently.

---

# INITIAL RECEIVER SYNCHRONIZATION

The mover must not depend on Actor `BeginPlay` order.

During controlled initialization:

1. create/validate owned components;
2. resolve the default moved component;
3. initialize to `InitialPosition`;
4. bind to Receiver effective-state notifications;
5. query the Receiver's current effective state;
6. process the current state according to the selected mode.

Expose a configuration equivalent to:

```text
bAnimateInitialReceiverState
```

Default to false.

## Initial synchronization without animation

When false, apply the logical result of the current Receiver state by snapping directly to the resulting endpoint without movement feedback.

Conceptual behavior:

```text
Latch + Receiver active
    -> snap End
    -> mark latch completed

FlipFlop + Receiver active
    -> treat as one activation from InitialPosition
    -> snap to opposite endpoint

PingPong + Receiver active
    -> snap End

PingPong + Receiver inactive
    -> snap Start
```

Do not emit movement-start, resumed, reversed, or endpoint-arrival presentation events for initialization snaps.

## Initial synchronization with animation

When true, process the current Receiver state as the corresponding runtime transition from `InitialPosition`.

Avoid duplicate processing if a Receiver notification arrives during initialization.

---

# RESET API

Provide an explicit operation equivalent to:

```text
ResetMover()
```

This is a generic local reset API only.

It must not depend on any project-specific reset or persistence system.

Required semantics:

```text
stop active interpolation
set bIsMovementPaused = false
disable movement Tick
clear blocked-notification state
restore MovementAlpha and State from InitialPosition
clear or restore Latch completion consistently
synchronize MovedComponent to the exact initial endpoint
emit one reset hook/event
```

Reset must not:

- mutate Controller requests;
- force the Receiver active or inactive;
- synthesize Receiver activation/deactivation events;
- automatically replay the current Receiver state after reset.

After reset, later Receiver transitions operate normally.

Provide a separate controlled operation equivalent to:

```text
SynchronizeWithCurrentReceiverState(bool bAnimate)
```

for callers that explicitly need to reapply the current Receiver state after a reset.

Do not combine these two behaviors implicitly.

---

# BLUEPRINT EXTENSION HOOKS

Provide Blueprint-observable hooks or delegates equivalent to:

```text
OnMovementStarted
OnMovementResumed
OnMovementReversed
OnMovementPaused
OnMovementBlocked
OnReachedStart
OnReachedEnd
OnMovedComponentChanged
OnMoverReset
```

Use the smallest Unreal mechanism consistent with existing PuzzleSystem conventions.

## Required event semantics

### `OnMovementStarted`

Emitted when movement begins from a stable endpoint toward the opposite endpoint.

### `OnMovementResumed`

Emitted when Receiver activation resumes movement previously paused by `Stop`.

Do not also report the same resume as a fresh start.

### `OnMovementReversed`

Emitted when an in-progress traversal changes from:

```text
MovingTowardStart <-> MovingTowardEnd
```

without first reaching an endpoint.

### `OnMovementPaused`

Emitted once when `Stop` pauses active movement.

### `OnMovementBlocked`

Emitted only when optional swept movement transitions from unblocked to blocked.

Do not spam it every Tick while the same obstruction remains.

### `OnReachedStart`

Emitted once when runtime movement reaches Start.

### `OnReachedEnd`

Emitted once when runtime movement reaches End.

Latch completion is updated before observers process the reached-End notification.

### `OnMovedComponentChanged`

Emitted after a successful runtime replacement and synchronization.

### `OnMoverReset`

Emitted after local state and transform have been restored.

## Per-frame Blueprint events

Do not expose a Blueprint-assignable event that broadcasts every movement Tick by default.

If subclasses need per-frame presentation logic, provide a protected overridable hook with clear performance documentation rather than a broad public multicast delegate.

---

# PUBLIC QUERY API

Provide safe queries equivalent to:

```text
GetMoverState()
GetMovementMode()
GetDeactivationBehavior()
GetMovementAlpha()
GetEasedAlpha()
IsMoving()
IsMovementPaused()
IsAtStart()
IsAtEnd()
IsLatchCompleted()
GetMovedComponent()
GetStartTransform()
GetEndTransform()
GetCurrentTargetTransform()
GetRemainingMovementTime()
```

Queries must not mutate state.

`GetRemainingMovementTime()` must use current direction, timing mode, progress, and separate return timing when configured.

For invalid Speed-mode configuration, return failure through the established API style rather than fabricating a result.

---

# CONTROLLED PUBLIC OPERATIONS

Required public operations:

```text
SetMovedComponent(NewComponent)
RestoreDefaultMovedComponent()
ResetMover()
SynchronizeWithCurrentReceiverState(bAnimate)
```

Movement target operations should normally remain protected for subclasses:

```text
RequestMoveTowardStart()
RequestMoveTowardEnd()
PauseMovement()
ResumeMovement()
```

Do not expose low-level setters for:

- `MovementAlpha`;
- `State`;
- `bIsMovementPaused`;
- latch completion;
- internal Tick state.

If a controlled teleport operation is useful, expose endpoint-specific commands rather than mutable state:

```text
SnapToStart()
SnapToEnd()
```

These should be protected or intentionally Blueprint-callable according to the existing module API style and must preserve all invariants.

---

# CONFIGURATION

Required designer-facing configuration:

```text
MovementMode
DeactivationBehavior
InitialPosition
TimingMode
bUseSeparateReturnTiming
ForwardSpeed
ReturnSpeed
ForwardMovementTime
ReturnMovementTime
InterpolationSource
BuiltInEasingType
EasingExponent or other verified built-in parameters
MovementCurve
bAnimateInitialReceiverState
bSweepMovement, only if safely implemented
DefaultMovedComponent reference
```

Use editor conditions to hide or disable irrelevant properties.

Examples:

```text
ForwardSpeed / ReturnSpeed
    -> visible only in Speed mode

ForwardMovementTime / ReturnMovementTime
    -> visible only in MovementTime mode

Return timing value
    -> visible only when bUseSeparateReturnTiming is true

BuiltInEasingType and built-in parameters
    -> visible only for BuiltInEasing

MovementCurve
    -> visible only for CustomCurve
```

Use clear categories and tooltips suitable for designers.

---

# VALIDATION

Validate at least:

```text
PuzzleReceiver exists
BillboardRoot exists
StartArrow exists
EndArrow exists
StartArrow and EndArrow are distinct
MovedComponent resolves when required
MovedComponent is movable
MovedComponent belongs to this Actor
MovedComponent is not an internal marker/root component
MovedComponent is not simulating physics
Start and End transforms are not identical when movement is expected
Speed values are positive in Speed mode
MovementTime values are positive in MovementTime mode
Speed mode is not used for a zero-translation transform-only path
CustomCurve exists when selected
built-in easing parameters are valid
```

Invalid configuration must fail predictably.

Do not:

- enable Tick with no valid component;
- report false endpoint completion;
- move an internal authoring component;
- silently replace an invalid curve with an unrelated easing type;
- silently normalize negative timing values into seemingly valid gameplay.

Use the module's existing log category and logging macros.

Do not introduce `LogTemp`.

---

# DEBUG STATE

Follow PuzzleSystem's established local and global debug controls.

When debug is enabled, make the following inspectable:

```text
Actor name
MovedComponent name
MovementMode
DeactivationBehavior
State
bIsMovementPaused
MovementAlpha
EasedAlpha
current target endpoint
Receiver effective active state
Latch completed state
TimingMode
active speed or full movement time
remaining movement time
InterpolationSource
built-in easing type or curve name
Start transform
End transform
optional blocked state
```

Useful visual debug:

```text
green marker at StartArrow
red marker at EndArrow
line between Start and End
arrow indicating current movement direction
marker at current interpolated transform
compact world-space label with state and alpha
```

Debug drawing must have negligible cost while disabled.

Do not add a permanently enabled debug Tick separate from active movement.

Editor arrow components already provide passive authoring visualization and do not replace runtime debug rules.

---

# REQUIRED BEHAVIOR SCENARIOS

Validate at least the following template-specific cases.

## 1. Basic movement to End

```text
InitialPosition = Start
Receiver Activated
-> MovingTowardEnd
-> MovementAlpha reaches 1
-> AtEnd
-> OnReachedEnd once
```

## 2. Basic movement to Start

In PingPong mode:

```text
AtEnd
Receiver Deactivated
-> MovingTowardStart
-> MovementAlpha reaches 0
-> AtStart
-> OnReachedStart once
```

## 3. Latch completes once

```text
Latch
Receiver Activated
-> reaches End
-> latch completed
Receiver cycles inactive/active again
-> no movement
```

## 4. Latch interrupted with Stop

```text
Latch
moving toward End
Receiver Deactivated
DeactivationBehavior = Stop
-> pause in place
Receiver Activated
-> resume toward End
-> latch completes only at End
```

## 5. Latch interrupted with Return

```text
Latch
moving toward End
Receiver Deactivated
DeactivationBehavior = Return
-> reverse toward Start
-> latch not completed
later Receiver Activated
-> move toward End again
```

## 6. Latch interrupted with Continue

```text
Latch
moving toward End
Receiver Deactivated
DeactivationBehavior = Continue
-> continue to End
-> latch completes
```

## 7. FlipFlop at endpoints

```text
AtStart + Activated -> End
AtEnd + Activated   -> Start
```

## 8. FlipFlop activation during movement

```text
MovingTowardEnd + Activated -> reverse toward Start
MovingTowardStart + Activated -> reverse toward End
```

No positional snap occurs.

## 9. FlipFlop Stop resume exception

```text
MovingTowardEnd
Receiver Deactivated with Stop
-> paused toward End
Receiver Activated
-> resume toward End
```

It must not flip toward Start on this resume activation.

## 10. FlipFlop Return

```text
MovingTowardEnd
Receiver Deactivated with Return
-> reverse toward Start
later Activated while returning
-> reverse toward End
```

## 11. FlipFlop Continue

```text
MovingTowardEnd
Receiver Deactivated with Continue
-> finish at End
```

## 12. PingPong normal cycle

```text
Receiver Activated -> End
Receiver Deactivated at End -> Start
```

## 13. PingPong Stop during activation movement

```text
moving toward End
Receiver Deactivated with Stop
-> pause
Receiver Activated
-> resume toward End
```

## 14. PingPong Return during activation movement

```text
moving toward End
Receiver Deactivated with Return
-> reverse toward Start
```

## 15. PingPong Continue during activation movement

```text
moving toward End
Receiver Deactivated with Continue
-> finish at End
-> do not automatically return to Start
```

## 16. Reversal preserves progress

```text
MovementAlpha = 0.6
MovingTowardEnd
request Start
-> State = MovingTowardStart
-> MovementAlpha remains 0.6
-> no transform snap
```

## 17. Speed timing

A translation path of known distance reaches the endpoint according to configured world-units-per-second speed.

## 18. MovementTime timing

A full traversal reaches the endpoint in the configured full duration.

## 19. Partial MovementTime reversal

Remaining time is proportional to current `MovementAlpha` and active direction.

## 20. Built-in easing

Linear progress remains authoritative while the visible transform follows the selected verified Unreal easing function.

## 21. Custom curve

A normalized custom curve changes visible interpolation without changing authoritative remaining-time calculations.

## 22. Runtime component replacement while moving

```text
MovementAlpha = 0.4
SetMovedComponent(NewComponent)
-> NewComponent snaps to current eased path transform
-> movement continues
```

## 23. Runtime component replacement while paused

The new component synchronizes to paused progress and remains paused until resumed.

## 24. Component invalidation

Destroying or invalidating the moved component stops Tick without falsely reaching an endpoint. Assigning a valid replacement allows continuation.

## 25. Initial Receiver active

Initialization handles the Receiver's already-active state correctly without depending on `BeginPlay` ordering.

## 26. Reset Latch

`ResetMover()` returns to configured initial position, clears completion appropriately, and permits a later first activation again.

## 27. Duplicate requests

Repeated requests toward the current target do not restart movement, duplicate events, or reset timing.

## 28. Blueprint child without overrides

The native Receiver integration, mode policies, movement timing, easing, pause, reversal, endpoint detection, and reset all work without Blueprint presentation logic.

---

# FORBIDDEN SHORTCUTS

Do not:

```text
create separate native mover classes for Latch, FlipFlop, and PingPong
use one generic Moving enum state plus a separate target enum
lose direction when Stop pauses movement
restart movement from an endpoint after Stop
consume Latch before End is actually reached
queue Continue deactivation for automatic execution at the endpoint
use EasedAlpha as authoritative movement progress
recalculate movement from the component's current transform every frame
allow Blueprint children to mutate State or MovementAlpha directly
create a mandatory mesh inside the abstract base
hard-code the moved component by component name
search the world repeatedly for the moved component
accept internal arrow or billboard components as movement targets
control components owned by another Actor in the initial implementation
move physics-simulating components through the base transform interpolation
leave Tick enabled while idle or paused
subscribe directly to Emitters
put Controller condition logic in the mover
add project-specific WorldState, perception, navigation, temporal, or replay code
publish project-specific gameplay signals
use custom easing implementations when verified Unreal built-ins already exist
spam movement-blocked events every Tick
```

---

# SUGGESTED MODULE STRUCTURE

Use the existing PuzzleSystem runtime module structure.

Place the new public Actor API in a coherent receiver/activator template folder equivalent to:

```text
Public/
└── Activators/
    └── PuzzleTransformMover.h

Private/
└── Activators/
    └── PuzzleTransformMover.cpp
```

If the existing module already organizes reusable Receiver behaviors under a different established folder, follow that convention instead of reorganizing unrelated files.

The file name should match the primary type:

```text
PuzzleTransformMover.h
PuzzleTransformMover.cpp
```

Do not create a new plugin or project module for this template.

---

# DOCUMENTATION REQUIREMENTS

Update the PuzzleSystem user-facing `Docs` folder.

Document at least:

- purpose of `APuzzleTransformMover`;
- relationship to `UPuzzleReceiverComponent`;
- how to create a Blueprint child;
- how to add and select the moved component;
- how StartArrow and EndArrow are authored;
- Latch, FlipFlop, and PingPong behavior;
- Stop, Return, and Continue behavior;
- Speed and MovementTime timing;
- built-in easing and custom curve usage;
- runtime component replacement;
- initialization behavior;
- reset behavior;
- debug options;
- important limitations such as physics and pure rotation in Speed mode.

Do not place Codex-specific workflow instructions in user documentation.

---

# IMPLEMENTATION SCOPE

When Codex is explicitly asked to implement this template, the scope includes:

```text
APuzzleTransformMover abstract Actor
EPuzzleTransformMoverState
EPuzzleTransformMoverMode
EPuzzleTransformMoverDeactivationBehavior
EPuzzleTransformMoverTimingMode
EPuzzleTransformMoverInterpolationSource
EPuzzleTransformMoverInitialPosition
Billboard root
colored Start and End arrow components
owned UPuzzleReceiverComponent
configurable/default moved-component reference
runtime SetMovedComponent support
normalized MovementAlpha
transform interpolation
verified Unreal built-in easing support
optional normalized UCurveFloat support
Speed and MovementTime timing
optional separate return timing
Latch policy
FlipFlop policy
PingPong policy
Stop, Return, Continue in-progress deactivation behavior
pause and resume
reversal
initial Receiver synchronization
explicit local reset API
Blueprint hooks/delegates
validation
PuzzleSystem debug integration
user documentation
compilation and runtime validation
```

Do not implement in this first task:

```text
concrete door or platform Blueprint assets
mandatory mesh components
mandatory audio or Niagara components
multi-point paths
splines
looping or automatic repeated oscillation
network replication
physics-driven movement
cross-Actor component control
project-specific reset systems
project-specific perception or noise
project-specific navigation updates
project-specific gameplay signals
```

---

# DEFINITION OF DONE

The template is complete only when:

- it lives inside the existing PuzzleSystem runtime plugin/module;
- it derives from `AActor` and is an abstract Blueprint parent;
- it owns a `UPuzzleReceiverComponent`;
- Start and End are authored through differently colored `UArrowComponent` markers;
- no mandatory movable mesh is created internally;
- a valid scene component can be configured and replaced at runtime;
- state uses `AtStart`, `MovingTowardEnd`, `AtEnd`, and `MovingTowardStart`;
- Stop preserves progress and resumes the same direction;
- Return reverses toward the opposite endpoint;
- Continue finishes the current movement without queued reversal;
- Latch completes only after End is reached;
- FlipFlop activation chooses the opposite directional/end state;
- PingPong activation commands End and stable deactivation commands Start;
- Speed and MovementTime timing both work;
- built-in Unreal easing is verified against the actual engine API;
- custom curve interpolation does not replace authoritative linear progress;
- Tick is active only while interpolation is advancing;
- invalid configuration fails predictably;
- Blueprint hooks work without owning core state;
- relevant PuzzleSystem documentation is updated;
- the affected Unreal target compiles successfully;
- required behavior scenarios are validated;
- no project-specific system dependency was introduced.
