# Black hole actor

`AParadoxBlackHole` lives in `Public/Environment` / `Private/Environment`. It replaces
the background-capture Blueprint setup for the single-Custom-node Unlit/Translucent
black-hole material. It creates a dynamic material instance, controls capture,
and exposes the disk, noise, lens, Doppler and color settings to designers and Blueprint.
The Doppler material is a separate copy; existing v2 and Blueprint assets are preserved.

Source: `Public/Environment/ParadoxBlackHole.h` and
`Private/Environment/ParadoxBlackHole.cpp`.

See [Feature switches and optimization](BLACK_HOLE_FEATURES.md) for actor flags,
compiled runtime variants, shared controls and future menu integration.

## Components and placement

```text
DefaultSceneRoot
├── SpringArm                   Target Arm Length = 300000 cm
│   ├── DirectionalLight
│   └── Sphere                  Engine sphere; default relative scale (3,3,3)
└── BackgroundCapture
```

Spring-arm collision testing and pawn-control rotation are disabled. Sphere is
attached to its end socket, has no collision/navigation effect, and casts no
shadow. Move and uniformly scale Sphere to suit the scene. The optional directional
light starts at intensity zero: the Unlit material requires no lighting. Enable
and configure that light explicitly if scene lighting is desired.

BackgroundCapture is attached to the root so it does not inherit Sphere's scale
or the arm offset. Main View Camera supplies the actual rendering camera pose and
FOV; the capture component needs no particular local rotation or position. Its
component transform is not a substitute for the main camera when these flags are
active. Projection Type must still match the main view. For orthographic rendering
also set Capture Ortho Width to the camera width, including when zoom changes.

## Setup

1. Create a Blueprint derived from **ParadoxBlackHole**, or place the native actor.
2. New actors default to `/Game/Vfx/BlackHole/MI_BlackHole_FogSafe_Structured_Doppler`. Its effective
   scalar/vector values, including inherited values, initialize native defaults.
3. Set Capture Projection / Capture Ortho Width to match the main camera.
4. Position the actor and set Sphere's uniform scale. Enable realtime in the
   Level Editor viewport to preview capture and disk animation.
5. Inspect **Paradox > Black Hole > Status**. Ready means resources/exclusion are
   configured, rather than that a GPU readback has completed.

The existing `BP_BlackHole` is not reparented or saved automatically. A new child
Blueprint inherits these native components; do not add duplicate components or
keep the old Blueprint capture initialization alongside the native setup.

**Base Material takes precedence over Sphere's material slot.** Assign
`MI_BlackHole_Doppler` to the actor's Base Material property, including on placed
instances that override Blueprint defaults. Assigning it only to Sphere does
not replace an older `MI_BlackHole` still configured as Base Material.

## Material parameters

The values below were copied from the on-disk MI_BlackHole_v2 into MI_BlackHole_Doppler. Constructor loading
also reads them from that asset; edits on a Blueprint/placed actor override them.

| Parameter | Effective MI default |
|---|---:|
| CoreRadius | 0.1 |
| DiskInnerRadius | 0.18 |
| DiskOuterRadius | 0.8 |
| DiskThickness | 0.005 |
| DiskDensity | 5 |
| RotationSpeed | 0.35 |
| EmissionStrength | 6 |
| HaloStrength | 0.25 |
| LensingStrength | 0.5 |
| RaySteps | 128 |
| NoiseScale | 8 |
| NoiseAmount | 1 |
| NoiseContrast | 2 |
| HotspotStrength | 1.5 |
| TurbulenceSpeed | 0.25 |
| RefractionStrength | 0.15 |
| RefractionMaxOffset | 0.35 |
| OrthoRefractionDistance | 4 |
| InnerColor RGBA | (0.473936, 0.866877, 1, 1) |
| OuterColor RGBA | (0.40625, 0.505869, 1, 1) |
| DopplerStrength | 0.65 |
| DopplerVelocity | 0.30 |
| DopplerColorStrength | 0.35 |
| DopplerApproachingColor RGBA | (0.473936, 0.866877, 1, 1) |
| DopplerRecedingColor RGBA | (0.40625, 0.505869, 1, 1) |
| BackgroundReady | 0 before capture setup; managed automatically |

