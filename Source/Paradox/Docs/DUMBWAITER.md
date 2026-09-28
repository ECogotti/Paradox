# Paradox Dumbwaiter

`AParadoxDumbwaiter` is the concrete paired-transfer endpoint for one
`AParadoxInsertablePickupableActor`. It composes the shared
`AParadoxPairedTransferEndpoint` transaction with the existing Inventory and Item Slot ownership
rules; it is intentionally not an Item Slot subclass and does not introduce another inventory.

## Authoring a pair

Place two Actors derived from `AParadoxDumbwaiter` and set each instance's **Linked Endpoint** to
the other. Pairing must be reciprocal, both Actors must be in the same World, and both endpoints
must be Dumbwaiters. Configure each inherited `PuzzleReceiver` through the normal PuzzleSystem
controller graph when Send/Insert should be puzzle-gated. An unwired paired-transfer Receiver is active
by default; once a valid Controller is connected, its result is authoritative. No Emitter is required by
the Dumbwaiter.

The inherited **Transfer Anchor** is the authoritative cargo placement transform. Position and
rotate it where stored cargo should appear. Make the Blueprint cart mesh a child of this component:
animating the anchor then carries both the cart and attached cargo. The Actor has no mandatory
mesh; the cabinet/column remains presentation.

Each endpoint exposes:

- **Accepted Cargo Query**, matched against an insertable pickupable's `InsertableTraits`; an empty
  query accepts every insertable;
- **Lock Stored Pickupable**, which blocks ordinary Pickup but does not block paired Send;
- **Initially Stored Pickupable**, an instance-only reference for an authored occupied baseline;
- inherited completion modes, which must remain `Explicit` for both phases;
- inherited paired-transfer and local debug settings.

Editor validation reports missing/non-reciprocal/incompatible pairs, a non-Explicit phase, invalid
authored cargo, and a placed pair authored with cargo at both ends. An initially stored item must
be a placed insertable in the same World, satisfy the cargo query, and have no other owner.

## Native components and interaction catalog

The native Actor contains the inherited scene root, Transfer Anchor and Puzzle Receiver plus:

- `UParadoxSelectableComponent`, including interaction-cell and puzzle-connection presentation;
- `USmartObjectComponent`, using `/Game/Data/Inventory/DA_ParadoxItemSlotSmartObject`;
- `UParadoxInteractionComponent`;
- `UWorldStateParticipantComponent`, restoring existence and cargo relationship in the Late phase.

Its interaction catalog is assembled in C++:

| Interaction | Definition | Availability |
| --- | --- | --- |
| Insert | `/Game/Data/GameplayActions/DA_ParadoxInsertItem` | active, pair Idle, both ends empty, requester holds compatible insertable |
| Pickup | `/Game/Data/GameplayActions/DA_ParadoxPickupFromItemSlot` | active, Idle, loaded, unlocked, requester inventory empty |
| Send | `/Game/Data/GameplayActions/DA_ParadoxSendDumbwaiter` | both endpoints active and Idle, source loaded, destination empty and compatible |

Insert and Pickup deliberately reuse the Item Slot action classes and their Inventory lock. Send
uses `UParadoxSendDumbwaiterInteractionAction` and the Interaction lock. Selection widgets consume
the standard interaction catalog and preflight results; the Actor contains no widget logic.

## Ownership and transfer invariants

An insertable pickupable may have exactly one authoritative owner:

```text
World | Character Inventory | Item Slot | Dumbwaiter | RestorePending
```

`GetCurrentItemSlot()` and `GetCurrentDumbwaiter()` are mutually exclusive. Both Item Slot and
Dumbwaiter storage use the existing `Inserted` pickupable state, presence policy and Inventory
passive-effect transitions. Insert removes passive effects exactly once; Pickup reapplies them
exactly once.

`TryInsertCargo` and `TryPickupCargo` are atomic with the requester's Inventory. Public validation
is available through `EvaluateAcceptCargo`, `CanAcceptCargo`, and `EvaluatePickupCargo`. A rejected
operation returns `FParadoxItemSlotOperationResult` and leaves every owner unchanged.

The pair has a combined capacity of one cargo. `IsLinkedDumbwaiterOccupied()` exposes the remote
state, and Insert is rejected even when the local cart is empty if its partner owns cargo. Insert,
Pickup, cargo destruction, commit, and World State restore refresh both interaction catalogs
immediately.

