# Paradox Oxygen Canister Pickup — Codex Implementation Specification

## Purpose

Implement one concrete consumable inventory item for **Paradox**:

```text
Oxygen Canister
```

The canister is a normal pickupable/equippable item built on top of the **existing Paradox inventory / pickup / item-action architecture**.

Its gameplay purpose is:

```text
pick up Oxygen Canister
    ↓
store/equip it through the existing inventory system
    ↓
execute item action: Use
    ↓
restore a designer-configurable amount of oxygen time
    ↓
consume/remove the canister only after successful use
```

This task must **not** create a second inventory framework, a second interaction framework, or a special Player-only shortcut.

The canister must work through the same item and action APIs used by other inventory content so it remains compatible with:

```text
Player
Clones
GameplayActions
IntentReplay
HUD equipment actions
future item extensions
```

This document supplements:

- root `AGENTS.md`;
- relevant local `CODEX` instructions;
- the current Inventory/Pickup documentation;
- the current Health/Oxygen implementation/documentation.

The repository is the source of truth for actual type names and APIs.

Do not invent Unreal or project APIs.

---

# 1. Mandatory investigation before implementation

Before modifying code, Codex must:

1. read root `AGENTS.md`;
2. identify the module/plugin that owns the existing Inventory/Pickup system;
3. search that module/plugin and relevant subdirectories for `CODEX` folders and read all applicable instructions;
4. read its existing `Docs`;
5. inspect the current pickupable/item base type;
6. inspect how an item enters and leaves the single equipment/inventory slot;
7. inspect current Pickup / Swap / Drop behavior;
8. inspect the current representation of item-specific/special actions;
9. determine whether an item action equivalent to `Use` already exists;
10. inspect how item actions are exposed to the gameplay HUD;
11. inspect how item actions are represented through `GameplayActions`;
12. inspect how those actions are recorded/replayed by `IntentReplay`;
13. inspect how Clones own or reproduce inventory state/actions;
14. inspect the current `UParadoxOxygenComponent` public API;
15. verify the exact API used to restore oxygen time in seconds;
16. inspect reset/reconstruction behavior for inventory items and picked-up world Actors;
17. inspect logging/debug conventions;
18. compile the appropriate target before changes.

If an existing pattern already solves part of this task, extend it.

Do not create parallel abstractions with different names merely for the Oxygen Canister.

---

# 2. Scope

The task includes:

```text
one reusable Oxygen Canister pickup/item Actor or item type
designer-configurable oxygen restoration in seconds
integration with existing Pickup / Inventory / Equipment flow
item action of type Use
Use validation
restoring oxygen through UParadoxOxygenComponent
consuming the item after successful Use
GameplayActions / IntentReplay compatibility
HUD special-action compatibility
reset/reconstruction compatibility
Blueprint presentation hooks
debug/logging
documentation
tests / validation
compilation
```

The task does **not** include:

```text
new Inventory system
new Oxygen system
new Health system
new generic consumables framework unless the existing architecture clearly needs a minimal reusable extension
new HUD layout
art assets
3D model
oxygen VFX
oxygen audio
stackable inventory
multiple inventory slots
crafting
automatic pickup consumption
```

---

# 3. Architectural rule

The Oxygen Canister is an **inventory item**, not an Oxygen-system Actor.

Required dependency direction:

```text
Inventory Item / Oxygen Canister
        ↓ on successful Use
UParadoxOxygenComponent
```

The Oxygen component must know nothing about:

```text
pickup Actors
inventory slots
Use actions
canister meshes
consumption/destruction of items
```

Similarly, the generic Inventory system must not acquire hard-coded knowledge of Oxygen.

The Oxygen-specific behavior belongs to the concrete canister item or to the smallest existing item-effect extension point already provided by the Inventory system.

---

# 4. Base type

Do **not** invent a new independent pickup hierarchy.

The canister must derive from, contain, or configure the existing pickupable/item abstraction used by Paradox.

Conceptually:

