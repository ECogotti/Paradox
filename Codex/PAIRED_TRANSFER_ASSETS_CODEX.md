# Paradox Paired Transfer Puzzle Assets — Codex Implementation Specification

## Purpose

Implement two new Paradox gameplay/puzzle assets that share the same paired asynchronous transfer architecture:

1. **Dumbwaiter / Portavivande** — transfers one compatible Pickupable from one endpoint to its paired endpoint.
2. **Teleport Gate / Varco Teleporto** — transfers a compatible Character from one endpoint to its paired endpoint.

Both assets must derive from the same abstract native Actor base.

The implementation must integrate with the existing project architecture instead of introducing parallel systems.

---

# Mandatory preflight

Before modifying code:

1. Read the root `AGENTS.md`.
2. Identify the module that should own these Paradox-specific Actors.
3. Search every relevant module/plugin directory for `CODEX` folders and read all applicable instructions.
4. Read the relevant `Docs` for:
   - PuzzleSystem;
   - Selection / interaction;
   - GameplayActions;
   - GridWorld / navigation;
   - Inventory / Pickupable / ItemSlot;
   - IntentReplay;
   - WorldState / rewind, if these Actors or transferred subjects participate in snapshots.
5. Inspect the actual public API and implementation of the systems above.
6. Find existing callers and existing Actor patterns before designing new APIs.
7. Do not invent Unreal or project APIs.
8. Prefer the smallest integration with existing systems.
9. Compile and validate each milestone before beginning the next one.
10. Update user-facing `Docs` for every module whose behavior/API changes.

The existing PuzzleSystem architecture must remain:

```text
UPuzzleEmitterComponent
        ↓
APuzzleController
        ↓
UPuzzleReceiverComponent
        ↓
Owning gameplay Actor reacts
```

These transfer Actors are **Receivers only by default**.

Do not add an Emitter merely to mirror Receiver state.

A future Emitter may be added only if a real observed gameplay fact must be published, for example `TransferCompleted` or `CargoArrived`.

---

# Architectural goals

The common concept is:

> A paired transfer endpoint performs one asynchronous transfer transaction between two explicitly linked world endpoints.

Required inheritance:

```text
AActor
└── AParadoxPairedTransferEndpoint        [Abstract]
    ├── AParadoxDumbwaiter
    └── AParadoxTeleportGate
```

Conceptual composition of the base:

```text
AParadoxPairedTransferEndpoint
├── Scene Root
├── UPuzzleReceiverComponent
├── Transfer / Exit Anchor
├── LinkedEndpoint
├── authoritative transfer state
├── current transfer subject
└── asynchronous transfer transaction logic
```

Do not make the PuzzleSystem own teleport behavior.

`UPuzzleReceiverComponent` owns only puzzle activation state.

`AParadoxPairedTransferEndpoint` and its subclasses own concrete gameplay behavior.

---

# Shared invariants

These invariants apply to every milestone.

## Pairing

Endpoints are explicitly connected in pairs.

Do not discover partners through world searches.

The editor configuration must make the paired endpoint obvious.

Validate pairing at startup/editor validation where appropriate.

At minimum detect:

- null pair;
- self-pairing;
- incompatible paired class where relevant;
- non-reciprocal configuration if reciprocal links are required by the chosen implementation.

Do not silently repair broken content at runtime unless an existing project convention explicitly supports that workflow.

## Puzzle activation

An endpoint may start a new transfer only when its existing `UPuzzleReceiverComponent` says the puzzle capability is active.

Do not bypass `APuzzleController`.

Do not inspect Emitters from these Actors.

If puzzle activation changes while a transfer is already in progress, the default policy for this task is:

```text
the current transaction is allowed to finish;
new transactions are rejected until the Receiver becomes active again.
```

This avoids half-completed transfers caused by a puzzle signal disappearing during an asynchronous transition.

## Pair locking

A transfer is a transaction involving both endpoints.

Before the asynchronous transition starts:

```text
Source = Sending
Destination = Receiving
```

Both states must be acquired synchronously before starting presentation or delayed work.

While either endpoint belongs to a transfer transaction:

- both endpoints reject new transfer requests;
- transfer-related interactions are unavailable;
- duplicate input cannot start another transaction;
- the destination cannot simultaneously send another subject.

The pair lock is released only when the transaction completes or is explicitly cancelled/rolled back through a safe path.

## No Tick

Do not use Tick for:

- transition timing;
- pair locking;
- interaction availability;
- transfer completion;
- puzzle activation.

