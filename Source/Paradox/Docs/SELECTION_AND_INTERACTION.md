# Paradox selection and world-space interaction UI

## Milestone 0 integration note

The project runtime module is named `Paradox`, rather than the architecture document's working
name `ParadoxGameplay`. `AParadoxPlayerController` owns mouse input, GridWorld pointer prediction,
and the new selection authority. Its single cursor hit is shared with
`UGridCellPointerComponent::UpdateFromHitResult` and the world-widget virtual pointer.

The project uses Unreal Engine 5.8. A clean `ParadoxEditor` baseline was compiled before the
Milestone 1-2 implementation. World State exposes reset start through
`UWorldStateSubsystem::OnRestoreStartedNative`; the selection component observes that event and
clears presentation before restoration mutates world state. Gameplay Actions already own action
lifecycle, Intent Replay records accepted semantic requests, and GridWorld owns cell identity,
occupancy, reservations, and runtime cell presentation.

Milestones 3-4 enable the engine `SmartObjects` plugin and add `SmartObjectsModule` as a public
runtime dependency. Smart Objects remain the authority for slot identity, transforms, conditions,
enabled state, and claims. Paradox adds a read-only multi-interaction catalog and GridWorld
presentation around that authority; it does not add a Paradox Smart Object behavior definition.

## Runtime composition

`AParadoxPlayerController` owns:

- `UParadoxSelectionComponent`, the authority for one hovered and one selected Actor;
- `UParadoxPuzzleCircuitRendererComponent`, the read-only circuit presentation for that same
  selected Actor;
- `UParadoxWidgetInteractionComponent`, a thin `UWidgetInteractionComponent` adapter using the
  controller's shared custom hit rather than a second world trace.

An Actor becomes selectable by owning `UParadoxSelectableComponent`. The native Pressure Plate,
Vertical Barrier, and Chrono Spawn include it by default, so their Blueprint children inherit the
capability without adding another component.

Pressure Plate and Vertical Barrier additionally own `USmartObjectComponent` and
`UParadoxInteractionComponent`. Chrono Spawn deliberately remains selectable-only.

Pressure Plate and Vertical Barrier also enable `bShowPuzzleConnectionsWhenSelected`; Chrono Spawn
does not. Circuit rendering, stencil ownership, routing, lifecycle, and troubleshooting are
documented in [Puzzle Circuit Overlay](PUZZLE_CIRCUIT_OVERLAY.md).

## Input behavior

- Hover changes only when the shared cursor hit resolves to a different selectable Actor.
- RMB selects an unselected Actor, replaces the current selection, or toggles the selected Actor
  off. RMB without a selectable under the cursor clears the current selection.
- LMB remains the navigation command even while a selectable Actor is hovered. Hover outline and
  GridWorld path preview are therefore presented at the same time.
- An interactive world-widget control receives LMB before navigation, so activating a widget
  control does not also submit click-to-move.
- Touch and Chrono Spawn selection retain their existing dedicated input paths. During the
  `ChronoSpawnSelection` phase the controller clears generic mouse hover and routes the pointer to
  the time-loop authority.
- While Drop targeting is active, it has higher input/presentation priority than ordinary selection
  and movement: LMB confirms the current valid GridWorld cell and RMB cancels. Invalid LMB keeps
  targeting active. World widgets and Chrono Spawn selection retain their earlier arbitration.

Pickupable Actors use the same selection and Smart Object interaction pipeline. Their native
catalog always offers Pickup and Swap; availability is decided by the Character's inherited
single-slot inventory. Held pickupables disable selection, Smart Object interaction and occupancy.
See [Paradox single-slot inventory](INVENTORY.md) for authoring, Drop targeting, replay and reset.

Item Slot Actors use that pipeline as semantic targets for Insert and Pickup-from-Slot. Their
native four-position Smart Object and action catalog require no Blueprint assembly. Occupancy and
activity transitions call `NotifyInteractionAffordanceChanged`, so a selected slot immediately
refreshes its requester-relative options. Inserted items disable their own selection, Smart Object,
interaction and GridWorld occupancy; only the slot remains interactable. See
[Paradox insertable items and item slots](ITEM_SLOTS.md).

