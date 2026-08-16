# Paradox Single-Slot Inventory System — Codex Specification

## Purpose

Implement a **very small, project-specific inventory system inside the Paradox gameplay module**.

This is **not a reusable plugin** and must not be moved into a new generic plugin.

The system exists to support the specific gameplay rules of Paradox:

- every controllable Character, including temporal clones, has exactly **one inventory slot**;
- world objects can be picked up through the already-existing Interaction System;
- swapping the currently held object with another world pickup is also an already-existing Interaction Action;
- dropping an object is a different action based on selecting a valid `GridWorld` cell;
- pickupable objects may expose active special actions;
- pickupable objects may apply passive effects while held;
- after a time-travel world reset, every Character inventory is emptied and all pickupable world objects return to their initial world state.

Do not redesign the existing Interaction System, Selection System, GridWorld, Gameplay Actions, Intent Replay, or WorldState plugin.

Before implementation, follow the root `AGENTS.md`, inspect all relevant `CODEX` instructions and `Docs`, and reuse the APIs and architectural patterns already present in the Paradox module and referenced plugins.

---

# Fixed architectural decisions

## 1. This belongs to the Paradox module

The inventory and pickupable gameplay described here are specific to Paradox.

Do not:

- create a new Inventory plugin;
- move Paradox-specific reset rules into a generic plugin;
- add Paradox-specific logic inside `GridWorld`, `WorldState`, `GameplayActions`, or the Interaction plugin/system;
- create a global inventory manager when ownership can remain on the Character and pickupable Actor.

Use the existing project module that owns Paradox-specific gameplay integration.

---

## 2. Every Character has exactly one slot

Player and clones use the same inventory capability.

Create or extend a Character-owned component equivalent to:

```text
UParadoxInventoryComponent
```

The exact name may adapt to existing module naming conventions.

The component owns exactly one authoritative equipped-item reference.

Conceptually:

```text
Character
└── UParadoxInventoryComponent
    └── EquippedPickupable : 0..1
```

Do not implement:

- inventory arrays;
- stacks;
- quantities;
- item categories;
- backpack capacity;
- multiple equipment slots;
- hotbars;
- weight limits.

Those are outside scope.

The component must expose controlled query/command APIs rather than public mutable state.

Conceptual queries/operations:

```text
HasItem()
GetEquippedItem()

CanEquip(Item)
Equip(Item)

CanUnequip()
Unequip()

ClearInventory()
```

Adapt names and signatures to existing project conventions after inspecting the codebase.

All transitions that change the held item must go through the authoritative inventory component so passive effects, events, ownership, and cleanup cannot become inconsistent.

No Tick is required.

---

# Standard pickupable Actor

Create a standard native Actor class for pickupable world objects.

Conceptually:

```text
AParadoxPickupableActor
```

Use the actual Paradox module naming convention if different.

This is the default native base designers should subclass in Blueprint for ordinary pickupable items.

## Mandatory integrations

The standard pickupable Actor must already be:

1. **Selectable**
2. **Interactable**
3. **Integrated with the WorldState plugin**

Do not require every Blueprint child to manually rebuild these capabilities.

### Selectable

Integrate with the selection/hover system already implemented in the Paradox module.

The pickupable Actor must participate through the existing selection capability/API used by other selectable gameplay Actors.

Do not invent a second outline, hover, or selection framework.

The Actor should therefore naturally support the project's existing behavior for selectable objects, including whatever hover/selection presentation and query hooks are already defined by that system.

Inspect the current implementation and use its established interface/component/base-class pattern.

### Interactable

Integrate with the existing Paradox Interaction System.

The Actor must be a valid interaction target through the same capability/API already used by other interactable Actors.

Do not recreate:

- interaction target validation;
- adjacency rules already owned by the Interaction System;
- interaction execution infrastructure;
- interaction UI infrastructure;
- replay integration already provided for Interaction Actions.

`Pickup` and `Swap` are Interaction Actions already well-defined in the project.

Treat them as actions of the existing Interaction Action type and only implement their inventory-specific behavior.

