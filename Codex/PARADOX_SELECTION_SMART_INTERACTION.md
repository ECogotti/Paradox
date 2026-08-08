# Paradox Selection, Smart Interaction, and Interaction Action Architecture

## Purpose

This document defines the architecture and implementation milestones for the **Paradox project-level selection and interaction system**.

This is **not a new plugin**.

The system belongs to the project integration module, conceptually:

```text
ParadoxGameplay
```

It must integrate existing generic systems instead of moving Paradox-specific behavior into them.

Relevant existing systems include:

```text
GridWorld
GameplayActions
IntentReplay
WorldState
TacticalPause
Unreal Smart Objects
UMG / WidgetComponent
```

The intended responsibility split is:

```text
Selection / Hover / World-space interaction UI
    -> ParadoxGameplay

Interaction affordance and interaction slots
    -> Unreal Smart Objects

Grid cell identity, occupancy, reservation, and runtime cell presentation
    -> GridWorld

Interaction execution
    -> GameplayActions

Recording and replay of the semantic interaction request
    -> IntentReplay

Reset lifecycle notification for transient Paradox presentation cleanup
    -> WorldState

Future decision-making about which interaction to perform
    -> GOAP / GoalAgents integration
```

The architecture must preserve the existing project rule that generic plugins do not depend on `ParadoxGameplay`.

Selection and overlay state are transient. They are cleared when WorldState reset begins and are never restored from WorldState snapshots.

---

# 1. Mandatory preliminary workflow for Codex

Before implementing any milestone:

1. Read the repository root `AGENTS.md`.
2. Identify the real `ParadoxGameplay` module or the actual project gameplay module that owns Paradox-specific code.
3. Search that module and every modified dependency for relevant `CODEX` folders.
4. Read the relevant `Docs` folders.
5. Inspect the existing implementation of:
   - Player Controller / input ownership;
   - `GameplayActions`;
   - `IntentReplay`;
   - `WorldState`;
   - `GridWorld`;
   - GridWorld runtime presentation;
   - Smart Objects usage already present in the project;
   - current module logging/debug conventions;
   - the actual WorldState reset-start/reset-complete notification API and lifecycle ordering.
6. Verify the actual Unreal Engine Smart Object API against the engine version used by the project.
7. Search the project for existing Custom Depth / Custom Stencil values before using the reserved stencil defined in this document.
8. Compile the appropriate editor target before making structural changes to establish a clean baseline.
9. Implement only the requested milestone.
10. Compile after every meaningful code change.
11. Update user-facing documentation in the owning module `Docs` folder.
12. Do not consider a milestone complete until the affected target compiles.

Do not invent Unreal APIs from memory.

---

# 2. Binding architectural decisions

These decisions are fixed for this system and must not be redesigned casually.

## 2.1 Project-level system, not a plugin

Do not create:

```text
Selection plugin
Interaction overlay plugin
Paradox interaction plugin
Smart interaction plugin
```

The selection and project-specific interaction integration belong inside `ParadoxGameplay`.

Existing generic plugins remain generic.

Required dependency direction:

```text
ParadoxGameplay
    -> GridWorld
    -> GameplayActions
    -> IntentReplay when integration code actually requires it
    -> WorldState for reset lifecycle observation
    -> Unreal Smart Objects
```

Never introduce:

```text
GridWorld -> ParadoxGameplay
GameplayActions -> ParadoxGameplay
IntentReplay -> ParadoxGameplay
WorldState -> ParadoxGameplay
TacticalPause -> ParadoxGameplay
```

The current project architecture already places tactical selection, overlays, and Paradox-specific presentation in the game module rather than in `TacticalPause`.

---

## 2.2 Keep selection, affordance, and execution separate

The system must distinguish three concepts:

### Selection

```text
What object is under the mouse?
What object is currently selected?
Should its outline and world-space widget be visible?
```

Owned by Paradox selection code.

### Interaction affordance

```text
What interactions does this Actor currently offer?
From which authored positions can they be used?
Is a slot currently free, claimed, or occupied?
```

Owned authoritatively by Smart Object data and runtime Smart Object state.

### Interaction execution

```text
What happens when an interaction is actually requested?
How does it validate, execute, succeed, fail, cancel, interrupt, or clean up?
```

Owned by `GameplayActions`.

Do not let the selection component execute gameplay behavior directly.

Do not let a Smart Object directly perform project gameplay effects.

Do not let a widget call Actor-specific functions such as:

```text
OpenDoor()
UseTerminal()
ActivateGenerator()
```

The widget submits a semantic interaction request through the interaction system.

---

## 2.3 Movement is explicitly out of scope

The interaction action implemented by this task must **not move the requester**.

Do not implement inside the interaction system:

```text
MoveTo interaction slot
pathfinding
automatic positioning
automatic facing
teleporting to a valid interaction cell
automatic GridWorld movement requests
automatic GridPathFollowing requests
GOAP movement
```

The interaction action may **validate the requester's current position** against a valid interaction slot/cell, but it must not solve an invalid position by moving the requester.

Future GOAP logic may decide separately that movement is required before submitting the interaction action.

Required conceptual future composition:

```text
GOAP decides:
    Move somewhere valid
    then
    Interact with target
```

This document implements only the `Interact` part.

---

# 3. High-level architecture

Recommended conceptual structure:

```text
PlayerController
└── UParadoxSelectionComponent
        ├── mouse hover detection
        ├── one selected Actor
        ├── left-click selection
        ├── right-click deselection
        └── world-space UI routing

Interactable Actor
├── UParadoxSelectableComponent
│       ├── hover state
│       ├── selection state
│       ├── Custom Depth outline presentation
│       ├── optional interaction-cell presentation
│       └── world-space widget ownership/configuration
│
├── UParadoxInteractionComponent
│       ├── Smart Object query bridge
│       ├── player/AI shared interaction queries
│       ├── Smart slot -> Grid cell resolution
│       └── semantic request building/submission helpers
│
└── USmartObjectComponent
        └── authored interaction slots and behavior definitions
                └── Paradox interaction behavior data
                        ├── InteractionTag
                        └── GameplayActionDefinition

Gameplay requester
    ↓
FGameplayActionRequest
    ↓
UGameplayActionComponent
    ↓
UParadoxInteractionActionBase
    ↓
Blueprint/C++ subclass implements concrete interaction
```

Working names may adapt to real project naming conventions discovered by Codex.

Do not rename existing project types only to match this document.

---

# 4. Selection ownership

## 4.1 `UParadoxSelectionComponent`

Create or extend one player-owned component conceptually named:

```text
UParadoxSelectionComponent
```

Preferred owner:

```text
PlayerController
```

It is the authoritative owner of:

```text
CurrentHoveredActor
CurrentSelectedActor
```

Only one Actor may be selected at a time.

Do not make every selectable Actor search the world to discover whether another Actor is selected.

---

## 4.2 Hover behavior

While selection input is enabled, the selection component performs one cursor hit query at the appropriate update frequency.

Prefer the existing project mouse-picking path if one already exists.

Do not create a second permanent cursor-trace implementation if the Player Controller already owns one.

Only process meaningful work when the hovered Actor changes:

```text
OldHover == NewHover
    -> no state transition

OldHover != NewHover
    -> EndHover old target
    -> BeginHover new target
```

Do not perform expensive per-frame component rebuilding when the target has not changed.

Tick is acceptable only for cursor tracking while this feature is enabled.

Disable or avoid the Tick when the selection system is inactive.

---

## 4.3 Selection input semantics

Required behavior:

### Hover