All exposed scalar/vector parameters in this MI are represented. It exposes no
texture parameters or static switches: DiskNoise and BackgroundTex are currently
fixed Texture Object nodes. BackgroundReady remains read-only because forcing it
on before a valid capture violates the material's fallback contract.

In Blueprint, setting an exposed disk/noise/lens/color property invokes its native
Blueprint setter and updates the MID immediately. `SetRefractionStrength` controls
the strength and capture together. `SetScalarParameter(Name, Value)` and
`SetColorParameter(Name, Color)` are equivalent named operations with a success
result. Unknown names, managed BackgroundReady, and non-finite values are rejected
with a diagnostic; they do not overwrite the prior property/MID value. Native C++
callers should use setters as well. Shader clamps still protect incompatible
radii, step counts, noise limits and minimum thickness.

## Doppler beaming and editable colors

`MM_BlackHole_Doppler` / `MI_BlackHole_Doppler` are copies of the v2 master and
instance. They retain its texture resources, scalar overrides and existing graph,
including SceneCapture refraction. New native actors use the Doppler MI. Existing
Blueprint Class Defaults and placed-instance Base Material overrides are retained:
assign the new MI explicitly and press Refresh Background, or call
`SetBaseMaterial(MI_BlackHole_Doppler)`. Assigning Sphere's material alone does not
change Base Material. The old v2 material remains usable without Doppler.

The five new inputs connect Scalar Parameters and Vector Parameters (RGB masks)
to the existing Float4 Custom node. RGB still connects to Emissive Color and A
to Opacity. The complete embedded source is also supplied in
`Shaders/BlackHoleDoppler.hlsl`; changing that file alone does not reimport the
Custom expression. Copy the updated code into the Custom node when editing it.

Designers can edit the **Paradox > Black Hole > Doppler** properties on a Blueprint
or placed actor. Blueprint property setters and the named parameter functions
update the existing MID immediately:

- **Doppler Strength**, 0–1: amount of the brightness/color effect. Zero restores
  the v2 output. It does not disable background capture or alter refraction.
- **Doppler Velocity**, 0–0.6: orbital speed at the inner radius, as a fraction of
  light speed. It falls as the square root of inner radius / sample radius.
  It controls beaming separately from the magnitude of Rotation Speed.
- **Doppler Color Strength**, 0–1: amount of the angular color blend. Zero keeps
  InnerColor/OuterColor and changes brightness only.
- **Doppler Approaching Color** and **Doppler Receding Color**: independent,
  absolute linear HDR RGB targets. Any colors, including values above 1, are
  accepted; alpha is ignored by the shader. These are blended colors, rather
  than tint multipliers. InnerColor/OuterColor still define the radial gradient.

The defaults retain the current blue palette. For a clearly visible diagnostic
palette use Strength=1, Color Strength=1, Velocity=0.6, Approaching Color=(1,0.04,0.01)
and Receding Color=(0.03,1,0.20). Those are optional tuning values, not asset defaults.

Positive/negative Rotation Speed selects orbital direction; reversing it swaps
which side approaches the camera. Zero Rotation Speed or zero Doppler Velocity
bypasses the effect. In the straight path the observer direction is the inverse
ray direction. With lensing, each sample uses the inverse local geodesic tangent,
so the direct disk and its lensed images observe the same orbital velocity field.
This avoids assigning an arbitrary fixed left/right color.

Brightness uses the artistic gain `delta^3`, clamped to 0.25–4, where
`delta = sqrt(1-beta^2)/(1-beta*mu)` and mu is the local velocity/observer alignment.
The color blend also fades with speed and abs(mu). This approximates relativistic
beaming for art direction; it is not spectral blackbody transport. Near a top view,
the approaching/receding color split diminishes, with possible transverse dimming.
Only disk emission changes: density, opacity, captured rays, halo, integration,
animated noise and captured-background composition retain v2 behavior. No texture
reads or ray samples are added. Non-finite scalar/color values are rejected by
native setters, and the shader clamps finite control values to the ranges above.

`SetBaseMaterial` switches the material while retaining the actor's exposed
settings. `SetCaptureProjection` validates and updates camera projection/width.
`InitializeBackground` reapplies all authored settings and reuses valid resources.
The **Refresh Background** editor button performs the same setup.

