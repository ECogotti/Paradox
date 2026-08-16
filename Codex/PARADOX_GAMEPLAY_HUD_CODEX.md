# Paradox Gameplay HUD — Codex Implementation Specification

## Purpose

Implement the first architecture for the **Paradox gameplay HUD** and perform one focused correction to the existing `TacticalPause` plugin UI.

The HUD is a **project-specific ParadoxGameplay integration layer**. It must compose presentation and commands coming from existing gameplay systems without moving authoritative gameplay state into UI code.

The initial HUD must support:

- Tactical Pause controls (`Play`, `Pause`, configured speed controls, etc.);
- the current one-slot equipment/inventory presentation;
- empty/full equipment icon state;
- standard `Drop` command;
- dynamic item-specific special action buttons when the equipped item exposes them;
- automatic root HUD visibility management;
- independently controllable visibility of HUD sections;
- a clean extension point for a future Oxygen progress bar, **without implementing an Oxygen gameplay system now**.

Also refactor `UTacticalPauseControlsWidget` so it is no longer a `UCommonActivatableWidget`.

---

# Mandatory first steps

Before changing code:

1. Read the repository root `AGENTS.md`.
2. Identify every affected module/plugin.
3. Search each affected area for all relevant `CODEX` folders and read the applicable Markdown instructions.
4. Read the current user-facing `Docs` for:
   - `ParadoxGameplay` / the project gameplay module that owns the inventory and HUD integration;
   - `TacticalPause`;
   - any existing UI framework used by the project;
   - the inventory/equipment implementation;
   - the GameplayActions integration used by pickup/drop/item actions, if present.
5. Inspect the actual current implementation before choosing names, inheritance, delegates, or dependencies.
6. Reuse existing types and APIs wherever they already represent the required concepts.
7. Do not invent Unreal Engine APIs or duplicate an existing project abstraction.

This specification defines responsibilities and behavior. Exact names may be adapted when the repository already has a stronger established convention.

---

# Architectural boundary

The Gameplay HUD belongs to the **project-specific Paradox gameplay layer**, not to generic plugins such as `TacticalPause`, `GameplayActions`, PuzzleSystem, GridWorld, etc.

Required dependency direction:

```text
TacticalPause -----------\
                         \
Inventory / Equipment ----> Paradox Gameplay HUD -> UMG presentation
                         /
Gameplay flow/state -----/

Future Oxygen -----------> Paradox Gameplay HUD
```

Forbidden dependency direction:

```text
TacticalPause -> Paradox Gameplay HUD
Inventory     -> Paradox Gameplay HUD
GameplayActions -> Paradox Gameplay HUD
```

Generic gameplay systems expose state, commands, queries, and events.

The HUD consumes those public APIs.

The HUD must not become a gameplay-state owner.

---

# Core design principles

## 1. UI is presentation, not authority

The authoritative state remains in the corresponding gameplay system.

Examples:

```text
Simulation paused / current speed -> TacticalPause
Equipped item                     -> Inventory
Can drop item                     -> Inventory / GameplayActions validation
Available item actions            -> Item / Inventory / GameplayActions
Future oxygen value               -> future Oxygen gameplay system
HUD visibility                    -> HUD coordinator policy based on gameplay context
```

Widgets display state and issue requests.

They must not independently mutate gameplay state to make the screen appear correct.

---

## 2. Event-driven updates

Do not add Tick to keep the HUD synchronized.

Prefer existing delegates/events such as:

```text
Tactical pause state changed
Simulation speed changed
Equipped item changed
Available equipped-item actions changed
Gameplay phase / player-control state changed
```

If a required notification is genuinely missing, add the smallest safe public notification to the authoritative owner rather than polling every frame.

---

## 3. One owner for HUD lifetime and root visibility

Do not allow unrelated gameplay systems to call `SetVisibility` directly on the root Gameplay HUD.

The HUD integration layer must own:

- creation;
- viewport registration;
- references;
- delegate binding;
- delegate cleanup;
- initial synchronization;
- root visibility policy;
- section visibility policy.