### WorldState

The standard pickupable Actor must participate in the existing `WorldState` plugin using the integration pattern already established in the project.

The required restored baseline includes at least the object's initial world state needed to guarantee:

```text
after time-travel reset:
    item is not held
    item has no stale holder
    item is back at its initial world position/state
```

Do not create a second snapshot/reset system inside the inventory feature.

Do not duplicate the WorldState plugin's responsibilities.

Inspect how current Paradox gameplay Actors register, capture baseline state, and restore state, then follow that pattern.

---

# Pickupable runtime capability

The standard Actor may directly own the pickupable state or use a dedicated Actor Component if that matches existing Paradox architecture better.

Prefer composition when practical.

Conceptually the pickupable capability needs:

```text
CurrentHolder
Held / World state
PickupableActions
PassiveEffects
```

The authoritative holder relationship must be consistent in both directions:

```text
Character Inventory -> Item
Item -> CurrentHolder
```

Do not allow one item to be held by two Characters or one Character to authoritatively hold two items.

All public operations must preserve this invariant.

---

# Pickup interaction

`Pickup` is an **existing Interaction Action type**.

Do not redefine what an Interaction Action is.

Do not build a parallel interaction execution path.

Required inventory semantics:

```text
Interaction Action: Pickup

Target:
    pickupable Actor in the world

Required inventory state:
    Character has a valid inventory component
    inventory slot is empty
    pickupable is currently available

Success:
    pickupable leaves its world-available state
    inventory slot becomes the authoritative holder
    pickupable records the Character as CurrentHolder
    pickup passive effects are applied
    normal pickup notifications/hooks fire
```

Use the Interaction System for all ordinary interaction validation already owned by that system.

The inventory feature should validate only inventory/pickup-specific requirements.

---

# Swap interaction

`Swap` is also an **existing Interaction Action type**.

Do not redefine the Interaction Action framework.

Required semantics:

```text
Character currently holds Item A
Character interacts with world Item B

Result:
    Item B becomes equipped
    Item A is placed into the world position/cell previously occupied by Item B
```

The swap must be atomic from the inventory system's point of view.

Conceptual order:

```text
validate complete swap first
remove Item A passive effects
release Item A from inventory
acquire Item B
place Item A in Item B's former valid world location
apply Item B passive effects
broadcast final state changes
```

Avoid exposing an intermediate invalid state to unrelated gameplay systems when practical.

If the existing Interaction Action lifecycle offers a safer transactional pattern, use it.

Do not implement swap by calling arbitrary Blueprint events in an order that can leave both items held, neither item valid, or passive effects duplicated.

---

# Drop action

Drop is **not** an Interaction Action against another Actor.

It is a separate gameplay action that targets a `GridWorld` cell.

Use the existing Gameplay Action architecture and naming conventions.

Conceptually:

```text
Paradox Drop Action
```

Do not create a new generic action framework.

The drop action must work for both the Player and clones through the same execution path used by the project's action/replay architecture where applicable.

---

# Drop targeting mode

Requesting Drop from UI begins a temporary cell-targeting state.

The system must reuse the **existing GridWorld runtime cell renderer**.

Do not create a second grid renderer, debug grid, decal grid, or bespoke world-space tile system.

The renderer should visually expose valid drop cells using its existing state/highlight capabilities.

A valid drop target cell must at minimum be:

```text
free for the pickupable placement
and
usable according to the relevant GridWorld occupancy/navigation rules
```

Prefer filtering out cells that cannot be completed because the Character has no reachable adjacent execution cell.

Use existing GridWorld queries rather than duplicating occupancy/pathfinding logic.

The targeting state must have clear enter/cancel/confirm cleanup so grid highlights do not remain active after:

- successful selection;
- cancellation;
- failed action;
- world reset;
- owner destruction.

---

# Drop execution when already adjacent

If the selected cell is already adjacent to the Character:

```text
select target cell
    ↓
revalidate target cell
    ↓
place equipped item in target cell
    ↓
remove passive effects
    ↓
clear inventory slot
    ↓
clear CurrentHolder
    ↓
finish action
```