## Fog-safe background refraction

Two independent variants are available in `/Game/Vfx/BlackHole` for scenes with
Exponential Height Fog:

| Material instance | Parent master | Features |
|---|---|---|
| `MI_BlackHole_FogSafe_NoDoppler` | `MM_BlackHole_FogSafe_NoDoppler` | Animated disk, lensing and background refraction; no Doppler code or parameters |
| `MI_BlackHole_FogSafe_Doppler` | `MM_BlackHole_FogSafe_Doppler` | The same effects plus Doppler beaming and the five editable Doppler controls |

Both retain the existing texture references and shared effective MI defaults.
NoDoppler removes the Doppler calculation and its five Custom inputs, rather than
merely setting Strength to zero. The native actor still exposes the Doppler
properties; those affect only the Doppler material. Shared disk, noise, color,
lens and capture properties work with either variant.

Previous v2 and Doppler assets are retained. The earlier `MI_BlackHole_FogSafe` /
`MM_BlackHole_FogSafe` pair also remains available and includes Doppler; the explicit
names above avoid ambiguity for new assignments. New actors now default to the
Structured FogSafe Doppler profile; existing assignments are retained. Assign the desired FogSafe instance to **Base Material**
on the Blueprint and placed instances, then Refresh Background, or call
`SetBaseMaterial` with that instance at runtime. Setting Sphere's material slot
alone does not switch the actor's source. No extra RT/capture or Blueprint wiring
is required.

The former refraction shader inserted an already-fogged captured background into
Emissive Color. Unreal then fogged the entire translucent sphere again. Refraction
increases opacity across the proxy, making this second scattering layer visible
as a bright bubble. Disabling capture fog can instead create a dark/clear bubble;
disabling fog on the whole material also changes the disk and captured-core look.

FogSafe inverse-transforms **only the captured background contribution** using
the current proxy fog's scattering S and transmission T: `(Background - S) / T`.
The normal translucent pass then fogs that contribution once, while retaining its
usual treatment of the disk, halo and captured rays. Background capture keeps its
Fog show flag enabled; the map's fog and lighting settings remain authored normally.

The master already has the required material flags:

- **Apply Fogging: enabled**.
- **Compute Fog Per Pixel: enabled**, so compensation and the engine pass evaluate
  the fog at the same pixel position instead of interpolating different vertex values.
- **Allow Negative Emissive Color: enabled**, required for the intermediate inverse
  scattering subtraction. Final scene composition receives the normal fog contribution.

Keep these flags when copying the full Custom code from
`Shaders/BlackHoleFogSafe_NoDoppler.hlsl` or `Shaders/BlackHoleFogSafe_Doppler.hlsl`.
`Shaders/BlackHoleFogSafe.hlsl` contains the earlier compatible Doppler variant.
The Custom node remains Float4 with RGB to Emissive Color and A to Opacity, without
Additional Defines, Includes or new inputs. BackgroundReady/RefractionStrength fallback
is retained. At very low transmission, replacement coverage fades between T=0.01
and T=0.03; fully opaque fog retains the original scene rather than dividing by zero.

This variant targets this project's UE 5.8.3 / SM6 deferred renderer and a single
main view. It uses the engine's Exponential Height Fog evaluation and, when enabled,
its integrated Volumetric Fog texture. Other atmospheric contributions such as
SkyAtmosphere aerial perspective, Local Fog Volumes or translucent cloud fogging
are outside the validated compensation scope. Changing the engine version or
renderer requires rechecking the shader helper signatures and fog composition.

`fog_safe_validation.json` records 18 passing checks: analytical/volumetric/dense
fog in both projections, finite HDR values, no-fog compatibility, native actor MID
and capture integration, and SM6 editor compositing compilation. In the controlled
no-displacement case the normalized HDR difference from refraction-off is at most
0.00128; it is zero with the tested fully opaque fog. Existing black-hole asset
hashes remain unchanged. These are temporary test-scene results; the placed game
existing actors still need the intended FogSafe Base Material assignment above;
new actors already use Structured FogSafe by default.

