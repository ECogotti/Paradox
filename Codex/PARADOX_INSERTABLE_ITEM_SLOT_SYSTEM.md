# Paradox Insertable Pickupables & Item Slots — Codex Specification

## Purpose

Implement a Paradox-specific subsystem for **insertable pickupable items and compatible world slots**.

This subsystem is built on top of the already-existing Paradox systems:

- single-slot Inventory;
- Selection / Hover;
- Interaction System;
- Gameplay Actions;
- Smart Objects;
- GridWorld;
- WorldState;
- PerceptionKnowledge;
- PuzzleSystem;
- IntentReplay integration already provided through Interaction / Gameplay Actions.

This is **not a new plugin**.

Implement it inside the Paradox gameplay module, following the existing module architecture, `AGENTS.md`, local `CODEX` instructions, and existing `Docs`.

Do not redesign or duplicate any of the systems above.

---

# Gameplay concept

The subsystem represents pairs such as:

```text
Magnetic Key     -> Key Reader
Battery          -> Battery Slot
Fuse             -> Fuse Panel
Access Card      -> Card Reader
```

All of these share the same semantic operation:

```text
Character holds an insertable pickupable
        ↓
Character interacts with a compatible Item Slot
        ↓
Insert Interaction Action
        ↓
Item leaves Character inventory
        ↓
Item becomes inserted into the Item Slot
```

Some slots allow the player to take the inserted object back.

Other slots permanently or temporarily lock the inserted item.

A specialized Item Slot also integrates with `PuzzleSystem` and acts as a Puzzle Emitter.

---

# Fixed class architecture

Create the architecture equivalent to:

```text
AParadoxPickupableActor
        │
        ▼
AParadoxInsertablePickupableActor


AParadoxItemSlotActor
        │
        ▼
AParadoxPuzzleItemSlotActor
```

Use actual project naming conventions if the existing classes use different prefixes or names.

Do not create concrete native classes such as:

```text
AMagneticKey
ABattery12V
AFuse10A
AKeyReader
ABatterySlot
AFusePanel
ACardReader
```

Those should normally be Blueprint children configured through data.

---

# Required investigation before implementation

Before modifying code, inspect the actual project implementation.

In particular, Codex must inspect:

```text
Paradox Inventory implementation
existing Pickup Interaction Action
existing Swap Interaction Action
Interaction Action base hierarchy
Selection / Hover integration
Smart Object interaction configuration
WorldState participant integration
PerceptionKnowledge observable integration
PuzzleSystem Emitter API
PuzzleSystem Receiver API
PuzzleSystem Controller gate implementation
PuzzleSystem delegates / hooks
IntentReplay interaction/action integration
```

Do not assume API names from this document.

The conceptual behavior below is authoritative; exact signatures must follow the real codebase.

---

# PART 1 — Insertable Pickupable

## Class

Create a native class equivalent to:

```text
AParadoxInsertablePickupableActor
    : AParadoxPickupableActor
```

The base Pickupable Actor already owns the normal pickupable integrations.

Do not reimplement those systems.

The Insertable Pickupable must inherit the existing behavior for:

```text
Selection / Hover
Interaction
Pickup
Swap
Inventory ownership
Pickupable Actions
Passive Effects
WorldState
Perception, if already provided by the standard Pickupable
```

The new class only adds the capability of being inserted into a compatible Item Slot.

---

# Insertable traits

Expose a designer-facing:

```text
FGameplayTagContainer InsertableTraits
```

Use semantic Gameplay Tags to describe static compatibility traits.

Example concepts:

```text
Item.Type.Key
Item.Type.Key.Magnetic
Item.Access.Level.2

Item.Type.Battery
Item.Battery.Voltage.12V
Item.Battery.Size.Small

Item.Type.Fuse
Item.Fuse.Rating.10A

Item.Type.Card
Item.Access.Level.3
```

The Insertable Pickupable must not know concrete slot classes.

Do not create hardcoded compatibility checks based on concrete pickupable subclasses.

Compatibility is owned by the Item Slot.

---

# Insertable ownership state

