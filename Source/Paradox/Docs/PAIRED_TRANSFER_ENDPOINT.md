# Paired Transfer Endpoint

`AParadoxPairedTransferEndpoint` is the project-level base for two-ended asynchronous transfers.
It owns the complete transaction invariant while leaving subject policy and presentation to a
native or Blueprint subclass. The base intentionally contains no Dumbwaiter, Teleport Gate,
Inventory, Character movement, Selection, GridWorld pathfinding, puzzle-condition, or Emitter
behavior. `AParadoxDumbwaiter`, documented in
[Paradox Dumbwaiter](DUMBWAITER.md); it supplies one-item Inventory semantics through the extension
hooks without moving them into this base. `AParadoxTeleportGate`, documented in
[Paradox Teleport Gate](TELEPORT_GATE.md), specializes the same transaction for semantic Character
transfer and GridWorld parking.

## Actor setup

Create a Blueprint subclass because the native class is abstract, then place exactly two endpoint
instances in the same World.

1. Set each endpoint's **Linked Endpoint** to the other endpoint. The relation must be reciprocal.
2. Position and rotate each **Transfer Anchor** at the destination placement transform.
3. Route PuzzleSystem Controllers to both owned **Puzzle Receiver** components when puzzle gating is
   required. Paired endpoints opt into `bActivateWhenUncontrolled`, so an unwired endpoint is active by
   default. Once a valid Controller registers, its true/false result is authoritative. Both Receivers
   must be active when a new transaction is requested; no Emitter is read directly by this class.
4. Choose a completion mode for each phase. `Timed` is the backward-compatible default: set a
   non-negative **Transfer Out Duration** on the source and **Transfer In Duration** on the
   destination. Zero uses a next-Game-Thread-tick completion. `Explicit` schedules no timer and
   waits indefinitely for the matching operation ID.
5. Override only the domain hooks required by the concrete asset.

Editor Data Validation reports a missing/self/non-reciprocal/incompatible pair, cross-World pair,
missing required components, and invalid timing values.

## Request and state contract

Call `EvaluateTransfer` for a structured, side-effect-free preflight or `RequestTransfer` to start.
The request carries the transfer subject, requester, and optionally the currently executing
`UGameplayActionInstance`. The returned `FParadoxTransferOperationResult` contains an authoritative
status, diagnostic string, and an operation ID after acquisition.

Before any asynchronous work begins, the source becomes `Sending` and its destination becomes
`Receiving`. Both share the same subject and operation ID. Any failed preflight leaves both sides
untouched. `EParadoxTransferPhase` then progresses as follows:

```text
Preparing -> TransferOut -> Committing -> TransferIn -> Finalizing -> None
```

`CompleteTransferOut(OperationId)` and `CompleteTransferIn(OperationId)` are public,
Blueprint-callable completion boundaries. The source completes Transfer-Out; either active
endpoint may complete Transfer-In, which routes safely to the source authority. A wrong operation
ID, a stale callback, or a callback received in the wrong phase returns `false` without mutating
state. Timed phases call these exact same boundaries from their native timers.

The lifecycle contract is:

- `ReceiveTransferOutStarted`: start the departure presentation;
- `CompleteTransferOut`: request the native commit through `PerformTransferCommit`;
- `ReceiveTransferInStarted`: start the arrival presentation;
- `CompleteTransferIn`: request native finalization and release the pair.

Gate and Dumbwaiter force both modes to `Explicit`; Data Validation reports an override back to
`Timed`. Other subclasses retain `Timed` unless they opt in.

The generic commit accepts only a valid, unattached Actor whose root component is `Movable`. It
places that Actor at the destination anchor with `TeleportPhysics`. Override
`PerformTransferCommit` for domain-specific synchronization rather than moving the subject from a
presentation event.

## Extension hooks

The base provides protected Blueprint-native hooks for:

- `CanRequestTransferWithAction`, for endpoint-specific semantic action validation;
- `CanTransferSubject`, `CanSourceStartTransfer`, and `CanDestinationReceiveTransfer`;
- `PrepareSubjectForTransfer`;
- `PerformTransferCommit`;
- `FinalizeTransferredSubject`;
- `HandleTransferCancelled`.

Observable delegates and optional Blueprint events cover requested, Transfer-Out started,
committed, Transfer-In started, completed, cancelled, and state-changed transitions. Overrides
must return failure and a useful diagnostic when they cannot preserve their invariant. The default
action-validation hook returns true, preserving the generic base and Dumbwaiter behavior.
Teleport Gate overrides it to require its exact running Enter action.

## Interaction and Gameplay Actions

Busy endpoints reject new requests without replacing the active transaction. When the requester
owns a `UGameplayActionComponent`, the endpoint also rejects a paused/non-operational component,
an external `GameplayAction.Lock.Interaction`, or another active action holding that exact lock.
An explicitly supplied action must belong to the requester's component, own the Interaction lock,
and be `Running` when `RequestTransfer` executes.

Endpoint state transitions and linked-Receiver state changes call the local
`UParadoxInteractionComponent::NotifyInteractionAffordanceChanged` when that optional component is
present. Concrete interactions should derive availability from `CanRequestTransfer`; do not cache
a second busy flag.

## Cancellation, reset, and Receiver policy

`CancelTransfer` affects only the matching live operation ID. `ResetTransferEndpoint`, subject or
endpoint destruction, EndPlay, and WorldState restore invalidate pending timers and release both
ends. A committed subject is finalized when safe; the base never fabricates a rollback after the
commit boundary.

Receiver activity is a start permission. Deactivation during an acquired transaction does not
abort it, but blocks the next request. This keeps an in-flight animation deterministic and avoids
half-finished pair state.

The inherited Receiver enables PuzzleSystem's opt-in uncontrolled fallback. This makes Gate and
Dumbwaiter pairs usable without placeholder Emitters or Controllers, while preserving ordinary puzzle
gating as soon as a valid Controller is connected. Other PuzzleSystem Receivers keep the historical
fail-closed default because their fallback property remains disabled.

WorldState restore start cancels an active transaction and blocks new requests. Either restore
terminal event re-enables request evaluation. Receiver activation remains owned by PuzzleSystem
and is not overwritten by transfer cleanup.

## Debugging

Diagnostics use the module's `LogParadox` category. Debug logging and one-frame anchor/pair drawing
require both gates:

```text
Paradox.PairedTransfer.Debug 1
Endpoint.bEnableDebug = true
```

The instance Details panel exposes authoritative state, phase, current subject, operation ID,
last status, cancellation reason, and diagnostic. Debug output is event-driven; the Actor never
ticks.

## Automation

After building `ParadoxEditor`, run:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests Paradox.PairedTransferEndpoint; Quit" -TestExit="Automation Test Queue Empty" -log
```

The suite covers the abstract/no-Tick contract, backward-compatible Timed progression, Explicit
phases that never self-advance, correct and stale completion IDs, generic transfer and anchor
commit, atomic pair locking, inactive Receiver failures, duplicate requests, reset/destruction,
return to `Idle`, and Receiver deactivation during an active transaction.
