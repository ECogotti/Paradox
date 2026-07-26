# ParadoxEditorTools module

## Purpose

`ParadoxEditorTools` is an extensible editor-only Unreal Engine 5.8 module for project content-production tools.

The first included tool is `StaticMeshVoxelBatch`, designed to correct voxel Static Mesh assets imported or generated with the wrong scale and to bake a bounds-based pivot into their source geometry.

## Current tools

### StaticMeshVoxelBatch

Works on:

- one or more selected Static Mesh assets;
- one or more selected Content Browser folders;
- direct folder contents only, or recursively through subfolders.

The action is available only for selections containing exclusively Static Mesh assets. Folder processing filters the discovered assets to `UStaticMesh`.

## Content Browser actions

### Static Mesh selection

Right-click a selection containing only Static Mesh assets and choose:

`Static Mesh Voxel Batch`

### Folder selection

Right-click one or more Content Browser folders and choose:

`Static Mesh Voxel Batch`

The options window exposes `Include subfolders` when launched from a folder.

## Scale values

Scale is expressed as a percentage per axis:

- `100, 100, 100`: unchanged size;
- `200, 100, 100`: double size on X only;
- `50, 50, 50`: half size on every axis.

Values must be greater than zero. Negative mirroring is intentionally not supported.

## Pivot presets

The pivot is baked by translating vertices so that the chosen bounds point becomes local origin `(0, 0, 0)`.

- `ZTop`: maximum Z, centered on X and Y;
- `ZCenter`: bounds center;
- `ZBelow`: minimum Z, centered on X and Y;
- `XTop`: maximum X, centered on Y and Z;
- `XCenter`: bounds center;
- `XBelow`: minimum X, centered on Y and Z;
- `YTop`: maximum Y, centered on X and Z;
- `YCenter`: bounds center;
- `YBelow`: minimum Y, centered on X and Z.

`XCenter`, `YCenter`, and `ZCenter` are intentionally separate choices but have the same geometric result.

## What is updated

The operation modifies all editable source LOD Mesh Descriptions. Rebuilt or generated data is refreshed through `UStaticMesh::Build`, including render data, generated LODs, Nanite data, and complex collision derived from triangles.

The tool also transforms:

- Static Mesh socket locations;
- standard simple collision shapes: spheres, boxes, capsules, tapered capsules, and convex elements.

Advanced level-set and skinned collision elements are not transformed and generate a warning in `LogParadoxEditorTools`.

## Undo and saving

The batch is wrapped in an editor transaction and can be undone with `Ctrl+Z`.

Assets are marked dirty but are not saved automatically. Review the result, then save the desired assets normally.

## Important behavior and limitations

- The operation is destructive to the Static Mesh source geometry stored in the asset. Work on source control or make a backup first.
- Reimporting from FBX, glTF, or another source file can overwrite the baked result because the external source asset is unchanged.
- Existing Actor and component transforms are not compensated. Moving the asset pivot changes where its geometry appears relative to already placed instances.
- Non-uniform scaling of sphere or capsule collision is inherently approximate because those primitive types cannot represent arbitrary ellipsoids or sheared shapes. Unreal's native simple-collision rescaling rules are used.
- Negative scale and mirroring are not supported.

## Extending the plugin

Add future tools as separate implementation files under `Private`, with tool-specific class names and menu identifiers. Keep the plugin and module named `ParadoxEditorTools`; do not create a new module for every small editor action unless its dependencies or lifecycle justify separation.

## Logging

The module owns `LogParadoxEditorTools` and logs successful operations, failures, and collision warnings for all contained tools.