The item may semantically exist in one of these ownership situations:

```text
World
Character Inventory
Item Slot
```

Do not necessarily introduce a new enum if the current Inventory implementation already has a suitable ownership/state model.

First inspect the existing Pickupable architecture.

The required invariants are:

```text
if Item is held by Character:
    CurrentHolder is valid
    CurrentItemSlot is null

if Item is inserted:
    CurrentHolder is null
    CurrentItemSlot is valid

if Item is free in the world:
    CurrentHolder is null
    CurrentItemSlot is null
```

An Item must never be authoritatively held by both a Character and an Item Slot.

Expose controlled queries equivalent to:

```text
IsInserted()
GetCurrentItemSlot()
```

Do not expose mutable ownership pointers to Blueprint.

---

# Bidirectional slot invariant

When an Item is inserted:

```text
Item.CurrentItemSlot == Slot
Slot.InsertedItem == Item
```

These two references must always agree.

All transitions must preserve this invariant.

Do not allow Blueprint code to mutate either side directly.

---

# PART 2 — Generic Item Slot

## Class

Create a native Actor equivalent to:

```text
AParadoxItemSlotActor
```

This represents a generic world container that can accept an Insertable Pickupable.

Examples of Blueprint children:

```text
BP_BatterySlot
BP_KeyReader
BP_FuseHolder
BP_CardReader
```

The native class must contain all generic integration required by ordinary Paradox gameplay Actors of this type.

---

# Mandatory system integrations

`AParadoxItemSlotActor` must integrate with the existing project systems for:

```text
Selectable / Hover
Interaction
Smart Object interaction positioning
WorldState
PerceptionKnowledge
```

Do not build parallel systems.

Inspect existing Paradox Actor patterns and reuse their interfaces, components, base classes, or helper APIs.

The Item Slot must be usable as a normal selectable/interactable Paradox world object without requiring every Blueprint child to manually rebuild these capabilities.

---

# Insert Anchor

Use:

```text
UArrowComponent InsertAnchor
```

NOT a generic `USceneComponent`.

The Arrow Component is both:

```text
the target transform for the inserted item
and
an editor-visible indication of insertion position/orientation
```

Designers must be able to position and rotate it in Blueprint.

The item should align / attach to the Arrow Component according to the existing Actor attachment conventions.

Do not hardcode battery/key/fuse-specific offsets.

---

# Slot compatibility query

Expose a designer-facing:

```text
FGameplayTagQuery AcceptedItemQuery
```

The Slot validates:

```text
AcceptedItemQuery
    against
InsertablePickupable.InsertableTraits
```

Example:

```text
Battery Slot:

ALL
    Item.Type.Battery
    Item.Battery.Voltage.12V
```

Example:

```text
Key Reader:

ALL
    Item.Type.Key.Magnetic
    Item.Access.Level.2
```

Use Unreal's existing Gameplay Tag Query system.

Do not invent another boolean tag-expression framework.

---

# `CanAcceptItem`

Provide a controlled query equivalent to:

```text
CanAcceptItem(Item, Requester)
```

The public query must validate all common invariants.

Conceptual evaluation:

```text
Slot valid
    ↓
IsSlotActive()
    ↓
Slot empty
    ↓
Item valid
    ↓
Item is insertable
    ↓
Item is currently held by Requester inventory
    ↓
AcceptedItemQuery matches InsertableTraits
    ↓
additional overridable validation
```

Do not let custom subclasses bypass the common safety checks.

Use a protected override / Blueprint extension hook equivalent to:

```text
CanAcceptItemAdditional(...)
```

or an equivalent pattern already used by the module.

This extension exists for dynamic requirements such as:

```text
Battery charge > 50%
Fuse integrity is valid
Key is not disabled
```

Do not encode arbitrary runtime values as Gameplay Tags merely to satisfy this API.

Do not create a large generic Requirement UObject framework yet unless an existing project system already provides one.

---

# Slot operational state — `IsSlotActive()`

Add a central slot-state query equivalent to:

```text
IsSlotActive()
```

This represents whether the Slot is currently operational and able to perform its slot-specific interactions.