Use events, timers, delegates, explicit completion callbacks, or an already existing project mechanism.

## Async safety

The transition is asynchronous.

A stale callback from an old transaction must never modify the current transaction.

Use an operation identity/generation/revision mechanism or the established equivalent in the project.

Conceptually:

```text
Operation 41 starts
Operation 41 is cancelled/reset
Operation 42 starts

late callback from Operation 41
    -> ignored
```

Account for Actor destruction, EndPlay, world teardown, and rewind/reset.

## Interaction semantics

Mouse/UI selection must request semantic gameplay interactions.

The UI must never directly move or teleport an Actor.

The transfer Actors must expose state that allows the existing Selection/Interaction system to show or hide/disable actions.

Player, clone/replay, and AI paths should converge on the same authoritative gameplay operation wherever the existing architecture supports that.

Do not duplicate player-only transfer logic inside the Actor.

## Replay

Teleport must be represented as a semantic interaction/action, not as recorded transform manipulation.

For example:

```text
Interact with Gate A -> Enter
```

not:

```text
SetActorLocation(destination)
```

Use the current IntentReplay / GameplayActions integration patterns already present in the repository.

## World reset

Inspect the existing WorldState participation of:

- interactable Actors;
- Pickupables;
- ItemSlots;
- Characters;
- puzzle Actors.

Ensure a rewind/reset cannot leave:

- stale transfer locks;
- a stale `CurrentTransferSubject`;
- an item logically stored in two endpoints;
- an item logically stored nowhere while still attached;
- a Character disabled after reset;
- pending timers/callbacks applying an obsolete transfer afterward.

Do not create a second snapshot system.

---

# Shared transfer state

Use one authoritative mutually exclusive state.

Conceptually:

```cpp
enum class EParadoxTransferEndpointState : uint8
{
    Idle,
    Sending,
    Receiving
};
```

Do not represent the same state using several authoritative booleans such as:

```text
bIsBusy
bIsSending
bIsReceiving
bIsTeleporting
```

Convenience query functions are fine, but they must derive from the authoritative state.

---

# MILESTONE 1 — `AParadoxPairedTransferEndpoint`

## Goal

Implement the abstract reusable base that owns the complete paired asynchronous transaction lifecycle.

At the end of this milestone, a synthetic/test subclass must be able to transfer an arbitrary accepted Actor between two endpoints without implementing Dumbwaiter- or Gate-specific rules.

No inventory-specific or Character-specific behavior belongs here.

---

## Responsibilities

`AParadoxPairedTransferEndpoint` must own:

- the existing `UPuzzleReceiverComponent`;
- explicit `LinkedEndpoint`;
- a scene/arrow/anchor component defining the destination placement transform;
- authoritative `EParadoxTransferEndpointState`;
- current transfer subject;
- current operation identity;
- pair acquisition/release;
- common request validation;
- asynchronous phase progression;
- cancellation/cleanup;
- interaction availability while busy;
- reusable extension hooks for subclasses.

The base must not own:

- Pickupable-specific storage state;
- Character movement policy;
- Selection UI implementation;
- GridWorld pathfinding;
- Inventory ownership;
- puzzle conditions;
- Emitter signals.

---

## Suggested public contract

Adapt names to existing project conventions after inspecting the code.

Conceptually provide controlled operations equivalent to:

```text
RequestTransfer(TransferSubject, Requester) -> result

CanRequestTransfer(TransferSubject, Requester) -> result/query

IsTransferInProgress()
GetTransferState()
GetLinkedEndpoint()
GetCurrentTransferSubject()
```

Prefer a useful result/failure reason if the existing interaction/action framework already has one.

Do not expose mutable transfer state.

---

## Internal transaction flow

Required conceptual state flow:

```text
RequestTransfer
    ↓
validate source
    ↓
validate destination
    ↓
validate subject through subclass hook
    ↓
acquire source + destination atomically
    ↓
Source = Sending
Destination = Receiving
    ↓
PrepareSubjectForTransfer
    ↓
Begin Transfer-Out async phase
    ↓
Transfer-Out completion
    ↓
CommitTransfer
    ↓
Begin Transfer-In async phase
    ↓
Transfer-In completion
    ↓
FinalizeTransferredSubject
    ↓
Source = Idle
Destination = Idle
    ↓
clear transaction state
```

The pair must be locked **before** starting any asynchronous phase.

---

## Common validation

A transfer request must fail predictably when at least one of the following is true:

