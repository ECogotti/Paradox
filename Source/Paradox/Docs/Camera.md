# Paradox free camera - Milestones 5-6

## Purpose and ownership

`AParadoxPlayerController` owns one transient `AParadoxCameraRig` and uses it as the view target.
The controller continues to possess `AParadoxPlayerCharacter`; camera input never swaps possession
and never changes Common UI input mode or focus.

The camera is enabled only when the map contains one enabled
`AParadoxCameraBoundsVolume`. Maps without a volume keep the character-mounted camera. A map whose
GameMode enables the time loop requires exactly one volume, so missing, duplicate, or invalid
volumes prevent loop initialization with a structured diagnostic.

`/Game/TopDown/MA_Playground` contains the authoritative volume. `Lvl_TopDown` has no volume and
retains its previous camera behavior.

## Configuration

Global defaults are available under **Project Settings > Game > Paradox Camera**. A bounds volume
can optionally replace the complete global configuration for its map. Partial overrides are not
merged.

The configuration contains:

- camera orientation and distance from the XY map plane;
- pan speed;
- initial, minimum, and maximum orthographic width;
- zoom units per input step;
- recenter duration;
- quarter-turn duration and easing;
- an inner boundary margin;
- a fallback aspect ratio used before a viewport is available.

`Rotation Duration` must be strictly positive. `Rotation Easing` controls the interpolation of a
quarter turn; `Custom` is intentionally unsupported because the configuration does not carry a
custom curve.

The volume center is the logical center by default. Enable its logical-center override only when
the desired initial/recenter point differs from the volume center. The initial footprint must fit
at that point.

`MA_Playground` uses the camera volume's XY center as its logical center, a 100 cm inner margin,
minimum width 800, maximum width 4800, and initial width 1500.

Invalid configuration is observable through `Get Camera Initialization Result`. The validator
rejects zero or non-finite required values, negative speeds/durations/margins, inverted zoom
ranges, an initial width outside the configured range, a camera parallel to the map plane, a
logical center that requires correction, and a volume that cannot contain the minimum view.

## Input

`IMC_Default` contains:

- `IA_CameraMove` (`Axis2D`): W, A, S, D;
- `IA_CameraZoom` (`Axis1D`): mouse wheel;
- `IA_CameraRecenter` (`Digital`): Space;
- `IA_CameraRotateLeft` (`Digital`): Q;
- `IA_CameraRotateRight` (`Digital`): E.

All five actions trigger while paused. The rotate actions use the `Started` trigger, so holding Q or
E produces only one request. The existing click, touch, and Enter rewind mappings are
preserved. Camera input is assigned on `BP_PlayerController` through the inherited action
properties, so designers can replace mappings without changing C++.

Q rotates by +90 degrees of Yaw and E by -90 degrees. A turn is always derived from the configured
base orientation and a discrete quarter-turn index, preventing accumulated drift. While a turn is
active every additional rotation request is ignored: it cannot replace, reverse, or queue the
current target. The camera snaps to the exact indexed orientation when the interpolation finishes.

Pan cancels an active recenter immediately. Recenter targets the player only during `ActiveRun`;
in every other phase it targets the configured logical center. Recenter preserves rotation and
zoom and does not create a follow camera. Pan directions are derived from the current camera Yaw,
so forward continues to move toward the top of the screen after every quarter turn. Pan, zoom, and
recenter remain available during the interpolation.

Camera controls are independent from gameplay movement gating. They remain available during
`ChronoSpawnSelection`, including the first selection before timeline T0 starts.

## Footprint and containment

The camera projects all four orthographic view corners onto the map XY plane. For each corner
offset `O` and normalized camera forward vector `F`, the planar offset is:

```text
P = O - F * (O.Z / F.Z)
```

The maximum absolute X/Y values of the four projected offsets form the footprint half-extents.
The focus position is clamped so those extents plus the inner margin remain inside the volume:

```text
MinFocus = Bounds.Min + Margin + FootprintExtent
MaxFocus = Bounds.Max - Margin - FootprintExtent
```

The maximum compatible orthographic width is recalculated from the current volume bounds, boundary
margin, camera orientation, and actual viewport aspect ratio. `Maximum Ortho Width` remains the
designer-authored ceiling, but the effective zoom-out ceiling is the lower of that value and the
geometric width that can contain the continuously rotated footprint over the complete 360-degree
yaw circle. No tuned map dimensions or zoom values are hard-coded. Pan, zoom, and recenter all use
the same current containment data. If a runtime bounds or aspect change makes the configured
minimum impossible, containment wins: the effective width is reduced temporarily and one warning
is emitted until the configuration becomes compatible again.

The rotation-safe zoom ceiling is applied during initialization, ordinary zoom input, and camera
updates, so all four future quarter-turn arcs remain available without changing zoom when Q or E is
pressed. Before a rotation starts, a defensive analytic check still validates its complete
90-degree arc, including intermediate angles. During a turn the focus and any active recenter target
are reclamped for the interpolated Yaw so every projected corner remains inside the volume for the
entire transition.

Camera updates use bounded real frame delta (`FApp::GetDeltaTime`) rather than world delta. Pan,
zoom, recenter, and rotation therefore stay frame-rate independent during Tactical Pause and time
dilation. The rotation state belongs to the persistent controller, like the rest of the free camera,
and does not depend on a possessed Character during Chrono Spawn selection.

## Blueprint API

Useful controller queries and commands:

- `Is Free Camera Ready`;
- `Get Free Camera Rig`;
- `Get Camera Bounds Volume`;
- `Get Camera Initialization Result`;
- `Get Camera Focus Location`;
- `Get Current Camera Ortho Width`;
- `Request Camera Recenter`.

The rig class is replaceable on a derived controller Blueprint. The native class remains the
complete default and enforces an orthographic projection.

## Debugging

Spatial debug is disabled by default and requires both:

1. `Enable Debug` on the specific camera volume;
2. the global console variable `Paradox.Camera.Debug 1`.

The overlay draws the authoritative box, projected footprint, requested/corrected center, and
current zoom. Disabling either switch stops all Paradox camera drawing immediately.

## Troubleshooting

- `MissingVolume`: add one enabled Paradox Camera Bounds Volume to a time-loop map.
- `MultipleVolumes`: disable or remove duplicates; the controller will not choose arbitrarily.
- `InvalidConfiguration`: inspect the structured message for range, orientation, or center errors.
- An invalid rotation configuration requires a finite positive duration and a non-`Custom` easing.
- If the logical center is reported as incompatible, disable its override to use the volume center,
  or move the override far enough inside the volume to contain the complete initial footprint.
- `VolumeTooSmall`: enlarge the XY extent, reduce the margin, or lower the minimum width.
- A rejected quarter turn indicates that bounds, aspect, or camera state changed before the dynamic
  rotation-safe constraint could be reapplied; the next camera update restores the invariant.
- `RigSpawnFailed`: verify the controller's Camera Rig Class derives from
  `AParadoxCameraRig`.
- A non-time-loop map with `NotConfigured` is intentional and uses the legacy character camera.