Examples of inactive state:

```text
not powered
mechanically blocked
locked
disabled
closed
broken
temporarily unavailable
```

The generic `AParadoxItemSlotActor` default should be:

```text
IsSlotActive() == true
```

The function must be overridable in an Unreal-safe way.

Prefer a structure equivalent to:

```text
public stable query:
    IsSlotActive()

protected replaceable evaluation:
    EvaluateSlotActive()
```

or a `BlueprintNativeEvent` if that matches established module conventions.

The public API must remain the safe authoritative query.

Do not expose a mutable public `bIsSlotActive` as the only architecture.

---

# Effect of `IsSlotActive()`

Slot-specific interactions must be unavailable or fail validation while the Slot is inactive.

At minimum:

```text
Insert requires IsSlotActive()
Pickup inserted item requires IsSlotActive()
```

Revalidate `IsSlotActive()` at action execution time.

Do not rely only on UI availability queries.

This is important for:

```text
Player interaction
clone replay
AI interaction
state changing between query and execution
```

---

# Slot occupancy

The Item Slot owns one authoritative:

```text
InsertedItem : 0..1
```

The slot may hold exactly one item.

Do not implement:

```text
slot arrays
multiple sockets
stacking
capacity
```

Those are outside scope.

Provide safe queries equivalent to:

```text
IsOccupied()
GetInsertedItem()
```

---

# Inserted item lock policy

Expose:

```text
bool bLockInsertedItem
```

designer-facing.

Semantics:

```text
false
    inserted item may be picked back up

true
    inserted item cannot be removed through ordinary Pickup interaction
```

This policy affects ordinary user interaction only.

Internal reset/cleanup must still be able to release or restore an item regardless of this flag.

---

# Interaction options on the Item Slot

The Slot may expose two slot-specific interactions:

```text
Insert
Pickup
```

They are context-sensitive.

---

# Insert Interaction Action

`Insert` is an existing-style Paradox Interaction Action.

Create an action equivalent to:

```text
UParadoxInsertItemInteractionAction
```

only after inspecting the actual Interaction Action hierarchy.

Do not redefine what an Interaction Action is.

Do not create a parallel execution framework.

Availability/execution requires conceptually:

```text
Slot.IsSlotActive()
Slot is empty
Requester has InventoryComponent
Requester inventory contains an Insertable Pickupable
Slot.CanAcceptItem(Item, Requester)
normal Interaction System validation passes
```

The action is requester-relative.

Do not store requester-relative availability as global Slot state.

---

# Insert action execution

The transition:

```text
Character Inventory
    ↓
Item Slot
```

must be treated as one validated transaction.

Do not mutate inventory before full validation succeeds.

Conceptual flow:

```text
VALIDATE COMPLETE OPERATION

then COMMIT:

remove passive effects from Character
clear Character inventory ownership
clear Item CurrentHolder

set Slot.InsertedItem
set Item.CurrentItemSlot
set Item logical state to inserted

remove any free-world/Grid occupancy owned by the pickupable
align / attach Item to InsertAnchor

refresh selection/interaction availability
refresh PerceptionKnowledge state
refresh Puzzle output if applicable
broadcast appropriate events
```

If validation fails:

```text
no authoritative ownership state changes
```

Do not leave the Item half-equipped or half-inserted.

---

# Pickup interaction for inserted items

An occupied Item Slot may optionally offer a `Pickup` interaction that returns the inserted object to the Requester's one-slot inventory.

Required availability:

```text
IsSlotActive()
InsertedItem is valid
bLockInsertedItem == false
Requester InventoryComponent exists
Requester inventory slot is empty
existing Pickup-specific validation passes
```

When these conditions are not met, ordinary Pickup from the Slot must not be available.

---

# Reuse the existing Inventory Pickup Action

This is a mandatory investigation point.

The Inventory system already has a Pickup Interaction Action.

Codex must inspect it before implementing Slot pickup.

Determine whether the existing Pickup Action is sufficiently extensible to support both:

```text
World Pickupable -> Inventory
Inserted Pickupable -> Inventory
```

