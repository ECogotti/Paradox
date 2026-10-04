# Black hole feature switches

The actor's **Paradox > Black Hole > Features** group controls compiled shader
variants. Paradox tuning is prioritized in Details; unrelated actor categories
remain below it. Disabled feature controls are hidden. Diagnostics are under the
advanced **Status** group and remain inspectable.

| Flag | Effect and work removed when disabled |
|---|---|
| Enable Accretion Disk | Disk emission, density integration and all disk texture reads; its detail flags become inactive |
| Enable Gravitational Lensing | Curved RK4 ray integration; the disk uses 64 straight samples and refraction becomes inactive |
| Enable Refraction | Background sampling, screen projection, foreground depth guard and inverse proxy-fog correction; SceneCapture stops |
| Enable Fine Noise | Fine-noise texture dimensions/samples and turbulence math |
| Enable Disk Structures | Filament/clump emission modulation |
| Enable Variable Volume | Correlated density, thickness and midplane variation |
| Enable Doppler | Orbital velocity, local observer projection, brightness beaming and color shift |
| Enable Glow | Artistic luminous ring and halo |

The black core and FogSafe material behavior are fundamental and remain enabled.
Refraction requires lensing. Disk details require the disk. Dependency changes
retain the requested flags and every tuning value: re-enabling the parent restores
the previously selected details. Lensing without refraction is supported.

The structure texture, amount, scale, lifetime and vortex strength are shared by
Disk Structures and Variable Volume. Their controls stay visible while either
feature is enabled. Four flow/structure reads per candidate segment remain until
both are off. Disabling one group removes its own calculations. Emission Strength
and Inner Color are shared by the disk and Glow. The minimum local thickness is
still 0.005. Animation uses the material Time input and does not restart on a switch.

## Material selection and existing actors

New native actors use `MI_BlackHole_FogSafe_Structured_Doppler` as their authoring
profile. Existing Base Material assignments, Blueprint assets, maps and tuning
values are retained. The initial feature preferences of existing FogSafe profiles
are inferred once: NoDoppler profiles keep Doppler off; nonstructured profiles
keep both new structure groups off. Later material changes retain the selected
feature preferences; use the flags to change the enabled effects.

Supported families are `MI_BlackHole_FogSafe`, its explicit Doppler/NoDoppler
pairs and their Structured variants. Children of those master families are
recognized. Base Material remains the authoring profile; Sphere's current MID
uses the selected modular variant. Do not infer active effects from the Base
Material asset's name after changing flags.

Legacy v2/non-FogSafe or unrelated custom materials still use their original
rendering/capture path. Optimized feature APIs reject them with a warning and
Feature State Message. Assign a supported FogSafe profile before using the flags.
Editing flags does not rewrite the original material.

## Blueprint and future options menus

Use `SetFeatureEnabled(Feature, Enabled)` for one effect, or `SetFeatureMask(Mask)`
for an atomic configuration. Both return success/failure. The exposed bool property
setters dispatch the same implementation immediately. Native C++ assignments do
not invoke Blueprint setters; C++ callers should use the functions.

`GetRequestedFeatureMask()` returns stored preferences. `ActiveFeatureMask` reports
the compiled effective configuration, after dependencies and zero strengths.
`bSupportsFeatureVariants` and `FeatureStateMessage` explain compatibility/errors.
Mask bits follow `EParadoxBlackHoleFeature` indices:

| Bit / value | Feature |
|---|---|
| 0 / 1 | AccretionDisk |
| 1 / 2 | GravitationalLensing |
| 2 / 4 | Refraction |
| 3 / 8 | FineNoise |
| 4 / 16 | DiskStructures |
| 5 / 32 | VariableVolume |
| 6 / 64 | Doppler |
| 7 / 128 | Glow |

Examples: all enabled = 255; full shader without refraction = 251; without
structures and variable volume = 207; core only = 0. Invalid masks outside 0..255
are rejected. A future menu can persist the requested mask and apply it once;
no menu or global scalability setting is added by this feature.

Zero Lensing Strength also disables refraction. Zero Refraction Strength stops
capture and selects the variant without background code. Zero Noise Amount,
Structure Amount, Doppler Strength/Velocity, Rotation Speed and Halo Strength
remove the corresponding inactive work where appropriate, preserving flag preferences.
The MID is reused when the effective mask is unchanged. A changed mask selects a
new compiled parent and reapplies actor tuning/colors/textures. The actor-owned
render target survives these changes and is reused when capture resumes.

`DiskStructureTexture=None` restores the authoring profile's default, or the
modular default when the original profile has no structure texture.

## Compiled assets and extension

`MM_BlackHole_FogSafe_Modular` has one Float4 Custom node and eight static switch
inputs. `Variants/MI_BlackHole_Flags_XX` contains 102 canonical configurations;
XX is a two-digit hexadecimal mask. `DA_BlackHoleMaterialVariants` holds hard
references to all variants and supported source profiles, making them cook
dependencies of the actor. This trades shader/package size and initial loading
for less runtime work. Runtime never changes a static switch on a MID or requests
shader compilation. First-use PSO/driver work still depends on project precaching.