## Outline setup

The selectable component outlines only direct Actor-owned `UStaticMeshComponent` and
`USkeletalMeshComponent` instances:

- stencil `230`: hovered;
- stencil `240`: selected;
- selected has priority over hovered.

On the first active presentation transition, each mesh's original Custom Depth enabled flag,
stencil value, and stencil write mask are cached. They are restored exactly when neither hover nor
selection remains. Meshes added during an active state are reconciled on the next meaningful state
transition; no component scan runs every frame. Destroyed meshes are skipped safely during cleanup.

Custom Stencil ranges `210-219`, `220-229`, `230-239`, and `240-249` are reserved respectively for
Puzzle Input, Puzzle Output, hover, and selection.
`r.CustomDepth=3` must remain enabled. Rendering is authored in a camera or Post Process Volume
using:

`/Game/Vfx/VfxMasterMaterials/MM_PP_Outline`

Runtime code does not add that material to cameras or create a global Post Process Volume. A level
without the material still updates Custom Depth/Stencil correctly but displays no outline.

Pressure Plate outlines both `FloorMesh` and `PlateMesh`. Vertical Barrier outlines its direct
`BarrierMesh`; a separate frame Actor needs its own selectable component. Chrono Spawn outlines its
`SelectionMesh` when generic selection is active.

## Optional world-space widget

Set `SelectionWidgetClass` on the selectable component to a Blueprint derived from
`UParadoxInteractionWidgetBase`. Leaving it unset is valid and creates no widget.

The component lazily creates one `UWidgetComponent`, reuses it while hidden, and destroys it during
Actor teardown. It uses World space, default draw size `400 x 160`, and an offset of `Z +100`.
`WidgetAnchor` may reference a Scene Component owned directly by the selected Actor; an external,
invalid, or empty reference falls back to that Actor's root. Do not use a Pressure Plate or another
gameplay Actor as the anchor for a Door widget. By default, the widget forward vector follows the
selecting camera's inverted forward vector (`-CameraForwardVector`); disable
`bFaceOwningPlayerCamera` to use `WidgetRelativeRotation` instead. Its UI collision remains
queryable by the widget pointer but never generates gameplay overlap events, blocks Pawns, or
affects navigation. Pickupable collision normalization does not overwrite this selection-owned UI
query state, so a dropped item can expose the same hoverable/clickable widget again.

Before showing the widget, the native base receives read-only context for the selected Actor,
selectable component, interaction component, selection authority, owning Player Controller, and
requester Pawn. Deselect, World State restore start, and teardown clear its option cache and context
before hiding or destroying the widget. Blueprints extend presentation through
`OnSelectionContextAssigned`, `OnSelectionContextCleared`, and
`OnInteractionOptionsRefreshed`.

The native widget intentionally contains no authored controls. A concrete Blueprint reads
`GetInteractionOptions`, may explicitly call `RefreshInteractionOptions`, and submits an exact tag
through `CanRequestInteraction` or `RequestInteraction`. Widget requests use the possessed Pawn as
the requester, `GameplayAction.Origin.Player` as origin, and the Player Controller as request
source. `OnInteractionRequestAccepted` and `OnInteractionRequestRejected` provide presentation
hooks for the structured result. AI and other C++/Blueprint callers use the same request API on the
target's interaction component.

## Smart slots and multiple interactions

`UParadoxInteractionComponent::InteractionDefinitions` is a catalog, not a one-slot/one-action
mapping. Each `FParadoxInteractionDefinition` contains:

- `InteractionTag`, the stable identity used by UI and later request submission;
- a soft reference to the `UGameplayActionDefinition` submitted for that interaction;
- `SlotActivityRequirements`, evaluated against each current Smart Object slot's Activity Tags.