`UParadoxSendDumbwaiterInteractionAction` succeeds as soon as `TrySendCargo` atomically acquires the
pair. The action is intentionally instantaneous; the paired transaction remains busy in the
background. Both phases are Explicit:

1. source `ReceiveTransferOutStarted` retracts the source cart;
2. the source Blueprint calls `CompleteTransferOut(OperationId)` when retraction ends;
3. native commit moves the sole cargo owner/backlink/attachment to the destination anchor;
4. destination `ReceiveTransferInStarted` extracts the destination cart;
5. the destination Blueprint calls `CompleteTransferIn(OperationId)` when extraction ends.

After transfer preparation succeeds, native code temporarily suspends the cargo's physical and
navigation presence for the whole background transaction. Actor/primitive collision, primitive
navigation relevance, GridWorld occupancy publication, and the Grid navigation modifier are all
disabled before the source cart starts retracting. They remain disabled through native commit and
destination extraction, then the cargo's previously captured authored `Inserted` configuration is
restored by `CompleteTransferIn`. Cancellation, reset, World State restore, and endpoint teardown
use the same idempotent restoration path on whichever endpoint still owns the cargo. Blueprint cart
Timelines must therefore animate only the `TransferAnchor`; they must not cache or toggle cargo
collision themselves.

Pickup, Insert, and reverse Send remain unavailable while the pair transaction is active. Wrong
or obsolete operation IDs are ignored, so a cancelled Timeline cannot complete a replacement
operation.

The destination repeats its cargo query and Blueprint/native additional-acceptance hook for every
Send. Changing the destination's policy therefore affects the next preflight without copying state
from the source.

## Blueprint extension and observation

Blueprint can specialize `Can Accept Cargo Additional` for a local compatibility decision. It is
always ANDed with native ownership, Receiver, pair, anchor and tag-query invariants.

Presentation hooks run only after native ownership is coherent:

- `On Stored Pickupable Changed` on the Dumbwaiter;
- `On Stored In Dumbwaiter` and `On Removed From Dumbwaiter` on the insertable pickupable;
- inherited paired-transfer requested/out/commit/in/completed/cancelled hooks.

`OnStoredPickupableChanged` remains the local cargo delegate. `OnPairOccupancyChanged` and the
Blueprint event **On Pair Occupancy Changed** provide `bSelfOccupied` and `bLinkedOccupied` from
each endpoint's perspective, after both sides are coherent. Use this pair event to initialize cart
pose and immediately retract the empty partner when cargo is inserted. Presentation hooks may
animate doors/carts, but must not mutate ownership or directly relocate the item.

## Replay

Insert and Send are ordinary semantic Gameplay Actions. Intent Replay records their action
definition and target rather than a cargo transform. A clone therefore repeats “Insert into this
Dumbwaiter” or “Send this Dumbwaiter”; current ownership, Receiver state, pair state and cargo
compatibility are revalidated when the replayed action executes.

## World State and cancellation

World State captures the soft cargo relationship rather than a second inventory snapshot. Restore
first cancels an active paired transaction and invalidates its pending callbacks, then clears
transient backlinks and reconstructs the captured owner/attachment. This covers an item stored but
not sent, pending Transfer-Out, committed Transfer-In and an already completed arrival.

Cancellation before commit preserves source ownership. Cancellation after commit preserves the
deterministic destination ownership. A baseline restore may subsequently reconstruct a different
captured owner. Destroying stored cargo clears the endpoint reference and refreshes affordances.

## Debugging and tests

Paired transaction diagnostics use the inherited two-part gate:

```text
Paradox.PairedTransfer.Debug 1
```

plus the endpoint's local **Enable Debug** option. Inventory/storage diagnostics continue to use
`LogParadox`; no Tick is enabled for the Dumbwaiter.

Run the focused suite with:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests Paradox.Dumbwaiter; Quit" -TestExit="Automation Test Queue Empty" -log
```

The suite covers native composition, Explicit phases, pair-capacity Insert rejection and
re-enablement after removal, pair occupancy events, compatible/incompatible Insert, busy/inactive
failures, immediate pair locking with background Transfer-Out, commit ownership, reverse Send,
shared Pickup rules, native action assets, authored cargo, no-Emitter composition, cancellation,
World State restore on both sides of the commit boundary, and exact collision/navigation presence
suspension and restoration across cancellation and successful Transfer-In.