---

# Main runtime coordinator

Create or extend a project-specific component equivalent in responsibility to:

```text
UParadoxGameplayHUDComponent
```

Preferred owner:

```text
AParadoxPlayerController
└── UParadoxGameplayHUDComponent
```

Use the existing player/controller architecture if the project already has a better-established owner with the same lifetime.

Do not introduce a WorldSubsystem merely to make the HUD globally reachable.

A subsystem is not required for this feature unless the existing project UI architecture already mandates one and its lifetime is demonstrably correct.

## Responsibilities

The coordinator must:

```text
create the root gameplay HUD widget
add/remove it from the viewport through the established project UI path
cache its reference safely
resolve the authoritative gameplay providers it needs
bind to their public state-change events
initialize the widget from current state after binding
update only the affected HUD section when source state changes
reevaluate root HUD visibility when relevant gameplay context changes
clean up all bindings and references symmetrically
```

The coordinator must not:

```text
own TacticalPause state
own inventory state
execute item effects directly
spawn a dropped item directly from UI code
implement oxygen simulation
poll state every frame
search the world repeatedly for dependencies
```

---

# Root widget

Provide a native project-specific root widget equivalent to:

```text
UParadoxGameplayHUDWidget
```

with a designer-facing Blueprint child such as:

```text
WBP_ParadoxGameplayHUD
```

Use project naming conventions if equivalents already exist.

The native root is a **template and presentation boundary**, not the gameplay coordinator.

It should expose clearly separated areas for:

```text
Tactical Pause controls
Equipment slot / actions
Future status widgets
```

Conceptual layout:

```text
UParadoxGameplayHUDWidget
│
├── TacticalPauseContainer
│     └── UTacticalPauseControlsWidget or project presentation child
│
├── EquipmentContainer
│     └── UParadoxEquipmentSlotWidget
│
└── StatusContainer
      └── future Oxygen widget location
```

Do not require the future Oxygen widget class to exist now.

---

# Automatic HUD visibility

Automatic visibility is a required feature of the system, not a responsibility left to each Blueprint screen.

Provide one authoritative visibility-policy boundary equivalent to:

```text
ShouldHUDBeVisible()
RefreshHUDVisibility()
```

The exact API may follow current project style.

## Required behavior

The coordinator should show the gameplay HUD during ordinary controllable gameplay and Tactical Pause.

It must be possible to hide it automatically during states where the gameplay HUD is not appropriate, for example current/future project states such as:

```text
loading / level transition
main menu or non-gameplay screen
rewind transition where normal gameplay controls are unavailable
non-interactive cinematic or equivalent modal state
player/controller not ready for gameplay
```

Do not hardcode speculative dependencies on systems that do not yet exist.

Inspect the current project for the existing authoritative gameplay-state hooks and use only real ones.

## Visibility override

Add a small controlled override only if useful with the current project patterns, conceptually:

```text
Automatic
ForcedVisible
ForcedHidden
```

This is useful for debug, tutorials, or special sequences, but avoid adding it if an existing project UI policy already provides the same capability.

## Extension point

The default policy must work natively.

Provide one intentional extension point for special project cases, e.g. a protected virtual or `BlueprintNativeEvent`, if this matches existing architecture.

Do not require Blueprint to implement basic HUD visibility.

---

# Section visibility

Root HUD visibility and individual section visibility are separate concepts.

Required semantic capability:

```text
HUD Root              Visible
Tactical Pause        Visible
Equipment             Visible
Future Oxygen         Collapsed
```

A section may be hidden/collapsed without hiding the root HUD.

Do not destroy/recreate widgets merely to hide a section.

Use ordinary visibility state unless a real lifecycle requirement says otherwise.

---

# Tactical Pause integration

`TacticalPause` remains a generic plugin responsible only for authoritative time-control behavior.

The Paradox HUD consumes its public API/events.

The integration must preserve:

```text
Play
Pause
normal speed
configured accelerated speeds
state synchronization when time state changes outside the UI itself
```

