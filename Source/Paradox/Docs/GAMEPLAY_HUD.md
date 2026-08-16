# Paradox Gameplay HUD

## Ownership and lifetime

`AParadoxPlayerController` owns one native `UParadoxGameplayHUDComponent`. For a local controller,
the component creates a single root `UParadoxGameplayHUDWidget`, adds it to the viewport, uses the
root-authored Tactical Pause and Equipment controls, and removes the root symmetrically at
`EndPlay`. Remote
controllers do not create UI.

The component is the authority for root visibility, section visibility, and presentation mode.
Widgets observe or request those states; they do not duplicate them. The native classes provide a
working C++ fallback, so a Blueprint asset is optional.

## Normal and Collapsed modes

The root uses `HUDModeSwitcher` with a fixed page contract:

- page `0`: `Normal`;
- page `1`: `Collapsed`.

`Tab` toggles the mode on the local Player Controller, including while Tactical Pause is active.
The key is exposed as `ToggleHUDModeKey`. Input is consumed when handled and is ignored while the
root HUD is hidden. A temporary hide does not reset the selected mode.

The native Collapsed page is intentionally empty and hit-test-invisible. A derived Widget Blueprint
may put any desired compact presentation in page 1. Switching pages never destroys section widgets;
Tactical Pause and Inventory keep receiving event-driven updates while collapsed.

`OnModeChanged(PreviousMode, NewMode)` runs after the native switcher index has changed. Override it
for presentation only. Request state changes through `SetHUDMode` or `ToggleHUDMode` on the
coordinator.

## Authoring a root Widget Blueprint

Derive from `UParadoxGameplayHUDWidget`. The native fallback is used when no Blueprint class is
assigned. For an authored hierarchy, provide these optional bindings with the exact names:

- `HUDModeSwitcher`, with at least two pages;
- `TacticalPauseContainer`, under Normal page 0;
- `EquipmentContainer`, under Normal page 0;
- `StatusContainer`, under Normal page 0;
- `CollapsedModeContainer`, under Collapsed page 1.

The root owns the complete HUD composition. An authored root must contain exactly one
`UTacticalPauseControlsWidget` descendant and exactly one `UParadoxInventoryWidget` descendant.
The coordinator discovers those existing descendants, binds the Inventory widget to the possessed
Character, and never creates, reparents, or replaces section widgets. The native root fallback
constructs both descendants inside its own containers, resolving the Tactical Pause plugin's
configured default widget class, so it remains complete without a Paradox root Blueprint asset.
When a named section container is absent, section visibility is applied directly
to the matching embedded descendant.
The Collapsed container is never populated by C++; it belongs to the designer. A missing switcher or fewer than
two pages produces a `LogParadox` warning and leaves the authoritative mode intact instead of
crashing.

Assign the root class through the inherited `GameplayHUDComponent` on the Player Controller
Blueprint. `HUDZOrder` controls viewport order. The coordinator deliberately exposes no Tactical
Pause or Equipment class override: choose those concrete widgets by composing them inside the root
Widget Blueprint.

## Visibility policy

Automatic visibility is:

- visible with a possessed `AParadoxCharacter` during ordinary gameplay;
- visible during Tactical Pause;
- visible in the time loop's `ActiveRun` phase;
- hidden during Chrono Spawn selection, rewind/reset, clone reconstruction/playback and terminal
  non-interactive phases;
- hidden when no valid Paradox Pawn is possessed.

`Automatic`, `ForcedVisible`, and `ForcedHidden` provide an explicit root override. Tactical Pause,
Equipment, and Status have independent `ESlateVisibility` values. Status defaults to `Collapsed`
and is reserved for future systems such as Oxygen; this feature does not create an Oxygen model.

The coordinator configures the local Player Controller with `Game and UI` input mode after adding
the HUD to the player screen. Screen-space widgets therefore receive mouse hover/click first, while
unhandled input continues to normal gameplay. It does not assign keyboard focus, does not lock the
mouse to the viewport, and keeps the cursor visible during capture. Disable
`bConfigureGameAndUIInputMode` only when another project-level UI router owns this policy.