Every matching definition produces an option for every matching enabled slot. Consequently one
slot can expose several interactions and one definition can apply to several slots. Empty catalogs
and a null Smart Object Definition are valid native defaults, allowing Blueprint children to assign
content later. When assigning a Definition, author an engine-valid Smart Object asset with the
required slots, Activity Tags, conditions, and transforms. Paradox reads the engine asset but does
not require or provide `UParadoxInteractionBehaviorDefinition`.

Use `QueryInteractionOptions` for the whole catalog or `QueryInteractionOptionsByTag` for a tag
subtree. Results are sorted deterministically by slot, interaction tag, then action asset and expose
the current slot transform and projected `FGridCellId`. Each option is:

- `Free`: the slot and projected cell are available to the requester;
- `Occupied`: another owner holds a Smart Object claim, GridWorld occupancy/reservation, or an
  overlapping traffic reservation;
- `GridUnresolved`: the current slot transform cannot resolve to a published GridWorld cell.

The requester is allowed to observe options covered by its own Smart Object claim, occupancy,
reservation, or traffic identity. Queries evaluate current Smart Object selection conditions and
availability but never acquire a claim. `HasFreeInteractionOption` is only a convenience query; it
does not reserve anything.

`RefreshInteractionSources` reconciles only direct Smart Object components owned by the same Actor.
At runtime the component observes Smart Object events and transform changes and publishes
`OnInteractionAffordanceChanged`. Local spatial diagnostics require both instance
`bEnableDebug=true` and the global gate:

```text
Paradox.Interaction.Debug 1
```

## Gameplay Action authoring and submission

Every non-empty catalog entry must reference a `UGameplayActionDefinition` configured as follows:

- `Instance Class` derives from `UParadoxInteractionActionBase`;
- `Journal Requirement` is `Optional` or `Required`, never `Disabled`;
- the Property Bag contains `Target` as a soft `AActor` object and `InteractionTag` as
  `FGameplayTag`, with those exact field names and types;
- any additional authored parameters needed by the concrete action may remain in the same bag.

`QueryInteractionOptionsByTag` deliberately accepts a tag subtree for presentation. Execution is
stricter: `CanRequestInteraction` and `RequestInteraction` require a concrete exact catalog tag and
never choose a child interaction implicitly. They also require the requester to directly own a
`UGameplayActionComponent`, the target to have a replay-stable world-authored identity, and at least
one free matching slot whose exact GridWorld cell is reachable by a complete controller-aware path.
In normal runtime Worlds this identity is represented by `RF_WasLoaded`; the Interaction Component
also preserves authored provenance across its load/PIE-duplication lifecycle. Actors actually
spawned at runtime remain unrecordable. No interaction rotates or teleports the requester.

The Gameplay Action Definition soft reference is loaded synchronously only during explicit
availability validation or request submission. Hover and the raw cell-overlay query do not load
it. `FParadoxInteractionRequestResult` reports request status, query status, scheduler
submission result, and a diagnostic so rejection is not reduced to a boolean.

## Interaction action lifecycle and claim ownership

`UParadoxInteractionActionBase` treats the Actor owning the requester's
`UGameplayActionComponent` as the spatial requester. `RequestSource` records where the request came
from; it does not replace that spatial identity. The public requester, target, and interaction
component getters are phase-safe: during `CanStartAction`, which runs before `OnActionInit`, they
resolve from the already initialized owning Action Component and immutable semantic Property Bag.
Concrete precondition hooks must therefore use these getters instead of assuming the runtime caches
have already been populated. `CanStartAction` repeats semantic, effect and
reachability preflight without claiming. `OnActionStarted` orders free candidates by complete-path
cost, cell identity and slot identity, then acquires a normal-priority Smart Object claim before
movement. A candidate lost to a claim or movement contention race is released and the next
unattempted cell is tried. Arrival requires the exact claimed cell and repeats target, claim and
effect validation before execution.

