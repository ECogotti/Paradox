# Paradox Material Expressions

`ParadoxMaterialExpressions` is the runtime-safe module in the `ParadoxEditorTools` plugin that owns native Material Expression classes. It deliberately has no dependency on the plugin's editor-only `ParadoxEditorTools` module, so Material assets containing these nodes can load in game and cooked targets.

## Paradox Outline

`Paradox Outline` is a native Material Editor node for post-process silhouettes. It samples Custom Depth and Custom Stencil around the current pixel, classifies stencil values into semantic Puzzle Input, Puzzle Output, Hover, and Selection ranges, and exposes independent masks plus a convenience color output.

The default semantic ranges are:

- Puzzle Input: `210-219` (inclusive).
- Puzzle Output: `220-229` (inclusive).
- Hover: `230-239` (inclusive).
- Selection: `240-249` (inclusive).

The category is determined by the range, not the exact stencil ID. Adjacent values inside one category do not create a seam solely because their numeric IDs differ. If custom ranges overlap, priority is Selection, Hover, Puzzle Output, then Puzzle Input.

## Required project and mesh setup

In **Project Settings > Engine > Rendering > Postprocessing**, set **Custom Depth-Stencil Pass** to **Enabled with Stencil**.

For every mesh that should participate:

1. Enable **Render CustomDepth Pass** on the Static Mesh or Skeletal Mesh component.
2. Set **CustomDepth Stencil Value** to a value in the desired semantic range.
3. Use `210-219` for Puzzle Input, `220-229` for Puzzle Output, `230-239` for Hover, and `240-249` for Selection unless the node ranges are overridden.

The node intentionally has no fallback when Custom Depth with stencil is disabled.

## Creating the material

1. Create a Material and set **Material Domain** to **Post Process**.
2. Right-click in the Material graph and search for `Paradox Outline`.
3. Add a **Scene Texture** node configured as `PostProcessInput0` for the original scene color.
4. Add `Paradox Outline.OutlineColor` to `PostProcessInput0`, or use `CombinedMask` in a custom `Lerp`/blend.
5. Connect the resulting RGB value to **Emissive Color**.
6. Place the Material in a Post Process Volume's **Post Process Materials** array, or apply it through another normal post-process blendable workflow.

The masks can also be connected directly to Emissive while debugging:

- `HoverMask` shows only Hover outlines.
- `SelectionMask` shows only Selection outlines.
- `PuzzleInputMask` and `PuzzleOutputMask` show the wire categories.
- `CombinedMask` shows the union of all four categories.

## Inputs

Every input pin is optional. When a pin is unconnected, the corresponding node property in the Details panel supplies its value.