Use the project's actual definition of grid adjacency.

Do not invent a second adjacency metric.

---

# Drop execution when target is distant

If the selected drop cell is not adjacent to the Character:

1. query the cells adjacent to the selected target cell;
2. discard invalid/unreachable candidates;
3. determine the best reachable adjacent cell;
4. move the Character there using the existing GridWorld movement/path-following architecture;
5. on arrival, revalidate the originally selected drop cell;
6. if still valid, perform the drop;
7. otherwise fail/cancel the action predictably.

The best adjacent execution cell should be selected by **GridWorld path cost/reachability**, not by naive straight-line world distance.

Conceptually:

```text
Selected Drop Cell
    ├── Adjacent A -> reachable, path cost 12
    ├── Adjacent B -> blocked
    ├── Adjacent C -> reachable, path cost 7
    └── Adjacent D -> reachable, path cost 10

Choose C
```

Reuse the existing Character/GridWorld movement action/path-following flow.

Do not teleport the Character.

Do not create a separate movement implementation inside inventory code.

---

# Drop revalidation

The selected target cell is the semantic target of the Drop Action.

If the target becomes invalid before execution completes, for example because another Actor occupies it while the Character is moving:

```text
Drop Action fails
```

Do not silently select a different drop destination.

This is important for deterministic gameplay and replay semantics.

Expose useful failure information through the existing Gameplay Action result/failure conventions.

---

# Pickupable Actions

Some held objects expose one or more active special actions.

Call these:

```text
Pickupable Actions
```

Create an extensible representation equivalent to:

```text
UPickupableAction
```

Use the actual naming convention of the Paradox module.

The primary extension path should be Blueprint-friendly if existing Paradox architecture supports that pattern.

A Pickupable Action is **not** a second action framework.

It must integrate with or dispatch through the existing Gameplay Action architecture.

Before implementing this type, inspect how project Gameplay Actions are currently defined, instantiated, requested, validated, executed, interrupted, replayed, and exposed to Blueprint.

Use that existing architecture.

Conceptually a Pickupable Action should expose enough information/API for UI and gameplay to ask:

```text
DisplayName
Icon

CanExecute(Character)
RequestExecute(Character)
```

Optional visibility/enabled queries may be added only if they are useful for the native widget architecture.

Do not hardcode example actions such as Throw, Scan, Activate, etc. into the core.

Designers/programmers must be able to define those later without modifying the inventory component.

---

# Passive effects

Pickupable objects may also apply passive effects while held.

Examples include:

```text
slowing the Character
altering a gameplay capability
changing a gameplay query result
```

Do not hardcode movement speed or any other specific passive stat into the inventory core.

Create an extensible passive-effect abstraction equivalent to:

```text
UPickupablePassiveEffect
```

with semantics equivalent to:

```text
Apply(Character)
Remove(Character)
```

The native inventory flow must guarantee:

```text
Equip
    -> apply all passive effects exactly once

Unequip / Drop / Swap-out / ClearInventory / reset cleanup
    -> remove all passive effects exactly once
```

Passive-effect cleanup must not depend on Blueprint authors remembering to call it manually.

Do not expose mutable internal application state unless required by a concrete implementation.

---

# Inventory state transitions

Centralize inventory transitions.

The system must not contain separate ad-hoc logic for:

```text
Pickup
Swap
Drop
Reset
```

that independently mutates the equipped pointer.

Use a small set of authoritative internal operations so every path consistently performs:

```text
validation
old passive removal
holder cleanup
inventory reference change
new holder assignment
new passive application
state notifications
```

This is especially important for WorldState reset and temporal clone behavior.

---

# Events and Blueprint hooks

Expose a minimal useful set of native/Blueprint-observable notifications.

Conceptually on the inventory component:

```text
OnEquippedItemChanged
OnItemEquipped
OnItemUnequipped
```

Use the smallest set that fits existing module conventions and avoid redundant events.

Conceptually on the pickupable Actor/capability:

```text
OnPickedUp
OnDropped
OnReturnedToInitialState
```

These hooks are primarily for presentation or Actor-specific behavior such as:

```text
mesh visibility
attachment
animation
audio
VFX
```

The native system must remain functionally correct even when no Blueprint hook is implemented.

Do not make Blueprint responsible for maintaining authoritative inventory state.

---

# Physical/presentation behavior

Do not hardcode one mandatory visual representation of a held item into the inventory core.

A pickupable Blueprint child may decide whether a held item is:

```text
attached to a socket
hidden while held
shown in the Character's hand
represented by another mesh
```

The standard native Actor should provide safe transition hooks, while the authoritative gameplay state remains native.

Collision/navigation behavior while held must be made consistent with existing project conventions.

Do not leave a held item occupying its old GridWorld cell.

---

# World reset and time travel

After a time-travel reset:

```text
every Player/Clone inventory slot is empty
every held-item passive effect is removed
every pickupable has no CurrentHolder
every pickupable returns to its initial WorldState baseline
```

Coordinate this with the existing WorldState reset lifecycle.

The required conceptual ordering is:

```text
1. inventory ownership cleanup
2. passive-effect cleanup
3. held-item detach/presentation cleanup
4. WorldState restore of pickupable world state
```

However, inspect the actual WorldState plugin lifecycle before implementing.

If the plugin already provides pre-reset/post-reset participant phases or dependency ordering, use those instead of inventing another reset coordinator.

The end-state invariant is more important than forcing a particular callback name.

Do not leave stale references from Characters to pickupables or from pickupables to old clone instances after reset.

---

# Player and clone parity

Clones must use the same inventory implementation as the Player.

Do not create:

```text
PlayerInventory
CloneInventory
```

as different systems.

Both must expose the same one-slot capability and execute the same inventory operations.

A clone may:

```text
Pickup
Swap
Drop
execute Pickupable Actions
receive Passive Effects
```

subject to the same validation rules as the Player.

Differences between player input and clone replay belong to the existing action/input/replay architecture, not the inventory component.

---

# Intent Replay compatibility

Do not add frame-by-frame inventory recording.

Use the semantic Gameplay Action / Interaction Action flow already established by the project.

Pickup and Swap inherit the existing Interaction Action recording/execution semantics.

Drop and Pickupable Actions must integrate with the existing action/replay architecture in the same semantic manner as comparable gameplay actions already implemented in the project.

Before changing `IntentReplay`, verify whether the existing Gameplay Action abstraction already provides everything needed.

Make the smallest necessary integration.

---

# Native widgets

Provide native widget bases that make inventory interaction simple for designer-authored Widget Blueprints.

Conceptually:

```text
UParadoxInventoryWidget
```

The widget should bind to or receive the relevant Character inventory component and expose convenient protected/public Blueprint APIs equivalent to:

```text
GetInventoryComponent()
GetEquippedPickupable()
HasEquippedItem()

GetPickupableActions()

CanDrop()
RequestDrop()

CanExecutePickupableAction(Action)
RequestPickupableAction(Action)
```

The widget must not mutate the equipped-item pointer directly.

Example intended Blueprint flow:

```text
Drop Button
    -> RequestDrop()
    -> Drop targeting mode begins
```

and:

```text
Special Action Button
    -> RequestPickupableAction(Action)
```

Provide a native action-entry/button widget base only if it materially reduces repeated Blueprint glue.

Conceptually:

```text
UPickupableActionWidget
```

It may receive one action definition/reference and expose presentation/execution helpers.

Do not hardcode the final visual style in native C++.

Native code owns safe access and request APIs; Widget Blueprints own presentation/layout.

---

# Suggested ownership structure

Conceptually:

```text
Character
└── UParadoxInventoryComponent
    └── EquippedPickupable

AParadoxPickupableActor
├── existing Selectable integration
├── existing Interactable integration
├── existing WorldState integration
├── Pickupable capability/state
├── PickupableActions[]
└── PassiveEffects[]
```

Do not force this exact component count if existing Paradox base classes already provide some of these capabilities.