Use this preference order:

```text
1. reuse the existing Pickup Action directly if its source abstraction already supports it;

2. minimally extend the existing Pickup Action with a generic source/acquisition hook if this preserves its current public behavior;

3. create a derived action such as PickupFromSlot only if the existing action cannot safely support the new source semantics.
```

Do not duplicate the entire Pickup implementation.

Do not change existing Pickup behavior for ordinary world items unless necessary.

Preserve existing Blueprint and replay compatibility.

---

# Pickup-from-slot transaction

The transition:

```text
Item Slot
    ↓
Character Inventory
```

must also be atomic.

Conceptual validation:

```text
Slot active
Slot occupied
item not locked
Requester inventory empty
InsertedItem valid
Pickup allowed by existing Inventory rules
```

Then commit:

```text
clear Slot.InsertedItem
clear Item.CurrentItemSlot

detach Item from InsertAnchor

equip Item into Requester inventory
set Item CurrentHolder
apply Item passive effects

refresh slot/item interaction state
refresh PerceptionKnowledge
refresh Puzzle output if applicable
broadcast normal state notifications
```

Do not expose an intermediate state where both Slot and Inventory own the Item.

---

# Interaction state examples

## Empty slot + compatible held item

```text
[ Insert ]
```

## Empty slot + incompatible held item

```text
Insert unavailable
```

## Occupied slot + unlocked + Requester inventory empty

```text
[ Pickup ]
```

## Occupied slot + locked

```text
Pickup unavailable
```

## Slot inactive

```text
Insert unavailable
Pickup unavailable
```

Other unrelated Interaction Actions owned by the Actor may still exist if configured by derived classes.

`IsSlotActive()` only gates the slot-specific behavior unless project architecture provides a reason to gate more broadly.

---

# Internal release API

Even if no separate `Remove` action is implemented, provide a controlled internal operation equivalent to:

```text
ReleaseInsertedItem(...)
```

This is needed for:

```text
WorldState restore
Actor destruction
future scripted ejection
future Remove Interaction Action
future machine-specific behavior
```

This internal API must not bypass ownership cleanup.

`bLockInsertedItem` must not block internal lifecycle/reset cleanup.

---

# Inserted item presentation

The Inserted Item remains a real Actor.

Do not destroy it.

While inserted, default gameplay semantics should ensure it is not treated as a normal free pickupable.

At minimum verify/update as appropriate:

```text
normal Pickup availability
Swap availability
free-world occupancy
collision/navigation contribution
selection behavior
interaction behavior
attachment
```

Exact behavior must follow the existing Pickupable architecture.

Do not assume "hide while inserted" as a universal rule.

Blueprint children may use presentation hooks for animation/VFX/audio/mesh transitions, but Blueprint must not own the authoritative relationship.

---

# Events / hooks

Expose the smallest useful set of notifications following existing Paradox style.

Conceptually:

```text
OnItemInserted
OnItemRemoved
OnInsertedItemChanged
OnSlotActiveStateChanged
```

Do not add redundant events if an existing delegate provides the same information.

Native behavior must work without Blueprint implementations.

---

# PerceptionKnowledge integration

The Item Slot must integrate with the existing `PerceptionKnowledge` system using its normal observable capability.

Do not use Smart Object state as a replacement for perception state.

Smart Object and PerceptionKnowledge remain separate capabilities.

Expose meaningful semantic state through the existing PerceptionKnowledge model when appropriate.

Potential concepts:

```text
Slot Active / Inactive
Slot Occupied / Empty
Slot Locked / Removable
```

Use the project's actual Gameplay Tag taxonomy.

Potential semantic events:

```text
Item Inserted
Item Removed
Slot Activated
Slot Deactivated
```

Do not create requester-relative perception state such as:

```text
Slot.AcceptsMyCurrentItem
```

because compatibility depends on the observing/requesting Character's inventory.

---

# WorldState integration

Both:

```text
AParadoxInsertablePickupableActor
AParadoxItemSlotActor
```

must participate in the existing `WorldState` plugin through the established project integration.

Do not create another reset/snapshot system.

---