| Input | Default | Meaning |
| --- | ---: | --- |
| `Thickness` | `1.0` | Screen-space outline radius in pixels. Runtime-clamped to `0-8`; `0` disables the edge response. |
| `Softness` | `0.0` | `0` uses the hard maximum neighbor response. Values up to `1` blend toward the average 8-direction coverage for a cheap feathered response. |
| `DepthThreshold` | `10.0 cm` | Used only when **Enable Internal Depth Edges** is enabled. Sets the minimum symmetric reciprocal-Custom-Depth curvature across opposing samples in the same semantic category, converted back to an approximate centimeter difference. |
| `StencilBoundaryStrength` | `1.0` | Strength of transitions between unrelated pixels and any of the four semantic categories. Runtime-clamped to `0-1`. |
| `HoverIntensity` | `1.0` | Multiplier applied only to `HoverMask`, clamped to `0-16` before final mask saturation. |
| `SelectionIntensity` | `1.0` | Multiplier applied only to `SelectionMask`, clamped to `0-16` before final mask saturation. |
| `HoverColor` | White | RGB contribution used by `OutlineColor` for Hover. HDR values are allowed. |
| `SelectionColor` | White | RGB contribution used by `OutlineColor` for Selection. HDR values are allowed. If connected to a Vector Parameter, do not name that parameter exactly `SelectionColor`; use a project-specific name such as `ParadoxSelectionOutlineColor`. |
| `OcclusionBias` | `0.1 cm` | Non-negative minimum tolerance used by `Visible Only` and `Occluded Only` Scene/Custom Depth comparisons. A conservative distance-scaled precision allowance is also applied far from the camera. |
| `HoverStencilMin` | `230` | Inclusive Hover range lower bound, clamped to `0-255`. |
| `HoverStencilMax` | `239` | Inclusive Hover range upper bound, clamped to `0-255`. Min/max may be entered in either order. |
| `SelectionStencilMin` | `240` | Inclusive Selection range lower bound, clamped to `0-255`. |
| `SelectionStencilMax` | `249` | Inclusive Selection range upper bound, clamped to `0-255`. Min/max may be entered in either order. |
| `PuzzleInputIntensity` | `1.0` | Multiplier applied only to `PuzzleInputMask`, clamped to `0-16`. |
| `PuzzleOutputIntensity` | `1.0` | Multiplier applied only to `PuzzleOutputMask`, clamped to `0-16`. |
| `PuzzleInputColor` | Cyan `(0.0, 0.5, 1.0)` | RGB contribution used by `OutlineColor` for Puzzle Input. |
| `PuzzleOutputColor` | Amber `(1.0, 0.35, 0.0)` | RGB contribution used by `OutlineColor` for Puzzle Output. |
| `PuzzleInputStencilMin` | `210` | Inclusive Puzzle Input range lower bound. |
| `PuzzleInputStencilMax` | `219` | Inclusive Puzzle Input range upper bound. |
| `PuzzleOutputStencilMin` | `220` | Inclusive Puzzle Output range lower bound. |
| `PuzzleOutputStencilMax` | `229` | Inclusive Puzzle Output range upper bound. |

## Occlusion mode

The Details panel exposes two independent properties. `Occlusion Mode` controls Hover and
Selection; `Puzzle Wire Occlusion Mode` controls Puzzle Input and Puzzle Output.

- `Visible Only` is the Hover/Selection default. Stencil categories are classified independently from visibility so precision changes cannot become false outlines. A completed boundary is retained only when its highlighted side is no farther than Scene Depth plus the effective occlusion tolerance.
- `Through Walls` skips Scene Depth testing, so Custom Depth silhouettes remain visible behind opaque geometry.
- `Occluded Only` retains a boundary only when Custom Depth is farther than opaque Scene Depth plus
  the effective tolerance.

Hover and Selection default to `Visible Only`. Puzzle wires default to `Occluded Only`: their
ordinary surface material remains visible whenever the wire itself is visible, while the post
process outline appears only through an opaque occluding mesh. An occluder must write Scene Depth;
translucent materials that do not write depth cannot hide/activate the wire outline.

## Internal depth edges

`Enable Internal Depth Edges` is disabled by default. In the default state the node outlines semantic stencil silhouettes only, preventing perspective gradients and depth-buffer precision from filling the interior of distant meshes.

Enable it only when real depth breaks inside one highlighted stencil category must also be outlined. `DepthThreshold` controls that optional branch; higher values accept fewer internal depth edges.

## Reserved SelectionColor parameter name

`SelectionColor` is a valid Paradox Outline input-pin name, but it is also a reserved Unreal Engine Material parameter name used by the editor selection overlay. A Material **Vector Parameter** named exactly `SelectionColor` can therefore feed its alpha into Unreal's hidden final selection-color blend. With the usual alpha value of `1`, that blend replaces the complete frame with a solid color.

Name the connected Vector Parameter something project-specific, for example `ParadoxSelectionOutlineColor`. The node still exposes the input pin as `SelectionColor`; only the Material parameter's own name must differ. The compiler reports an actionable Material error when it can detect this reserved-name connection directly or through reroute nodes.

## Outputs and priority

