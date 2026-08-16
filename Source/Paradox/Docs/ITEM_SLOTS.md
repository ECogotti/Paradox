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

- disables selection, interaction, Smart Object registration, and GridWorld occupancy;
- keeps the Actor visible and preserves its authored scale;
- enforces the pickupable collisionless/no-navigation contract;
- attaches it to the slot's `InsertAnchor` with aligned location and rotation.

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

`AcceptedItemQuery` is evaluated against `InsertableTraits`; an empty query accepts every
insertable. `bLockInsertedItem` prevents only ordinary pickup from the slot. Internal release,
destruction cleanup, and WorldState restoration can always clear the relationship. For an authored
occupied baseline, assign `InsertedItem` on the placed slot instance.

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
`UPuzzleReceiverComponent`. `bRequirePuzzleReceiverForActivation` defaults to false; when enabled,
the receiver's effective active state becomes another mandatory native activity gate. The default
output is:

```text
IsSlotActive() && IsOccupied()
```

It is published on `Puzzle.Signal.Paradox.ItemSlot.Satisfied` via `SetSignalState`. Receiver
changes, occupancy changes, WorldState completion, explicit activity notifications, and inserted
item notifications refresh the output without Tick. Routing remains the standard controller-local
`Emitter -> Controller -> Receiver` flow.

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