```text
Existing Pickupable / Inventory Item base
    ↓
Oxygen Canister
```

Possible conceptual name:

```text
AParadoxOxygenCanister
```

but Codex must follow the repository's current naming pattern.

If the inventory system uses:

```text
Actor subclasses
item definition UObjects
Data Assets
components
strategy/action UObjects
```

use the existing architecture instead of forcing an Actor-subclass solution.

The goal is the gameplay asset, not a specific inheritance tree.

---

# 5. Designer-facing oxygen value

The canister restores oxygen in the same unit used by the Oxygen System:

```text
seconds
```

Expose one designer-facing value equivalent to:

```text
OxygenRestoreSeconds
```

Example:

```text
OxygenRestoreSeconds = 30
```

means:

```text
successful Use
-> add 30 seconds to the owning Character's remaining oxygen
```

Use the actual Oxygen API, conceptually equivalent to:

```text
RestoreOxygenSeconds(OxygenRestoreSeconds)
```

Do not convert the value into arbitrary oxygen points.

Do not duplicate oxygen capacity/current-state calculations inside the item.

The Oxygen component remains authoritative.

---

# 6. Pickup behavior

The Oxygen Canister must use the **normal existing Pickup path**.

Expected conceptual flow:

```text
Canister exists in world
    ↓
Player/Clone requests Pickup
    ↓
existing Pickup validation
    ↓
existing single-slot Inventory behavior
```

The canister must participate in whatever the project already does for:

```text
empty-slot pickup
swap when slot is occupied
drop
world representation while dropped
inventory ownership
replay/reset
```

Do not special-case collision-based auto-consumption.

Picking up the item does **not** restore oxygen.

Required behavior:

```text
Pickup != Use
```

The player can carry the canister and decide when to consume it.

---

# 7. Required item action: `Use`

The Oxygen Canister must expose an item/equipment action semantically equivalent to:

```text
Use
```

Codex must first determine whether the current action system already contains:

```text
Use
Item.Use
Inventory.Use
GameplayAction.Use
```

or an equivalent semantic action.

## If `Use` already exists

Reuse it.

Do not introduce:

```text
UseOxygenCanister
DrinkOxygen
ConsumeCanister
```

as a new top-level action type when the generic `Use` action already represents this behavior.

The concrete item decides what `Use` means.

## If no generic `Use` item action exists

Extend the existing item-action architecture with the smallest generic `Use` concept.

The action must remain useful for future items such as:

```text
medkit
battery
temporary consumable
tool with an immediate effect
```

Do not put Oxygen-specific behavior inside the generic `Use` action implementation.

Required separation:

```text
generic Use action
    ↓
asks currently equipped item to execute its Use behavior
    ↓
Oxygen Canister implementation restores oxygen
```

---

# 8. `Use` operates on the equipped/current item

The `Use` request must operate through the inventory's authoritative currently equipped/held item state.

Do not allow normal gameplay to use an arbitrary world canister by passing a raw Actor reference directly to Oxygen.

Conceptually:

```text
Character / Inventory
    ↓
current equipped item = Oxygen Canister
    ↓
Use action
    ↓
CanUse
    ↓
ExecuteUse
```

This keeps:

```text
Player
Clone replay
HUD
inventory ownership
item consumption
```

on the same path.

---

# 9. Use validation

The canister must validate whether it can currently be used.

At minimum, successful Use requires:

```text
item is currently owned/equipped according to Inventory rules
owning Character is valid
Character is alive / operational according to current Health architecture
Character owns a valid UParadoxOxygenComponent
OxygenRestoreSeconds > 0
oxygen is not already at its effective maximum duration/capacity
item has not already been consumed
```

Use the real Health/Character APIs rather than duplicating dead-state logic.

## Full oxygen

If oxygen is already full:

```text
Use fails
canister is NOT consumed
```

This prevents wasting the item accidentally.

## Partial restore

If:

```text
Remaining = 170 s
Maximum = 180 s
Canister = +30 s
```