- source Receiver is inactive;
- destination Receiver is inactive;
- source has no valid pair;
- source is paired to itself;
- source is not `Idle`;
- destination is not `Idle`;
- transfer subject is null/invalid;
- subclass rejects the subject;
- subclass rejects source or destination conditions;
- an existing action/interaction lock says the requester cannot start the operation.

A failed request must not partially acquire one endpoint.

---

## Async transition contract

Do not hardcode a specific animation technology into the base.

Inspect whether the project already has an asynchronous interaction/transition pattern and reuse it when appropriate.

The required architectural contract is:

```text
BeginTransferOut(...)
    -> asynchronous work
    -> explicit completion notification

CommitTransfer(...)

BeginTransferIn(...)
    -> asynchronous work
    -> explicit completion notification
```

The base must own the authoritative phase progression.

Blueprint may control presentation, but Blueprint must not directly mutate the transaction state.

If the project has no suitable existing mechanism, implement the smallest native async-capable fallback consistent with current project conventions, for example configurable timer-backed completion plus Blueprint presentation hooks.

Never use arbitrary Delay hacks inside gameplay logic.

---

## Commit boundary

All actual relocation must pass through one controlled commit boundary.

Conceptually:

```text
PerformTransferCommit(Subject, Destination)
```

The base may provide a default placement using the destination anchor only if doing so is safe for generic Actors.

Subclasses must be able to override the commit behavior when their domain requires additional synchronization.

Do not let Blueprint presentation events directly call `SetActorTransform` as the authoritative transfer path.

---

## Required subclass extension points

Provide protected/native extension points equivalent in responsibility to:

```text
CanTransferSubject(...)
CanSourceStartTransfer(...)
CanDestinationReceiveTransfer(...)

PrepareSubjectForTransfer(...)
PerformTransferCommit(...)
FinalizeTransferredSubject(...)
HandleTransferCancelled(...)
```

Also expose optional presentation hooks/events for:

```text
Transfer requested
Transfer-out started
Transfer committed
Transfer-in started
Transfer completed
Transfer failed/cancelled
```

Use `BlueprintNativeEvent`, protected virtuals, delegates, or existing project conventions intentionally.

C++ must keep the invariants even when Blueprint does nothing.

---

## Cleanup

On EndPlay/reset/destruction:

- invalidate pending operation callbacks;
- clear timers/delegates owned by the endpoint;
- safely release this endpoint from an active pair transaction;
- never leave the partner permanently busy;
- never invoke callbacks on destroyed subjects/endpoints.

If safe rollback is impossible after the commit boundary, preserve a deterministic final state and report the failure rather than manufacturing a fake rollback.

---

## Debug

Follow the project/global debug rules.

Make the following state inspectable:

```text
Actor name
Receiver active/inactive
Linked endpoint
Transfer state
Current subject
Current operation identity
Current transfer phase
Why the last request failed, when useful
```

If spatial debug is appropriate, show the linked endpoint and transfer anchor only when both global and local debug are enabled.

---

## Milestone 1 validation scenarios

Validate at least:

1. Two valid endpoints transfer one accepted test Actor.
2. Source inactive -> request rejected.
3. Destination inactive -> request rejected.
4. Source busy -> second request rejected.
5. Destination busy -> request rejected.
6. Both endpoints become locked before async transition starts.
7. Duplicate interaction during transfer does nothing harmful.
8. Old async completion callback after cancellation/reset is ignored.
9. Endpoint destruction does not leave its pair permanently locked.
10. Completed transaction returns both endpoints to `Idle`.
11. Receiver deactivates during transfer -> current operation completes, new operations remain blocked.
12. No Tick is required.

Compile and validate this milestone before implementing a concrete asset.

---

# MILESTONE 2 — `AParadoxDumbwaiter`

## Goal

Implement the Portavivande as a concrete paired transfer endpoint for Pickupable cargo.

It must support the semantic interaction sequence:

```text
Insert
Send
```

and reuse the existing Pickup/ItemSlot behavior where possible.

Do not implement a second independent inventory system.

---

## Pre-implementation investigation

Before writing the class, inspect the current:

- Pickupable abstraction/interface/base class;
- Inventory ownership rules;
- Pickup / Swap / Drop actions;
- `AParadoxItemSlotActor`;
- existing Insert or Use interactions;
- ItemSlot lock/unlock behavior;
- interaction selection UI;
- IntentReplay recording for these actions.

Determine whether reusable slot behavior is already componentized.

### Important

