# Paradox Editor Tools

Plugin for Paradox native Material Expressions and extensible editor content-production tools in Unreal Engine 5.8.

## Installation

1. Copy the `ParadoxEditorTools` folder into the project's `Plugins` directory.
2. Regenerate project files if required.
3. Build the Editor target.
4. Enable **Paradox Editor Tools** in **Edit > Plugins** if it is not enabled automatically.
5. Restart the editor.

## Modules

- `ParadoxMaterialExpressions` (Runtime): owns serializable native Material Expression classes used by Material assets in Editor, game, and cooked targets.
- `ParadoxEditorTools` (Editor): owns Content Browser and other content-production integrations.

## Included features

- `StaticMeshVoxelBatch`: batch correction of voxel Static Mesh scale and bounds-based pivot, from selected assets or Content Browser folders.
- `Paradox Outline`: native Post Process Material node producing Hover and Selection outline masks from Custom Depth and Custom Stencil.

Detailed behavior and limitations are documented in:

`Source/ParadoxEditorTools/Docs/README.md`

`Source/ParadoxMaterialExpressions/Docs/README.md`
