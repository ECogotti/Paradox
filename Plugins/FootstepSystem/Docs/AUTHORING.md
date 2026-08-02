# Authoring Guide

## Configure Physical Surfaces

1. Open **Project Settings > Physics > Physical Surface**.
2. Name the required slots, for example `Concrete` and `Mud`.
3. Create a Physical Material for each surface, such as `PM_Concrete` and `PM_Mud`.
4. Set each Physical Material's **Surface Type** to the matching project surface.
5. Assign the Physical Material through the floor material, material instance, or
   collision/body setup used by the queried geometry.

The footstep trace has `bReturnPhysicalMaterial` enabled. If collision returns no
Physical Material, the plugin deliberately reports `SurfaceType_Default`.

## Create a Footstep Profile

Create **Miscellaneous > Data Asset**, choose `Footstep Profile`, and add an entry to
`Surface Responses` for each surface.

Each response supports:

- sound and volume multiplier;
- minimum and maximum pitch;
- Niagara system and uniform scale;
- decal material, size, and lifetime;
- independent audio, Niagara, and decal spawn flags.

An exact map entry always wins. Enable `Use Default Response` only when an explicit
catch-all is desired. The fallback is not the same as a
`SurfaceType_Default` map entry: the map entry handles that surface specifically,
while the fallback handles every missing key.

Editor validation reports invalid/non-finite ranges as errors. It warns when a spawn
flag is enabled without its asset. Keep volume, scale, decal dimensions, and lifetime
non-negative; pitch values must be positive and ordered minimum to maximum.

## Configure the actor

Add `Footstep Component` to the actor that owns the animation mesh.

- Assign the profile.
- For the default mesh policy, configure sockets that exist on the mesh or skeleton.
- Use `Explicit Component` only when the intended skeletal mesh is not the mesh
  firing the notify or the owner's first skeletal mesh.
- Keep the default `Visibility` trace channel unless the project uses a dedicated
  floor-query channel.
- Make sure floors block that channel.
- Use a line trace for precise contacts or the sphere trace when small animation
  offsets should still find nearby ground.

The socket values may name skeletal sockets or bones recognized by
`USkeletalMeshComponent::DoesSocketExist`.

## Author animation notifies

Open an animation sequence and add **Footstep** at the frame where the foot reaches
the floor. Set:

- `Foot` to Left, Right, or Unspecified;
- `Socket Override` only when this notify needs a different socket;
- `Normalized Intensity` for the authored strength.

The editor label displays `Footstep (Left)`, `Footstep (Right)`, or `Footstep`.
Notifies do not fire in editor preview by default. Add one notify per actual contact;
rapid duplicate diagnostics report suspicious spacing without suppressing either
request.

## Blueprint usage

Bind `On Footstep Generated` on the component. The event is read-only and can be
used even when the profile has no response. Check `Had Valid Floor Hit` before using
contact data when miss broadcasting is enabled.

For non-animation callers, construct `Footstep Request` and call
`Submit Footstep Request`. A `true` result means that the event delegate ran. It
does not guarantee that a sound, Niagara system, or decal was spawned.

## Exact two-surface PIE scenario

Use this repeatable setup to validate a project integration without plugin-owned
binary assets:

1. In Physics settings, name `SurfaceType1` as `Concrete` and `SurfaceType2` as
   `Mud`.
2. Create `PM_Concrete` with surface `Concrete` and `PM_Mud` with surface `Mud`.
3. Create two visible floor actors side by side. Give both collision that blocks
   `Visibility`; assign `PM_Concrete` to one and `PM_Mud` to the other.
4. Create `DA_Footstep_Test`. Add `Concrete` and `Mud` map entries.
5. Assign visibly/audibly distinct assets or tuning to each response. For example,
   use different sounds and Niagara colors. Leave fallback disabled for this test.
6. Add `Footstep Component` to the test character, assign the profile, and enter the
   character's real left/right socket or bone names.
7. Add left and right `Footstep` notifies at contact frames in the walking
   animation.
8. On the component enable local debug, then enable at least `Socket`, `Trace`,
   `Hit`, `Surface`, and `Response` categories.
9. Start PIE and run `FootstepSystem.Debug 1` in the console.
10. Walk from the concrete floor onto the mud floor.

Expected result: every contact draws from its authored socket, the trace hits the
correct floor, the label changes from Concrete/`PM_Concrete` to Mud/`PM_Mud`, and
the two different profile responses play. With `FootstepSystem.Debug 0`, all plugin
draw output stops immediately while events and feedback continue.

If a surface always resolves as Default, inspect the actual collision material path,
not only the visible material. See [DEBUGGING.md](DEBUGGING.md).