Derive a native or Blueprint action class and implement `ExecuteInteraction`. The protected hooks
`CanSatisfyInteractionPreconditions`, `CanExecuteInteraction`,
`IsInteractionOutcomeSatisfied`, `OnInteractionContextResolved`, and `OnInteractionSlotClaimed` allow
policy and presentation around the invariant-preserving native path. Finish through
`CompleteInteractionSuccess` or `CompleteInteractionFailure`; the inherited empty implementation
fails immediately with `GameplayAction.Result.Failure.Paradox.Interaction.NotImplemented` and never leaves
an action suspended.

Read-only getters expose the requester, target, target interaction component, exact tag, selected
slot, slot transform, projected GridWorld cell, and claim state. Claim release is centralized and
idempotent in action cleanup, covering success, failure, cancellation, interruption, abort,
requester/target teardown, and world teardown. The action registers the engine's slot-invalidation
callback, forwards pause/resume/cancel to its transient movement executor, and observes affordance
changes. An outcome completed externally during movement succeeds; lost Activate prerequisites
fail immediately. Selection reset only removes hover, outline, widget, cache, and
cell overlay; it never cancels a Gameplay Action or releases its claim.

During Intent Replay the newly created instance belongs to the recipient Character's Action
Component. A clone therefore becomes the requester automatically; the Player Controller stored as
the original request source is neither recorded as spatial authority nor reused for inventory,
movement, slot resolution, or effects.

### Standard Receiver and Emitter actions

Create Data Assets from `UParadoxReceiverInteractionActionDefinition` for Open/Close and from
`UParadoxEmitterInteractionActionDefinition` for Signal On/Off. Both Definition classes already
provide `Target`, `InteractionTag`, `NavigationFilter`, `AcceptanceRadius`, `bAllowStrafe`, Movement
and Interaction locks, Reject blocked policy, Optional journaling, and the correct native Instance
Class.

Receiver assets add `ReceiverComponentName` and `Command`. The target Receiver must use `Manual`;
Activate requires its Controller prerequisites, while Deactivate does not. With an empty component
name the target must own exactly one Receiver. Emitter assets add `EmitterComponentName`, exact
`SignalTag`, and `Command`. Activate is allowed with no exact-signal consumers, or when at least one
exact primary Graph link has a Bypassed/Open gate. Deactivate ignores gates and clears an active raw
signal. Multiple matching components always require an explicit component name.

## Intent Replay contract

Accepted requests use the existing Gameplay Actions journal. No separate interaction recorder is
used. The recorded intent keeps the Gameplay Action Definition identity and authored Property Bag,
including only semantic `Target` and `InteractionTag` plus any action-specific parameters. Slot and
claim handles, transforms, projected cells, occupancy, and reservations are runtime state and are
not recorded.

Replay constructs a fresh action instance, resolves the already-loaded soft target, and reevaluates
the requester's position, reachable Smart Object slots, Puzzle prerequisites, path and claim.
Runtime path, cell, slot and claim data are never recorded. A runtime-spawned or unresolved target
fails explicitly; replay does not fabricate identity. Playback writes results to its own execution journal and never mutates the
source track.

## Selected interaction cells

`bShowInteractionCellsWhenSelected` on the selectable controls only the GridWorld affordance
overlay. It defaults to false and is enabled natively on Pressure Plate and Vertical Barrier. On
every selectable target with an interaction component, `UParadoxSelectionComponent` maintains one
event-driven option cache plus one availability result per exact catalog tag, queried with the
possessed Pawn. Options feed the overlay; availability feeds widget button state and, when opted
in, one weak-owner GridWorld overlay session. It refreshes after Smart Object affordance,
transform, GridWorld, occupancy/reservation, traffic changes, or an explicit refresh; there is no
permanent polling. Receiver state/prerequisites, Emitter signals, Puzzle Graph topology/link state,
and requester action completion also trigger refresh.

Options are deduplicated by persistent cell. A cell is `Primary` when at least one option on it is
`Free`; otherwise a resolved cell is `Secondary`. Free therefore wins regardless of catalog order.
The shared project style is:

```text
/Game/Data/GridWorld/DA_ParadoxGridCellStyle
```

It uses `/GridWorldSystem/Presentation/SM_GridWorldBlock` and
`/GridWorldSystem/Presentation/M_GridRuntimeCell` for both path cells and interaction cells. The
overlay is local presentation only: it never changes topology, walkability, occupancy,
reservations, or navigation revisions. Hover, direct cell selection, interaction overlays, path
state, and navigation state remain independently stored by GridWorld.

The session is replaced when the selected target changes and released on deselect, Actor teardown,
selection component teardown, and World State restore-start. An empty catalog, null Smart Object
Definition, no matching slot, or all unresolved slots simply produces no interaction-cell session.

## Blueprint API and debugging

Use `GetSelectedActor`, `GetHoveredActor`, `DeselectCurrentActor`, and
`ResetSelectionState` on the selection component. Selectable state is read-only through
`IsHovered`, `IsSelected`, `GetSelectionPresentationState`, and `GetInteractionWidget`.

Selection, selectable, and interaction delegates notify state changes after native state is valid. Selectable
BlueprintNativeEvent hooks may replace or extend presentation without owning the selection
invariant. Local `bEnableDebug` logs only meaningful transitions through `LogParadox`; it is off by
default and never logs per frame.

## Troubleshooting

- No visible outline: verify the level's Post Process setup, `MM_PP_Outline`, and
  `r.CustomDepth=3`.
- Actor does not hover: verify it owns a selectable component, `bCanBeHovered`, and a direct mesh
  that blocks the Visibility trace.
- Widget is not visible: verify the assigned Blueprint derives from `ParadoxInteractionWidgetBase`,
  has visible authored content, and is assigned on the selected Actor's inherited selectable
  component. Keep camera-facing enabled unless the anchor has an explicitly authored rotation.
- Selecting an Actor affects a trigger: do not use that trigger Actor as `WidgetAnchor`. Anchors are
  intentionally restricted to components owned by the selected Actor, and generated widgets never
  emit overlap events.
- Actor does not select: verify `bCanBeSelected` and that Chrono Spawn selection is not currently
  the dedicated pointer authority, then use RMB.
- Widget does not appear: assign a concrete Blueprint derived from the native widget base and
  verify the anchor/root component is valid.
- Widget click also navigates: ensure controls are hit-testable and interactable; decorative widget
  regions intentionally do not consume LMB.
- Interaction request is rejected: inspect `FParadoxInteractionRequestResult`. Verify an exact
  catalog tag, requester Gameplay Action component, world-authored target, free reachable slot,
  Definition class/journaling, component name, Puzzle prerequisites/gates, and Property Bag fields.
- Button remains disabled: inspect `GetInteractionAvailability`. `NoFreeSlot` means authority or
  occupancy is blocking every slot; `NoReachableSlot` means no complete GridWorld path;
  `EffectUnavailable` means the endpoint is already in the requested state or concrete policy
  rejected preflight; `SchedulerRejected` means an execution lock is busy.
- Interaction starts but immediately fails: implement `ExecuteInteraction` in the concrete action
  and finish it with one of the protected completion wrappers. Recheck current slot availability if
  the failure reports a race between submission and start.
- No interaction cells: verify the selected Actor opted in, owns an interaction component, its
  Smart Object Definition is valid and registered, catalog tags match the slot Activity Tags, and
  the slot transform projects to a published GridWorld cell.
- Interaction cell remains orange/red: inspect Smart Object claim state, GridWorld
  occupancy/reservations, and traffic reservations. The query ignores only identities belonging to
  the supplied requester.
- No puzzle wires: verify `bShowPuzzleConnectionsWhenSelected`, initialized Puzzle Controller
  topology, and the post-process stencil setup. See [Puzzle Circuit Overlay](PUZZLE_CIRCUIT_OVERLAY.md).