Use `OnHUDVisibilityChanged`, `OnHUDSectionVisibilityChanged`, and `OnHUDModeChanged` for external
presentation. Possession and time-loop phase bindings are event-driven; the HUD has no Tick.

## Equipment section

`UParadoxInventoryWidget` is the native Equipment section. It binds explicitly to the currently
possessed `AParadoxCharacter` and displays either an empty state or:

- `PickupableDisplayName` and `PickupableIcon` from the equipped pickupable;
- a Drop button;
- one dynamic button for every valid `UParadoxPickupableAction`.

Button availability refreshes from Inventory, Drop Targeting, pickupable action invalidation and
Gameplay Actions lifecycle events. Requests still use the existing authoritative Inventory and
Gameplay Actions APIs; the widget never mutates inventory directly and never treats its owning
Player Controller as the acting Character.

An authored `UParadoxInventoryWidget` Blueprint requires five named variables (`BindWidget`):
`EquipmentStateSwitcher`, `EmptySlotIcon`, `EquippedItemIcon`, `DropButton`, and
`SpecialActionsContainer`. `EquipmentStateSwitcher` uses page 0 for Empty and page 1 for Equipped.
`EquippedItemName` is an optional `UCommonTextBlock` (`BindWidgetOptional`) and may be omitted by
icon-only layouts. `DropButton` must derive from `UCommonButtonBase`; a plain UMG Button is
intentionally rejected by Blueprint compilation.

The class assigned to `ActionButtonWidgetClass` must derive from
`UParadoxInventoryActionButtonWidget` and provide the required `ActionButton`, `ActionIcon`, and
`ActionLabel` variables. `ActionButton` must derive from `UCommonButtonBase` and `ActionLabel` must
be a `UCommonTextBlock`. The native fallbacks follow the same Common UI type contract.

For custom item content, set the pickupable presentation fields and call the protected
`NotifyPickupableActionsChanged` hook after a native runtime action-catalog change. Blueprint uses
`SetPickupableActions`, which validates, deduplicates, assigns and broadcasts atomically.

## Tactical Pause section

`UTacticalPauseControlsWidget` is now a persistent `UUserWidget`. It binds in `NativeConstruct` and
unbinds in `NativeDestruct`; it no longer belongs to a Common Activatable stack. Common Buttons and
their styles remain supported. The Gameplay HUD root owns its one instance; the coordinator does
not transfer a widget from the Tactical Pause local-player subsystem.

The Paradox configuration should keep `bCreateDefaultWidgetAutomatically` disabled because the
Gameplay HUD is the project owner. Standalone projects using the plugin may still enable automatic
creation or manually call `AddToViewport`/`RemoveFromParent`.

## Blueprint and C++ API

The coordinator exposes `GetGameplayHUDWidget`, `GetHUDMode`, `IsHUDCollapsed`, `SetHUDMode`,
`ToggleHUDMode`, `SetVisibilityOverride`, `SetSectionVisibility`, and `RefreshHUDVisibility`.
The root widget exposes read-only coordinator, Player Controller, mode and Collapsed container
queries plus context and mode presentation hooks.

## Troubleshooting

- No HUD: verify a local `AParadoxPlayerController` possesses an `AParadoxCharacter`, then inspect
  the time-loop phase and visibility override.
- Tab does nothing: the root must currently be visible; also verify `ToggleHUDModeKey` and that the
  controller has initialized its input component.
- Blueprint does not switch pages: verify `HUDModeSwitcher` is a variable with that exact name and
  has Normal at index 0 and Collapsed at index 1.
- Empty normal sections: verify the root contains one Tactical Pause controls widget and one
  Inventory widget; named containers are optional presentation groups, not widget factories.
- Embedded Inventory is visible but does not react to the mouse: keep its visibility `Visible` or
  `Self Hit Test Invisible`, ensure no ancestor is `Not Hit-Testable (Self & All Children)`, and
  leave `bConfigureGameAndUIInputMode` enabled on the Gameplay HUD component.
- Duplicate Tactical Pause controls: disable plugin automatic creation; the project-owned root and
  the opt-in plugin-owned viewport widget are separate creation paths.
- Equipment actions stay disabled: inspect the action Definition, the acting Character's
  `UGameplayActionComponent`, current scheduler locks and the diagnostic returned by the request.
