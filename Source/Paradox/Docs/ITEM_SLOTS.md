# Paradox insertable items and item slots

## Purpose and ownership

`AParadoxInsertablePickupableActor` extends the normal pickupable with data-driven
`InsertableTraits` and one private weak backlink to its current `AParadoxItemSlotActor`. An item is
always in exactly one authoritative state: World, Held by one Character inventory, Inserted in one
slot, or restore-pending. `IsInserted` and `GetCurrentItemSlot` are read-only queries; callers never
write either side of the relationship directly.

`AParadoxItemSlotActor` owns one private `InsertedItem`. Use `IsOccupied`, `GetInsertedItem`,
`IsSlotActive`, `CanAcceptItem`, `EvaluateAcceptItem`, and `EvaluatePickupInsertedItem` for safe
queries. `TryInsertItem` transfers the requester's currently equipped insertable into the slot;
`TryPickupInsertedItem` transfers the current unlocked item into an empty requester inventory.
Both return `FParadoxItemSlotOperationResult`, including a specific status and diagnostic.

The inventory and slot validate the complete transition before mutation, reject reentrancy and
restore-time operations, update all ownership references before notification, and never publish an
observable intermediate state. Applied passive effects are removed once, in reverse order, on
insertion and applied once on pickup from a slot. `OnInsertedItemChanged` and
`OnSlotActiveStateChanged` are the only public state notifications.

## Authoring insertable items

Create an Actor Blueprint derived from `AParadoxInsertablePickupableActor` and configure inherited
pickupable presentation as documented in [Paradox single-slot inventory](INVENTORY.md). Add static
semantic tags to `InsertableTraits`, for example `Item.Type.Battery` and
`Item.Battery.Voltage.12V`. The Actor remains a standard world pickupable until inserted.

When inserted, native code:

- disables selection, interaction, and Smart Object registration;
- keeps the Actor visible and preserves its authored scale;
- defaults to the pickupable collisionless/no-navigation contract;
- attaches it to the slot's `InsertAnchor` with aligned location and rotation.

Two independent properties under `Paradox | Item Slot | Inserted Presence` can opt an insertable
back into physical presence while it remains attached:

- **Enable Authored Collision While Inserted** restores the initial primitive collision modes and
  channel responses, and enables overlaps for query-capable primitives.
- **Enable Navigation Blocking While Inserted** enables navigation relevance and blocks the cells
  covered by the inherited occupancy bounds through the same GridWorld navigation modifier used by
  world-state pickupables.

Both default to disabled. They are independent from the corresponding World-state properties, and
Held/restore-pending items always disable both capabilities.

`On Inserted Into Slot` and `On Removed From Slot` are presentation-only Blueprint hooks. They run
after the authoritative relationship is coherent and must not implement ownership. Call
`NotifyOwningSlotRelevantStateChanged` when an inserted item's dynamic state can affect a derived
puzzle-slot output.

## Authoring slots

Create an Actor Blueprint derived from `AParadoxItemSlotActor`. The native class already owns
Selection, Interaction, Smart Object, WorldState and PerceptionKnowledge components. It also loads:

```text
/Game/Data/Inventory/DA_ParadoxItemSlotSmartObject
/Game/Data/GameplayActions/DA_ParadoxInsertItem
/Game/Data/GameplayActions/DA_ParadoxPickupFromItemSlot
```

The Smart Object supplies four cardinal approach positions at 100 cm. `InsertAnchor` is independent
of those approach positions and is the exact attachment transform for the item. Replace the Smart
Object Definition in a Blueprint when the level uses a different GridWorld cell size or approach
layout.

The Details panel exposes `AcceptedItemQuery` as **Allowed Item Query**. It is evaluated against
`InsertableTraits`; an empty query accepts every insertable. This is the hard compatibility filter:
if it fails, Insert returns `IncompatibleTraits` and neither Inventory nor Slot ownership changes.
Use it for broad compatibility such as “key/card only” or “12 V battery only”.
`bLockInsertedItem` prevents only ordinary pickup from the slot. Internal release,
destruction cleanup, and WorldState restoration can always clear the relationship. For an authored
occupied baseline, use **Initially Inserted Item** on the placed slot instance. Its Actor picker
shows only `AParadoxInsertablePickupableActor` instances. Editor validation applies the same
**Allowed Item Query** used by the Insert action and reports an incompatible selection; runtime also
rejects invalid authored data safely.

At first play, a valid authored item is assigned to the Slot without submitting a Gameplay Action or
touching a Character inventory. Native initialization establishes both ownership references, snaps
and attaches the item to `InsertAnchor`, applies its Inserted presence policy, and refreshes
interaction, perception, Emitter output, and Receiver item permission. World State captures that
relationship as part of the baseline and reconstructs the same attachment and puzzle state on every
reset.

