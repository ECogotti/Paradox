# Paradox free camera

## Purpose and ownership

`AParadoxPlayerController` owns one transient `AParadoxCameraRig` and uses it as the view target.
The controller continues to possess `AParadoxPlayerCharacter`; camera input never swaps possession
and never changes Common UI input mode or focus.

The camera is enabled only when the map contains one enabled
`AParadoxCameraBoundsVolume`. Maps without a volume keep the character-mounted camera. A map whose
GameMode enables the time loop requires exactly one volume, so missing, duplicate, or invalid
volumes prevent loop initialization with a structured diagnostic.

Choose **Orthographic** or **Perspective** with `Projection Mode` on that volume. The controller
reads the choice when it initializes the rig; changing the property during play does not change an
already initialized camera. Existing volumes remain orthographic by default.

`/Game/TopDown/MA_Playground` contains the authoritative volume. `Lvl_TopDown` has no volume and
retains its previous camera behavior.

## Configuration

Global defaults are available under **Project Settings > Game > Paradox Camera**. A bounds volume
can optionally replace the complete global configuration for its map. Partial overrides are not
merged.

The shared configuration contains:

- camera orientation (Pitch sets the inclination toward the XY map plane);
- pan speed;
- zoom units per input step;
- recenter duration;
- quarter-turn duration and easing;
- an inner boundary margin;
- a fallback aspect ratio used before a viewport is available.

Orthographic mode also uses the fixed `Camera Distance` and initial, minimum, and maximum
`Ortho Width`. Perspective mode uses a horizontal `Perspective Field Of View` (default 60 degrees)
and initial, minimum, and maximum `Camera Arm Distance` (defaults 2000, 800, and 4800 cm). This arm
distance is the exact geometric separation from the XY pivot to the camera. There is no spring-arm
collision shortening. The mouse wheel changes only the active mode's zoom value; the configured
Pitch and perspective FOV stay fixed. A positive wheel step zooms in by the shared `Zoom Units Per
Step`.

`Rotation Duration` must be strictly positive. `Rotation Easing` controls the interpolation of a
quarter turn; `Custom` is intentionally unsupported because the configuration does not carry a
custom curve.

The volume center is the logical center by default. Enable its logical-center override only when
the desired initial/recenter point differs from the volume center. In orthographic mode the initial
footprint must fit at that point. In perspective mode the pivot must fit inside the volume's inner
margin.

`MA_Playground` uses the camera volume's XY center as its logical center, a 100 cm inner margin,
minimum width 800, maximum width 4800, and initial width 1500.

Invalid configuration is observable through `Get Camera Initialization Result`. The validator
checks the fields used by the selected projection and rejects non-finite required values,
negative speeds/durations/margins, inverted zoom ranges, an initial zoom outside its range, and a
logical center that requires correction. Orthographic mode also requires room for its minimum view;
perspective mode requires room for the pivot and inner margin.
Perspective FOV must be within 1-170 degrees and all four corner rays must point toward the XY
plane; a shallow Pitch or wide FOV that reaches the horizon is rejected.

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

In orthographic mode the camera bounds the full visible footprint on the volume's logical XY plane.
It projects all four view corners. For each corner offset `O` and
normalized camera forward vector `F`, the planar offset is:

```text
P = O - F * (O.Z / F.Z)
```

The orthographic focus position is clamped so the footprint extents plus the inner margin remain
inside the volume:

```text
MinFocus = Bounds.Min + Margin + FootprintExtent
MaxFocus = Bounds.Max - Margin - FootprintExtent
```

The orthographic maximum compatible width is recalculated from the current volume bounds, boundary
margin, camera orientation, and actual viewport aspect ratio. Its effective zoom-out ceiling is the
lower of the configured maximum and the geometric limit for the complete 360-degree yaw arc. If a
runtime bounds or aspect change makes the configured minimum width impossible, containment wins:
the width is reduced temporarily and one warning is emitted until the configuration becomes
compatible again. A defensive check also validates each complete 90-degree turn. During rotation,
the focus and any active recenter target are reclamped so every projected corner stays inside.

In perspective mode only the **pivot** is constrained to the volume's XY box, inset by the
configured boundary margin. WASD, recenter, and rotation keep that pivot inside; visible terrain
may extend past the box edge. Wheel zoom changes the camera-pivot distance only within the
configured minimum and maximum distances. The perspective arm distance is not reduced by volume
size, aspect ratio, or camera rotation. If a volume shrinks at runtime below the configured margin,
the pivot is held at its center on the affected axis. The four perspective corner rays are still
projected onto the pivot's XY plane for diagnostics and to validate that FOV and Pitch face the
plane without crossing the horizon.

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
- `Get Active Camera Projection Mode`;
- `Get Current Camera Arm Distance` (the fixed pivot-to-camera distance in orthographic mode);
- `Request Camera Recenter`.

The rig class is replaceable on a derived controller Blueprint. The native class remains the
complete default and enforces the projection selected by the volume. `Get Current Camera Ortho
Width` returns zero while perspective mode is active. Read the projection and distance getters
after `Is Free Camera Ready` becomes true; a map without a volume has no free-camera configuration.

## Debugging

Spatial debug is disabled by default and requires both:

1. `Enable Debug` on the specific camera volume;
2. the global console variable `Paradox.Camera.Debug 1`.

The overlay draws the authoritative box, projected footprint, requested/corrected center, and
current zoom for the active projection. The perspective footprint can extend beyond the box because
only its pivot is constrained. Disabling either switch stops all Paradox camera drawing immediately.

## Troubleshooting

- `MissingVolume`: add one enabled Paradox Camera Bounds Volume to a time-loop map.
- `MultipleVolumes`: disable or remove duplicates; the controller will not choose arbitrarily.
- `InvalidConfiguration`: inspect the structured message for range, orientation, or center errors.
- An invalid rotation configuration requires a finite positive duration and a non-`Custom` easing.
- If the logical center is reported as incompatible, disable its override to use the volume center,
  or move the override farther inside. Orthographic mode needs room for the complete initial view;
  perspective mode needs room for the pivot and margin.
- `VolumeTooSmall`: in orthographic mode enlarge the XY extent, reduce the margin, or lower the
  minimum width. In perspective mode enlarge the XY extent or reduce the margin.
- A perspective horizon error means the configured Pitch and FOV do not let every corner ray meet
  the XY plane; increase the downward inclination or reduce the FOV.
- A rejected orthographic quarter turn indicates that bounds, aspect, or camera state changed before
  the dynamic rotation-safe constraint could be reapplied; the next update restores the invariant.
- `RigSpawnFailed`: verify the controller's Camera Rig Class derives from
  `AParadoxCameraRig`.
- A non-time-loop map with `NotConfigured` is intentional and uses the legacy character camera.