The separately saved NoDoppler and Doppler variants are also freshly loaded,
compiled and tested by `validate_fog_safe_variants.py`, with 51 passing checks.
It checks the connected
32/37-input graphs, required fog flags, absence/presence of all five Doppler
parameters and code, fog invariance in both projections, dense and volumetric
fog, finite HDR, compatibility with the respective old material without fog,
native actor capture/parameter integration and unchanged on-disk assets. Its
report is `fog_safe_variants_validation.json`; fixture edits remain unsaved.
The editor-compositing usage flag is enabled only for its final compilation check,
after the world-render comparisons; it is not enabled in the saved materials.

## Structured disk: filaments, concentrations and variable volume

Use `MI_BlackHole_FogSafe_Structured_NoDoppler` or
`MI_BlackHole_FogSafe_Structured_Doppler` from `/Game/Vfx/BlackHole`, through the
actor's **Base Material** and Refresh Background. These are new copies; previous
materials, Blueprint Class Defaults and placed actor settings remain authored.
Both keep FogSafe compensation, refraction and animated fine noise. The Doppler
copy retains its five controls and independent HDR colors. Native Base Material
still defaults to the previous Doppler MI, so switching is explicit.

The added Texture Object Parameter `DiskStructure` uses `T_BH_DiskStructure`, a
1024x512 periodic linear RGBA data texture. R/G encode signed flow as `2*RG-1`, B
contains interrupted elongated filaments, A contains broad correlated concentrations
centered near 0.5. Import with sRGB off, Masks compression, alpha retained, Wrap
X/Y, Trilinear, SimpleAverage mipmaps and virtual texture streaming off. The PNG
and statistics are in `SourceArt/BlackHole`; regenerate them with
`Tools/BlackHole/generate_disk_structure.py` (numpy/Pillow, seed 834722). Reimport
the generated PNG after regenerating; its alpha is concentration data.

The two full single-Custom-node sources are
`Shaders/BlackHoleFogSafe_Structured_NoDoppler.hlsl` and
`Shaders/BlackHoleFogSafe_Structured_Doppler.hlsl`. They have 41/46 inputs. The nine
added inputs are the texture and the scalars below. Float4, RGB to Emissive and A
to Opacity, and the three FogSafe material flags remain the same. Copying HLSL
alone does not create/connect the new parameter nodes. The supplied masters
already contain these connections; Additional Defines/Includes remain empty.

| Control | Default | Shader range / use |
|---|---:|---|
| StructureAmount | 0.8 | 0-1; zero bypasses new texture reads and restores the previous FogSafe output |
| StructureScale | 2 | Rounded angular repetitions, 1-8; radial repetition is one |
| FilamentStrength | 1 | 0-3; filament emission contrast |
| ClumpStrength | 1.5 | 0-4; concentration emission contrast |
| StructureLifetime | 8 seconds | 1-30; lifetime of each blended phase |
| VortexStrength | 0.2 | 0-0.5; bounded displacement in texture coordinates |
| DensityVariation | 0.35 | 0-0.75; concentration-driven density variation |
| ThicknessVariation | 0.3 | 0-0.5; concentration-driven thickness and small midplane undulation |

These properties live under **Paradox > Black Hole > Structure**. Blueprint setters
and `SetScalarParameter` update the MID immediately without rebuilding capture.
`DiskStructureTexture` optionally replaces the Texture2D; None restores the
current source material's `DiskStructure` default, including after a previous
override or a material switch. The reference is tracked by Unreal GC. The eight
scalar settings and optional texture override persist across reinitialization.
Older materials continue to work and ignore these added controls.

