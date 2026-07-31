# Debugging and Validation

## Enabling runtime diagnostics

Debug output requires both controls:

1. enable `Enable Debug` on the specific `Footstep Component`;
2. run `FootstepSystem.Debug 1`.

`FootstepSystem.Debug 0` is the master kill switch and is the default. It immediately
disables every plugin-owned visual diagnostic, regardless of component settings.
The effective state is `global AND local`.

The local bitmask separates:

| Category | Output |
| --- | --- |
| Notify | foot, sanitized intensity, and notify location |
| Socket | socket marker and socket name |
| Trace | line or sphere-sweep endpoints, green on hit and red on miss |
| Hit | contact point and surface-normal arrow |
| Surface | surface name and Physical Material in the result label |
| Response | exact/fallback/none plus selected assets |
| Audio | marker and sound name when audio is spawned |
| Niagara | marker and system name when Niagara is spawned |
| Decal | marker and material name when a decal is spawned |
| Diagnostics | rapid-notify timing warnings |

Draw duration and rapid-notify threshold are configurable per component. Drawing is
excluded entirely when the build has no `ENABLE_DRAW_DEBUG` support. Disabled debug
does not perform the corresponding draw calculations.

## Logs

All plugin logs use `LogFootstepSystem`. Expected configuration problems are
warnings, including a missing profile, a missing socket, or a surface without a
response. Repeated missing-socket and missing-surface warnings are limited per
component so animation loops do not spam the log.

Rapid contacts inside the configured threshold are diagnosed only when the
`Diagnostics` category is active. They are never suppressed.

## Troubleshooting

### No event

- Confirm the component and animation mesh have the same owner.
- Confirm gameplay has begun and the component is active.
- Check that the selected mesh policy resolves the intended skeletal mesh.
- Check exact socket/bone spelling. The plugin does not choose a fallback after a
  missing or nonexistent selected socket.
- Enable `Notify`, `Socket`, and `Trace` categories.

### Event reports no floor hit

- Make sure the floor blocks the component's trace channel.
- Increase trace length only after confirming the socket marker is correct.
- Use the sphere trace for small spatial mismatches.
- Confirm the owner is not the only blocking hit; it is ignored by default.

### Surface is always Default

- Ensure the floor's collision path returns the intended Physical Material.
- Confirm the material's Surface Type is configured in Project Settings.
- Keep the trace non-complex unless the project's complex collision materials are
  deliberately authored for this use.
- Use `Surface` debug to inspect both the surface name and material object.

### Event exists but feedback is absent

- Confirm the exact surface entry exists or enable the explicit profile fallback.
- Check component-level and response-level spawn flags.
- Check that the response references a valid asset.
- Confirm intensity is greater than zero and multipliers/sizes are nonzero.
- Dedicated servers intentionally skip all cosmetics.
- Run asset data validation to find invalid ranges and missing asset warnings.

### Duplicate-looking steps

Enable `Diagnostics` and inspect notify timing. Move or remove unintended duplicate
animation notifies. The plugin reports close notifies but does not silently discard
them.

## Build and automation

Compile the UE 5.8 editor target:

```powershell
& 'D:\Giochi\UE_5.8\Engine\Build\BatchFiles\Build.bat' ParadoxEditor Win64 Development '-Project=C:\Users\ecogo\Documents\Unreal Projects\Paradox\Paradox.uproject' -WaitMutex -FromMsBuild
```

Run the plugin suite headlessly:

```powershell
& 'D:\Giochi\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Users\ecogo\Documents\Unreal Projects\Paradox\Paradox.uproject' -Unattended -NoP4 -NullRHI -NoSound '-ExecCmds=Automation RunTests StartsWith:FootstepSystem; Quit' '-TestExit=Automation Test Queue Empty'
```

The suite covers request sanitization, profile selection, strict socket precedence,
event snapshots with and without Physical Materials, a real transient-world trace,
miss policy, inactive lifetime, no-Tick behavior, debug gates, feedback spawn
predicates, and a notify owner without a component.
