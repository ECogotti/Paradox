# Paradox Post Process Outline Material Node — Codex Specification

## Goal

Create a reusable native Unreal Engine 5.6 Material Editor node that generates configurable post-process outlines from `Custom Depth` and `Custom Stencil`.

The node must support two independent outline categories:

```text
Hover
Selection
```

Reserved stencil ranges:

```text
Hover:
    230–239

Selection:
    240–249
```

Default stencil values:

```text
Hover = 230
Selection = 240
```

The node must classify stencil values by range, not only by exact value.

The final implementation must be a real native Material Editor expression derived from the appropriate Unreal Engine material-expression base class.

Do not leave the final solution as a generic `Custom` HLSL node requiring manual code inside Material assets.

---

# Mandatory project workflow

Before modifying code:

1. Read the root `AGENTS.md`.
2. Identify the module/plugin that should own this feature.
3. Search for relevant local `CODEX` folders and read them.
4. Read relevant existing `Docs`.
5. Inspect the current project for existing custom Material Expressions.
6. Inspect Unreal Engine 5.6 source for the correct `UMaterialExpression` implementation pattern.
7. Verify every Unreal API instead of relying on old tutorials or memory.
8. Compile the affected target after implementation.
9. Fix all compilation errors caused by the change.
10. Update user-facing documentation in the owning module/plugin `Docs` folder.

Do not modify Engine source.

---

# Required Material Editor node

Create a dedicated Material Expression conceptually equivalent to:

```text
UMaterialExpressionParadoxOutline
```

Exact naming may adapt to project conventions.

The node must:

- appear in Material Editor search;
- have a clear display name such as `Paradox Outline`;
- appear under a clear category such as `Paradox` or `Paradox | Post Process`;
- expose meaningful input pins;
- expose separate outputs for Hover and Selection;
- work inside Post Process materials;
- serialize correctly inside Material assets;
- survive editor restart and material recompilation.

Useful search keywords:

```text
outline
hover
selection
stencil
custom depth
post process
paradox
```

---

# Unreal Engine 5.6 API verification

Before implementing, inspect UE5.6 native Material Expressions and verify the correct path for shader generation.

At minimum inspect examples equivalent to:

```text
UMaterialExpression
UMaterialExpressionSceneTexture
UMaterialExpressionSceneDepth
UMaterialExpressionSceneTexelSize
one expression with multiple outputs
one expression using the current UE5.6 HLSL generation path
```

Verify which overrides are actually required in UE5.6, such as:

```text
Compile(...)
GenerateHLSLExpression(...)
Build(...)
GetCaption(...)
GetCreationName(...)
GetKeywords(...)
GetInputs(...)
GetInputType(...)
GetOutputType(...)
```

Use only APIs verified in the installed engine version.

Do not assume an old `Compile()`-only implementation is sufficient.

---

# Stencil classification

Internally classify stencil values using range tests equivalent to:

```text
IsHoverStencil(S)
    = S >= HoverStencilMin
      AND
      S <= HoverStencilMax

IsSelectionStencil(S)
    = S >= SelectionStencilMin
      AND
      S <= SelectionStencilMax
```

Default values:

```text
HoverStencilMin = 230
HoverStencilMax = 239

SelectionStencilMin = 240
SelectionStencilMax = 249
```

The ranges must remain editable either through Material inputs or designer-facing node properties.

Preferred behavior:

```text
unconnected/default
    -> use 230–239 and 240–249

overridden
    -> use supplied values
```

If custom ranges overlap, Selection has priority.

---

# Core rendering behavior

The node must detect screen-space silhouettes by sampling neighbouring pixels around the current post-process pixel.

Conceptually:

```text
Current pixel
    ↓
CustomDepth + CustomStencil
    ↓
Neighbour samples
    ↓
Stencil category classification
    ↓
Depth / category discontinuity detection
    ↓
HoverMask
SelectionMask
```

The outline must be generated around relevant silhouettes.

Hover and Selection must remain outline-only and must not generate a full fill mask for the
entire object. Puzzle Input and Puzzle Output are the deliberate exception: their final masks and
color contribution include both the sampled boundary and the visible/occluded interior selected by
the independent Puzzle Wire occlusion policy.

Do not outline every small internal depth variation of one continuous highlighted surface.

---

# Required node inputs

## `Thickness`

Scalar.

Controls outline thickness in screen-space pixels or an equivalent pixel-scaled radius.

Default:

```text
1.0
```

Requirements:

- larger values increase outline thickness;
- sampling cost remains bounded;
- clamp to a practical supported range;
- do not allow arbitrary values to create unbounded shader loops.

Determine the practical maximum after testing shader cost.

---

## `Softness`

Scalar.

Controls feathering of the outline edge.

Default:

```text
0.0
```

Semantics:

```text
0 -> hard edge
higher values -> progressively softer edge
```

Do not implement this using an expensive unbounded blur.

Use a cheap controlled falloff.

---