# WorldState baseline requirements

An Item Slot baseline may be:

```text
Empty
```

or:

```text
Occupied by a specific Insertable Item
```

Support both designs where compatible with the existing WorldState architecture.

The reset must restore the logical relationship, not only Transforms.

Example baseline:

```text
Slot_A.InsertedItem = Battery_A
Battery_A.CurrentItemSlot = Slot_A
Battery_A attached/aligned to Slot_A.InsertAnchor
Battery_A not held by Character
```

Another baseline:

```text
Slot_A empty
Battery_A free in world
```

After WorldState reset the complete state must be coherent across:

```text
Character Inventory
Item CurrentHolder
Item CurrentItemSlot
Slot InsertedItem
Item transform / attachment
interaction availability
PerceptionKnowledge state
Puzzle output
```

Inspect the actual WorldState lifecycle and dependency ordering.

If it provides pre-restore/post-restore hooks or dependency ordering, use them.

Do not invent another reset coordinator unless absolutely required.

---

# Reset safety

World reset may occur while:

```text
item is held
item is inserted
Insert Action is executing
Pickup-from-slot Action is executing
slot becomes active/inactive
clone is interacting
```

No stale callback may reapply a pre-reset transition after reset.

All ownership references must be consistent after restoration.

---

# PART 3 — Puzzle Item Slot

## Class

Create:

```text
AParadoxPuzzleItemSlotActor
    : AParadoxItemSlotActor
```

This class adds PuzzleSystem integration.

Do not reimplement Selection, Interaction, Smart Object, WorldState, PerceptionKnowledge, Insert, Pickup, locking, compatibility, InsertAnchor, or ownership.

All of those come from the generic Item Slot.

---

# Puzzle Emitter capability

The Puzzle Item Slot must own:

```text
UPuzzleEmitterComponent
```

using the actual PuzzleSystem API.

Expose a designer-configurable:

```text
OutputSignalTag
```

following the existing PuzzleSystem conventions.

The default output represents whether the Item Slot currently satisfies its puzzle condition.

---

# Default puzzle output

Use an overridable evaluation function equivalent to:

```text
EvaluatePuzzleOutput()
```

Default semantic result:

```text
IsSlotActive()
AND
HasInsertedItem()
```

Because incompatible items cannot successfully enter the Slot, an occupied valid Slot is normally sufficient for the inserted-item portion of the condition.

Keep the evaluation overridable for future cases such as:

```text
battery inserted AND battery charge > 0
card inserted AND card still valid
fuse inserted AND fuse intact
```

Do not require subclasses to publish signals manually.

Use a stable native refresh path equivalent to:

```text
RefreshPuzzleOutput()
    ↓
EvaluatePuzzleOutput()
    ↓
UPuzzleEmitterComponent.SetSignalState(...)
```

---

# PuzzleSystem event-driven refresh

Do not use Tick.

Refresh puzzle output when relevant state changes.

At minimum:

```text
item inserted
item removed
slot active state changes
WorldState restore completes
relevant inserted-item state explicitly changes
```

If future dynamic Insertable state can affect `EvaluatePuzzleOutput()`, require an explicit event/notification path.

Do not poll the Item every frame.

---

# Operational Puzzle input — investigate Receiver/Gate architecture

A future Puzzle Item Slot may itself need to be active only when another puzzle condition is satisfied.

Example:

```text
Pressure Plate powers Card Reader
```

The Card Reader then:

```text
cannot accept/remove cards while unpowered
and
must not emit CardAccepted while unpowered
```

Codex must inspect the actual PuzzleSystem implementation before deciding how this operational state should be integrated.

---

# Mandatory PuzzleSystem investigation

Inspect:

```text
UPuzzleEmitterComponent
UPuzzleReceiverComponent
APuzzleController
current gate implementation
current gate runtime state
gate-related delegates
Controller output delegates
Receiver state delegates
any existing helper APIs for enabled/disabled puzzle capabilities
```

Explicitly verify whether the codebase already provides hooks equivalent to:

```text
OnGatesActivated
OnGatesDeactivated
OnGateStateChanged
```