| Output | Meaning |
| --- | --- |
| `HoverMask` | Hover silhouette mask after Selection priority is applied. |
| `SelectionMask` | Selection silhouette mask. |
| `CombinedMask` | Saturated maximum of the final Puzzle Input, Puzzle Output, Hover, and Selection masks. |
| `OutlineColor` | Sum of each category color multiplied by its final priority-filtered mask. |
| `PuzzleInputMask` | Puzzle Input silhouette mask after higher-category priority is applied. |
| `PuzzleOutputMask` | Puzzle Output silhouette mask after Selection/Hover priority is applied. |

`CombinedMask` and `OutlineColor` include all four categories. Priority is Selection, Hover, Puzzle Output, then Puzzle Input, so ambiguous pixels do not mix category colors.

## Sampling and performance

The node uses a fixed 8-direction ring: four cardinal and four diagonal samples. The ring radius is `Thickness` multiplied by inverse viewport size, which keeps thickness approximately stable in screen-space pixels across viewport resolutions and screen percentages.

Per connected output evaluation:

- When both category policies use `Through Walls`: 9 Custom Depth + 9 Custom Stencil reads = 18
  scene-texture reads.
- When either category policy uses `Visible Only` or `Occluded Only`: the same 18 reads + 9 Scene
  Depth reads = 27 scene-texture reads.

The kernel size is fixed; Material inputs never control loop bounds, and no CPU work or allocation occurs at runtime. Puzzle Input/Output reuse the existing Custom Depth/Stencil samples and add only classification/color ALU. When several outputs are consumed in one Material, check the Material shader statistics because compiler/backend common-subexpression elimination can vary by platform.

## Known limitations

- The node is restricted to Post Process materials.
- It depends on valid Custom Depth and Custom Stencil data and does not repair project or component setup.
- Leave the Material's advanced **Enable new material translator** option disabled. The installed binary UE 5.8 build does not export the Material IR implementation needed by an external plugin expression; the normal `FMaterialCompiler` translator is supported.
- It is a screen-space effect, so very thin geometry, temporal jitter, dynamic resolution, and disocclusion can affect edge stability.
- `Occluded Only` responds only to opaque/depth-writing occluders. It does not infer occlusion from
  translucent color, collision, Actor visibility flags, or Custom Stencil on the occluder.
- When internal depth edges are enabled, `DepthThreshold` measures symmetric reciprocal-depth curvature converted to an approximate difference in Unreal units (centimeters), with a small distance-scaled precision allowance.
- The fixed 8-sample kernel is intentionally bounded and is not a large blur; very high softness does not behave like a Gaussian blur.
- Unreal Engine 5.8 exposes `UMaterialExpression::Build` and Material IR types in headers, but the installed binary Engine does not export the required Material IR implementation symbols to external plugin modules. The node therefore uses the exported `FMaterialCompiler` path also used by external native expressions in this engine build.

## Troubleshooting after a plugin update

Native `Compile` changes in a Material Expression do not necessarily invalidate an already cached Material shader map. If an existing `Paradox Outline` node still shows behavior from an older plugin build, close and reopen Unreal Editor, delete that node from the Material, add a fresh `Paradox Outline` node, reconnect it, then Apply and Save the Material. Replacing the node changes the Material graph structurally and forces Unreal to emit and compile the current implementation.

For a quick `OutlineColor` check, connect it directly to Emissive Color. The background must be black and only Puzzle Input/Output, Hover, or Selection outlines must be colored. Restore the final composition afterward by adding `OutlineColor` to the RGB output of `SceneTexture:PostProcessInput0`.

## Automated coverage

`Paradox.MaterialExpressions.Outline.Contract` verifies node creation, preservation of all 21
inputs and 6 outputs, append-only occlusion enum values, independent defaults, visible/occluded
depth semantics, all range bounds, same-category ID classification, four-category priority, and
Post Process domain restrictions.

Visual validation should still cover Static and Skeletal Meshes, occluded and visible objects, thickness values `1`, `2`, and `4`, softness, multiple viewport sizes, Material save/reopen, and the target platforms used by the project.
