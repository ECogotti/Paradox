# Architecture

## Responsibilities

`UAnimNotify_Footstep` contains only animation-authored request data. It finds
`UFootstepComponent` on the mesh owner and forwards the request together with the
source mesh. It does not trace, select a surface response, or spawn feedback.

`UFootstepComponent` owns request validation, mesh and socket resolution, the floor
query, event publication, optional default feedback, and runtime diagnostics. It
does not tick. Cached skeletal meshes are weak references.

`UFootstepProfile` is a data asset that maps `EPhysicalSurface` values to copied
`FFootstepSurfaceResponse` values. The copied response is transient stack data; no
per-footstep `UObject` is created.

## Request flow

For each accepted request the component performs this sequence:

1. Reject requests outside the component's active gameplay lifetime.
2. Sanitize intensity to a finite value in `[0, 1]`.
3. Resolve the skeletal mesh according to the configured policy.
4. Resolve the socket using strict override/foot/default precedence.
5. Reject missing or nonexistent sockets.
6. Build trace start and end from the socket's world location.
7. Execute one line trace or sphere sweep with physical-material return enabled.
8. Build an `FFootstepEvent` snapshot.
9. Resolve an exact surface response, then the optional profile fallback.
10. Broadcast the native delegate.
11. Broadcast the Blueprint delegate.
12. If the owner is still valid, run enabled cosmetic feedback.

The default line trace begins 10 cm above the socket and travels 50 cm downward on
`ECC_Visibility`. It is non-complex and ignores the owning actor by default. The
alternative sphere sweep defaults to a 4 cm radius.

## Hit and miss semantics

A blocking hit is valid even when `FHitResult::PhysMaterial` is null. In that case
the event's surface is `SurfaceType_Default`, allowing an explicit default-surface
entry or profile fallback to handle it.

A miss is not public by default. When `Broadcast On No Floor Hit` is enabled, the
component publishes an event with `bHadValidFloorHit == false`, the trace end as its
world location, and no cosmetic feedback. The submit function then returns `true`.

Missing profiles and missing surface responses do not invalidate a floor event.
They only prevent default feedback. Missing meshes and sockets prevent event
generation because the trace cannot be defined reliably.

## Lifetime and safety

The component accepts requests after `BeginPlay` and stops before `EndPlay` cleanup.
It unbinds native observers and clears weak/cache state during shutdown. No timer or
per-frame work is registered.

Delegate callbacks are allowed to alter or destroy gameplay objects. After both
delegate stages, the implementation validates the component, owner, and world before
feedback. Cosmetic work is also globally skipped on dedicated servers.

`FFootstepEvent` is a point-in-time snapshot, but its object fields are ordinary
Unreal object references and may later become invalid. Consumers should use them
while handling the event or validate them before later use.

## Extension points

Use the native or Blueprint delegate to drive project-specific consequences such as
camera effects, gameplay tags, analytics, or an external AI-hearing system. The
plugin intentionally does not know about those domains.

Default feedback is data driven:

- exact or fallback response selection;
- independent component and response spawn flags;
- volume, pitch, Niagara scale, decal size, and decal lifetime;
- independent asset references.

Project code can ignore built-in feedback, disable its families, and consume only
the neutral event without changing the plugin.

## Module boundary

`FootstepSystem` is a single runtime module. Runtime source does not depend on editor
modules, project modules, AI, perception, or replay systems. Editor-only data
validation is compiled behind `WITH_EDITOR`; draw helpers are compiled behind
`ENABLE_DRAW_DEBUG`.
