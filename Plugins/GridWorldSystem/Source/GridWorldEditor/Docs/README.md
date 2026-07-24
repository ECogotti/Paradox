# GridWorldEditor module

`GridWorldEditor` extends the existing Level Editor Build menu; it does not create a custom editor window.

The module provides full build, selection rebuild, clear, validation, Supported Agent selection, cell inspection and visualization toggles. Selection rebuild converts selected Actor bounds to navigation dirty areas; runtime code maps those areas to chunks with a one-chunk halo.

Moving, rotating, or scaling a `GridNavigationBoundsVolume` automatically triggers a GridWorld build after the Transform edit completes through Unreal's native navigation-bounds notification. Editing cell dimensions, chunk size, Movement Mode, corner cutting, path-following style/tolerances, slope/step/drop limits, collision profile or Supported Agent dimensions sends the same notification. Translation and scale change only the covered range of the world-anchored lattice; cell `(0,0,0)` remains centered at world origin. Rotation still changes grid orientation, and logical cell dimensions remain unchanged.

Each bounds also exposes **Auto Rebuild On Geometry Changes**, enabled by default. When Unreal reports a native geometry dirty area for navigation-relevant collision, only intersecting volumes with this option enabled rebuild their affected chunks. The option does not suppress bounds Transform/property rebuilds or explicit Build-menu commands. It relies on the Level Editor's global **Update Navigation Automatically** preference.

After a successful full or incremental editor build, GridWorld marks the package containing its `GridNavigationData` actor dirty. Save the level, or use **Save All** for sublevels/External Actors, to retain the generated versioned snapshot across closing and reopening a level or the editor. Clearing generated data marks the same package dirty so the clear is also persistent after save.

The editor module depends on the runtime module, while `GridWorld` has no dependency on `GridWorldEditor` or other editor-only modules.

Validation reports invalid or zero Actor scale, non-box brushes, invalid dimensions, duplicate grid GUIDs and ambiguous overlaps through `LogGridWorld`. Full Actor rotation and non-uniform scale are supported. Validation and failed builds never clear the last valid runtime snapshot.