## `DepthThreshold`

Scalar.

Controls sensitivity to Custom Depth discontinuities.

Purpose:

- reduce false internal lines;
- tune silhouette detection;
- compensate for depth precision differences.

Must be non-negative.

Use a verified UE5.6 depth comparison strategy and document its semantics.

---

## `StencilBoundaryStrength`

Scalar.

Default:

```text
1.0
```

Controls contribution from transitions between semantic stencil categories.

It must support boundaries such as:

```text
non-highlighted ↔ Hover
non-highlighted ↔ Selection
Hover ↔ Selection
```

It must not create internal seams solely because neighbouring objects use different numeric stencil IDs inside the same semantic range.

Examples:

```text
230 beside 231
    -> same Hover category

240 beside 241
    -> same Selection category
```

---

## `HoverIntensity`

Scalar.

Default:

```text
1.0
```

Multiplies only `HoverMask`.

---

## `SelectionIntensity`

Scalar.

Default:

```text
1.0
```

Multiplies only `SelectionMask`.

---

## `HoverColor`

Vector3 or Vector4.

Used by the optional ready-to-composite color output.

Do not hard-code the Hover color in C++.

---

## `SelectionColor`

Vector3 or Vector4.

Used by the optional ready-to-composite color output.

Do not hard-code the Selection color in C++.

---

## `OcclusionBias`

Scalar.

Used when comparing Scene Depth against Custom Depth.

Purpose:

- avoid flicker;
- handle nearly equal depths;
- compensate for depth precision.

Must be non-negative.

Use a conservative documented default.

---

# Occlusion policy

Expose a designer-facing option equivalent to:

```text
VisibleOnly
ThroughWalls
```

Default:

```text
VisibleOnly
```

## `VisibleOnly`

Compare Scene Depth and Custom Depth.

The outline must be hidden when the Custom Depth surface is behind opaque scene geometry.

## `ThroughWalls`

Allow the outline to remain visible even when the Custom Depth surface is occluded.

Use whichever representation is most appropriate for a native Material Expression:

```text
enum property
static option
input
```

Prefer the simplest editor-facing solution that does not create unnecessary shader permutations.

---

# Required outputs

Expose at least:

```text
HoverMask
SelectionMask
CombinedMask
```

Recommended convenience output:

```text
OutlineColor
```

Conceptually:

```text
OutlineColor =
    HoverColor * HoverMask
    +
    SelectionColor * SelectionMask
```

Selection must visually override Hover when both contributions overlap.

Do not return only a final RGB result.

Separate masks are required.

---

# Selection visual priority

Selection must have higher visual priority than Hover.

Equivalent behavior:

```text
FinalSelectionMask = computed Selection mask

FinalHoverMask =
    computed Hover mask
    * (1 - FinalSelectionMask)
```

or another equivalent solution.

The result must avoid unwanted color mixing between Hover and Selection at ambiguous pixels.

---

# Boundary behavior

These transitions may generate an outline:

```text
Background/unrelated stencil ↔ Hover
Background/unrelated stencil ↔ Selection
Hover ↔ Selection
```

Same-category numeric ID changes must not create a seam by default:

```text
230 ↔ 231 -> no internal seam
235 ↔ 239 -> no internal seam
240 ↔ 241 -> no internal seam
245 ↔ 249 -> no internal seam
```

The stencil range is the semantic category.

---

# Sampling pattern

This is a full-screen post-process effect.

Shader cost must remain predictable.

Evaluate compact fixed sampling patterns such as:

```text
4-neighbour cross
8-neighbour ring
small fixed radial sample pattern
multi-radius sampling
```

Choose the smallest pattern that produces acceptable silhouettes, including diagonal edges.

Requirements:

- no arbitrary dynamic sample loop driven directly by material input;
- no large blur kernel;
- no unbounded iteration;
- no CPU per-frame work;
- no allocations;
- no expensive multi-pass blur unless demonstrated to be necessary.

Document the final number of scene texture samples used by the node.

---

# Thickness and viewport resolution

Outline thickness must remain approximately stable in screen space across viewport resolutions.

Use verified UE5.6 scene texel size / viewport data.

Conceptually:

```text
SampleUV =
    CurrentUV
    + Direction
    * SceneTexelSize
    * Thickness
```

Do not use hard-coded UV offsets.

Test different:

```text
viewport sizes
screen percentages
```

and verify visual consistency.

---

# Custom Depth / Custom Stencil requirements

Document that the project must have Custom Depth configured with stencil support.

The node depends on valid access to:

```text
CustomDepth
CustomStencil
```

Do not add fallback systems when stencil support is disabled.

The documentation must clearly state the required Unreal project setting.

---

# Material graph usage

Document a minimal usage example.

Conceptually:

```text
PostProcessInput0
        │
        ├─────────────────┐
        │                 │
Paradox Outline           │
        │                 │
        ├ HoverMask       │
        ├ SelectionMask   │
        ├ CombinedMask    │
        └ OutlineColor    │
                          │
Final emissive/color composition
```