```text
Mouse enters selectable Actor
    -> Hovered = true

Mouse leaves selectable Actor
    -> Hovered = false
```

### Left mouse button

If no world-space widget control consumed the click:

```text
LMB on unselected selectable Actor
    -> deselect previous Actor if any
    -> select clicked Actor

LMB on currently selected Actor
    -> deselect it

LMB on non-selectable world geometry
    -> do not automatically deselect
```

Only one Actor may remain selected.

### Right mouse button

```text
RMB anywhere
    -> deselect current Actor
```

This is a project rule.

### Selecting another Actor

```text
Actor A selected
LMB on Actor B
    -> Actor A deselected
    -> Actor B selected
```

---

## 4.4 Widget input priority

World-space widget interaction must take priority over left-click world selection.

Required behavior:

```text
LMB
    ↓
if an interactive world-space widget control consumes the click:
    widget handles it
    selection does not toggle
else:
    world selection handles the click
```

Do not allow clicking a button inside the selected Actor's widget to simultaneously deselect the Actor.

Right-click deselection remains a project-level selection command unless future explicit UI requirements override it.

Codex must inspect the project's existing input system and choose the smallest correct Unreal integration rather than inventing a second input stack.

---

# 5. Selectable Actor capability

## 5.1 `UParadoxSelectableComponent`

A world Actor becomes selectable by owning a component conceptually named:

```text
UParadoxSelectableComponent
```

Prefer composition over a mandatory `AParadoxSelectableActor` inheritance hierarchy.

The same component may be used by:

```text
doors
terminals
switches
generators
machines
puzzle objects
future AI-inspectable world objects
```

The component owns only selection/presentation behavior.

It does not own concrete gameplay interaction effects.

---

## 5.2 Designer-facing configuration

Expose only useful configuration.

Recommended initial properties:

```text
bCanBeHovered = true
bCanBeSelected = true

bShowInteractionCellsWhenSelected = false

SelectionWidgetClass
WidgetRelativeOffset
OptionalWidgetAnchorComponentName
WidgetDrawSize or equivalent minimal presentation configuration

bEnableDebug = false
```

Do not expose mutable authoritative runtime state directly.

Use controlled queries for:

```text
IsHovered()
IsSelected()
GetSelectionPresentationState()
GetInteractionWidget()
```

---

## 5.3 Presentation state

Hover and selection are logically independent.

Derived visual priority:

```text
Selected > Hovered > None
```

The derived state also determines the Paradox Custom Stencil value:

```text
Hovered only -> 230
Selected     -> 240
None         -> restore original mesh state
```

Required case:

```text
Hover Actor
Select Actor
Move mouse away
    -> Hovered becomes false
    -> Selected remains true
    -> outline remains active
```

If right-click deselects while the cursor is still above the Actor:

```text
Selected -> Hovered
```

The outline therefore remains active because hover is still true.

Only when neither hover nor selection is active should the outline be removed.

---

# 6. Custom Depth / Custom Stencil outline

## 6.1 Custom Stencil ranges and visual states

Hover and selection must be visually distinct and must be addressable independently by the user-authored Post Process Material.

Reserve **two separate Custom Stencil ranges** for Paradox project-level selectable presentation:

```text
230-239 = Paradox Hover Outline range
240-249 = Paradox Selected Outline range
```

Initial fixed values:

```text
230 = Paradox Hover Outline
240 = Paradox Selected Outline
```

Working constants should be centralized rather than scattered as magic numbers:

```text
ParadoxStencil.HoverOutline = 230
ParadoxStencil.SelectedOutline = 240
```

The remaining values in each range are intentionally reserved for future Paradox presentation variants without requiring a global stencil redesign.

The first implementation uses only:

```text
Hovered only
    -> stencil 230

Selected
    -> stencil 240
```

Selection has visual priority over hover:

```text
Selected > Hovered > None
```

Therefore:

```text
Actor hovered
    -> stencil 230

Actor becomes selected while still hovered
    -> replace stencil 230 with stencil 240

Selected Actor loses hover
    -> remain stencil 240

Selected Actor is deselected while cursor is still over it
    -> replace stencil 240 with stencil 230

Actor is neither selected nor hovered
    -> restore its original Custom Depth / Stencil state
```

Do not render both Paradox stencil states simultaneously on the same mesh component.

The Post Process Material is responsible for mapping the two stencil ranges to two different visual colors/styles.

Before implementation, Codex must search the entire project for existing Custom Stencil ownership or values that collide with **either reserved range**:

```text
230-239
240-249
```

If any value in either range is already semantically owned by another project system:

1. do not silently repurpose it;
2. report the collision;
3. determine whether the conflicting system already owns a documented range;
4. choose two other unused contiguous ranges only when necessary;
5. preserve the separation between Hover and Selected ranges;
6. update this documentation and any project stencil registry/constants accordingly.

Do not resolve a collision by falling back to one shared Hover/Selected stencil value.

---

## 6.2 Mesh components affected

When the selectable outline is active, apply Custom Depth/Stencil to all compatible mesh components directly owned by the Actor:

```text
UStaticMeshComponent
USkeletalMeshComponent
```

Do not recursively modify Child Actor components by default.

Do not modify unrelated Primitive Components unless a future requirement explicitly adds them.

The implementation must work when an Actor owns:

```text
multiple static meshes
multiple skeletal meshes
a mix of both
```

---

## 6.3 Preserve pre-existing render state

The selection system must not destroy another system's Custom Depth configuration.

Before changing each affected mesh component, cache enough state to restore exactly what was present before the selection system took ownership.

At minimum preserve the actual engine properties equivalent to:

```text
Render CustomDepth enabled state
CustomDepthStencilValue
any directly changed stencil write mask state
```

Use the exact Unreal API for the project engine version.

On final removal of the Paradox outline:

```text
restore original component render state
```

Do not simply force:

```text
RenderCustomDepth = false
Stencil = 0
```

because the component may already be used by another presentation/debug system.

If a mesh component is destroyed while highlighted, cleanup must tolerate it.

If compatible components are added dynamically, reconcile the component set on meaningful selection transitions rather than scanning every frame unless the real project lifecycle requires something else.

---

## 6.4 Post-process material ownership

The actual outline rendering is supplied by a **user-authored Post Process Material**.

Code owns only:

```text
Custom Depth enable/disable
Custom Stencil assignment
state restoration
```

Code does not recreate outline shader logic.

See `USER-AUTHORED ASSETS` below.

---

# 7. World-space interaction widget

## 7.1 Widget presentation

A selected Actor may show a widget near itself.

The widget must be rendered as a world-space widget, conceptually through:

```text
UWidgetComponent
Space = World
```

The widget is not a normal fixed HUD panel.

Required visibility:

```text
Not selected -> hidden/not active
Selected     -> visible
Deselected   -> hidden/not active
```

The widget does not depend on hover once selection exists.

---

## 7.2 Widget placement

Support a simple authored placement policy:

```text
Optional explicit SceneComponent anchor
else Actor root + WidgetRelativeOffset
```

Do not require every Actor to contain a specific native anchor component.

If the project already has an established world-widget anchoring system, reuse it.

The component should own or manage the runtime widget presentation without requiring Actor-specific Blueprint code merely to show/hide it.

---

## 7.3 Native widget template

Create a native, Blueprint-extensible base widget conceptually named:

```text
UParadoxInteractionWidgetBase : UUserWidget
```

This class must contain **no concrete interaction buttons or object-specific UI**.

It is a template for rapid Blueprint implementation.

Expose safe, read-only context such as:

```text
GetSelectedActor()
GetSelectableComponent()
GetInteractionComponent()
GetOwningPlayerController()
GetSelectionComponent()
GetCurrentRequester()
GetAvailableInteractionOptions()
CanRequestInteraction(...)
RequestInteraction(...)
```

Use the actual existing GameplayActions request APIs rather than bypassing them.

Useful Blueprint extension hooks may include:

```text
OnSelectionContextAssigned
OnSelectionContextCleared
OnInteractionOptionsRefreshed
OnInteractionRequestAccepted
OnInteractionRequestRejected
```

Use the smallest useful set.

Do not implement:

```text
Use button
Open button
Hack button
Door UI
Terminal UI
Generator UI
inventory UI
GOAP UI
```

The native widget base must remain presentation-agnostic.

A designer may later create:

```text
WBP_DoorInteraction
WBP_TerminalInteraction
WBP_GeneratorInteraction
```

all derived from the same native template.

---

# 8. Smart Objects are the interaction-affordance authority

## 8.1 Smart Object responsibility

Smart Objects define:

```text
which interaction activities are offered
which slots allow those activities
slot transforms
runtime slot availability
claim / occupied state
```

The project interaction component must not maintain a duplicate authored array of "interaction cells."

Do not author interaction cells directly as Grid coordinates.

Required source:

```text
Smart Object slots
    -> runtime world slot transforms
    -> GridWorld world-to-cell resolution
```

Because Smart Object slot transforms are relative to the Smart Object/Actor, moving or rotating the Actor naturally changes the world position from which the slot resolves.

Do not cache a permanent Grid Cell ID as authored Smart Object data.

---

## 8.2 Custom Smart Object interaction behavior definition

Create a project-specific Smart Object behavior definition or the closest native extension point supported by the actual engine version.

Working concept:

```text
UParadoxInteractionBehaviorDefinition
```

Codex must inspect the actual Smart Object behavior-definition API before finalizing the base class.

The behavior data should identify at least:

```text
InteractionTag
GameplayActionDefinition
```

Recommended semantics:

```text
InteractionTag
    semantic activity identifier

GameplayActionDefinition
    authored UGameplayActionDefinition for this interaction
```

Prefer a soft/asset-safe reference to the Gameplay Action Definition when compatible with the existing APIs.

Do not store runtime action instances.

Do not store runtime Smart Object Slot Handles as authored persistent data.

Do not store hard runtime Actor references in the behavior definition.

---

## 8.3 Each interactable may have its own Gameplay Action

This is intentional.

Examples:

```text
Door interaction
    -> its own GameplayActionDefinition
    -> Blueprint/C++ action class derived from UParadoxInteractionActionBase

Terminal interaction
    -> different GameplayActionDefinition
    -> different Blueprint/C++ subclass

Generator interaction
    -> different GameplayActionDefinition
    -> different Blueprint/C++ subclass
```

Do not implement one giant native `Interact` action containing branches such as:

```text
if Door...
if Terminal...
if Generator...
```

The shared native base owns only common interaction lifecycle and context.

Concrete interaction behavior belongs to concrete Gameplay Action subclasses.

---

# 9. `UParadoxInteractionComponent`

## 9.1 Purpose

Create a project interaction capability conceptually named:

```text
UParadoxInteractionComponent
```

It is a **bridge/query facade**, not a second interaction database.

It combines:

```text
Smart Object authored/runtime information
+
GridWorld spatial information
+
GameplayActions request submission
```

It is the API shared by:

```text
Player selection/widget
AI
future GOAP
future replay-aware project integration
```

---

## 9.2 Non-responsibilities

The component must not:

```text
move a Character
perform pathfinding
choose a GOAP plan
execute Actor-specific effects directly
duplicate Smart Object slot ownership
become the source of Grid occupancy
record IntentReplay entries manually
```

IntentReplay records accepted Gameplay Action requests through its existing journal integration.

---

## 9.3 Interaction query result

Create a read-only result type appropriate for Blueprint and C++.

Working concept:

```text
FParadoxInteractionOption
```

It may expose:

```text
InteractionTag
GameplayActionDefinition
Target Actor
Candidate slot information
Current availability
Grid cell information when resolvable
Failure/invalid reason when useful
```

Candidate slot runtime data may expose a transient handle for immediate runtime execution/debugging, but persistent/replay data must not depend on that handle.

Do not expose mutable Smart Object internals.

---

## 9.4 Query is requester-relative

Interaction availability must be queried for a requester:

```text
Player Pawn
Clone
AI Pawn
future GOAP agent
```

Conceptual API:

```text
QueryInteractionOptions(Requester)
QueryInteractionOptions(Requester, InteractionTag)
CanRequesterInteractNow(Requester, InteractionTag)
```

Use actual project conventions for return/result structures.

The same target can produce different availability for different requesters.

---

# 10. Smart Object slots to GridWorld cells

## 10.1 Resolution pipeline

For every relevant Smart Object slot:

```text
Smart Object slot
    ↓
current slot world transform
    ↓
GridWorld world-to-cell / projection API
    ↓
FGridCellId
```

Use the actual existing GridWorld public query facade.

Do not calculate Grid coordinates independently inside `ParadoxGameplay`.

Do not derive cell identity from HISM/ISM instance indices.

---

## 10.2 Authored adjacency

The Smart Object Definition is the authored source of valid interaction positions.

If the designer wants only adjacent cells, the designer places only slots that resolve to adjacent cells.

Do not create an independent "scan every adjacent Grid cell around Actor bounds" algorithm.

Do not infer interaction positions from Actor bounds.

This preserves authorial control for irregular objects and asymmetric interactions.

---

## 10.3 Two presentation states

Interaction cells shown for the selected Actor have exactly two first-milestone presentation states:

```text
Free
Occupied
```

Do not create a large interaction-cell state enum in this task.

Internal query diagnostics may contain richer reasons.

---

## 10.4 `Free`

A cell/slot is `Free` for the requester when the current project policy allows that requester to use it.

At minimum consider:

```text
Smart Object slot is usable/free for requester
GridWorld cell is valid
GridWorld cell is not occupied/reserved by another relevant owner according to the project query policy
```

If the requester itself currently occupies the cell that corresponds to its valid interaction slot, that must not automatically make the cell unavailable to itself.

Availability is requester-relative.

Use GridWorld's existing occupancy/reservation query context rather than hardcoding clone/player classes inside GridWorld.

---

## 10.5 `Occupied`

A displayed interaction cell is `Occupied` when it corresponds to an authored interaction slot but is currently unavailable because another runtime user or relevant Grid occupancy/reservation owns the position.

Examples may include:

```text
Smart Object slot claimed by another user
Smart Object slot occupied by another user
Grid cell occupied by another relevant agent
Grid reservation owned by another relevant agent
```

The exact policy must reuse existing GridWorld occupancy channels/query context where available.

Do not conflate:

```text
Smart Object claim
```

with:

```text
GridWorld reservation
```

They remain different concepts.

---

# 11. Interaction-cell visualization

## 11.1 Selectable option

Expose on the selectable component:

```text
bShowInteractionCellsWhenSelected
```

Required behavior:

```text
Selected = true
AND
bShowInteractionCellsWhenSelected = true
    -> query interaction slots for current player/requester
    -> resolve slots to Grid cells
    -> show Free / Occupied states

Deselected
    -> remove only presentation owned by this selection session
```

Do not show interaction cells only because an Actor is hovered.

---

## 11.2 Reuse GridWorld runtime presentation

Do not implement:

```text
one mesh Actor per interaction cell
one StaticMeshComponent per interaction cell
a second grid renderer in ParadoxGameplay
a duplicate Grid cell visualization database
```

Use or minimally extend the existing GridWorld runtime presentation APIs.

GridWorld already separates visual interaction/path/navigation layers and addresses cells through stable `FGridCellId`.

The Paradox system should submit project presentation state through the public presentation layer.

If the current GridWorld API cannot represent a project-owned `Free/Occupied` interaction overlay without abusing authoritative navigation state:

1. inspect the existing runtime presentation architecture;
2. make the smallest generic presentation extension in GridWorld;
3. keep the extension generic;
4. do not add Paradox-specific classes or tags to GridWorld;
5. compile and update GridWorld documentation as required.

Do not mutate authoritative Grid occupancy merely to change color.

---

## 11.3 Presentation lifetime

The interaction-cell presentation is owned by the current selection.

Required cleanup:

```text
selection changes
    -> remove previous Actor interaction-cell presentation
    -> add new Actor presentation if enabled

deselect
    -> clear presentation

selected Actor destroyed
    -> clear presentation

world teardown
    -> clear presentation safely
```

Do not clear unrelated GridWorld visual sessions owned by another system.

Use a handle/session/owner-token mechanism if the existing GridWorld presentation API provides one.

---

# 12. Interaction Gameplay Action template

## 12.1 Native base action

Create a Blueprint/C++ extensible Gameplay Action class conceptually named:

```text
UParadoxInteractionActionBase : UGameplayActionInstance
```

It is a **template**, not a concrete gameplay action.

It must follow the existing `GameplayActions` lifecycle and scheduler rules.

Do not create a second action scheduler.

Do not modify `GameplayActions` core unless a verified generic limitation requires the smallest backward-compatible change.

---

## 12.2 Standard semantic parameters

The interaction request must contain replay-safe semantic information.

Required conceptual values:

```text
Target
InteractionTag
```

### Target

The Replay Track must not depend on a hard runtime Actor pointer.

Use the existing GameplayActions Property Bag and IntentReplay recordability policies.

For world-authored interactables, prefer a replay-safe soft reference/path form compatible with the existing system.

Codex must inspect the actual existing parameter helper API and use its verified soft-object support.

### InteractionTag

Use an `FGameplayTag` describing the requested semantic activity.

Example namespace may be project-defined, such as:

```text
Paradox.Interaction.Use
Paradox.Interaction.Activate
```

Do not invent a large tag taxonomy in this task.

Use tags already established in the project when available.

---

## 12.3 Do not record runtime placement identity

Do not make any of these required Replay Track parameters:

```text
SmartObjectSlotHandle
SmartObject runtime object handle
Grid cell runtime handle
HISM instance index
exact slot world transform
runtime claim handle
```

Do not record the originally used Grid Cell as the semantic interaction request.

IntentReplay should preserve:

```text
Target
InteractionTag
GameplayActionDefinition identity
action parameters
```

At execution/replay time, the action resolves the current valid runtime slot again.

This preserves intent rather than one fragile execution detail.

---

## 12.4 Current-position validation only

Because movement is outside this task, the base action may validate:

```text
Is the requester currently positioned in a valid interaction cell/slot for this Target + InteractionTag?
```

If not:

```text
fail/reject with a structured interaction failure
```

Do not move the requester.

Do not select a distant slot and start pathfinding.

Do not teleport.

Do not rotate automatically unless a later requirement explicitly adds it.

---

## 12.5 Slot resolution at action execution

When an interaction request begins execution:

```text
resolve Target from replay-safe parameter
    ↓
find/validate UParadoxInteractionComponent
    ↓
query Smart Object interaction matching InteractionTag
    ↓
resolve current requester position to valid candidate slot/cell
    ↓
attempt Smart Object claim when required
    ↓
execute concrete interaction hook
```

If multiple slots resolve to the requester's current valid position, choose deterministically using stable Smart Object/slot ordering available from the actual API.

Do not use unordered-container iteration as a hidden tie-breaker.

---

## 12.6 Smart Object claim lifecycle

Smart Object claim is part of interaction concurrency, not movement.

The base interaction action should own the claim lifecycle when a claim is required.

Required principles:

```text
validation query
    -> no mutation

action starts
    -> attempt authoritative claim

claim fails
    -> structured action failure

claim succeeds
    -> concrete interaction may execute

success/failure/cancel/interruption/abort
    -> release claim exactly once
```

Do not retain a claim after the action reaches a terminal state.

Cleanup must tolerate target destruction and world teardown.

Do not use a GridWorld reservation as a substitute for the Smart Object claim.

---

## 12.7 Blueprint and C++ extension hooks

The base class must be useful as a parent for both Blueprint and C++ actions.

Expose stable public invariants and protected replaceable hooks.

Useful conceptual hooks include:

```text
CanExecuteInteraction
OnInteractionContextResolved
OnInteractionSlotClaimed
ExecuteInteraction
OnInteractionSucceeded
OnInteractionFailed
OnInteractionCancelled
OnInteractionInterrupted
OnInteractionAborted
OnInteractionCleanup
```

Do not expose internal scheduler state mutation.

Use `BlueprintNativeEvent`, `BlueprintImplementableEvent`, protected virtual C++ functions, or delegates according to the existing `GameplayActions` conventions.

The exact hook set should remain small enough to be understandable but rich enough to avoid reimplementing common lifecycle logic.

---

## 12.8 Useful read-only interaction context

Concrete Blueprint/C++ interaction actions should be able to safely query:

```text
Requester / action owner
Target Actor
UParadoxInteractionComponent
InteractionTag
Resolved Smart Object slot
Resolved slot world transform
Resolved Grid cell ID when available
Gameplay Action parameters
Action handle
current action state through existing GameplayActions API
```

Do not expose mutable Smart Object or GridWorld internals.

---

## 12.9 Controlled completion

Concrete actions must complete through the existing `GameplayActions` controlled completion API.

Provide convenient protected wrappers only when they improve Blueprint usability without bypassing the scheduler.

Examples conceptually:

```text
CompleteInteractionSuccess()
CompleteInteractionFailure(FGameplayTag Reason)
```

Do not create an independent "interaction completed" state machine competing with `GameplayActions`.

If the base template reaches `ExecuteInteraction` without a Blueprint/C++ implementation and no native behavior exists, it must fail predictably with a clear `NotImplemented`-style structured failure rather than hanging forever.

---

# 13. Per-interactable action authoring

Every concrete interactable may define its own action.

Expected designer/programmer flow:

```text
1. Create Blueprint or C++ class derived from UParadoxInteractionActionBase.
2. Implement only the concrete interaction behavior.
3. Create a UGameplayActionDefinition asset.
4. Set ActionClass to the concrete interaction class.
5. Configure ActionTag / locks / priority / parameters through the existing GameplayActions system.
6. Reference that GameplayActionDefinition from the Smart Object interaction behavior definition used by the Actor's slot(s).
```

Examples:

```text
BP_Action_OpenDoor
BP_Action_UseTerminal
BP_Action_ResetMachine
```

These are content-specific and are intentionally outside this core implementation task.

Do not implement any of them as part of this architecture task.

---

# 14. Interaction request path

## 14.1 Player

Expected flow:

```text
Player selects Actor
    ↓
world-space widget queries interaction options
    ↓
player clicks a Blueprint-authored button
    ↓
widget calls generic RequestInteraction(InteractionTag)
    ↓
UParadoxInteractionComponent resolves GameplayActionDefinition
    ↓
create FGameplayActionRequest through existing authoritative API
    ↓
set replay-safe Target
    ↓
set InteractionTag
    ↓
OriginTag = Player project/default origin
    ↓
submit through requester's UGameplayActionComponent
```