Prefer reusing the established Actor/component hierarchy over duplicating components.

---

# Public API safety

Inventory operations must fail predictably.

Handle at least:

```text
null pickupable
pickupable already held
Character slot already occupied
attempt to drop with empty inventory
invalid drop cell
no reachable adjacent execution cell
target cell becomes invalid while moving
Character destroyed during an action
pickupable destroyed while held
world reset during targeting/action execution
swap target becomes invalid during the interaction
```

Use existing result/failure enums and logging conventions where possible.

Do not use fatal assertions for recoverable gameplay failures.

---

# Debugging

Follow the Paradox module's existing global + local debug rules.

Make inventory state inspectable without adding always-on visualization.

Useful debug state includes:

## Inventory component

```text
owner Character
equipped item
current passive effects
active inventory-related action
```

## Pickupable

```text
Actor name
world/held state
CurrentHolder
number/list of Pickupable Actions
number/list of Passive Effects
WorldState participation
Selectable capability valid
Interactable capability valid
```

## Drop targeting/action

When relevant:

```text
selected target cell
target validity
candidate adjacent execution cells
reachability/path cost
chosen execution cell
movement/action state
failure reason
```

Prefer the existing GridWorld visualization for spatial data.

Do not create an overlapping visualization system.

---

# Documentation

Update/create the appropriate human-facing documentation in the Paradox module `Docs` folder.

Document at least:

```text
purpose
single-slot rule
how to make a pickupable Blueprint from the standard Actor
Selectable integration
Interactable integration
WorldState/reset behavior
Pickup and Swap Interaction Actions
Drop workflow
Pickupable Actions
Passive Effects
native widget usage
Player/clone behavior
common failure cases
```

Do not put Codex-specific workflow rules in `Docs`.

---

# Validation scenarios

Validate at least the following.

## 1. Basic pickup

```text
empty Character inventory
interact with pickupable
-> item held
-> slot occupied
-> CurrentHolder correct
-> passive effects applied once
```

## 2. Pickup while slot occupied

Ordinary Pickup must fail unless the requested interaction is explicitly Swap.

## 3. Swap

```text
Character holds A
interacts with B using Swap
-> Character holds B
-> A occupies B's former valid world location
-> A passives removed
-> B passives applied
-> no duplicated ownership
```

## 4. Basic adjacent drop

Select a valid adjacent free cell.

Item is dropped there and slot becomes empty.

## 5. Distant drop

Select a valid distant cell.

Character reaches the best reachable adjacent cell and then drops the item into the selected cell.

## 6. Distant drop target invalidates

The selected target cell becomes invalid during movement.

Drop fails without silently choosing another target.

## 7. No reachable adjacent execution cell

The target must be rejected or the action must fail predictably.

## 8. Drop targeting cancellation

Highlights and temporary targeting state are completely cleaned up.

## 9. Passive effect lifecycle

Passive effect applies once on equip and is removed once on every path that removes the item.

## 10. Pickupable Action

Held item exposes an action that the inventory widget can query and request through existing Gameplay Action infrastructure.

## 11. Pickupable without special actions

The item remains fully valid; UI exposes no special-action buttons.

## 12. Pickupable without passive effects

The item remains fully valid.

## 13. Player and clone parity

Player and clone can independently hold one object each.

Neither can exceed one slot.

## 14. World reset

Before reset:

```text
Player holds A
Clone holds B
C is elsewhere after being dropped
```

After reset:

```text
Player slot empty
Clone slot empty
A/B/C have no holder
all passive effects removed
A/B/C restored to initial WorldState baseline
```

## 15. Reset during drop targeting

Temporary GridWorld highlighting and action state are cleared.

## 16. Reset while a Character is moving for distant drop

No stale completion callback may drop an item after reset.

## 17. Pickupable is selectable

The standard pickupable Actor participates correctly in existing hover/selection behavior without extra Blueprint wiring.

## 18. Pickupable is interactable

The standard pickupable Actor is recognized by the existing Interaction System and can host Pickup/Swap Interaction Actions without extra capability wiring.