The Material author must remain free to decide how the masks are blended with the scene.

Do not force one final compositing style inside the Material Expression.

---

# Designer usability

Use meaningful:

```text
pin names
tooltips
display names
property categories
defaults
clamps
```

Expose only controls useful for tuning the outline.

Do not expose internal implementation details as public material parameters.

The node should be understandable directly from the Material Editor.

---

# Debug usefulness

The outputs must make it easy to inspect the algorithm.

At minimum:

```text
HoverMask
SelectionMask
CombinedMask
```

must be independently connectable to Emissive for debugging.

An optional extra output may encode category information if it is essentially free, but do not complicate the normal shader path just for debugging.

---

# Required tests

## 1. Hover default

Stencil:

```text
230
```

Expected:

```text
HoverMask > 0 on silhouette
SelectionMask = 0
```

---

## 2. Hover range upper bound

Stencil:

```text
239
```

Expected:

```text
classified as Hover
```

---

## 3. Selection default

Stencil:

```text
240
```

Expected:

```text
SelectionMask > 0 on silhouette
HoverMask = 0 where Selection has priority
```

---

## 4. Selection range upper bound

Stencil:

```text
249
```

Expected:

```text
classified as Selection
```

---

## 5. Values outside ranges

Test:

```text
0
1
100
229
250
255
```

Expected:

```text
no Hover outline
no Selection outline
```

with default ranges.

---

## 6. Two Hover IDs touching

Objects:

```text
230
231
```

Expected:

```text
single contiguous Hover silhouette
no seam solely because the stencil numbers differ
```

---

## 7. Two Selection IDs touching

Objects:

```text
240
241
```

Expected:

```text
single contiguous Selection silhouette
no seam solely because the stencil numbers differ
```

---

## 8. Hover beside Selection

Objects:

```text
230
240
```

Expected:

- stable boundary;
- no flicker;
- Selection visual contribution has priority.

---

## 9. Visible object

With:

```text
OcclusionMode = VisibleOnly
```

an unobstructed object must show its outline.

---

## 10. Occluded object

Highlighted object behind opaque geometry.

Expected:

```text
VisibleOnly -> suppressed
ThroughWalls -> visible
```

---

## 11. Thickness

Test at least:

```text
1
2
4
```

Expected:

- visible increase in outline width;
- bounded shader cost;
- no major artifacts.

---

## 12. Softness

Test:

```text
0
positive value
```

Expected:

```text
hard edge
vs
feathered edge
```

without destroying silhouette stability.

---

## 13. Static Mesh

Custom Depth + Custom Stencil on a Static Mesh must generate the expected outline.

---

## 14. Skeletal Mesh

Custom Depth + Custom Stencil on a Skeletal Mesh must generate the expected outline.

---

## 15. Different resolutions

Test multiple viewport sizes / screen percentages.

Expected:

```text
approximately stable screen-space thickness
```

---

## 16. Material serialization

Create a Material using the node.

Then:

```text
save
close editor
reopen editor
open material
recompile material
```

Expected:

```text
node remains valid
inputs/outputs remain connected
material compiles
```

---

## 17. Runtime / cooked compatibility

Verify runtime module ownership and dependencies.

The expression must not require editor-only modules when the Material asset is loaded in a cooked build.

Compile the relevant Editor and Game targets as appropriate.

If packaging/cooking is available in the project workflow, validate it.

---

# Documentation requirements

Update the owning module/plugin documentation with:

```text
what the node does
required Custom Depth setting
default Hover stencil range
default Selection stencil range
meaning of every input
meaning of every output
occlusion modes
example Post Process material graph usage
performance/sample-count notes
known limitations
```

---

# Acceptance criteria

The task is complete only when:

- a native dedicated Material Editor node exists;
- it appears in Material Editor search;
- it works in a Post Process material;
- it uses verified UE5.6 APIs;
- Hover range `230–239` works;
- Selection range `240–249` works;
- Hover and Selection masks are separate;
- `CombinedMask` is available;
- Selection has visual priority;
- same-category different stencil IDs do not create unwanted seams;
- `Thickness` is configurable;
- `Softness` is configurable;
- `DepthThreshold` is configurable;
- `StencilBoundaryStrength` is configurable;
- Hover and Selection intensity are independent;
- Hover and Selection colors are configurable;
- visible-only and through-wall modes work;
- occlusion bias is configurable;
- Static Meshes work;
- Skeletal Meshes work;
- thickness is approximately resolution-independent;
- shader sampling cost is bounded and documented;
- runtime/editor module boundaries are correct;
- relevant Docs are updated;
- affected targets compile successfully;
- no unrelated files or systems are modified.

---

# Scope

Implement only the native Material Editor outline node and the minimum supporting rendering/module/documentation code required for it.

The task is complete when the node can read Custom Depth / Custom Stencil and generate the required Hover and Selection outline masks.