or a query that represents equivalent semantics.

Do not invent such APIs if they do not exist.

---

# Important architectural rule: Controller Gates are not automatically Slot Active state

The existing PuzzleSystem gate architecture must remain the source of truth.

If Controller gates are implemented as Controller-local conditions over input bindings, do not reinterpret them as global Emitter state.

Do not modify `UPuzzleEmitterComponent` merely so that an Emitter can "know its own gates" if gates actually belong to Controllers.

One Emitter signal may be accepted by one Controller and blocked by another.

Therefore:

```text
Controller Input Gate
```

is not necessarily the same concept as:

```text
AParadoxItemSlotActor::IsSlotActive()
```

Keep those semantics separate.

---

# Preferred integration when Slot availability is driven by PuzzleSystem

If the current PuzzleSystem does not already expose a semantically correct gate/activation hook for this Actor, prefer composing the Puzzle Item Slot as both:

```text
Puzzle Receiver
+
Puzzle Emitter
```

Conceptually:

```text
AParadoxPuzzleItemSlotActor
├── UPuzzleReceiverComponent   // operational activation / power
└── UPuzzleEmitterComponent    // slot result output
```

This follows the PuzzleSystem architecture where one Actor may own both Receiver and Emitter capabilities.

Do not bypass the Controller.

Example:

```text
Pressure Plate Emitter
        ↓
Puzzle Controller
        ↓
Card Reader Receiver
        ↓
Card Reader operational state
```

Separate output:

```text
Card inserted into active Card Reader
        ↓
Card Reader Emitter
        ↓
another Puzzle Controller
        ↓
Door Receiver
```

This preserves:

```text
Emitter -> Controller -> Receiver
```

for both directions.

---

# Puzzle Item Slot `IsSlotActive()`

The derived Puzzle Item Slot may override the generic Slot evaluation.

If a `UPuzzleReceiverComponent` is used for operational enable/power, the default Puzzle Slot implementation may conceptually become:

```text
IsSlotActive()
    -> Receiver effective active state
```

or:

```text
Base local active condition
AND
Receiver effective active state
```

depending on the most coherent integration with the existing base API.

Do not force this exact code shape before inspecting the real implementation.

The architectural requirement is:

```text
PuzzleSystem may drive the Item Slot operational state
without duplicating Puzzle Controller condition logic
```

and:

```text
IsSlotActive() remains overridable
```

so derived content can add local conditions such as:

```text
Receiver active
AND
not mechanically broken
```

---

# Receiver state delegates

If `UPuzzleReceiverComponent` already exposes effective-state notifications such as:

```text
OnReceiverActivated
OnReceiverDeactivated
OnReceiverStateChanged
```

bind to them.

Use those events to refresh:

```text
slot active state
Interaction availability
PerceptionKnowledge state
Puzzle output
presentation hooks
```

Do not poll Receiver state on Tick.

If the actual plugin provides a different public API, follow it.

---

# Gate-specific hook decision rule

When implementing operational Puzzle activation, use this priority:

```text
1. Reuse an existing PuzzleSystem hook if it semantically represents the Slot's global operational state.

2. Otherwise compose the Slot with UPuzzleReceiverComponent and drive IsSlotActive() from effective Receiver state.

3. Do not add Emitter knowledge of Controller-local gates merely for this feature.

4. Modify PuzzleSystem only if investigation proves a genuinely missing generic extension point, and only with the smallest architecture-preserving change.
```

Any PuzzleSystem modification must follow that plugin's own `CODEX` and `Docs` rules and must compile independently.

---

# Interaction / replay integration

Do not add custom manual recording to `IntentReplay`.

`Insert` must use the normal Interaction Action / Gameplay Action path already used by other interactions.

Pickup from Slot must reuse or derive from the existing Inventory Pickup Action.

The semantic action target is the Item Slot.

At execution time, all requester-relative state must be revalidated.

This includes:

```text
current Inventory item
Slot occupancy
Slot active state
compatibility
lock state
Pickup capacity
```

A clone replaying an old Insert must be allowed to fail if its current Inventory state no longer satisfies the action.