Do **not** make `AParadoxDumbwaiter` derive from `AParadoxItemSlotActor` merely for code reuse unless the existing architecture proves that it is a genuine semantic `is-a` relationship.

Prefer composition.

If reusable slot capability is currently trapped inside `AParadoxItemSlotActor`, do not automatically refactor it during this milestone.

First determine the smallest safe reuse path and report/refactor only what is actually necessary.

A potential future `UParadoxItemSlotComponent` is allowed only if inspection demonstrates a real reusable capability and the refactor can preserve existing serialized behavior.

---

## Additional authoritative state

The Dumbwaiter needs one authoritative cargo reference or the equivalent state already owned by the reusable ItemSlot capability.

Conceptually:

```text
StoredPickupable
```

Do not duplicate cargo ownership if the existing ItemSlot system already has an authoritative owner.

The invariant is:

```text
one Dumbwaiter endpoint can contain at most one cargo item
```

unless the existing game design explicitly supports multiple cargo items.

For this task, implement one item only.

---

## Interactions

Required semantic actions:

### `Insert`

Available when:

- endpoint Receiver is active;
- endpoint is `Idle`;
- endpoint is empty;
- requester has/provides a compatible Pickupable according to existing Inventory rules.

Insert does **not** teleport the item.

It stores/attaches/places the item using the existing slot/inventory ownership model.

Conceptually:

```text
Player owns Pickupable
    ↓
Insert
    ↓
Dumbwaiter owns/contains Pickupable
    ↓
item aligned to cargo anchor
```

### `Send`

Available when:

- source Receiver is active;
- destination Receiver is active;
- both endpoints are `Idle`;
- source contains a valid Pickupable;
- destination can accept the cargo;
- destination is empty.

`Send` invokes the common `RequestTransfer` transaction.

### Retrieval

If the existing ItemSlot design supports optional pickup/retrieval from a slot, preserve that concept.

A stored item may be recoverable through the existing Pickup action when content configuration allows it.

Do not create a duplicate bespoke `RemoveItem` interaction if the current inventory framework already solves this.

---

## Dumbwaiter transfer behavior

The Dumbwaiter subclass must implement the base extension points so that:

### Before transfer

- source still owns exactly one valid cargo item;
- destination is empty;
- cargo becomes unavailable for normal interaction while the pair is busy;
- no duplicate Send can start.

### Commit

The logical cargo ownership must move atomically:

```text
Source cargo = null
Destination cargo = Subject
```

and the Actor must be aligned/attached/registered using the existing slot contract.

Do not create a frame where both endpoints logically own the same item.

Do not leave the source owning the item after the destination has accepted it.

### After transfer

The destination cargo remains non-interactable until Transfer-In completes.

After completion:

- both endpoints are `Idle`;
- the item becomes interactable according to normal slot rules;
- destination may expose `Send` back in the opposite direction;
- pickup/retrieval becomes available when configuration permits it.

---

## Pair compatibility

A Dumbwaiter should pair only with another compatible Dumbwaiter/endpoint capable of receiving the same cargo semantics.

Validate configuration rather than relying on a runtime cast failure during Send.

---

## Selection UI integration

Reuse the existing selection/interaction framework.

The UI should reflect actual authoritative availability.

Typical state:

```text
EMPTY + carrying compatible item
    -> Insert

LOADED
    -> Send
    -> Pickup/Retrieve if allowed

BUSY
    -> transfer interactions disabled/hidden according to current UI convention

RECEIVER INACTIVE
    -> interactions disabled/hidden according to current UI convention
```

Do not put widget implementation logic in the Actor if the existing Selection system owns presentation.

Expose/query gameplay actions; let Selection render them.

---

## Replay

The replay system should record/replay semantic actions such as:

```text
Insert item into Dumbwaiter A
Send Dumbwaiter A
```

A clone must issue the same interaction request and let the current Dumbwaiter state validate it.

Do not force replay success by directly moving the item.

---

## WorldState/reset

Verify at least these reset cases:

- item inserted but not sent;
- transfer-out pending;
- transfer committed but transfer-in pending;
- item already arrived;
- reset while pair is locked.

After reset, cargo logical ownership, attachment, inventory ownership, and endpoint states must agree.

---

## Milestone 2 validation scenarios

Validate at least:

