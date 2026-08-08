"""Create the project GridWorld style shared by paths and interaction cells."""

import unreal


ASSET_PATH = "/Game/Data/GridWorld/DA_ParadoxGridCellStyle"
MESH_PATH = "/GridWorldSystem/Presentation/SM_GridWorldBlock"
MATERIAL_PATH = "/GridWorldSystem/Presentation/M_GridRuntimeCell"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
    require(
        unreal.EditorAssetLibrary.delete_asset(ASSET_PATH),
        f"Could not replace {ASSET_PATH}",
    )

style_class = require(
    unreal.load_class(None, "/Script/GridWorld.GridCellVisualStyle"),
    "UGridCellVisualStyle is unavailable; compile GridWorld before running this script",
)
mesh = require(unreal.load_asset(MESH_PATH), f"Missing mesh {MESH_PATH}")
material = require(unreal.load_asset(MATERIAL_PATH), f"Missing material {MATERIAL_PATH}")
style = require(
    unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DA_ParadoxGridCellStyle",
        "/Game/Data/GridWorld",
        style_class,
        unreal.DataAssetFactory(),
    ),
    "Could not create DA_ParadoxGridCellStyle",
)
style.set_editor_property("cell_mesh", mesh)
style.set_editor_property("cell_material", material)
style.set_editor_property(
    "primary_overlay_color", unreal.LinearColor(0.05, 1.0, 0.55, 0.75)
)
style.set_editor_property(
    "secondary_overlay_color", unreal.LinearColor(1.0, 0.2, 0.05, 0.8)
)
style.set_editor_property("cell_inset_fraction", 0.03)
style.set_editor_property("surface_offset", 2.0)
style.set_editor_property("start_cull_distance", 5000)
style.set_editor_property("end_cull_distance", 15000)
style.set_editor_property("cast_shadow", False)
require(
    unreal.EditorAssetLibrary.save_loaded_asset(style, only_if_is_dirty=False),
    f"Could not save {ASSET_PATH}",
)
unreal.log(f"Created {style.get_path_name()} with shared GridWorld mesh and material")