Regeneration tools:

1. Run `Tools/BlackHole/build_modular_shader.py` with Python to produce
   `Shaders/BlackHoleFogSafe_Modular.hlsl` from the preserved structured source.
2. Build ParadoxEditor Development.
3. Run `Tools/BlackHole/create_material_variants.py` with Unreal Python. It saves
   only the modular master, variants and catalog, and checks previous asset hashes.

All Enable inputs must be static-switch constants 0/1. Connecting runtime scalar
parameters would retain shader code and defeat this optimization. SceneDepth
Dependency is separately gated in the material graph to remove that material
dependency when refraction is off. RGB goes to Emissive Color and A to Opacity;
the Refraction pin remains disconnected. The three FogSafe material flags and
HDR capture configuration remain unchanged.

The catalog must have exactly one material per canonical mask. Missing/duplicate
entries fail initialization observably. New custom shader families should provide
their own complete catalog and source-profile mapping rather than claiming support
for a shader with different behavior. Single main view, external camera and uniform
scale limits from [Black hole actor](BLACK_HOLE.md) still apply.

## Verification

Reports, previews and GPU traces are under `Saved/CodexBlackHoleValidation`.
`features_actor_validation.json` covers all 256 requested masks, their 102 effective
configurations, Blueprint setters, retained tuning, render-target reuse, dependency
restoration, invalid inputs, legacy rejection and initial Blueprint migration.
`features_material_creation.json` records actual SM6 compiler statistics and saved
asset references. GPU draw measurements exclude background capture and report its
cost separately; savings depend on screen coverage, scene, GPU and sample count.

UE 5.8.3 / SM6 validation passed **1,841 actor/Blueprint checks** and **127 render
checks**. All 102 shader maps are complete, including the editor compositing/debug
permutations. The 24 HDR comparisons against the four existing FogSafe profiles
(two projections, 128/192/256 steps) have zero normalized difference in this fixture.
At large scale/world offsets the modular and previous shaders also match; the
orthographic capture's own clip-plane/background behavior is unchanged. Exponential
and volumetric fog comparisons with zero displacement differ by at most 0.00128,
below the 0.003 criterion. Saved previous asset hashes are unchanged.

The paired benchmark alternates order over four rounds of 32 samples, RTX 3080,
D3D12 SM6, 768×512, base Disk Thickness 0.005. Values below are proxy ParallelDraw
medians and exclude the background capture:

| Configuration | 128 steps | 192 steps | 256 steps |
|---|---:|---:|---:|
| Full | 0.908 ms | 1.306 ms | 1.679 ms |
| Disk off | 0.330 ms | 0.413 ms | 0.515 ms |
| Lensing off (also refraction off) | 0.536 ms | 0.508 ms | 0.490 ms |
| Refraction off | 0.823 ms | 1.075 ms | 1.322 ms |
| Fine noise off | 0.964 ms | 1.199 ms | 1.414 ms |
| Structures off, variable volume retained | 0.985 ms | 1.223 ms | 1.422 ms |
| Variable volume off, structures retained | 0.955 ms | 1.247 ms | 1.423 ms |
| Doppler off | 0.836 ms | 1.110 ms | 1.250 ms |
| Glow off | 0.925 ms | 1.248 ms | 1.422 ms |
| Both structure groups off | 0.822 ms | 1.153 ms | 1.319 ms |
| Core only | 0.046 ms | 0.046 ms | 0.043 ms |

The separate background SceneRender median is **2.294 ms** in this simple fixture;
disabling refraction stops that pass as well as removing its proxy shader code.
This is not a prediction for the full game scene. At 128 steps, several small
removals did not improve elapsed GPU time despite eliminating code/resources.
GPU scheduling, register allocation and scene coverage make timings non-monotonic;
use the paired results rather than assuming every toggle yields the same saving.

Compiler evidence: full = 1,445 pixel instructions / 6 samplers; refraction off =
1,119 / 4; fine noise off = 1,372 / 5; both structure groups off = 1,271 / 5;
core only = 554 / 2. Counts include Unreal's base pass; sampler counts are not
per-segment texture-read counts. `features_compiler_statistics.json` and
`features_gpu_measurements.json` contain the complete tables and round medians.

`Features_Comparison.png` shows the actual variants. `Features_Animated.gif` renders
12 seconds while stepping through full, no fine noise, no shared structure field,
no Doppler, no refraction, no lensing, no disk and core-only. The clock continues
between modes. The visual preset uses Disk Thickness 0.01 for readability, fixed
exposure and Bloom disabled; that preset is not applied to actors automatically.

In Development Editor builds, `Paradox.BlackHole.InspectShaders` checks all catalog
entries' actual SM6 rendering shader maps and logs complete/total in LogParadox.
For a validation fixture that changes the master in memory, request
`Paradox.BlackHole.CacheVariantShaders` and then wait for asset compilation before
rendering. Material statistics alone can leave partial shader maps after graph
changes. These test helpers are explicit editor operations, have no per-frame
work, do not save assets, and are absent from Shipping.