The widget does not call the target Actor's concrete gameplay function.

---

## 14.2 AI / future GOAP

Future AI uses the same query and submission API:

```text
AI / GOAP
    ↓
QueryInteractionOptions(AI requester)
    ↓
choose interaction
    ↓
RequestInteraction(...)
    ↓
same GameplayAction request path
```

Do not create a Player-only interaction execution path.

The first milestones do not implement GOAP decision logic.

---

# 15. IntentReplay integration

## 15.1 No manual interaction recording

`ParadoxGameplay` must not manually append an "interaction record" to IntentReplay.

The interaction is recorded because it is submitted as a normal accepted `GameplayActions` request.

Required existing architecture:

```text
Interaction request
    ↓
UGameplayActionComponent
    ↓
immutable Gameplay Action journal event
    ↓
IntentReplay journal sink
    ↓
Replay Track
```

Do not bypass the journal contract.

---

## 15.2 Replay semantics

IntentReplay records:

```text
what interaction was requested
```

not:

```text
which transient runtime Smart Object handle happened to execute it
```

Required semantic replay data:

```text
GameplayActionDefinition identity
Target soft reference / replay-safe identity
InteractionTag
other authored action parameters
effective priority / policy already owned by IntentReplay
```

At replay:

```text
IntentReplay creates a fresh GameplayActionRequest
    ↓
new UParadoxInteractionActionBase-derived runtime instance
    ↓
resolve current Target
    ↓
resolve current interaction slot
    ↓
validate current requester position
    ↓
claim current slot
    ↓
execute current interaction
```

The replayed action produces its own runtime result.

Do not mutate the source Replay Track based on the new slot or result.

---

## 15.3 World Actor identity limitation

The existing IntentReplay default recordability policy rejects hard runtime world-object references and allows valid soft-object references.

Therefore the interaction request must use a replay-safe target representation.

For normal world-authored Actors, a soft object path may be sufficient.

For runtime-spawned Actors whose path cannot be preserved across resets, do not fake recordability.

Such Actors require a future stable identity/resolution policy.

The core first milestone may document and fail such a case explicitly.

---

# 16. Interaction widget and availability refresh

The widget may request the current interaction options on demand.

Do not add a permanent high-cost poll simply to keep button availability updated.

Prefer refresh triggers such as:

```text
selection established
selection target changed
Smart Object runtime availability changed
Grid occupancy/reservation revision relevant to shown cells changed
explicit widget refresh request
```

Use existing delegates/revisions when available.

If the current Smart Object or GridWorld public API does not expose an efficient event for a specific case, prefer a narrow active-selection refresh policy rather than scanning every interactable in the world.

Only the currently selected Actor needs live player-facing interaction-cell presentation.

---

# 17. WorldState reset lifecycle integration

## 17.1 Architectural role

This system must integrate with `WorldState` only as a **reset lifecycle observer**.

Selection, hover, widget visibility, Custom Stencil presentation, and selected interaction-cell overlays are transient player-facing state.

They are **not world state** and must not be serialized into WorldState snapshots.

Do not register the selection system as a normal WorldState snapshot participant merely to preserve or restore:

```text
Hovered Actor
Selected Actor
world-space widget visibility
interaction-cell visualization
current outline stencil state
selection-owned GridWorld presentation handle
```

These values must be discarded on reset.

Required ownership rule:

```text
WorldState
    = authoritative world reset lifecycle

ParadoxGameplay
    = observes reset lifecycle
      and clears its own transient presentation state
```

WorldState must remain unaware of Paradox selection classes.

---

## 17.2 Reset-start behavior

Prefer cleanup at the **start of reset**, before world objects begin restoration.

Codex must inspect the real WorldState public API and bind to the verified reset-start notification.

Conceptual flow:

```text
WorldState Reset Started
        ↓
ParadoxGameplay integration
        ↓
UParadoxSelectionComponent::ResetSelectionState()
        ↓
clear hover
clear selected Actor
hide/remove selected world-space widget
clear selection-owned interaction-cell presentation
restore original Custom Depth / Custom Stencil state
invalidate stale transient presentation references
        ↓
WorldState continues authoritative restore
```

Do not wait until reset completion to remove player-facing selection state unless the actual WorldState lifecycle API makes earlier cleanup impossible.

If the real API exposes both reset-start and reset-complete events, use reset-start for clearing transient selection state.

Reset-complete may be observed later only if a concrete post-reset feature requires it.

---

## 17.3 `ResetSelectionState`

Expose one controlled, idempotent operation on the authoritative player selection owner.

Working concept:

```text
ResetSelectionState()
```

Required semantics:

```text
clear CurrentHoveredActor
clear CurrentSelectedActor
notify old hovered target that hover ended when still valid
notify old selected target that selection ended when still valid
hide/remove selected world-space widget
release/clear selection-owned GridWorld presentation handle/session
restore Paradox-owned Custom Depth / Custom Stencil presentation
clear transient cached interaction presentation state
```

The function must be safe when:

```text
nothing is hovered
nothing is selected
selected Actor was already destroyed
hovered Actor was already destroyed
widget does not exist
interaction-cell presentation was never created
reset is requested more than once
world teardown is already underway
```

Repeated calls must not produce duplicate destructive side effects.

---

## 17.4 Custom Depth / Stencil cleanup during reset

The selection system already preserves each affected mesh component's pre-existing Custom Depth / Custom Stencil configuration.

During reset cleanup:

```text
Paradox Hover stencil 230
or
Paradox Selected stencil 240
```

must be removed by restoring the exact cached pre-selection state.

Do not perform reset cleanup by forcing:

```text
Render CustomDepth = false
Stencil = 0
```

unless that was genuinely the original component state.

If WorldState later restores that Actor's own render state as part of the world snapshot, both systems must remain compatible because Paradox selection cleanup restores what it captured rather than inventing a baseline.

---

## 17.5 Interaction-cell presentation cleanup

Selected interaction-cell rendering is transient presentation and belongs to the current selection session.

On WorldState reset start:

```text
release/clear only the GridWorld presentation session/handle owned by selection
```

Do not globally clear GridWorld runtime visualization.

Do not remove presentation owned by:

```text
path previews
navigation debug
other gameplay systems
other presentation sessions
```

If GridWorld provides a handle/session ownership API, preserve and release that exact handle.

If a generic handle extension is required, make the smallest generic change inside GridWorld.

Do not add Paradox-specific reset logic inside GridWorld.

---

## 17.6 World-space widget cleanup

On reset start:

```text
selected Actor widget
    -> loses selection context
    -> hides/removes runtime presentation
```

The widget must not retain stale strong references to an Actor being restored or destroyed.

If the implementation keeps one reusable widget instance, clear its context before WorldState begins object restoration.

If the implementation creates per-selection widget instances, destroy/remove them through the normal lifecycle-safe path.

Do not require individual Blueprint widgets to implement reset cleanup correctly.

Native selection infrastructure owns the invariant.

---

## 17.7 Interaction Gameplay Actions are not selection cleanup

Do **not** make `ResetSelectionState()` cancel Gameplay Actions or release Smart Object claims.

Selection cleanup owns presentation only.

Running interaction execution belongs to `GameplayActions`.

Smart Object claim cleanup belongs to the running interaction action.

Required separation:

```text
WorldState reset lifecycle
    ├── Selection integration
    │       -> clears transient player-facing selection/presentation
    │
    └── Gameplay lifecycle integration
            -> may cancel/abort incompatible running actions
                    ↓
              UParadoxInteractionActionBase cleanup
                    ↓
              releases Smart Object claim
```

Do not release a Smart Object claim merely because the Actor was deselected.

Do not release a Smart Object claim from the widget.

Do not release a Smart Object claim from the selectable component.

---

## 17.8 Running interaction actions during WorldState reset

Codex must inspect the existing `GameplayActions` and `WorldState` integration before adding new behavior.

If the project already has a generic mechanism that aborts/cancels running Gameplay Actions during world reset, reuse it.

If no such mechanism exists, **do not silently add a new global GameplayActions redesign in an early milestone**.

Document the gap during Milestone 0 and handle it in the integration-hardening milestone unless the user explicitly requests it earlier.

Any eventual reset-driven action cancellation must remain outside `WorldState` core and outside the selection component.

Preferred dependency direction:

```text
ParadoxGameplay integration
    observes WorldState reset
    requests cancellation/abort through existing GameplayActions public API
```

Never introduce:

```text
WorldState -> GameplayActions
WorldState -> UParadoxInteractionActionBase
```

unless the generic plugin architecture already explicitly defines such a dependency.

---

## 17.9 Smart Object state and WorldState

Do not automatically register Smart Object slot state as WorldState snapshot data merely because interactions use Smart Objects.

For this architecture:

```text
Smart Object authored slots
    = interaction affordance

Smart Object claim
    = runtime concurrency ownership

WorldState
    = world reset authority
```

A running interaction action releases its claim through its normal action cleanup when that action is cancelled, interrupted, aborted, fails, or succeeds.

If a future gameplay feature makes Smart Object enable/disable state itself persistent puzzle/world state, design that separately.

Do not add it preemptively.

---

## 17.10 WorldState validation scenarios

Validate at least:

### Scenario A — Reset while selected

```text
Actor selected
world-space widget visible
outline uses Selected stencil 240
interaction cells visible
WorldState reset starts
```

Expected:

```text
selection cleared immediately
widget hidden/removed
interaction-cell presentation cleared
mesh pre-selection Custom Depth/Stencil restored
no stale Actor references remain
WorldState continues reset
```

### Scenario B — Reset while hovered only

```text
Actor hovered
outline uses Hover stencil 230
WorldState reset starts
```

Expected:

```text
hover cleared
original mesh render state restored
```

### Scenario C — Deselect cleanup is idempotent

```text
ResetSelectionState()
ResetSelectionState() again
```

Expected:

```text
no crash
no duplicate invalid cleanup
no unrelated presentation cleared
```

### Scenario D — Selected Actor destroyed before reset callback

Expected:

```text
cleanup tolerates invalid weak references
widget context clears safely
GridWorld presentation handle is still released
```

### Scenario E — Reset while interaction Gameplay Action is running

Selection cleanup may happen immediately.

The action's Smart Object claim must remain owned by the action until the gameplay-action reset/cancel/abort lifecycle handles it.

The selection system must not directly release the claim.

---

# 18. Debugging

Debug support must follow the root project debug rules.

Do not use `LogTemp` in committed code.

Use the owning module's existing log category and macros.

If the module lacks the required project logging infrastructure, add the smallest module-compliant solution.

Useful local debug state:

## Selection component

```text
Hovered Actor
Selected Actor
cursor hit Actor
whether widget consumed LMB
selection enabled state
```

## Selectable component

```text
Hovered
Selected
outline active
mesh components currently modified
original stencil/custom-depth state cache count
interaction cells enabled
widget class
```

## Interaction component

```text
Smart Object component
available InteractionTags
candidate slot count
slot world transforms
resolved Grid cell IDs
Free/Occupied state
rejection reason
```

## Interaction action

```text
Target
InteractionTag
resolved slot
resolved Grid cell
claim status
current action state
terminal result
```

Visual debug must be disabled by default.

Do not confuse actual player-facing outline/cell presentation with debug drawing.

---

# 19. Performance requirements

Avoid:

```text
world searches every frame
scanning every interactable every frame
rebuilding Smart Object slot data every frame
allocating one Actor per Grid cell
creating one PrimitiveComponent per interaction cell
repeatedly rewriting Custom Depth when hover target has not changed
high-frequency logs
```

Allowed active costs include:

```text
one cursor query while player selection is enabled
refreshing the currently selected Actor's interaction presentation when relevant state changes
Smart Object/GridWorld queries when an interaction is requested
```

Use Unreal Insights scopes only where the real implementation shows meaningful cost.

Potential scopes if useful:

```text
ParadoxSelection_UpdateHover
ParadoxInteraction_QueryOptions
ParadoxInteraction_ResolveGridCells
ParadoxInteraction_SubmitAction
```

Do not instrument trivial getters.

---

# 20. Explicit non-goals

Do not implement as part of this task:

```text
automatic movement to Smart Object slots
MoveTo gameplay actions for interaction
path preview to interaction slots
automatic facing
automatic animation alignment
GOAP planner
GOAP action selection
concrete Door interaction
concrete Terminal interaction
concrete Generator interaction
generic "do everything" interaction action
inventory
dialogue
pickup/drop
interaction animation framework
network replication architecture beyond preserving current project conventions
new GridWorld navigation logic
new IntentReplay recording model
serializing selection/hover/widget state into WorldState snapshots
making WorldState depend on Paradox selection or interaction classes
making WorldState directly release Smart Object claims
new TacticalPause responsibilities
new selection plugin
```

---

# 21. User-authored assets

Codex must not fabricate binary `.uasset` content.

The following assets are authored manually in Unreal Editor by the user.

Codex should accept the final asset paths from the user and use them only where the relevant milestone requires them.

## 20.1 Mandatory: Post Process outline material

Working asset name:

```text
M_PP_ParadoxSelectableOutline
```

User provides:

```text
OUTLINE_POST_PROCESS_MATERIAL_PATH = [USER_TO_FILL]
```

Requirements for the material:

```text
Material Domain = Post Process
reads Custom Depth / Custom Stencil
renders the Hover outline color/style for stencil range 230-239
renders the Selected outline color/style for stencil range 240-249
the initial runtime values are 230 for Hover and 240 for Selected
does not require code-side color parameters unless later requested
```

The user is responsible for the shader/visual result.

Codex is responsible only for the Custom Depth/Stencil state applied to Actor meshes.

If the material must be added to an existing Camera/PostProcessVolume, Codex must inspect the current project camera/post-process ownership first and make only the smallest requested integration.

Do not create a new global PostProcessVolume unless explicitly required.

---

## 20.2 Mandatory for usable UI content: Widget Blueprint derived from native template

After Codex creates:

```text
UParadoxInteractionWidgetBase
```

the user creates a UMG Blueprint derived from it.

Working asset name:

```text
WBP_ParadoxInteractionTemplate
```

User provides:

```text
DEFAULT_INTERACTION_WIDGET_PATH = [USER_TO_FILL]
```

The initial asset may be visually minimal.

The native C++ base must not depend on any specific named buttons inside this Blueprint.

The Blueprint is a presentation surface for future custom interaction widgets.

---

## 20.3 Per interactable: Smart Object Definition

Every real interactable that uses Smart Object slots requires an authored Smart Object Definition or the project's equivalent existing asset.

Example working names:

```text
SO_Door_Interaction
SO_Terminal_Interaction
SO_Generator_Interaction
```

For each content Actor the user authors:

```text
slot positions
slot rotations
interaction behavior definition
InteractionTag
reference to the correct GameplayActionDefinition
```