Do not force the world into the historical state merely to preserve replay.

---

# Smart Object integration

Use the existing Paradox Interaction + Smart Object architecture.

The Item Slot's Insert/Pickup affordances should use existing interaction slots/approach positions.

Do not use `InsertAnchor` as the Character interaction position.

These are different concepts:

```text
Smart Object Slot
    = where the Character stands to interact

InsertAnchor Arrow
    = where/orientation the pickupable is placed
```

Do not conflate them.

---

# Debugging

Follow root and module debug rules.

No always-on debug.

Make the following state inspectable when debug is enabled.

## Insertable Pickupable

```text
Actor name
InsertableTraits
CurrentHolder
CurrentItemSlot
World / Held / Inserted semantic state
WorldState participation
```

## Generic Item Slot

```text
Actor name
IsSlotActive result
InsertedItem
bLockInsertedItem
AcceptedItemQuery
InsertAnchor transform
Selectable integration valid
Interaction integration valid
WorldState integration valid
PerceptionKnowledge integration valid
```

When a compatibility check fails, make it possible to determine whether:

```text
Item was null
Item was not insertable
Slot was inactive
Slot was occupied
Tag query failed
Additional validation failed
Requester did not own item
```

## Puzzle Item Slot

Additionally expose:

```text
Puzzle Emitter component
OutputSignalTag
EvaluatePuzzleOutput result
Puzzle Receiver component, if used
Receiver effective state
why IsSlotActive is false
```

Use module log categories/macros.

Do not introduce `LogTemp` in committed code.

---

# Validation scenarios

Validate at least:

1. compatible Insert succeeds and ownership becomes Slot-only;
2. incompatible Insert fails without mutation;
3. Insert with empty inventory fails safely;
4. Insert into occupied Slot fails safely;
5. Insert while Slot inactive fails safely;
6. two requesters with different inventories get different Insert availability;
7. held-item passive effects are removed exactly once on Insert;
8. unlocked occupied Slot allows Pickup into empty inventory;
9. locked occupied Slot does not offer ordinary Pickup;
10. lock never blocks reset/internal release;
11. Pickup with occupied requester inventory fails safely;
12. ordinary world Pickup still behaves exactly as before;
13. Slot pickup reuses, minimally extends, or derives from existing Pickup rather than duplicating it;
14. inserted Item aligns to `UArrowComponent InsertAnchor`;
15. empty WorldState baseline restores correctly;
16. occupied WorldState baseline restores the bidirectional Item/Slot relationship;
17. reset during Insert/Pickup produces no stale ownership callback;
18. Perception state updates event-driven;
19. Blueprint children can configure traits/query/anchor/lock without native subclasses;
20. active + occupied Puzzle Slot emits true;
21. inactive Puzzle Slot emits false even if an Item remains inserted;
22. Receiver-driven activation updates `IsSlotActive()` without Tick if Receiver composition is used;
23. powered Card Reader chain works through Emitter -> Controller -> Receiver on both input and output sides;
24. Controller-local gates remain Controller-local;
25. derived Slot can override additional compatibility and `IsSlotActive()` without reimplementing ownership transitions.

---

# Forbidden shortcuts

Do not:

```text
create a new plugin for this feature
create native Battery/Fuse/Key classes just for compatibility
hardcode compatibility using concrete class casts
create a second Inventory ownership system
allow Item to be held and inserted simultaneously
allow Slot and Item ownership references to disagree
use a generic SceneComponent instead of UArrowComponent for InsertAnchor
use InsertAnchor as the Smart Object interaction position
mutate Inventory before Insert validation completes
duplicate the existing Pickup Action without first inspecting/extending it
let bLockInsertedItem block WorldState/internal cleanup
let Blueprint mutate InsertedItem or CurrentItemSlot directly
create a new Selection system
create a new Interaction system
create a new Perception system
create a new WorldState/reset system
create a new Puzzle routing system
let Puzzle Slot activate Receivers directly
bypass APuzzleController
make an Emitter automatically know Controller-local gate state
invent OnGatesActivated / OnGatesDeactivated APIs without finding them in code
use Tick to monitor Slot active state
use Tick to monitor Receiver state
poll Item compatibility every frame
manually record Insert/Pickup in IntentReplay if existing Interaction Action integration already handles it
```

