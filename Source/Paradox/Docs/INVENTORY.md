# Paradox single-slot inventory

## Purpose and ownership

Every `AParadoxCharacter` owns one native `UParadoxInventoryComponent`. The component is therefore
present on both `AParadoxPlayerCharacter` and `AParadoxCloneCharacter`; controllers and UI are only
adapters and never own inventory state. The component stores one private authoritative pickupable
reference and keeps it coherent with `AParadoxPickupableActor::CurrentHolder`.

Pickup and Swap resolve their requester from the Actor that owns the executing
`UGameplayActionComponent`, including during the pre-`OnActionInit` validation phase. The interaction
widget's Player Controller is only `RequestSource` metadata. On replay, a fresh action owned by the
clone therefore resolves the clone and its inherited inventory; it never falls back to the recorded
Player or the local controller. Target and interaction capability are likewise resolved from the
immutable semantic Property Bag during preflight, before runtime caches exist.

Insertable pickupables add a third gameplay state, `Inserted`, which is mutually exclusive with
World and Held. Transfers between the Character slot and an `AParadoxItemSlotActor` use the same
inventory-owned effect bookkeeping and preserve the `Inventory/CurrentHolder` and
`ItemSlot/CurrentItemSlot` invariants atomically. See
[Paradox insertable items and item slots](ITEM_SLOTS.md) for authoring and replay behavior.

Use `HasItem`, `GetEquippedItem`, `CanEquip`, and `CanUnequip` for Blueprint-safe queries. Native
Gameplay Actions call the validated `TryEquip`, `TrySwap`, and `TryDropAtTransform` transitions.
Recoverable failures return `FParadoxInventoryOperationResult` and a diagnostic instead of asserting.
`OnEquippedItemChanged(PreviousItem, NewItem)` publishes only completed transitions; Swap emits its
single final state and never exposes an empty intermediate slot. Reentrant transitions are rejected.

## Authoring a pickupable

Create an Actor Blueprint with `AParadoxPickupableActor` as parent, then configure its inherited
`PickupableMesh`. The native parent already includes:

- `UParadoxSelectableComponent` and `UParadoxInteractionComponent`;
- a `USmartObjectComponent` using `/Game/Data/Inventory/DA_ParadoxPickupableSmartObject`;
- a one-cell `UGridNavigationOccupancyComponent` used as the shared navigation-bounds authoring volume;
- a `UWorldStateParticipantComponent` for existence and transform restoration;
- the Pickup and Swap catalog entries backed by
  `/Game/Data/GameplayActions/DA_ParadoxPickupInteraction` and
  `/Game/Data/GameplayActions/DA_ParadoxSwapInteraction`.

The standard Smart Object has four cardinal slots at 100 cm. A Blueprint intended for another cell
size or footprint should assign a replacement Smart Object Definition and occupancy bounds. The
native occupancy publishes runtime cells only when **Enable Navigation Blocking** is active and the
item is in the world. A default traversable pickupable publishes no occupancy owner at all. This is
required because GridWorld's `Reserved Corridor` policy treats every active non-reservation
occupancy owner as a live agent even when `bBlocksWhenConsidered=false`; leaving a Key Card active
would therefore produce a valid preview but make execution wait and repeatedly repath around a
stationary occupant.

Pickupables default to non-physical and non-blocking. While the item is in `World` state,
the inherited `PickupableMesh` normally uses query-only collision, ignores every channel except
`Visibility`, and blocks `Visibility` solely so the shared cursor trace can resolve hover and
selection. It still ignores Pawns, emits no overlaps, has physics/gravity disabled, and uses
`CanEverAffectNavigation=false`, so it neither carves Unreal navigation nor prevents traversal.
Every other inherited or Blueprint-added primitive is forced to `NoCollision`, except the transient
world-space interaction `UWidgetComponent` owned by `UParadoxSelectableComponent`. That component
retains its `UI` query profile only while shown, so the virtual mouse can hover and click it; it
still ignores Pawns, emits no overlaps, has no physics, and cannot affect navigation. It returns to
`NoCollision` when selection hides it. Pickupable state normalization deliberately preserves this
selection-owned query state, including after Drop. The Static Mesh asset
must provide query geometry compatible with the controller's complex Visibility trace; for simple
authored collision, set Collision Complexity to `Use Simple Collision As Complex`. Do not use
pickupable collision or overlap events for gameplay unless the Actor explicitly opts into authored
world collision as described below.

Two Actor properties under `Paradox | Inventory | World Presence` opt into authored physical
presence without changing existing assets:

- **Enable Authored World Collision** defaults to disabled. When enabled, each primitive's initial
  Collision Enabled mode and channel responses are respected while the item is in `World` state.
  Query-capable authored primitives automatically enable overlap events, so a dropped item can drive
  overlap-based gameplay such as `APressurePlate`. Collision and overlaps are disabled again while
  Held and restored after Drop/reset. Insertable subclasses can configure the same behavior
  independently for their `Inserted` state.
- **Enable Navigation Blocking** defaults to disabled. When enabled, world primitives set
  `Can Ever Affect Navigation=true` and the inherited `GridNavigationModifierComponent` blocks the
  GridWorld cells covered by `OccupancyComponent`. The modifier mirrors that component's transform
  and `BoxExtent`, so `OccupancyComponent` remains the single authoring volume. Changing the flag or
  its bounds refreshes the GridWorld overlay in editor; picking up the item restores those cells and
  dropping it removes them again. This is the same runtime topology mechanism used by
  `AParadoxVerticalBarrier`, independent of the movement query filter's occupancy policy.

These base options apply only while the item is available in the world. Held and restore-pending
items still disable Actor/component collision, Unreal navigation relevance and active GridWorld
occupancy/modifier blocking. Insertable subclasses expose separate, default-off collision and
navigation options for their `Inserted` state.

World state enables the Visibility selection query, selection state and interaction state. Logical
occupancy is enabled only for navigation-blocking pickupables. Held and Inserted states clear
selection and interaction; their physical presence follows the state-specific flags, while Held
always disables it and hides the Actor by default. The native transition
remains authoritative; `On Picked Up`, `On Dropped`, and `On Returned To Initial State` are optional
presentation hooks. A Blueprint may show or attach the item from `On Picked Up` without replacing
ownership logic. Visibility authored before the first pickup is restored on Drop/reset; the native
code reapplies the query-only/non-physical/no-navigation contract after every presentation hook.

## Passive effects and item actions

Add instanced `UParadoxPickupablePassiveEffect` objects to `Passive Effects`. Override `Apply` and
`Remove` in Blueprint or C++. The inventory records only effects it actually applied, applies each
valid object once, and removes them once in reverse order when dropping, swapping, resetting, or
losing a destroyed item.

`UParadoxPickupableAction` is the optional UI descriptor for an equipped item's special action. Set
its display name, icon and a `UGameplayActionDefinition`. The definition should derive from
`UParadoxPickupableGameplayActionDefinition` so its Property Bag contains the replay-safe soft
`Pickupable` parameter. Its instance should derive from `UParadoxPickupableGameplayActionBase` and
override `CanExecutePickupableAction` and `ExecutePickupableAction`. Submission always uses the
Character's ordinary Gameplay Actions component and is therefore journalled and replayable.

Empty effect/action arrays are valid. A missing special-action Definition fails with a diagnostic.

Set `PickupableDisplayName` and the soft `PickupableIcon` on each pickupable for native HUD
presentation. `SetPickupableActions` validates and deduplicates a runtime replacement catalog, then
publishes `OnPickupableActionsChanged`; native subclasses changing authored action state directly
can call the protected `NotifyPickupableActionsChanged` hook. The HUD therefore refreshes without a
polling Tick.

Inventory interaction diagnostics distinguish an invalid requester class, a missing inventory
component, and a non-pickupable target. This makes a rejected preflight actionable without treating
all three configuration failures as the same error.

## Drop targeting and execution

`UParadoxInventoryWidget` is a complete native Equipment widget and still supports an authored
Blueprint replacement. It shows an empty/equipped state, item icon/name, Drop, and one dynamic
button per pickupable action. It requires an explicit `SetInventoryCharacter` binding. The Character is the
inventory data source and the subject of every requested item action; `GetOwningPlayer()` identifies
only the local widget/input owner and is never used to infer inventory state. This distinction lets
one widget instance observe either the current Player or any clone and safely rebind at runtime.
After `Create Widget`, call `Set Inventory Character` with the Character being presented; passing
`None` unbinds the previous inventory. The widget exposes the current item/actions,
`RequestPickupableAction`, and `RequestDrop`. Inventory, item-catalog, Drop Targeting and Gameplay
Action lifecycle delegates update content and enabled states without Tick. Designers may replace the
native layout in one Widget Blueprint; action entries use the configurable
`ActionButtonWidgetClass` and require no per-action asset by default.

Blueprint replacements require `EquipmentStateSwitcher`, `EmptySlotIcon`, `EquippedItemIcon`,
`DropButton`, and `SpecialActionsContainer` with those exact variable names. Page 0 of the switcher
is Empty and page 1 is Equipped. `DropButton` must derive from `UCommonButtonBase`.
`EquippedItemName` is an optional `UCommonTextBlock`: icon-only layouts may omit it entirely.