Do not hardcode speed buttons in the Paradox HUD if the plugin already exposes a configured list/data model.

The UI must reflect the authoritative TacticalPause state even when the change originated from keyboard input, another widget, Blueprint, C++, or another valid caller.

---

# Required TacticalPause widget correction

Refactor the existing:

```text
UTacticalPauseControlsWidget
```

which currently derives from:

```text
UCommonActivatableWidget
```

The controls widget must **no longer be an activatable widget**.

The intended role is a normal persistent child widget embedded inside the Paradox gameplay HUD.

It does not need its own CommonUI activation/deactivation lifecycle or input-routing participation merely to display clickable controls.

## Target semantics

Prefer a normal `UUserWidget` base unless inspection of the current implementation proves that `UCommonUserWidget` provides a concrete required feature that is unrelated to activation/input routing.

In all cases:

```text
DO NOT retain UCommonActivatableWidget inheritance
DO NOT require ActivateWidget / DeactivateWidget for normal operation
DO NOT require an ActivatableWidgetStack just to host the TacticalPause controls
DO NOT make visibility depend on CommonUI activation state
```

## Preserve Common UI child widgets and styling

The controls widget may continue to contain and use Common UI elements such as:

```text
UCommonButtonBase-derived buttons
UCommonTextBlock
Common button style assets
Common text style assets
CommonActionWidget where actually useful
```

The goal of this refactor is to remove unnecessary **activatable/input-routing behavior**, not to remove Common UI styling or reusable Common UI controls.

Inspect all current button/text bindings and preserve existing visual/style functionality.

## Remove obsolete lifecycle code

After changing inheritance, inspect all code that depends on activatable lifecycle functions/events.

Remove or replace logic that only exists because the widget was activatable, including where applicable:

```text
NativeOnActivated
NativeOnDeactivated
activation-bound input registration
activation-only state synchronization
stack push/pop assumptions
activatable-widget-specific focus behavior
```

Do not mechanically delete behavior that still matters.

For each removed activatable hook, determine its actual responsibility and move necessary initialization/synchronization to the correct ordinary widget lifecycle or explicit HUD coordinator update path.

Prefer explicit state synchronization from TacticalPause over activation side effects.

## Input routing

Do not recreate CommonActivatableWidget behavior manually.

If TacticalPause keyboard/gamepad shortcuts exist outside the buttons, they should remain owned by the appropriate player/input/TacticalPause integration layer rather than by this passive controls panel unless the existing architecture explicitly says otherwise.

The clickable Common Buttons remain valid UI controls.

---

# Equipment HUD

Create a project-specific equipment presentation widget equivalent to:

```text
UParadoxEquipmentSlotWidget
```

The current inventory model has one equipment/inventory slot.

The UI must support at least two presentation states.

## Empty

```text
Equipment Slot
└── Empty icon / empty presentation
```

## Occupied

```text
Equipment Slot
├── Equipped item icon
├── Drop button
└── zero or more Special Action buttons
```

Do not remove the entire equipment area merely because no item is equipped unless the configured design explicitly requests that presentation.

The default should support a visible empty-slot state.

---

# Equipment item presentation

Do not branch in HUD code on concrete item classes such as:

```text
if Battery...
if Card...
if Key...
```

The equipment UI must consume generic presentation/action information exposed through the existing inventory/item/action architecture.

Before adding new structs or interfaces, inspect the current Inventory and GameplayActions code for existing equivalents.

Required semantic data includes only what the UI truly needs, for example:

```text
item display icon
optional display name / tooltip if already supported
whether Drop should be available/enabled
item-specific available actions
```

Avoid copying large mutable gameplay objects into UI-only state.

---

# Drop action

`Drop` is a standard equipment/inventory command and must remain separate from item-specific special actions.

Conceptual structure:

```text
Equipment actions
├── Standard
│     └── Drop
└── Item-specific
      ├── Action A
      ├── Action B
      └── ...
```