The flow is evaluated at the ray's disk position, so the direct disk and lensed
arcs show the same structures. Differential rotation stretches them; two phases
offset by half a lifetime fade before resetting. Deterministic offsets vary the
next generation, avoiding a visibly identical global loop every eight seconds.
The bounded drift prevents indefinite deformation. Reversing RotationSpeed swaps
orbital direction; zero stops orbital rotation while flow renewal can continue.
The pre-existing TurbulenceSpeed still controls fine noise independently.
The bounded two-phase flow follows the approach in Valve's
[Water Flow presentation](https://cdn.fastly.steamstatic.com/apps/valve/2010/siggraph2010_vlachos_waterflow.pdf),
adapted to polar coordinates and an orbiting emission/volume field.

The large fields modulate emission around their stored mean, while density and
local Gaussian thickness vary together. Local thickness is clamped to 0.005-0.20.
Midplane displacement is proportional to ThicknessVariation and stays below 20%
of base thickness. The vertical Gaussian is integrated across both ray-segment
halves using an erf polynomial, reducing missed thin regions without adding ray
steps. StructureAmount blends this integral back to the previous midpoint result
at zero. Setting DensityVariation/ThicknessVariation to zero keeps their spatial
fields uniform, while the improved integration still operates when StructureAmount
is positive.

The optional balanced volume preset is **DiskThickness=0.01**, with the defaults
above and the existing colors, density and emission. This preset is not applied
automatically: the authored base thickness remains 0.005. For an emission-only
comparison set DensityVariation=ThicknessVariation=0. Begin with fixed exposure
and Bloom disabled; enable Bloom after adjusting structure contrast.

The additional sampling is at most four reads per candidate disk segment (two
flow, two structure). Explicit mips account for pixel/segment size, differential
shear and a conservative flow Jacobian bound of 37 for the generated texture.
Custom override textures should use the same smooth flow encoding and channel
layout. Sharp high-frequency flow maps can need a larger bound; the supplied
generator reports its bound and filament mean, which are embedded by
`Tools/BlackHole/build_structured_shaders.py`. There is no new capture or RT.

Validation reports and display previews are under `Saved/CodexBlackHoleValidation`.
`structured_validation.json` covers disabled HDR comparisons, phases, camera seams,
independent volume controls, both projections, large scales, core coverage, height/
volumetric fog and immediate native Blueprint scalar/texture setters. Fixture
edits remain unsaved; validation also compares all on-disk black-hole asset hashes.

### Structured validation and performance

ParadoxEditor Win64 Development compiles successfully with the current native DLL.
Both new saved masters compile for UE 5.8.3 / PCD3D_SM6, including the final editor
compositing check, with no shader errors. `structured_validation.json` records
95 passing checks. At StructureAmount=0, the normalized HDR difference from the
respective previous FogSafe variant is zero in this fixture for both projections
and 128/192/256 steps (required tolerance 0.003). The gaussian polynomial is also
checked against Python's `math.erf`; maximum segment-profile error is 0.00000161.

Views cover 7/35/90 degree inclination, perspective/orthographic, camera seams,
phase restarts, independent density/thickness controls, core coverage, height and
volumetric fog, and large scale/world coordinates. The scale-30000 comparison
has normalized HDR difference 0.00255 from scale 3. Immediate Blueprint setters,
texture override/reset, resource reuse and cleanup pass. All 16 previous
black-hole asset hashes are unchanged. Maps and Blueprint assets are not saved.

The GPU comparison alternates old/disabled/enabled cases across four rounds of
32 frames after warm-up. Background capture runs once before the measured regions;
only the black-hole proxy is drawn, isolating its GPU ParallelDraw time. With the
Doppler variant, authored thickness 0.005 and StructureAmount=0.8, at 768x512 on
RTX 3080 / D3D12 SM6:

| RaySteps | Previous FogSafe | Structure disabled | Structure enabled | Added vs previous |
|---|---:|---:|---:|---:|
| 128 | 0.731 ms | 0.792 ms | 0.887 ms | 0.156 ms |
| 192 | 1.128 ms | 1.168 ms | 1.195 ms | 0.067 ms |
| 256 | 1.395 ms | 1.404 ms | 1.512 ms | 0.117 ms |

These are medians across 128 frames per case. GPU clocks vary between rounds;
the medians of paired per-round differences are 0.130/0.098/0.078 ms. Treat the
numbers as a local comparison, rather than a scaling rule for other resolutions
or coverage. Disabling structure skips its texture reads but does not remove the
larger shader's compiled code/register overhead. The background capture cost is
unchanged and excluded. Full timings are in `structured_gpu_measurements.json`.

`Structured_Animated.gif` is a 12-second actual shader capture, with the existing
blue palette, optional thickness 0.01, 192 steps, a 20-degree camera, fixed manual
exposure and Bloom disabled. Its black background and refraction-off view isolate
the disk; refraction/fog are tested separately. `Structured_Comparison.png` compares
both old/new pairs under the same settings. The GIF restarts its recorded clip;
the shader itself evolves continuously and is not forced into a 12-second loop.
These are temporary preview settings, not changes to saved materials or actors.

## Capture and render-target ownership

Capture uses Final Color HDR in linear working color space, Main View Camera,
Main View Family and Main View Resolution, with divisor (1,1). Ignore Screen
Percentage is false so the capture matches the main internal resolution and AA
jitter. It does not render inside the main renderer. Capture-on-movement is off;
capture-every-frame is enabled only while refraction is active. The actor and its
child actors are hidden only from this capture, preventing recursion while
remaining visible in the main view.

Exposure is manual/neutral with physical camera exposure disabled. Capture bloom,
motion blur, eye adaptation, local exposure, color grading and tonemapper are
disabled. Main-view post-processing stays responsible for final exposure/bloom.
The material Refraction pin stays disconnected.

Two material configurations are supported:

- A **Texture Object Parameter** named `BackgroundTex`: create/reuse an
  actor-owned transient RGBA16f linear target, gamma 1, bilinear, Clamp X/Y,
  without mipmaps/UAV. The initial 512×512 dimensions are replaced at render
  time by Main View Resolution. The MID receives this target.
- A fixed **Texture Object**, as in the supplied material: locate its single
  referenced Render Target and capture into that exact existing resource. The
  target must already be RGBA16f, gamma 1, Clamp X/Y, without auto mipmaps. Asset
  settings and files are not changed. This resource is shared, so use a single
  black hole/view. Multiple fixed targets are rejected as ambiguous. Converting
  the background node to the parameter above enables private per-actor targets.

UPROPERTY references keep transient resources alive. Reinitialization and
zero-strength disable/re-enable reuse valid resources. EndPlay/Destroyed disable
capture, detach its target, restore the source material, and release references.
No shared asset render target is explicitly destroyed. The actor has no Tick;
the engine's capture component schedules deferred capture with the main view.

## Status, failure and limits

`CaptureState`, `CaptureStateMessage`, `BackgroundReady`, `BlackHoleMID` and
`BackgroundRT` expose instance state. Missing material/mesh, invalid settings,
missing background resource, incompatible fixed target, or unsupported neutral
HDR capture set Error and keep BackgroundReady=0. The existing disk/lens can
continue through the material's fallback. Errors are logged on state changes in
LogParadox, rather than every frame. Zero refraction, disabled editor preview and
dedicated-server worlds disable capture.

If the disk appears but background distortion is absent, inspect Capture State
Message first. Actor BaseMaterial and Sphere's slot can differ, especially after
reparenting an existing Blueprint. For a missing BackgroundReady or RefractionStrength,
select the actor instance in the level, set **Base Material = MI_BlackHole_Doppler**
(or MI_BlackHole_v2 for the previous effect),
and use **Refresh Background**. Also check the Blueprint's Class Defaults and
any placed-instance overrides. `SetBaseMaterial(MI_BlackHole_v2)` is the Blueprint
operation that both assigns the source and immediately reinitializes capture.
Changing a source property without reinitializing can leave the previous error
displayed. The old `MI_BlackHole` lacks capture parameters, so capture stays off.
MID/RT remaining None with this error means initialization stopped before creating
resources; assigning an RT manually cannot repair a missing scalar parameter.
Inspect the actor's **Paradox > Black Hole > Base Material** property, and the same
property in the Blueprint's **Class Defaults**. Sphere > Materials > Element 0
is a different assignment. If needed, the read-only `inspect_live_black_hole.py`
script under Saved/CodexBlackHoleValidation reports the actual source and sphere
assignments in the running editor. The corresponding `fix_live_black_hole.py`
uses SetBaseMaterial on exactly one selected black-hole actor, validates capture,
and records the result without saving assets. It changes only that placed actor;
Blueprint Class Defaults still need the intended source for future instances.
That older repair script assigns v2; for Doppler use the new MI through Base Material
or SetBaseMaterial instead.

The installed engine fixes the Tonemapper show flag on in Shipping. This actor
reports that configuration as unavailable and keeps capture off; a capture-only
linear copy pass must be implemented/validated before Shipping refraction can
be enabled. Editor and Development are the intended validated targets.

The shader still supports one main view, one black hole, an external camera and
uniform Sphere scale. Keep the SkySphere sorted behind the black hole. Main
camera orthographic clip planes must encompass the sky/scene. The shader's
foreground guard, screen-edge fallback and finite-distance artistic approximation
are unchanged. This class does not simulate a physical curved spacetime or apply
a second deformation to the disk.

## Validation

Build the ParadoxEditor Development target after changes. The temporary scripts
and results under `Saved/CodexBlackHoleValidation` exercise the actual MI defaults,
component hierarchy, MID setters, capture start/stop, render-target reuse, invalid
inputs, editor construction and cleanup. They do not save content assets.

Validation in UE 5.8.3 / SM6 passed the ParadoxEditor Development build without
new warnings/errors and 75 lifecycle/parameter checks. Native viewport captures
verify that moving BackgroundCapture by (1000000,-2000000,3000000) cm and applying
an arbitrary rotation leaves sampled HDR values identical in both perspective
and orthographic projection. This comparison reads the target after a completed
main-view render and a warm-up render for each projection: Slate ticks alone do
not guarantee fresh capture data in a hidden/offscreen editor viewport.

Reports: `black_hole_actor_validation.json` and
`black_hole_native_view_validation.json`. The corresponding `NativeBlackHole_*.png`
images show the native actor with its current blue MI colors in a temporary
checker/starfield scene. The on-disk project map and existing assets were not saved.

### Doppler validation

The ParadoxEditor Win64 Development build succeeds with the updated native class
and current project DLL. The two new assets compile for PCD3D_SM6; editor compositing
also compiles with no shader errors. The temporary harness uses their actual graph,
current noise texture and MI values, replacing only Time with a fixed preview time
and the background texture resource in memory. No existing content asset is saved.
SHA-256 checks confirm all eight original black-hole assets are unchanged.

`doppler_actor_validation.json` records 100 passing checks: effective defaults,
hierarchy, all five immediate setters, independent HDR colors, non-finite rejection,
capture resource stability, editor initialization, disable/re-enable and cleanup.
`doppler_render_report.json` covers lateral, tilted, top, rotated-camera and
orthographic views, 128/192/256 steps, and a scale of 30000 at large world coordinates.
Reversed rotation moves the approaching color/bright side to the opposite side.
Core occlusion, disk foreground contribution, lensed arcs and background distortion
retain the original composition. Bloom is disabled and exposure is fixed in previews.

With Strength=0, Velocity=0 or Rotation Speed=0, the rendered linear HDR difference
from v2 is at most 0.005859375 in a channel; the normalized difference
`abs(new-old)/(1+max(abs(new),abs(old)))` is below 0.002. Repeated baseline captures
are identical. This is shader/half-float numerical variation, not a bitwise equality
claim. Sample count, texture sample count and opacity/density equations are unchanged.

The paired GPU benchmark alternates case order across four rounds of 32 frames
after warm-up. At 768×512 on RTX 3080 / D3D12 SM6, median GPU ParallelDraw timings are:

| RaySteps | v2 | Doppler disabled | Doppler enabled | Added vs v2 |
|---|---:|---:|---:|---:|
| 128 | 0.625 ms | 0.660 ms | 0.700 ms | 0.075 ms |
| 192 | 0.932 ms | 0.946 ms | 1.008 ms | 0.076 ms |
| 256 | 1.106 ms | 1.175 ms | 1.284 ms | 0.178 ms |

Enabling Doppler within the new shader adds approximately 0.040/0.062/0.109 ms.
The separate background-capture SceneRender median was 2.298 ms for this small
procedural test scene, and is not included in the draw times above. Timings depend
on coverage, resolution, scene and GPU clocks; use them as a local comparison.
Reports: `doppler_paired_measurements.json` and `doppler_gpu_measurements.json`.
Previews: `Doppler_Comparison.png`, `Doppler_Animated.gif` and the individual
`Doppler_*.png` files under Saved/CodexBlackHoleValidation.