`IsSlotActive` always combines the non-bypassable native requirement with
`EvaluateAdditionalSlotActive` using logical AND. Override the Blueprint-native hook only for an
additional local condition. `CanAcceptItemAdditional` can add requester/item policy after all
ownership, activity, capacity, and tag-query checks pass. Call
`NotifySlotActiveStateMayHaveChanged` when a dynamic condition changes; the system has no Tick.

## Interaction actions and replay

The native interaction catalog contains Insert and Pickup-from-Slot without Blueprint assembly.
Both use the slot Actor as the semantic target and the common replay-safe `Target` and
`InteractionTag` property-bag fields. Their standard definitions use Optional journaling, Reject
concurrency, and Movement, Interaction, and Inventory locks.

The action resolves a current Smart Object approach, moves and claims through the normal Paradox
interaction lifecycle, then revalidates the requester, exact item, slot state, compatibility,
capacity, and lock before committing. Intent Replay records only the semantic slot target and
interaction tag. A clone creates a fresh action and may fail normally if its current inventory or
the reconstructed slot is no longer compatible. Pickup-from-Slot derives from the ordinary Pickup
action and replaces only source validation/acquisition, so movement, claims, pause, abort, and
replay behavior remain shared.

Occupancy and activity changes call the Interaction component's targeted affordance refresh. A
selected slot therefore updates available options immediately without rebuilding the interaction
system.

## WorldState and lifecycle

The slot participates in WorldState's `Late` restore phase and captures its item as a soft object
reference. Restore start invalidates in-flight ownership and detaches both sides. WorldState remains
responsible for Actor existence and transform; after property restoration the slot reconstructs the
bidirectional relationship, presentation, interaction affordances, perception state, and puzzle
output. Empty and occupied authored baselines are both supported.

Destroying an inserted item clears the slot. Destroying a gameplay slot releases a surviving item
to the world; editor/world teardown avoids manufacturing gameplay state during shutdown.

## Perception and puzzle slots

The slot's PerceptionKnowledge Source publishes persistent, event-driven states for Active,
Occupied, Locked, and Removable. Compatibility is requester-relative and is deliberately not
published as world state.

`AParadoxPuzzleItemSlotActor` additionally composes one `UPuzzleEmitterComponent` and one
`UPuzzleReceiverComponent`. `PuzzleRole` selects the item-driven behavior: `Emitter`, `Receiver`,
or `Emitter and Receiver`. `RightItemTags` contains exact alternative item traits; any exact match
marks an inserted item as correct. An empty container preserves legacy content by treating every
allowed inserted item as correct. A wrong-but-allowed item remains inserted but does not satisfy
the puzzle Slot. This keeps insertion compatibility separate from puzzle correctness.

In `Emitter` or `Emitter and Receiver` mode, the default output is:

```text
IsSlotActive() && IsRightItemInserted()
```

It is published on `Puzzle.Signal.Paradox.ItemSlot.Satisfied` via `SetSignalState`. Receiver
changes, occupancy changes, WorldState completion, explicit activity notifications, and inserted
item notifications refresh the output without Tick. Routing remains the standard controller-local
`Emitter -> Controller -> Receiver` flow. `Receiver`-only mode does not publish an output channel.

In `Receiver` or `Emitter and Receiver` mode, native code sets the owned Receiver to `Manual`.
The correct inserted item supplies the explicit manual permission, while Puzzle Controllers still
supply the prerequisite. Effective activation therefore requires both:

```text
Controller prerequisite active && correct item inserted
```

If the item is already present when the Controller prerequisite arrives, Paradox reapplies the
permission on the next Game Thread tick because PuzzleSystem intentionally rejects commands during
its synchronous Receiver notification. This is event-driven and does not add Tick. Removing or
invalidating the correct item revokes the manual activation immediately.

`bRequirePuzzleReceiverForActivation` remains a separate, optional operational-power gate for the
Slot itself and defaults to false. Avoid enabling it on an initially empty Receiver-role Slot unless
an authored external flow can power the Slot before insertion; otherwise authoring can create a
logical insertion deadlock.

## Debugging and troubleshooting

Spatial diagnostics require both the slot's local `bEnableDebug` and the global gate:

```text
Paradox.Inventory.Debug 1
```

Diagnostics use `LogParadox`; recoverable validation failures return structured results and never
assert. If an interaction is missing, verify the native assets resolve, the slot is active, a Smart
Object approach is available, and the requester owns the required inventory state. If an item is
rejected, inspect `EvaluateAcceptItem` before changing its tag query or traits.

Placed Item Slots remain valid semantic targets in PIE even when World Partition/external-Actor
duplication does not retain `RF_WasLoaded`: the native Interaction Component preserves their
authored load/PIE-duplication provenance. A slot genuinely spawned at runtime is still rejected
unless a future explicit replay identity strategy is provided for it.