## 19. Pickupable is a WorldState participant

Move/pickup/drop the standard Actor, trigger the established world reset, and verify it returns to its baseline state with no stale ownership.

## 20. Blueprint child with no custom hooks

Core pickup, swap, drop, reset, passive lifecycle, and inventory state remain correct even when the Blueprint child implements no optional presentation hooks.

---

# Forbidden shortcuts

Do not:

```text
create a generic inventory plugin
add more than one inventory slot
create an inventory array hidden behind a "capacity = 1" setting
build a parallel Interaction Action system
build a parallel Gameplay Action system
build a parallel selection/hover system
build a parallel WorldState/reset system
build a second GridWorld cell renderer
teleport the Character to perform distant Drop
pick the execution cell using only Euclidean distance
silently retarget a Drop when the selected cell becomes invalid
hardcode movement-speed slowdown into the inventory component
hardcode concrete Pickupable Actions into the inventory component
let Blueprint mutate authoritative equipped-item state
let Blueprint manually manage passive cleanup
use Tick for ordinary inventory, pickup, passive, or drop-targeting state
create separate Player and Clone inventory implementations
leave a held item occupying its old GridWorld cell
leave stale holder references after time travel/reset
require every pickupable Blueprint child to manually add Selectable integration
require every pickupable Blueprint child to manually add Interactable integration
require every pickupable Blueprint child to manually implement WorldState participation
```

---

# Implementation order

Use the existing codebase architecture as the source of truth for exact type names and APIs.

Suggested order:

1. inspect Paradox module `CODEX` and `Docs`;
2. inspect existing Character base used by both Player and clones;
3. inspect Selection/Hover architecture;
4. inspect Interaction System and existing Interaction Actions;
5. inspect Gameplay Action lifecycle;
6. inspect IntentReplay integration with semantic actions;
7. inspect GridWorld renderer, occupancy queries, adjacency queries, path cost, and Character movement flow;
8. inspect WorldState participant/reset patterns used by existing Paradox Actors;
9. implement single-slot inventory component;
10. implement standard pickupable Actor with built-in Selectable + Interactable + WorldState integrations;
11. implement pickupable authoritative ownership/state transitions;
12. implement Pickup Interaction Action behavior using the existing Interaction Action type;
13. implement Swap Interaction Action behavior using the existing Interaction Action type;
14. implement passive-effect abstraction/lifecycle;
15. implement Pickupable Action abstraction integrated with existing Gameplay Actions;
16. implement Drop targeting state using the existing GridWorld renderer;
17. implement Drop Gameplay Action and movement/revalidation flow;
18. integrate Player and clone Characters with the same inventory component;
19. integrate world-reset cleanup and restoration;
20. implement native widget APIs;
21. add debug inspection;
22. update Paradox module documentation;
23. compile the affected target;
24. validate the scenarios above;
25. review the final diff and remove unrelated changes.

---

# Completion criteria

The task is complete only when:

- Player and clones use the same one-slot inventory implementation;
- no Character can hold more than one pickupable;
- the standard native pickupable Actor is selectable through the existing Paradox selection system;
- the standard native pickupable Actor is interactable through the existing Interaction System;
- the standard native pickupable Actor participates in the existing WorldState plugin;
- Pickup uses the existing Interaction Action architecture;
- Swap uses the existing Interaction Action architecture;
- Drop uses the existing Gameplay Action + GridWorld architecture;
- distant Drop moves to the best reachable adjacent execution cell before dropping;
- the originally selected Drop cell is revalidated at execution time;
- Pickupable Actions use the existing Gameplay Action infrastructure rather than a parallel executor;
- passive effects apply/remove exactly once through centralized inventory transitions;
- native widget APIs can request Drop and Pickupable Actions without directly mutating inventory state;
- all inventory slots are cleared during the Paradox time-travel reset;
- all pickupable Actors return to their WorldState baseline after reset;
- no stale holder/passive/action/targeting state survives reset;
- relevant documentation is updated;
- the affected target compiles successfully.