the Oxygen component clamps normally:

```text
Remaining -> 180 s
```

The canister is still considered successfully used because it restored a positive amount.

Prefer using an Oxygen API/result that lets the caller know whether any oxygen was actually restored.

If the current Oxygen API cannot report this safely, Codex may minimally extend it with a result such as:

```text
bool RestoreOxygenSeconds(...)
```

or:

```text
float RestoreOxygenSeconds(...) // actual restored seconds
```

only if doing so is consistent with the existing public API.

Do not infer success only from intended input when the component can reject or clamp the operation.

---

# 10. Atomic use-and-consume semantics

The item must be consumed **only after the oxygen restoration succeeds**.

Required ordering:

```text
Use requested
    ↓
validate inventory ownership/current item
    ↓
validate Character/Oxygen state
    ↓
request oxygen restoration
    ↓
confirm restoration succeeded
    ↓
consume/remove item from Inventory
```

Forbidden ordering:

```text
remove item first
    ↓
try to restore oxygen
    ↓
restoration fails
    ↓
item already lost
```

The operation should behave transactionally from the player's point of view.

If restoration fails:

```text
Use fails
item remains equipped
```

---

# 11. Item consumption

After successful Use:

```text
Oxygen Canister is consumed
```

Meaning:

```text
it no longer occupies the equipment/inventory slot
it cannot be dropped afterward
it cannot be used again
its world/equipped representation is removed according to existing Inventory ownership rules
```

Use the Inventory system's existing remove/consume/destroy path if one exists.

Do not simply call:

```text
Destroy()
```

from the canister without first letting the authoritative Inventory system update its state.

The Inventory system must not retain a dangling reference to a destroyed item.

If the existing framework has no item-consumption API, add the smallest generic operation needed, conceptually equivalent to:

```text
ConsumeCurrentItem()
RemoveOwnedItem(...)
```

and make it reusable for future consumables.

---

# 12. Consume exactly once

Protect against:

```text
double input
duplicate replay request
reentrant callbacks
multiple UI clicks
network/event duplication if applicable later
```

Once successful Use begins/commits, the same canister cannot restore oxygen twice.

Use the existing action/inventory state guards where available.

Do not add a high-complexity transaction system for one item, but ensure one physical item produces at most one successful restoration.

---

# 13. GameplayActions integration

`Use` must be represented through the existing semantic gameplay-action system.

Conceptually:

```text
Gameplay Action: Use equipped item
```

The action should separate the normal phases already expected by `GameplayActions`:

```text
request
validation
execution
completion
failure
```

The action should **not** encode:

```text
OxygenRestoreSeconds
Oxygen Canister class
```

unless existing action payload architecture records concrete item identity/state in that manner.

The generic semantic request should remain approximately:

```text
Use current/equipped item
```

The equipped item owns its concrete result.

This allows future consumables to reuse the same action.

---

# 14. IntentReplay integration

The use of the canister must be recordable/replayable through the normal `IntentReplay` path.

The replayed semantic intention is:

```text
Use equipped item
```

not:

```text
set oxygen to recorded value
restore exactly what happened in the original run by writing runtime state directly
```

A Clone must execute the action against its **current runtime state**.

Example:

```text
Original Player run:
    oxygen = 40 s
    Use canister +30
    -> 70 s

Clone replay:
    oxygen = 55 s because the world/run evolved differently
    replays Use
    -> 85 s
```

This is correct.

Do not record Oxygen state changes as authoritative replay commands.

The canister/item action is the intention; Oxygen remains simulated runtime state.

---

# 15. Replay failure semantics

Because runtime state may differ, replayed `Use` may fail.

Examples:

```text
Clone no longer has the canister
Canister was previously consumed through a divergent sequence
Clone is dead
Clone has no valid Oxygen component
Oxygen is already full
```

In these cases:

```text
Use request fails through normal GameplayActions failure semantics
item must not be consumed when restoration did not happen
replay system handles the action failure according to its existing policy
```