The Drop button must **request the existing Drop gameplay flow**.

It must not implement dropping itself.

Specifically, UI code must not:

```text
choose the final world spawn location by itself
spawn/move the item directly
clear the inventory directly while bypassing the inventory API
bypass GridWorld drop-cell validation
bypass GameplayActions / replay-compatible action execution when that is the current architecture
```

Inspect the already-implemented Inventory Drop action and call/extend the correct public request path.

Preserve replay/intent semantics already established by the inventory implementation.

---

# Item-specific special actions

The equipment section must support a dynamic set of extra buttons provided by the currently equipped item.

Examples are intentionally generic:

```text
Use
Activate
Scan
Rotate
Trigger
```

Do not hardcode those actions into the HUD.

## Source of truth

Inspect the current pickupable item / inventory / GameplayActions architecture.

Prefer extending an existing item-action abstraction if one already exists.

Only introduce a new UI-facing action descriptor if there is no current representation that can safely expose the required presentation information.

If a new descriptor is required, keep it small and semantic, conceptually:

```text
Action identifier
Display label
Icon
Visible state, if needed
Enabled state, if needed
```

Do not put action execution logic into the descriptor.

## Dynamic buttons

The widget must be able to rebuild/update its special-action area when:

```text
an item is equipped
an item is unequipped
an item is swapped
available actions on the same equipped item change at runtime
an action becomes enabled/disabled without changing equipped item
```

Do not assume actions are immutable for the entire equip lifetime.

Avoid rebuilding the entire root HUD when only action buttons changed.

## Executing an action

Button flow must be equivalent to:

```text
Special Action button
    ↓
request action by semantic ID / existing gameplay-action handle
    ↓
authoritative Inventory / GameplayActions path
    ↓
validation
    ↓
execution
    ↓
completion / failure
```

The widget must not call concrete gameplay-effect functions on the item just because a button was clicked, unless the current authoritative item-action architecture already defines that as the correct public request API.

---

# Runtime event integration

Codex must inspect and reuse existing events where possible.

The HUD needs semantic notifications equivalent to:

```text
OnEquippedItemChanged
OnEquippedItemActionsChanged
OnTacticalPauseStateChanged
OnTacticalPauseSpeedChanged
HUD-relevant gameplay-state changed
```

Do not add duplicate delegates if an existing broader event already reliably provides the required invalidation.

When an event fires, query the authoritative owner for its current state rather than trusting stale duplicated state carried indefinitely by UI code.

---

# Optional HUD view-state

A small internal presentation state is acceptable if it simplifies synchronization, but it must remain a **view-state cache**, not a second gameplay authority.

Conceptually:

```text
FParadoxHUDViewState

Root
    Visible

TacticalPause
    Current state
    Current speed

Equipment
    Item presentation
    Drop availability
    Special action presentation list

Future Status
    no Oxygen state yet
```

Do not introduce this struct if direct targeted widget updates fit the existing project architecture more cleanly.

---

# Future Oxygen extension point

Do **not** implement Oxygen gameplay now.

Do not invent:

```text
UOxygenComponent
Oxygen subsystem
oxygen depletion logic
oxygen save/reset rules
oxygen damage/death rules
```

The root HUD should simply be structurally ready to host a future Oxygen status widget/progress bar.

Initial expected presentation:

```text
Oxygen section = Collapsed / not instantiated according to the chosen layout implementation
```

When a real Oxygen mechanic is implemented later, the intended dependency will be:

```text
Future Oxygen gameplay owner
    ↓ state + OnOxygenChanged
Paradox Gameplay HUD coordinator
    ↓ presentation update
Oxygen widget / progress bar
```

Do not add speculative APIs now solely for that future feature.

---

# Blueprint-facing design

The C++ implementation must provide useful native behavior without requiring Blueprint to maintain state invariants.

Blueprint should primarily control:

```text
layout
animations
icons
button/text styles
sounds
presentation transitions
optional visual hooks
```

Blueprint must not be required to:

```text
bind core gameplay delegates correctly
keep TacticalPause state synchronized
track authoritative equipped item
maintain root HUD visibility policy
execute Drop logic
execute special gameplay actions directly
```

Expose only intentional properties/events with clear categories and tooltips.

---

# Common UI usage requirements

The project may continue using Common UI for its useful presentation/control primitives even though the Gameplay HUD and TacticalPause controls panel are not activatable screens.

It is acceptable and intended to use Common UI child elements inside an ordinary widget tree, especially:

```text
Common Button classes
Common Text classes
Common style assets
input glyph widgets where actually required
```

Do not convert the entire Gameplay HUD into `UCommonActivatableWidget` just to use Common Button/Text styling.

Activatable widgets should be reserved for screens/panels that genuinely need CommonUI activation state and input-routing semantics.

---

# Lifetime and initialization

HUD startup must not depend on arbitrary Actor BeginPlay order where that can be avoided.

Required semantics:

1. identify the local HUD owner;
2. create the configured root HUD widget when dependencies and owning player are valid;
3. bind source delegates;
4. query current authoritative state;
5. push initial state into all existing HUD sections;
6. evaluate root and section visibility.

If the controlled Pawn can change, inspect the current controller/pawn lifecycle and rebind only the dependencies that are Pawn-owned.

Do not assume a cached Pawn/component reference remains valid forever.

## Shutdown

Symmetrically:

```text
unbind delegates
remove/clear widget according to project lifetime
clear weak/GC-tracked references as appropriate
prevent callbacks during teardown
```

---

# Error handling

Recoverable UI configuration failures must remain observable.

Examples:

```text
missing configured root widget class
configured widget missing a required bindable child
TacticalPause provider unavailable when controls are expected
Inventory provider unavailable when equipment section is expected
unknown special action requested
```

Use the owning module's established log category/macros.

Do not introduce `LogTemp` in committed code.

Prefer a hidden/disabled affected section over crashing the game for a recoverable presentation error, while logging enough context to diagnose the configuration.

Do not silently pretend an action succeeded when its gameplay request failed.

---

# Performance

The HUD is event-driven.

Do not add per-frame polling for:

```text
pause state
equipped item
special actions
visibility
future oxygen
```

Dynamic special-action button creation should occur only when the relevant presentation model changes.

Do not add Unreal Insights scopes to trivial UI setters. Instrument only if investigation reveals a genuinely meaningful expensive path.

---

# Documentation

Update/create user-facing `Docs` as required by `AGENTS.md`.

Documentation must explain at least:

## Paradox Gameplay HUD

```text
purpose and architecture
how the HUD is created
how automatic visibility works
how to assign/replace the root Blueprint widget
TacticalPause integration
empty/full equipment behavior
Drop request flow
dynamic special action flow
how to provide item action presentation data
future Oxygen extension point
Blueprint extension hooks
```

## TacticalPause

Update TacticalPause docs to state that:

```text
UTacticalPauseControlsWidget is a normal non-activatable widget
it can be embedded directly in another UMG widget
Common Buttons / Common Text / Common style assets are still supported
activation/deactivation is no longer required for the controls panel
TacticalPause gameplay state remains authoritative in the plugin
```

Remove documentation that tells users to push this controls widget onto an Activatable Widget Stack if that was only required by the old inheritance.

---

# Validation scenarios

Codex must validate at least the following after implementation.

## 1. HUD creation

```text
Gameplay starts
-> one Gameplay HUD is created for the local player
-> initial source state is reflected immediately
```

No duplicate HUD is created after repeated initialization paths.

## 2. Root automatic visibility

```text
normal gameplay -> visible
valid project state that suppresses gameplay HUD -> hidden
return to gameplay -> visible
```

Use real existing project states for this test rather than inventing a fake gameplay phase system.

## 3. TacticalPause embedded controls

```text
Gameplay HUD visible
-> TacticalPause controls visible as ordinary child widget
-> no ActivateWidget() call required
```

## 4. TacticalPause button click

