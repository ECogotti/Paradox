# Paradox Teleport Gate

`AParadoxTeleportGate` is the Character-specific paired-transfer endpoint. It transfers an
`AParadoxCharacter` only through the semantic `Enter` interaction; collision overlap and Puzzle
Emitters never initiate a teleport. The Gate Actor itself never ticks. Its acquired Enter Gameplay
Action enables action ticking only while driving an ingress or egress segment. Player and clone
Characters use the same Gameplay Action path.

## Authoring

Place two Gate instances in the same World and configure each `LinkedEndpoint` to point back to
the other. Both inherited `PuzzleReceiver` components must be active before Enter can start. Paired
endpoints enable the opt-in uncontrolled fallback, so an unwired Gate is active without a placeholder
Emitter. As soon as a valid Controller registers, its result becomes authoritative and may deactivate
the Gate. The pair is rejected when it is missing, self-linked, non-reciprocal, cross-World, or linked
to a different endpoint class.

Position and rotate the inherited `TransferAnchor` **inside the tunnel**. At the source it is the
end of ingress; at the destination it is the collision-safe teleport location and its Arrow
rotation becomes the Character rotation passed to `TeleportTo`. The anchor does not need a
walkable GridWorld/NavMesh cell, but the physical tunnel between the external slot and anchor must
be clear enough for Character Movement. The Arrow may be authored either at the Character actor
origin or on the tunnel floor. Collision-safe destination resolution first preserves the legacy
actor-origin convention; if that would move too far from the authored point, it retries the Arrow
as a feet position by adding the Character capsule half-height. Any further collision adjustment
must remain within **Tunnel Traversal Acceptance Radius**, so a blocking tunnel is not mistaken for
a valid floor correction.

The native Gate creates:

- `UParadoxSelectableComponent`;
- `USmartObjectComponent`, using
  `/Game/Data/GridWorld/SOD_TeleportGate` for one external approach position;
- `UParadoxInteractionComponent`, with the Enter definition
  `/Game/Data/GameplayActions/DA_ParadoxEnterTeleportGate`;
- `PermanentNavigationBlocker`, a `UGridNavigationModifierComponent` that always blocks its
  authored footprint;
- `TransitNavigationBlocker`, a second modifier that is non-blocking while idle and blocks only
  during the forced ingress, teleport, and egress of an acquired Enter transfer.

Author the two navigation footprints independently in the Gate Blueprint. The permanent blocker
should cover the solid body or cells that can never be traversed; it must not overlap the external
Smart Object interaction cell, otherwise Enter cannot approach the Gate. The transit blocker may
cover that external cell or the temporary passage region: native sequencing first validates and
reserves the destination. The NavMesh/GridWorld approach to the source slot remains open. Only when
the Enter action starts its forced ingress does it enable the transit blockers on **both** endpoints
for the complete ingress/teleport/egress execution. Commit revalidates the already-owned atomic
reservation without
requiring the intentionally blocked destination cell to become generally walkable. Both transit
modifiers reopen during Transfer-In finalization for placement and parking. If the two modifier
footprints overlap, the permanent blocker still wins and the transit toggle has no visible effect in
the shared cells.

Each Gate must resolve exactly one Smart Object slot. That slot is outside the tunnel on a valid,
walkable GridWorld cell; Data Validation rejects zero or multiple slots. The Gate also validates
that both completion modes remain `Explicit` and that **Tunnel Traversal Acceptance Radius** is
positive.

## Enter contract

Use `EvaluateEnter(Character, Action)` for side-effect-free validation and
`TryEnter(Character, Action)` from the running native Enter action. The Gate accepts only:

- an `AParadoxCharacter` with Controller, Character Movement, and Gameplay Actions;
- `Requester == Subject`;
- `UParadoxEnterTeleportGateInteractionAction` targeting that exact Gate with
  `Interaction.Paradox.TeleportGate.Enter`;
- a reciprocal `AParadoxTeleportGate` pair whose two Receivers are active and endpoints are idle.

The Enter definition owns Movement and Interaction locks and uses `JournalRequirement=Required`.
The recorded payload contains only the semantic target and interaction tag; exit coordinates and
transforms are resolved fresh during execution. This gives player and Intent Replay clones the
same path while allowing current collision and GridWorld state to remain authoritative.

Interaction-catalog preflight validates the stable Gate contract: the pair, Receivers, unique
external slots, walkable destination cell, traffic availability, and registered finite anchors.
It deliberately does **not** run `FindTeleportSpot`. Collision-safe tunnel placement is transient
world state and is resolved only during acquired transfer preparation, after the Character has
reached and claimed the source slot. Consequently a valid distant Enter is not rejected before its
approach because of the Character's current placement near unrelated collision. If preparation
cannot obtain a safe destination anchor, the accepted action fails normally and releases the pair,
the traffic reservation, and the Smart Object claim.

## Transfer and GridWorld transaction

