# Footstep System

`FootstepSystem` is a standalone Unreal Engine 5.8 runtime plugin that converts
animation foot-contact markers into surface-aware events. A component performs one
floor query, publishes an immutable event snapshot, then may play data-driven audio,
spawn Niagara, and place a decal.

The plugin has no dependency on Paradox gameplay modules and does not emit AI noise,
replicate requests, or create an object for each step. Its only module dependencies
are `Core`, `CoreUObject`, `Engine`, `PhysicsCore`, and `Niagara`.

## Installation

The plugin is enabled in `Paradox.uproject`. For another project:

1. Copy `FootstepSystem` into the project's `Plugins` directory.
2. Enable **Footstep System** and **Niagara** in the Plugins window.
3. Restart the editor and compile the project.
4. Configure the project's Physical Surfaces before authoring profiles.

No binary assets ship with the plugin. Sounds, Niagara systems, decal materials,
Physical Materials, and the profile must be created for the receiving project.

## Runtime setup

1. Add `Footstep Component` to the actor that owns the animated skeletal mesh.
2. Assign a `Footstep Profile`.
3. Configure `Left Foot Socket`, `Right Foot Socket`, and, when needed,
   `Default Foot Socket`.
4. Add `Footstep` notifies to foot-contact frames in animation sequences.
5. Set each notify's foot, optional socket override, and normalized intensity.

The default mesh policy uses the skeletal mesh that fired the notify. A direct call
to `SubmitFootstepRequest` instead resolves and weakly caches the owner's first
skeletal mesh. `Explicit Component` uses the configured component reference.

Socket selection is strict:

1. request socket override;
2. configured left or right socket;
3. configured default socket for `Unspecified`.

A missing name or a socket that does not exist rejects the request. The component
does not guess another socket.

## Events

`SubmitFootstepRequest` is available to Blueprint and C++. The C++ animation path is
`SubmitFootstepRequestFromAnimation`. A `true` return value means that a public
event was actually broadcast; it does not mean that cosmetic feedback spawned.

Blueprint consumers bind to `On Footstep Generated`. C++ consumers can bind without
reflection overhead:

```cpp
FootstepComponent->OnFootstepGeneratedNative().AddUObject(
    this,
    &ThisClass::HandleFootstep);

void ThisClass::HandleFootstep(const FFootstepEvent& Event)
{
    if (Event.bHadValidFloorHit)
    {
        // Read the snapshot. Do not retain assumptions about the hit component.
    }
}
```

The event contains the source actor, hit actor and component, Physical Material,
surface type, trace endpoints, contact point and normal, foot, clamped intensity,
owner speed, and hit validity. A valid hit without a Physical Material uses
`SurfaceType_Default`.

Native listeners run first, Blueprint listeners second, and built-in cosmetic
feedback last. If a callback destroys the owner, feedback is safely skipped.

## Intensity and feedback

Request intensity is made finite and clamped to `[0, 1]`; NaN and infinity become
`1.0`. Intensity multiplies sound volume, Niagara uniform scale, and all decal size
components. An intensity of zero still publishes the event but spawns no cosmetics.
Pitch is randomized between the validated minimum and maximum.

Each feedback family can be disabled on the component and in each surface response.
A response is selected by exact Physical Surface first, then by the profile's
explicit optional fallback. No response means the event remains available but the
plugin spawns nothing.

## Networking

The plugin is deliberately local and introduces no replication or RPC. Run the
notify/request on every machine that needs the event, or replicate a higher-level
gameplay decision in project code. Dedicated servers still resolve traces and
broadcast events but skip audio, Niagara, and decals.

See [AUTHORING.md](AUTHORING.md) for asset setup, [ARCHITECTURE.md](ARCHITECTURE.md)
for execution details, and [DEBUGGING.md](DEBUGGING.md) for diagnostics and tests.