1. Insert compatible Pickupable into empty active Dumbwaiter.
2. Cannot insert into occupied Dumbwaiter.
3. Cannot insert while busy.
4. Cannot Send empty Dumbwaiter.
5. Cannot Send to occupied destination.
6. Cannot Send while source or destination Receiver is inactive.
7. Send locks both Dumbwaiters immediately.
8. Cargo interaction is blocked during transfer.
9. Commit transfers logical cargo ownership exactly once.
10. Cargo becomes available only after Transfer-In completion.
11. Reverse Send works after the first transfer completes.
12. Existing Pickup/retrieval rules still work when enabled.
13. Replay interaction uses semantic Insert/Send path.
14. Reset does not duplicate or lose cargo.
15. No new Emitter is required.

Compile and validate before implementing the Teleport Gate.

---

# MILESTONE 3 — `AParadoxTeleportGate`

## Goal

Implement the Varco Teleporto as a concrete paired transfer endpoint for Characters.

The Character must **not** enter the gate through normal navigation.

The transfer starts only through an explicit semantic interaction, such as `Enter`.

---

## Core navigation rule

The gate itself is not a navigation destination that the pawn walks through.

Required player flow:

```text
Select Teleport Gate
    ↓
Selection UI exposes Enter
    ↓
GameplayActions / interaction requests approach
    ↓
Character reaches valid adjacent interaction position
    ↓
Enter interaction executes
    ↓
Gate.RequestTransfer(Character)
```

Do not implement:

```text
Character overlaps gate
    -> automatic teleport
```

Do not make overlap the authoritative trigger.

An overlap may exist for visuals/sensors if needed, but it must not replace the interaction request.

---

## Interaction approach

Inspect the existing Selection/Interaction/GridWorld APIs for how an Actor declares:

- adjacent interaction cells;
- interaction anchor/approach point;
- reachability;
- facing requirements;
- blocked/non-navigable cells.

Reuse that system.

Do not create a second pathfinding or adjacency system inside the Gate.

The Gate's physical/internal cell must not become the pawn's ordinary walk target.

---

## Destination placement

The paired Gate must expose a destination/exit anchor whose final position corresponds to a valid place for the Character.

The exit should normally be outside the blocked Gate geometry and consistent with GridWorld occupancy/navigation.

Do not blindly use raw `SetActorLocation` if doing so would desynchronize:

- GridWorld occupancy;
- path following;
- movement component state;
- controller movement requests;
- replay/action state.

Inspect existing teleport/reposition/snap-to-cell APIs first.

Use the correct authoritative relocation path for this project.

---

## Character validation

The Gate must accept the Character abstraction already used by the project.

Do not hardcode only the player pawn if clones/AI share a compatible Character type or interface.

The design goal is:

```text
Player
Clone
AI character, when allowed
    ↓
same Gate transfer contract
```

Player-only behavior belongs in player input/Selection, not inside the Gate transaction.

---

## Before transfer

When the transaction is acquired:

- stop/cancel/suspend current movement through the existing action/movement contract;
- block new interactions/actions that would conflict with the transfer;
- prevent path following from attempting to enter the Gate;
- keep the Character owned by exactly one transfer operation;
- disable only the capabilities necessary for transfer safety.

Do not globally disable unrelated world systems.

---

## Commit

The Gate subclass overrides or specializes the base commit boundary to relocate the Character through the existing GridWorld/movement-safe path.

Conceptually:

```text
Source Gate
    ↓
Character removed/reconciled from source movement occupancy
    ↓
Character placed at Destination Exit Anchor / valid cell
    ↓
destination movement/occupancy state reconciled
```

The exact calls must come from the actual project API after inspection.

Do not invent them.

---

## After transfer

Only after Transfer-In completes:

- Character transfer lock is released;
- normal selection/interactions may resume;
- movement/path following may resume;
- GridWorld occupancy must be valid;
- no stale source movement request may pull the Character back;
- the Character is available to the next semantic action/replay step.

---

## Puzzle activation

The Gate is a Puzzle Receiver.

When inactive:

- `Enter` cannot start;
- the Character may still approach/select it if that is consistent with the existing UI convention, but the action must report unavailable;
- the Character must not teleport.

If activation disappears after a transfer already acquired both endpoints, allow the current transfer to finish.

---

## Selection UI

The player selects the Gate as an interactable object.

Typical action:

```text
Enter
```

Availability is based on authoritative gameplay checks:

- Receiver active;
- source/destination idle;
- valid pair;
- destination active;
- requester is valid Character;
- Character is in a valid interaction position or the action system can route it there.

Do not encode UI text/state as the authority.

---

## Replay

IntentReplay must record the semantic Gate interaction.

Conceptually:

```text
Interact with Gate A -> Enter
```

When a clone reaches that replayed action, it issues the same request.

The Gate decides whether the request currently succeeds.

Do not replay recorded world coordinates as the teleport behavior.

---

## WorldState/reset

Validate reset during:

- approach before Enter;
- Transfer-Out;
- immediately before commit;
- immediately after commit;
- Transfer-In;
- completed transfer.

After reset:

- Character is not permanently interaction-locked;
- movement is not permanently disabled;
- pair states are not stale;
- obsolete callbacks cannot move the Character;
- GridWorld and Character transform agree.

---

## Milestone 3 validation scenarios

Validate at least:

1. Player selects Gate and sees/executes Enter through existing Selection.
2. Pawn approaches an adjacent valid interaction position instead of pathing into the Gate.
3. Walking/overlapping the Gate without interaction does not teleport.
4. Inactive Receiver prevents Enter.
5. Invalid/unpaired Gate prevents Enter.
6. Busy source prevents Enter.
7. Busy destination prevents Enter.
8. Both Gates lock before transition starts.
9. Character cannot perform conflicting interactions during transfer.
10. Character movement/path following cannot continue stale movement during transfer.
11. Commit places Character using GridWorld/movement-safe project API.
12. Character becomes usable only after Transfer-In completes.
13. A valid clone can use the same semantic interaction path.
14. Replay does not depend on recorded teleport coordinates.
15. Reset during every transfer phase leaves Character and gates valid.
16. No overlap-triggered automatic teleport exists.
17. No Emitter is required.

---

# Final integration checks

After all three class milestones are complete:

## Pair behavior

Test:

```text
Dumbwaiter A <-> Dumbwaiter B
TeleportGate A <-> TeleportGate B
```

Ensure a transfer can originate from either side after the previous transaction has completed.

## Puzzle wiring

For each concrete asset, configure:

```text
Emitter
    ↓
APuzzleController
    ↓
Asset.UPuzzleReceiverComponent
```

Verify:

- inactive -> interaction unavailable;
- active -> interaction available;
- deactivation during an existing transfer does not corrupt the transfer;
- no direct Emitter-to-asset logic was added.

## Selection / interactions

Verify that the availability shown by Selection matches the gameplay validation.

The Actor remains authoritative.

UI must not manufacture success.

## Rewind

Perform rewind/reset from every meaningful transaction phase.

No stale async callback may modify the reset world.

## Documentation

Update relevant human-facing `Docs` with:

- purpose of paired transfer endpoints;
- how to create a pair;
- how Puzzle Receiver activation affects them;
- how async transitions are completed;
- Dumbwaiter setup and Insert/Send flow;
- Teleport Gate setup and Enter/approach flow;
- debugging;
- failure cases;
- reset/replay integration.

Do not put Codex-specific instructions into user-facing `Docs`.

---

# Explicit non-goals

Do not add in this task unless existing architecture strictly requires them:

- global teleport manager;
- global paired-endpoint registry;
- world search for matching endpoints;
- new Emitter signals;
- automatic Receiver-to-Emitter mirroring;
- Tick-based transfer state;
- overlap-triggered Gate teleport;
- a second inventory system;
- a second GridWorld/pathfinding system;
- a second replay system;
- a new generic interaction framework;
- multiplayer/network prediction architecture;
- multiple cargo slots per Dumbwaiter;
- arbitrary multi-destination teleport routing;
- automatic random destination selection.

---

# Definition of done

The task is complete only when:

1. `AParadoxPairedTransferEndpoint` is implemented, documented, compiled, and validated.
2. `AParadoxDumbwaiter` is implemented, documented, compiled, and validated on top of the base.
3. `AParadoxTeleportGate` is implemented, documented, compiled, and validated on top of the base.
4. Both concrete classes use the existing `UPuzzleReceiverComponent` and normal Controller wiring.
5. Neither concrete class requires an Emitter.
6. Pair locking prevents concurrent conflicting transfers.
7. Async stale callbacks are safe.
8. Selection drives semantic interactions rather than direct movement/teleport.
9. Gate navigation never requires walking into the Gate.
10. Dumbwaiter cargo ownership cannot duplicate or disappear during a normal transfer.
11. Character movement/GridWorld state remains coherent after Gate transfer.
12. Replay uses semantic interactions.
13. World reset leaves no stale locks/timers/callbacks.
14. All affected targets compile successfully.
15. Relevant user-facing documentation is updated.
16. Final diff contains no unrelated refactor.