Do not add a special "force oxygen canister use because it happened historically" rule.

---

# 16. HUD integration

The current gameplay HUD already exposes the equipment slot and item-specific/special actions.

The Oxygen Canister must advertise or provide:

```text
Use
```

through that existing item-action interface.

Expected UI concept:

```text
[ Oxygen Canister icon ]
        [Drop]
        [Use]
```

Codex must integrate with the current data/action-discovery mechanism.

Do not hard-code:

```text
if item is OxygenCanister -> show Use button
```

inside `UParadoxHUDWidget`.

The HUD should discover that the current item supports `Use` in the same generic manner intended for other special item actions.

If this generic discovery path does not yet exist but is already required by the HUD architecture, implement the smallest reusable version rather than an Oxygen-specific branch.

---

# 17. UI action state

The `Use` action should expose whether it is currently executable.

Examples:

```text
Oxygen not full
-> Use available

Oxygen full
-> Use unavailable / disabled

Character dead
-> Use unavailable

no valid Oxygen Component
-> Use unavailable
```

Follow existing HUD/action conventions for:

```text
hidden
visible but disabled
tooltip/failure reason
```

Do not create a new UI policy only for this item.

The gameplay action must still independently validate; UI state is not authoritative.

---

# 18. Blueprint/content workflow

The native implementation should provide a base that designers can turn into the actual game asset.

Expected designer responsibilities:

```text
mesh
materials
icon
display name
description
pickup VFX
pickup SFX
use VFX
use SFX
OxygenRestoreSeconds
```

Core C++/native logic should own:

```text
Use validation
oxygen restoration
successful-use state transition
inventory consumption
```

Do not require Blueprint to manually:

```text
find Oxygen Component
restore seconds
clear inventory slot
destroy the Actor
```

just to make the standard canister work.

---

# 19. Presentation hooks

Provide Blueprint/presentation hooks consistent with the existing item architecture.

Useful semantics include:

```text
OnCanisterUseSucceeded
OnCanisterUseFailed
```

or generic inherited hooks such as:

```text
OnUseSucceeded
OnUseFailed
```

Prefer the generic existing hooks if available.

The success hook may expose:

```text
requested restore seconds
actual restored seconds
using Character
```

when useful.

These hooks are intended for:

```text
audio
VFX
animation
HUD feedback
```

They must not own gameplay correctness.

---

# 20. World/reset lifecycle

The canister must participate in the project's existing reset/reconstruction model.

Codex must inspect how picked-up/dropped/consumed inventory Actors currently behave during timeline reset.

Required conceptual result:

```text
timeline baseline contains canister in world
Player picks it up
Player uses and consumes it
timeline resets
-> canister returns to the correct baseline state
```

Likewise, Clone reconstruction/replay must receive whatever inventory state the existing temporal architecture defines.

Do not implement a bespoke Oxygen-Canister respawn manager.

Use:

```text
existing WorldState participation
existing inventory snapshot/reset
existing item reconstruction
```

whichever is actually authoritative.

---

# 21. Dropped canister

Before successful Use, the item remains a normal inventory item and therefore should continue to support existing:

```text
Drop
Swap
Pickup
```

behavior.

Example:

```text
Player picks up canister
Player drops canister
-> canister returns to world through normal Drop path
-> can still be picked up later
```

After successful Use:

```text
item consumed
-> there is nothing to Drop
```

---

# 22. No automatic use on pickup

Hard requirement:

```text
Pickup does NOT restore oxygen.
```

Do not implement:

```text
overlap canister
-> immediately add oxygen
-> destroy canister
```

The intended gameplay is inventory-based and deliberate.

The sequence is:

```text
Pickup
then later
Use
```

---

# 23. No use after death

A dead Character cannot use the canister.

This should already be enforced by the existing Character/GameplayActions/inventory operational-state rules if they exist.

The item must not create a side path that allows:

```text
dead Clone
-> Use Oxygen Canister
-> restore oxygen
```

Oxygen restoration itself also does not revive Health.

