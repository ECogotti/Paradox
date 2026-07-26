# ParadoxEditorTools architecture

`ParadoxEditorTools` is an editor-only module intended to host multiple independent content-production and validation tools for the Paradox project.

## Module responsibilities

- Register editor integrations and Content Browser actions through `UToolMenus`.
- Keep each tool isolated behind a dedicated implementation type and tool-specific menu identifiers.
- Share only module-wide infrastructure such as logging and lifecycle registration.
- Remain free of runtime gameplay dependencies.

## Current tool: StaticMeshVoxelBatch

`StaticMeshVoxelBatch` corrects voxel Static Mesh assets whose imported scale is wrong and optionally rebakes their pivot.

Responsibilities:

- Collect only `UStaticMesh` assets from selections or folders.
- Optionally recurse through subfolders.
- Show a modal Slate options dialog.
- Bake independent XYZ scale percentages into source `FMeshDescription` data.
- Bake one of nine bounds-based pivot presets.
- Keep sockets and supported simple collision aligned.
- Rebuild the asset and mark it dirty without saving.

## Boundaries

- Do not add runtime dependencies or gameplay behavior.
- Do not auto-save assets.
- Do not compensate placed Actors unless a future explicit feature requests it.
- Keep folder scanning in the Asset Registry and avoid loading non-Static-Mesh assets.
- Preserve the nine user-facing pivot presets, including the three equivalent center entries.
- New tools must use their own clearly scoped class and file names instead of expanding `StaticMeshVoxelBatch` with unrelated behavior.