```text
click Pause
-> plugin state changes
-> UI reflects authoritative paused state

click Play
-> plugin state changes
-> UI reflects authoritative playing state
```

## 5. TacticalPause external change

Change time state/speed through a valid path outside the widget.

```text
external TacticalPause state change
-> controls update without reactivation/recreation
```

## 6. Common UI styling retained

The refactored non-activatable TacticalPause widget still uses the existing Common Button/Common Text styles correctly.

## 7. Empty equipment slot

```text
no equipped item
-> empty icon/presentation shown
-> Drop unavailable
-> no special action buttons
```

## 8. Equipped item with no special actions

```text
item equipped
-> item icon shown
-> Drop available when authoritative system permits
-> no special action buttons
```

## 9. Equipped item with special actions

```text
item equipped
-> item icon shown
-> Drop shown
-> one button per currently available special action
```

## 10. Runtime action-set change

Without unequipping the item:

```text
available actions change
-> special-action section updates
-> root HUD is not recreated
```

## 11. Drop

```text
click Drop
-> existing authoritative Drop request path begins
-> UI does not directly spawn/drop item
-> when inventory state changes, equipment UI becomes empty
```

## 12. Special action request

```text
click item action
-> authoritative action request path receives correct semantic action
-> validation/execution remain outside UI
```

## 13. Section visibility independence

Hide/collapse one HUD section while keeping the root and other sections visible.

## 14. No Oxygen system required

The project builds and HUD works with no Oxygen gameplay class present.

## 15. Cleanup

End play / destroy owning controller / perform relevant local-player teardown:

```text
no stale delegates
no duplicate callbacks
no invalid widget access
```

---

# TacticalPause regression checks

Because `UTacticalPauseControlsWidget` inheritance changes, specifically search for and validate every caller that assumes it is a `UCommonActivatableWidget`.

Search at least for:

```text
UTacticalPauseControlsWidget references
ActivateWidget
DeactivateWidget
NativeOnActivated
NativeOnDeactivated
UCommonActivatableWidgetStack
PushWidget
GetActiveWidget
CommonUI action bindings tied to the old controls widget
```

Update only the callers genuinely affected by this concrete widget change.

Do not redesign unrelated CommonUI screens.

---

# Forbidden shortcuts

Do not:

```text
make the root Gameplay HUD the authoritative owner of gameplay state
make generic plugins depend on ParadoxGameplay HUD classes
use Tick for normal state synchronization
hardcode concrete item classes in HUD logic
hardcode item-specific buttons into the equipment widget
execute Drop implementation inside UMG
bypass existing GameplayActions / Inventory validation
require Blueprint to keep core HUD state synchronized
turn the root Gameplay HUD into a CommonActivatableWidget merely for styling
keep UTacticalPauseControlsWidget activatable out of convenience
manually emulate CommonActivatableWidget input routing after removing the inheritance
implement Oxygen gameplay now
create speculative Oxygen APIs
add global managers/subsystems without a demonstrated lifetime need
perform runtime world searches every time the HUD updates
silently swallow failed gameplay requests
```

---

# Build and completion requirements

This task is not complete until Codex has:

1. read root and local instructions;
2. inspected the current HUD/UI, TacticalPause, Inventory, and relevant GameplayActions implementations;
3. implemented the smallest architecture consistent with existing project patterns;
4. converted `UTacticalPauseControlsWidget` away from `UCommonActivatableWidget`;
5. preserved Common Button/Common Text styling support;
6. implemented automatic HUD visibility;
7. implemented equipment empty/full presentation;
8. connected Drop through the existing authoritative action path;
9. implemented dynamic item-specific action presentation/request routing;
10. left Oxygen as presentation-ready future extension only;
11. updated relevant `Docs`;
12. compiled the affected target successfully;
13. fixed every compile error caused by the change;
14. validated the required runtime scenarios where possible;
15. reviewed the final diff and removed unrelated changes.

If compilation cannot be executed because of an external environment limitation, report exactly what was not validated and why. Do not present uncompiled affected code as finished.