User provides these paths when integrating concrete Actors.

A single test Smart Object asset may be provided for implementation validation:

```text
TEST_SMART_OBJECT_DEFINITION_PATH = [OPTIONAL_USER_TO_FILL]
```

Do not make one universal Smart Object asset if different objects genuinely require different slot layouts.

---

## 20.4 Per interaction: Gameplay Action Definition

Each concrete interaction requires a normal `UGameplayActionDefinition` asset using the existing GameplayActions plugin.

Example working names:

```text
GA_Door_Open
GA_Terminal_Use
GA_Generator_Activate
```

The user creates these as content is authored.

Each Definition references a concrete Blueprint/C++ subclass derived from:

```text
UParadoxInteractionActionBase
```

No concrete action Definition is required to exist in the core architecture milestone if automated/transient test setup can validate the base class safely.

User provides concrete Definition paths later:

```text
INTERACTION_ACTION_DEFINITION_PATH = [PER_INTERACTABLE_USER_TO_FILL]
```

---

## 20.5 Potential GridWorld presentation style asset

First inspect the existing GridWorld runtime presentation implementation.

If it already has a suitable configurable visual-style asset/material capable of representing an additional project-owned Free/Occupied overlay, reuse it.

Do not request a new asset unnecessarily.

If a new presentation style asset is genuinely required, Codex must report the exact required asset class and parameters before depending on it.

Potential placeholder:

```text
INTERACTION_CELL_VISUAL_STYLE_PATH = [ONLY_IF_REQUIRED]
```

Do not create a new Grid cell mesh/material system inside `ParadoxGameplay`.

---

# 22. Implementation milestones

Each milestone is intentionally independently reviewable.

Codex must stop after the explicitly requested milestone unless the user asks to continue.

---

# Milestone 0 — Repository and API inspection

## Goal

Establish the real integration points before writing code.

## Tasks

Inspect:

```text
ParadoxGameplay module structure
Player Controller / input code
existing selectable/interaction code if any
GameplayActions public API
IntentReplay recordability and journal APIs
WorldState reset-start/reset-complete lifecycle APIs
existing WorldState <-> GameplayActions reset integration, if any
GridWorld world-to-cell APIs
GridWorld occupancy/reservation APIs
GridWorld runtime presentation APIs
Smart Object APIs and project usage
existing Custom Stencil registry/usages
existing module logs/debug controls
```

Determine:

```text
real type names
real module dependencies
real engine Smart Object behavior-definition base class
real slot query/claim APIs
real GameplayActions Blueprint/C++ extension pattern
real IntentReplay soft-reference compatibility
real WorldState reset-start notification and ordering
real existing policy for cancelling running GameplayActions during reset, if one exists
real GridWorld presentation handle/session model
```

## Output

Do not implement the feature yet.

Produce a concise implementation note documenting any required deviations from the working names in this document.

Compile baseline.

---

# Milestone 1 — Hover, selection, and outline core

## Goal

Implement Actor hover/selection without interaction logic.

## Implement

```text
UParadoxSelectionComponent
UParadoxSelectableComponent
single selected Actor
hover tracking
LMB select/toggle
RMB deselect
selection replacement
outline state
Custom Stencil 230 for Hover
Custom Stencil 240 for Selected
StaticMeshComponent + SkeletalMeshComponent application
pre-existing Custom Depth/Stencil state preservation/restoration
selection/hover delegates/hooks
idempotent ResetSelectionState()
WorldState reset-start observer integration for hover/selection/outline cleanup
local debug
tests
Docs
```

## Do not implement yet

```text
Smart Objects
interaction cells
world-space widget
Gameplay Actions interaction template
IntentReplay interaction validation
```

## Validation

At minimum:

1. Hover applies stencil 230 to all owned Static/Skeletal Mesh components.
2. Selecting the hovered Actor changes its Paradox stencil from 230 to 240.
3. Mouse exit removes the hover state when unselected.
4. Selected Actor remains on stencil 240 after mouse exit.
5. Deselect while still hovered falls back from stencil 240 to stencil 230.
6. LMB same selected Actor deselects.
7. RMB deselects.
8. Selecting B deselects A.
9. LMB on empty world does not deselect.
10. Existing Custom Depth/stencil values restore exactly.
11. Destroying a highlighted Actor/component is safe.
12. WorldState Reset Started clears hover and selection immediately.
13. Reset cleanup restores the exact pre-selection Custom Depth/Stencil state.
14. Repeated ResetSelectionState() calls are safe.

---

# Milestone 2 — World-space widget template

## Goal

Add generic selected-Actor world-space UI.

## Implement

```text
UParadoxInteractionWidgetBase
world-space UWidgetComponent presentation
selection context assignment
show/hide on selection
anchor or root+offset placement
safe context getters
generic interaction-query/request entry points may exist as stubs until Milestone 3/5
widget input priority over LMB world selection
Blueprint extension hooks
tests
Docs
```

## Asset dependency

User supplies:

```text
DEFAULT_INTERACTION_WIDGET_PATH
```

after creating a Blueprint derived from the native base.

## Do not implement

```text
specific buttons
Door UI
Terminal UI
Smart Object slot logic
movement
```

## Validation

1. Selecting Actor creates/shows the configured world-space widget.
2. Deselect hides/removes it according to chosen lifecycle policy.
3. Widget has correct selected Actor context.
4. Clicking a widget control does not toggle world selection.
5. Widget teardown is safe during Actor/world destruction.
6. WorldState Reset Started clears widget selection context and hides/removes the widget.

---

# Milestone 3 — Smart Object interaction affordance

## Goal

Make Smart Objects the authoritative authored source for interaction possibilities.

## Implement

```text
UParadoxInteractionBehaviorDefinition or verified engine-equivalent
UParadoxInteractionComponent
Smart Object component resolution
InteractionTag + GameplayActionDefinition behavior data
requester-relative interaction queries
slot runtime query
slot world transform query
Smart slot -> GridWorld FGridCellId resolution
Free/Occupied interaction-cell classification data
Blueprint/C++ read-only query API
debug
tests
Docs
```

## Important

Do not render the cells yet unless necessary for debug.

Do not execute Gameplay Actions yet.

Do not move the requester.

## Validation

1. One Smart Object slot resolves to the expected current world transform.
2. Moving/rotating the owning Actor changes the slot world transform correctly.
3. The new transform resolves to the current Grid cell.
4. Several slots resolve independently.
5. InteractionTag filtering works.
6. GameplayActionDefinition is discoverable from authored behavior data.
7. Current requester can query availability.
8. Smart Object claim/occupancy and Grid occupancy can produce `Occupied`.
9. Requester's own valid current cell is not incorrectly rejected as "occupied by self."
10. No authored Grid cell coordinate is stored in Smart Object behavior data.

---

# Milestone 4 — Selected interaction-cell presentation

## Goal

Render valid interaction cells for a selected Actor.

## Implement

```text
bShowInteractionCellsWhenSelected
selection-owned GridWorld presentation session/handle
Free state
Occupied state
refresh on relevant selected-target/availability changes
cleanup on deselection/target change/destruction
WorldState reset-start cleanup for the selection-owned GridWorld presentation session
```

Reuse the existing GridWorld runtime renderer/presentation layer.

If a small generic GridWorld presentation API extension is required, implement it inside GridWorld without Paradox semantics.

## Do not implement

```text
pathfinding
movement preview
automatic movement
GOAP
```

## Validation