---

# 24. Suggested semantic APIs

These names are conceptual only.

Codex must adapt them to the actual repository.

## Concrete item

```text
OxygenRestoreSeconds

CanUse(...)
Use(...)
```

or an override of existing generic item methods such as:

```text
CanExecuteItemAction(ActionType)
ExecuteItemAction(ActionType)
```

## Oxygen dependency

Conceptually:

```text
UParadoxOxygenComponent::RestoreOxygenSeconds(...)
```

Prefer a result that can distinguish:

```text
no restoration
partial restoration
full requested restoration
```

when the existing API allows it.

## Inventory consumption

Conceptually:

```text
ConsumeCurrentItem()
RemoveItem(...)
```

Use the existing authoritative API.

## Item action

Conceptually:

```text
Use
```

Prefer an existing:

```text
enum
GameplayTag
action UObject/type
```

rather than adding a new representation solely for this task.

---

# 25. Failure reasons

If the current GameplayActions/item-action architecture supports explicit failure reasons, expose useful generic reasons equivalent to:

```text
NoItemEquipped
ItemDoesNotSupportUse
InvalidOwner
OwnerNotOperational
MissingOxygenComponent
OxygenAlreadyFull
InvalidRestoreAmount
ItemAlreadyConsumed
RestoreFailed
```

Do not add a giant Oxygen-specific failure framework if the action system only uses a smaller result model.

Failures must remain observable and debuggable.

---

# 26. Debugging

Use existing module logging/debug conventions.

Useful debug information:

```text
canister Actor/item identity
owning Character
OxygenRestoreSeconds
current inventory/equipped status
whether Use is currently valid
failure reason
actual seconds restored on successful Use
consumption/removal result
```

Do not spam logs every frame.

Log meaningful events such as:

```text
successful Use
configuration error
failed item consumption after successful effect — this is an error/invariant violation
```

A successful oxygen restoration followed by failure to remove the item would risk duplication and must be treated seriously rather than silently ignored.

Design the sequence/API to make this failure path as difficult as possible.

---

# 27. Required behavior scenarios

Validate at least the following.

## 27.1 Pickup does not consume

```text
Canister +30 s in world
Player has 60 / 180 oxygen
Player picks up canister
-> oxygen remains 60
-> canister occupies inventory/equipment slot
```

## 27.2 Normal Use

```text
oxygen = 60 / 180
canister = +30
Use
-> oxygen = 90
-> canister removed/consumed
-> inventory slot becomes empty
```

## 27.3 Clamp at maximum

```text
oxygen = 170 / 180
canister = +30
Use
-> oxygen = 180
-> Use succeeds
-> canister consumed
```

## 27.4 Full oxygen

```text
oxygen = 180 / 180
Use
-> action rejected
-> oxygen remains 180
-> canister remains in inventory
```

## 27.5 Invalid configured restore

```text
OxygenRestoreSeconds <= 0
-> item configuration invalid
-> Use fails safely
-> item not consumed
```

## 27.6 Drop before use

```text
pick up
drop
-> no oxygen restored
-> item returns to normal world pickup state
```

## 27.7 Swap

Canister participates in existing single-slot Swap rules exactly like another pickupable.

## 27.8 Double Use request

Two rapid/reentrant Use requests against the same physical item:

```text
-> at most one oxygen restoration
-> at most one item consumption
```

## 27.9 Player gameplay action

Player activates HUD/item action `Use`:

```text
-> normal GameplayAction path
-> current canister used
```

## 27.10 Clone replay

Recorded Player action:

```text
Use equipped item
```

Clone replays it through `IntentReplay`.

If Clone still owns a usable canister:

```text
-> oxygen restored based on Clone's current oxygen state
-> its canister consumed
```

## 27.11 Divergent Clone state

Clone oxygen is already full when replay reaches `Use`:

```text
-> Use fails normally
-> item is not consumed
-> replay handles failure through existing policy
```

## 27.12 Dead Clone