After the Character reaches and claims the external Smart Object slot, preparation:

1. stops controller path following and clears Character Movement velocity;
2. stores the source slot transform/cell for pre-commit recovery;
3. resolves and atomically claims the destination's external slot cell;
4. resolves a collision-safe destination tunnel anchor with `UWorld::FindTeleportSpot`;
5. starts forced Character Movement input from the source slot to the source anchor.

The two internal tunnel segments do not use `UPathFollowingComponent`, GridWorld path solving,
NavMesh projection, or a synthetic navigation path. While a segment is active, the Enter action
ticks and calls `AddMovementInput(DirectionToTarget, 1.0, true)` on the Character. The forced flag
keeps the acquired action authoritative even if ordinary pawn input is being ignored. Character
Movement still owns velocity, acceleration, locomotion, and collision, so the tunnel geometry must
remain physically traversable; its internal anchor still requires no NavMesh coverage. Direction,
arrival, and watchdog distance are evaluated on world XY: a floor-authored Arrow therefore does not
prevent ingress from starting because the Character actor origin is at the capsule center.

Each segment receives a watchdog duration of `2 * Distance / MaxWalkSpeed`, measured when that
segment starts. Reaching **Tunnel Traversal Acceptance Radius** completes it normally. If the
watchdog expires first, the transaction deliberately advances anyway: ingress performs the
teleport, while egress finalizes Transfer-In and collision-safely places the Character on the
reserved external interaction slot when it has not arrived there. A non-positive `MaxWalkSpeed`
uses a minimal watchdog delay so the action cannot remain locked forever. Pausing the Gameplay
Action pauses both movement input and the active watchdog; resume continues both.

Completed or timed-out ingress calls `CompleteTransferOut`. The commit revalidates the destination slot claim
and anchor, teleports to the internal destination anchor, removes the obsolete source
corridor/parking, and starts forced egress to the destination slot. The temporary destination goal
claim remains owned throughout egress. Only after the external slot is reached does the action call
`CompleteTransferIn`; a watchdog expiry requests the same completion path. Finalization aligns the
Character to the reserved slot when necessary, converts the claim into parking, releases the
temporary claim, refreshes occupancy, and completes Enter successfully.

Starting forced ingress enables both `TransitNavigationBlocker` components; action submission,
pathfinding approach, and source-slot claiming leave them open. Both remain blocking through
ingress, `TeleportTo`, and egress. Normal finalization starts only after the Character reaches the
destination interaction slot, then reopens both immediately before validating placement and parking.
On an egress watchdog expiry, reopening and forced slot recovery happen synchronously in that same
completion call, after which Enter reaches its terminal success.
Preparation failure, action cancellation, endpoint reset, subject destruction, World State restore,
and EndPlay also restore both transit modifiers to non-blocking state. These are GridWorld overlay
updates through `SetBlockingEnabled`; they do not rebuild sampled navigation geometry.

The Enter action therefore remains `Running` and retains Movement/Interaction locks plus the
source Smart Object claim for the complete ingress/teleport/egress sequence. Affordance refreshes
validate the acquired claim without requiring the Character to remain at the original cell.

## Failure, reset, and lifetime

Before commit, cancellation, reset, or a real transaction failure collision-safely recovers the
Character to the source slot and releases the destination reservation. Collision that prevents
physical ingress is handled by the watchdog and still advances to the teleport. After commit there
is no rollback: cancellation or finalization recovery returns the Character to the destination
slot and realigns parking/occupancy there. Collision that prevents physical egress is handled by
the watchdog through this same destination recovery. Teardown skips spatial recovery after world
objects have begun EndPlay.

Action cancellation, endpoint reset, World State restore, subject destruction, endpoint
destruction, and EndPlay cancel by operation ID. Cleanup is idempotent; stale callbacks cannot
affect a replacement operation. Receiver deactivation after acquisition does not abort the current
transaction, but blocks the next one.

## Debugging and verification

The Gate uses the inherited event-driven paired-transfer diagnostics. Enable both:

```text
Paradox.PairedTransfer.Debug 1
Gate.bEnableDebug = true
```

Run focused automation after compiling `ParadoxEditor`:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests Paradox.TeleportGate; Quit" -TestExit="Automation Test Queue Empty" -log
```

The suite covers the unique external slot, non-walkable internal anchors, forced movement input in
both directions, floor-authored anchors/capsule-origin correction, planar timeout calculation from
`MaxWalkSpeed`, pause/resume, forced ingress commit and
successful egress recovery on timeout, commit only after ingress, action lifetime through egress,
deferred parking, pre/post-commit recovery, player/clone parity, coordinate-free replay, Receiver
deactivation after acquisition, stale callbacks, cancellation cleanup, no overlap authority,
deferred collision-safe placement preflight, permanent/transit navigation modifier defaults and
pair-wide lifetime, no Gate Actor Tick, and no Emitter.