Likewise, a Blueprint derived from `UParadoxInventoryActionButtonWidget` requires `ActionButton`,
`ActionIcon`, and `ActionLabel`. Use a Common Button-derived class for `ActionButton` and
`UCommonTextBlock` for `ActionLabel`. Missing or incorrectly typed variables intentionally fail
Widget Blueprint compilation instead of silently disabling part of the inventory UI.

The Gameplay HUD binds this widget automatically to the currently possessed Paradox Character. A
standalone inventory screen must still call `SetInventoryCharacter` explicitly. See
[Paradox Gameplay HUD](GAMEPLAY_HUD.md) for the persistent root and collapsed-mode contract.

`RequestDrop` passes the explicitly bound Character to the controller-owned
`UParadoxDropTargetingComponent`. The controller remains the local input/presentation owner, while
the targeting session captures the independent source Character and its equipped item. All cell
evaluation, confirmation and action submission use that captured Character; changing inventory or
destroying either source while targeting cancels and cleans the session instead of falling back to
the controller Pawn. The component reuses both the existing `UGridCellPointerComponent` and the
controller's single `UGridPathPreviewComponent`, including its cell/line renderers, styles, cache,
priority and automatic renderer enablement. LMB confirms only a valid, committable target; clicking
an invalid cell, empty screen space, or a target without a complete path keeps the session active.
RMB and the keyboard cancel action always terminate Drop Mode and clear the path and item preview,
regardless of the current hit. A valid confirmation ends targeting even if action submission is
subsequently rejected. During Tactical Pause the request uses the source Player's
`SubmitOrReplaceNextAction`; a clone has no player-only planning adapter and is rejected
diagnostically rather than redirecting the Drop to the possessed Pawn. Outside Tactical Pause the
request is submitted to the explicitly bound Character's normal Gameplay Actions component.

For each pointed cell the targeting component performs only constant-time cell availability checks
and one cached path-preview request. The `StopBeforeRequestedGoal` terminal policy queries the
complete path to the pointed Drop cell once, displays the prefix ending at its immediate ordinary
predecessor, and exports that prefix as an exact `FGridInjectedPath`. It does not scan the grid or
evaluate every neighbor. Partial paths, paths shorter than the required start/target pair, and paths
whose final segment is not an ordinary GridWorld edge cannot be committed.

The final Drop cell shows one transient preview of the equipped item's actual `PickupableMesh`.
`DropPreviewMaterial` on `UParadoxDropTargetingComponent` optionally replaces every material slot;
when unset, the item's authored materials are retained. Preview and authoritative placement both
use `GetDropPlacementTransform`, so rotation, offset and scale agree. The preview has no collision,
overlap, physics, shadows, or navigation influence and is destroyed on every targeting exit path.
Its single mesh component is hosted by a transient, non-replicated visual Actor because Unreal
Player Controllers are hidden Actors and would suppress any primitive component they own. The
transient Actor is presentation-only and never participates in Drop authority or replay.

`/Game/Data/GameplayActions/DA_ParadoxDrop` carries the semantic Drop `TargetCell`, `PathSource`,
and exact `InjectedPath`. `UParadoxDropAction` moves only to the recorded approach cell using
`UGridMoveToCellExecution`, `RejectOccupied`, and `RecalculateToOriginalGoal`; it drops immediately
when already on that approach cell. Replay first attempts the recorded path and may use GridWorld's
normal fallback only toward the same recorded approach cell. The clone runtime copy tolerates
transient dynamic-agent traffic revisions while preserving that exact approach, including after an
`InvalidStart` recovery; static topology and ordinary occupancy validation remain strict. It never
recalculates toward the Drop cell and never chooses another neighbor. At arrival the action
revalidates the same item, exact
selected cell, current adjacency and availability. A newly occupied Drop cell fails without
retargeting. Pause, resume, cancel, interruption, time-loop abort, and teardown propagate to the
movement executor; stale completions are ignored.

## World State reset and lifetime

At `OnRestoreStartedNative`, inventories remove effects, clear holder/attachment state and put held
pickupables in `RestorePending`. Inventory transitions remain blocked until World State reports
completion or failure. World State restores existence and transform; pickupables then restore their
world capabilities and the successful-baseline presentation hook. Gameplay Action abort remains the
time loop's existing responsibility.

Destroying an equipped item clears its inventory reference and passives. Character teardown uses
the same idempotent cleanup path.

## Diagnostics

All committed diagnostics use `LogParadox`. Runtime inventory debug requires both gates:

```text
Paradox.Inventory.Debug 1
```

and the local `bEnableDebug` flag on the relevant inventory, pickupable, Drop action, or targeting
component. Both default off. Drop paths use the shared GridWorld path presentation rather than a
separate all-cells overlay or custom debug geometry.

Run the focused scenarios with:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests Paradox.Inventory; Quit" -TestExit="Automation Test Queue Empty" -log
```