Dead Clone cannot use the item.

## 27.13 Timeline reset

Canister consumed during one run:

```text
timeline reset
-> canister returns/reconstructs according to world baseline and existing reset architecture
```

No permanent disappearance caused by ordinary timeline rewind unless the project's persistence rules explicitly say otherwise.

## 27.14 HUD action discovery

When canister is equipped:

```text
HUD discovers Use through generic item-action mechanism
```

When another item does not support Use:

```text
HUD does not invent a Use action for it
```

---

# 28. Tests

Add automated tests where practical using the project's existing testing approach.

High-value tests:

```text
Use at partial oxygen -> restore + consume
Use at full oxygen -> reject + retain item
clamp at maximum
double-use protection
invalid restore value
missing Oxygen component
item consumption state consistency
```

Gameplay/world integration cases such as full IntentReplay and timeline reconstruction may be validated through the project's established integration/manual testing if automated coverage would require excessive unrelated infrastructure.

Document what was actually validated.

---

# 29. Documentation

Update the relevant Inventory/Pickup documentation and Oxygen integration documentation.

Document at least:

```text
what the Oxygen Canister is
how to create/configure a Blueprint child
OxygenRestoreSeconds
Pickup vs Use distinction
how Use is exposed
successful-use consumption
full-oxygen behavior
Drop / Swap behavior
GameplayActions / IntentReplay behavior
reset behavior
debugging
```

Do not put Codex-only instructions inside user-facing `Docs`.

---

# 30. Recommended implementation order

```text
1. read AGENTS / CODEX / Docs
2. inspect current inventory/pickup/item hierarchy
3. inspect current item special-action representation
4. inspect GameplayActions + IntentReplay path
5. inspect Oxygen restore API
6. decide smallest integration using existing abstractions
7. add/reuse generic Use action
8. add Oxygen Canister concrete item behavior
9. implement Use validation
10. implement oxygen restoration
11. implement atomic successful-use consumption
12. integrate HUD item-action discovery
13. verify Drop / Swap remain unchanged
14. verify GameplayActions integration
15. verify IntentReplay integration
16. verify reset/reconstruction
17. add presentation hooks
18. add tests/validation
19. update Docs
20. compile
21. review final diff
```

---

# 31. Forbidden shortcuts

Do not:

```text
restore oxygen on Pickup
create a second inventory slot for canisters
make the HUD directly call UParadoxOxygenComponent
hard-code Oxygen Canister class checks inside UParadoxHUDWidget
bypass GameplayActions for Use
bypass IntentReplay semantics
record direct oxygen state writes in replay
consume the item before restoration is confirmed
consume the item when oxygen is already full
allow one physical canister to restore oxygen multiple times
Destroy() the item without updating the authoritative Inventory state
create a Player-only Use implementation
create a Clone-only Use implementation
let dead Characters use the item
let Oxygen Component own inventory/item destruction
create a global Oxygen Canister manager
create a new generic consumables framework unless the current inventory architecture genuinely needs a minimal reusable extension
```

---

# 32. Definition of Done

The task is complete only when:

- the canister is implemented through the existing pickup/inventory architecture;
- designers can configure restoration in seconds;
- Pickup alone does not change oxygen;
- the item exposes the generic `Use` action;
- `Use` is available through the current item-action/HUD architecture;
- `Use` runs through GameplayActions;
- `IntentReplay` can record/replay the semantic use action;
- successful Use restores oxygen through `UParadoxOxygenComponent`;
- restoration is based on current runtime state, not recorded oxygen values;
- full oxygen rejects Use and preserves the item;
- successful Use consumes/removes the canister from the authoritative Inventory state;
- the same item cannot be successfully consumed twice;
- Drop and Swap continue to use existing behavior before consumption;
- timeline reset/reconstruction restores the item according to existing baseline rules;
- Player and Clone use the same implementation;
- documentation is updated;
- relevant tests/validation pass;
- affected target compiles successfully;
- final diff contains no unrelated changes.
