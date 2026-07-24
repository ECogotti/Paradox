"""Create the GridWorld milestone-1 default presentation assets in plugin content."""

import unreal


PRESENTATION_PATH = "/GridWorldSystem/Presentation"
MESH_PATH = f"{PRESENTATION_PATH}/SM_GridRuntimeCell"
MATERIAL_PATH = f"{PRESENTATION_PATH}/M_GridRuntimeCell"
STYLE_PATH = f"{PRESENTATION_PATH}/DA_GridRuntimeCellStyle_Default"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def replace_asset(asset_path):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        require(unreal.EditorAssetLibrary.delete_asset(asset_path), f"Could not replace {asset_path}")


def create_mesh():
    replace_asset(MESH_PATH)
    mesh = require(
        unreal.EditorAssetLibrary.duplicate_asset("/Engine/BasicShapes/Plane", MESH_PATH),
        "Could not duplicate the Engine plane",
    )
    static_mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if static_mesh_editor:
        mesh.set_editor_property("customized_collision", True)
        require(static_mesh_editor.remove_collisions(mesh), "Could not remove runtime-plane collision")
    else:
        unreal.log_warning(
            "StaticMeshEditorSubsystem is unavailable; component-level NoCollision remains enforced"
        )
    body_setup = mesh.get_editor_property("body_setup")
    if body_setup:
        body_setup.set_editor_property(
            "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX
        )
    mesh.set_editor_property("allow_cpu_access", False)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    return mesh


def create_custom_data(material, data_index, default_value, x, y, description):
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionPerInstanceCustomData,
        node_pos_x=x,
        node_pos_y=y,
    )
    require(expression, f"Could not create material custom-data expression {data_index}")
    expression.set_editor_property("data_index", data_index)
    expression.set_editor_property("const_default_value", default_value)
    expression.set_editor_property("desc", description)
    return expression


def create_material():
    replace_asset(MATERIAL_PATH)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = require(
        asset_tools.create_asset(
            "M_GridRuntimeCell",
            PRESENTATION_PATH,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        ),
        "Could not create the GridWorld runtime material",
    )
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", False)
    material.set_editor_property("used_with_instanced_static_meshes", True)

    red = create_custom_data(material, 4, 0.08, -700, -180, "Resolved color R")
    green = create_custom_data(material, 5, 0.75, -700, -60, "Resolved color G")
    blue = create_custom_data(material, 6, 0.18, -700, 80, "Resolved color B")
    alpha = create_custom_data(material, 7, 0.20, -300, 260, "Resolved color A")

    red_green = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, node_pos_x=-430, node_pos_y=-100
    )
    rgb = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, node_pos_x=-180, node_pos_y=-40
    )
    require(red_green and rgb, "Could not create material vector composition nodes")
    require(
        unreal.MaterialEditingLibrary.connect_material_expressions(red, "", red_green, "A"),
        "Could not connect resolved red",
    )
    require(
        unreal.MaterialEditingLibrary.connect_material_expressions(green, "", red_green, "B"),
        "Could not connect resolved green",
    )
    require(
        unreal.MaterialEditingLibrary.connect_material_expressions(red_green, "", rgb, "A"),
        "Could not connect resolved red/green",
    )
    require(
        unreal.MaterialEditingLibrary.connect_material_expressions(blue, "", rgb, "B"),
        "Could not connect resolved blue",
    )
    require(
        unreal.MaterialEditingLibrary.connect_material_property(
            rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        ),
        "Could not connect runtime cell emissive color",
    )
    require(
        unreal.MaterialEditingLibrary.connect_material_property(
            alpha, "", unreal.MaterialProperty.MP_OPACITY
        ),
        "Could not connect runtime cell opacity",
    )
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def create_style(mesh, material):
    replace_asset(STYLE_PATH)
    style_class = require(
        unreal.load_class(None, "/Script/GridWorld.GridCellVisualStyle"),
        "UGridCellVisualStyle is unavailable; compile GridWorld before running this script",
    )
    style = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_GridRuntimeCellStyle_Default",
            PRESENTATION_PATH,
            style_class,
            unreal.DataAssetFactory(),
        ),
        "Could not create the default GridWorld cell style",
    )
    style.set_editor_property("cell_mesh", mesh)
    style.set_editor_property("cell_material", material)
    style.set_editor_property("cell_inset_fraction", 0.03)
    style.set_editor_property("surface_offset", 2.0)
    style.set_editor_property("start_cull_distance", 5000)
    style.set_editor_property("end_cull_distance", 15000)
    style.set_editor_property("cast_shadow", False)
    unreal.EditorAssetLibrary.save_loaded_asset(style, only_if_is_dirty=False)
    return style


mesh_asset = create_mesh()
material_asset = create_material()
style_asset = create_style(mesh_asset, material_asset)
unreal.EditorAssetLibrary.save_directory(PRESENTATION_PATH, only_if_is_dirty=False, recursive=True)
unreal.log(
    f"GridWorld runtime presentation assets created: {mesh_asset.get_path_name()}, "
    f"{material_asset.get_path_name()}, {style_asset.get_path_name()}"
)