1. Feature disabled -> selecting Actor does not show interaction cells.
2. Feature enabled -> only Smart Object slot-derived cells are shown.
3. Free/Occupied changes are visible.
4. Deselection clears only this selection's cell presentation.
5. Selecting another Actor replaces the previous presentation.
6. Actor movement/rotation causes slots to resolve to updated cells on refresh.
7. No duplicate per-cell Actors or Primitive Components are created.
8. WorldState Reset Started clears only the selection-owned interaction-cell presentation.

---

# Milestone 5 — Interaction Gameplay Action template and submission

## Goal

Execute interactions exclusively through GameplayActions.

## Implement

```text
UParadoxInteractionActionBase
replay-safe Target parameter
InteractionTag parameter
generic request construction/submission through existing GameplayActions APIs
resolve target
resolve interaction component
resolve current valid slot from requester position
Smart Object claim
common lifecycle cleanup
Blueprint/C++ extension hooks
read-only interaction context
controlled success/failure helpers when useful
structured failure reasons
widget RequestInteraction functional path
AI-callable same request path
tests
Docs
```

## Critical restriction

The base action must never move the requester.

If the requester is not currently in a valid interaction position:

```text
interaction fails predictably
```

## Concrete content

Do not implement:

```text
Open Door
Use Terminal
Activate Generator
```

Every concrete interactable receives its own Blueprint/C++ action later.

## Validation

1. Valid requester + valid slot can submit action.
2. Invalid current position fails without movement.
3. Target resolves from replay-safe representation.
4. InteractionTag resolves authored Smart Object behavior.
5. Smart Object claim occurs during execution, not pure validation.
6. Claim failure produces structured action failure.
7. Success releases claim.
8. Failure releases claim.
9. Cancel releases claim.
10. Interrupt releases claim.
11. Abort/world teardown releases claim safely.
12. Base action without concrete `ExecuteInteraction` behavior fails predictably rather than hanging.
13. Widget and AI use the same submission path.

---

# Milestone 6 — IntentReplay interaction validation

## Goal

Verify that the new interaction actions are recorded and replayed through the existing IntentReplay architecture.

## Preferred result

No invasive IntentReplay redesign is required.

The action should already be recorded because it is a normal accepted GameplayActions request.

## Implement only if required

The smallest generic/backward-compatible adjustment necessary for:

```text
soft target parameter recordability
InteractionTag recording
Definition identity
replay request reconstruction
```

Do not add Smart Object dependencies to IntentReplay core.

Do not add Paradox interaction types to IntentReplay core.

## Validation

1. Start IntentReplay Recording Session.
2. Submit one interaction Gameplay Action with Player origin.
3. Accepted action produces one Replay Track entry.
4. Track stores replay-safe Target and InteractionTag.
5. Track does not store hard runtime Actor pointer.
6. Track does not store Smart Object Slot Handle.
7. Track does not store runtime Grid cell handle/index.
8. Finalize track.
9. Replay creates a fresh GameplayAction request/instance.
10. Replayed interaction resolves current target/slot again.
11. Replay-generated action receives replay origin according to existing IntentReplay policy.
12. Source Replay Track remains immutable.
13. Replay Execution Journal stores the replayed action's own result.
14. If the replay requester is no longer in a valid interaction position, interaction fails normally; it does not auto-move.

---

# Milestone 7 — Integration hardening

## Goal

Finish the feature as a maintainable project system.

## Tasks

```text
full validation
editor-facing tooltips/categories
debug controls
logging
lifecycle cleanup
PIE restart safety
world teardown safety
WorldState reset lifecycle safety
running GameplayAction reset-policy validation
documentation
final automated tests
diff review
```

Validate at least:

```text
missing SmartObjectComponent
missing interaction behavior
invalid InteractionTag
missing GameplayActionDefinition
missing GameplayActionComponent on requester
unrecordable Target
unresolved Target on replay
destroyed selected Actor
destroyed target during action
destroyed requester during action
invalid Grid projection
duplicate slots resolving to same Grid cell
multiple interactions on one Actor
multiple slots for one interaction
slot unavailable between query and action start
selection/widget teardown during world reset
interaction-cell presentation teardown during world reset
running interaction action during world reset
Smart Object claim ownership during selection reset
```

Use structured failures where appropriate.

Do not silently convert invalid interaction configuration into success.

---

# 23. Suggested project folder organization

Use the existing ParadoxGameplay module structure if established.

If no stronger local convention exists, a compact feature-oriented structure may look like:

```text
Source/ParadoxGameplay/
├── Public/
│   └── Interaction/
│       ├── ParadoxSelectionComponent.h
│       ├── ParadoxSelectableComponent.h
│       ├── ParadoxInteractionComponent.h
│       ├── ParadoxInteractionActionBase.h
│       ├── ParadoxInteractionWidgetBase.h
│       ├── ParadoxInteractionBehaviorDefinition.h
│       └── ParadoxInteractionTypes.h
│
├── Private/
│   └── Interaction/
│       ├── ParadoxSelectionComponent.cpp
│       ├── ParadoxSelectableComponent.cpp
│       ├── ParadoxInteractionComponent.cpp
│       ├── ParadoxInteractionActionBase.cpp
│       ├── ParadoxInteractionWidgetBase.cpp
│       ├── ParadoxInteractionBehaviorDefinition.cpp
│       └── Tests/
│
├── CODEX/
└── Docs/
```

Do not reorganize unrelated files merely to match this proposal.

Expose only headers that genuinely need to be public.

---

# 24. Definition of done for the complete feature

The complete architecture is established only when:

- the system lives in `ParadoxGameplay`;
- exactly one Actor can be selected;
- hover and selection follow the required mouse semantics;
- selectable Static/Skeletal Mesh components use the reserved Hover/Selected Custom Stencil ranges;
- original Custom Depth/Stencil state is restored safely;
- the outline shader remains user-authored;
- selected Actors can show an interactive world-space widget;
- the widget base is Blueprint-extensible and contains no concrete gameplay UI;
- Smart Objects own authored interaction slots and availability;
- Smart Object slots resolve dynamically to current GridWorld cells;
- optional interaction-cell visualization shows Free/Occupied;
- GridWorld remains the spatial/occupancy/presentation authority;
- no second Grid renderer exists in ParadoxGameplay;
- `UParadoxInteractionActionBase` is extensible from Blueprint and C++;
- every concrete interactable may provide its own Gameplay Action;
- interaction execution always passes through GameplayActions;
- the base interaction action never moves the requester;
- interaction validates current slot/cell instead of auto-positioning;
- Smart Object claims are released on every terminal lifecycle path;
- IntentReplay records the interaction as a semantic Gameplay Action request;
- Replay Track stores replay-safe target identity and InteractionTag;
- Replay Track does not depend on transient Smart Object slot handles;
- replay creates a fresh Gameplay Action instance and re-resolves current runtime interaction state;
- WorldState reset-start clears transient hover and selection state;
- WorldState reset-start hides/removes the selected world-space widget;
- WorldState reset-start clears only selection-owned GridWorld interaction-cell presentation;
- WorldState reset cleanup restores the exact pre-selection Custom Depth/Stencil state;
- selection/hover/widget/interaction-cell presentation are not serialized as WorldState snapshot data;
- WorldState core does not depend on Paradox selection/interaction classes;
- selection cleanup does not directly release Smart Object claims or cancel Gameplay Actions;
- any running-action reset policy uses existing GameplayActions public lifecycle APIs from the Paradox integration layer;
- no concrete Door/Terminal/Generator action is implemented by this task;
- relevant automated tests pass;
- documentation is updated;
- the affected editor target compiles successfully.