---

# Suggested implementation order

1. Read root `AGENTS.md`.
2. Read Paradox module `CODEX`.
3. Read Paradox module `Docs`.
4. Inspect current Inventory implementation and `AParadoxPickupableActor`.
5. Inspect existing Pickup/Swap Interaction Actions.
6. Inspect Interaction Action requester-relative validation.
7. Inspect Selection/Hover and Smart Object integration.
8. Inspect WorldState participant patterns.
9. Inspect PerceptionKnowledge integration.
10. Read PuzzleSystem `CODEX` and `Docs`.
11. Inspect `UPuzzleEmitterComponent`, `UPuzzleReceiverComponent`, `APuzzleController`, gates, and delegates.
12. Decide/document whether operational Puzzle activation uses an existing hook or Receiver composition.
13. Implement `AParadoxInsertablePickupableActor`.
14. Implement generic Item Slot ownership and compatibility.
15. Add `UArrowComponent InsertAnchor`.
16. Implement overridable `IsSlotActive()`.
17. Implement Insert Interaction Action using existing Interaction architecture.
18. Reuse/minimally extend/derive existing Pickup for Pickup-from-Slot.
19. Add `bLockInsertedItem`.
20. Integrate WorldState and PerceptionKnowledge.
21. Implement `AParadoxPuzzleItemSlotActor`.
22. Integrate Puzzle Emitter and, if needed, Puzzle Receiver.
23. Add event-driven refresh/debugging.
24. Update user-facing `Docs`.
25. Compile affected target and fix all introduced errors.
26. Validate scenarios above.
27. Review final diff and remove unrelated changes.

---

# Documentation requirements

Update the relevant Paradox module `Docs`.

Document at minimum:

```text
Insertable Pickupable purpose
InsertableTraits
Item ownership states
Item Slot purpose
AcceptedItemQuery
InsertAnchor Arrow
IsSlotActive()
Insert workflow
Pickup-from-slot workflow
bLockInsertedItem
WorldState behavior
PerceptionKnowledge behavior
Puzzle Item Slot
Puzzle input / Receiver behavior
Puzzle output / Emitter behavior
Controller gate caveat
Blueprint setup examples
debugging
```

If PuzzleSystem itself is modified, update its own `Docs` as required by `AGENTS.md`.

---

# Completion criteria

The task is complete only when:

- `AParadoxInsertablePickupableActor` extends the existing Pickupable architecture;
- compatibility is data-driven through Gameplay Tags / `FGameplayTagQuery`;
- `AParadoxItemSlotActor` is selectable, interactable, WorldState-aware, and PerceptionKnowledge-aware using existing systems;
- `InsertAnchor` is a `UArrowComponent`;
- every Item Slot owns at most one inserted item;
- inserted ownership is mutually consistent between Item and Slot;
- `IsSlotActive()` exists and is safely overridable;
- Insert requires active Slot + compatible Item in requester inventory;
- Insert is a validated atomic ownership transfer;
- an inserted item may be picked back up when `bLockInsertedItem == false`;
- locked inserted items cannot be removed through ordinary Pickup;
- existing Inventory Pickup behavior remains unchanged;
- Codex verified whether Pickup could be reused, minimally extended, or required a derived action;
- WorldState restores empty and occupied baselines correctly;
- PerceptionKnowledge state updates event-driven;
- Puzzle Item Slot owns a Puzzle Emitter;
- default Puzzle output is based on active Slot + inserted Item;
- Puzzle operational activation reuses an existing correct hook or uses Puzzle Receiver composition;
- Controller-local gates are not incorrectly treated as Emitter-global state;
- Puzzle flow remains `Emitter -> Controller -> Receiver`;
- no Tick is introduced for ordinary Slot state;
- Player, clone, and AI interaction paths remain requester-relative;
- relevant documentation is updated;
- the affected Unreal target compiles successfully.
