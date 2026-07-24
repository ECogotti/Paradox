"""Create the GridWorld milestone-3 default strict-line presentation assets."""

import unreal


PRESENTATION_PATH = "/GridWorldSystem/Presentation"
SEGMENT_MESH_PATH = f"{PRESENTATION_PATH}/SM_GridRuntimePathLineSegment"
MARKER_MESH_PATH = f"{PRESENTATION_PATH}/SM_GridRuntimePathLineMarker"
MATERIAL_PATH = f"{PRESENTATION_PATH}/M_GridRuntimePathLine"
STYLE_PATH = f"{PRESENTATION_PATH}/DA_GridRuntimePathLineStyle_Default"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def replace_asset(asset_path):
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        require(unreal.EditorAssetLibrary.delete_asset(asset_path), f"Could not replace {asset_path}")


def duplicate_mesh(source_path, target_path):
    replace_asset(target_path)
    mesh = require(
        unreal.EditorAssetLibrary.duplicate_asset(source_path, target_path),
        f"Could not duplicate {source_path}",
    )
    static_mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if static_mesh_editor:
        mesh.set_editor_property("customized_collision", True)
        require(static_mesh_editor.remove_collisions(mesh), f"Could not remove collision from {target_path}")
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
    material = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_GridRuntimePathLine",
            PRESENTATION_PATH,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        ),
        "Could not create the GridWorld path-line material",
    )
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", False)
    material.set_editor_property("used_with_instanced_static_meshes", True)

    red = create_custom_data(material, 2, 0.05, -700, -180, "Resolved line color R")
    green = create_custom_data(material, 3, 0.35, -700, -60, "Resolved line color G")
    blue = create_custom_data(material, 4, 1.0, -700, 80, "Resolved line color B")
    alpha = create_custom_data(material, 5, 0.95, -300, 260, "Resolved line color A")

    red_green = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, node_pos_x=-430, node_pos_y=-100
    )
    rgb = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionAppendVector, node_pos_x=-180, node_pos_y=-40
    )
    require(red_green and rgb, "Could not create path-line material vector nodes")
    require(unreal.MaterialEditingLibrary.connect_material_expressions(red, "", red_green, "A"), "Could not connect red")
    require(unreal.MaterialEditingLibrary.connect_material_expressions(green, "", red_green, "B"), "Could not connect green")
    require(unreal.MaterialEditingLibrary.connect_material_expressions(red_green, "", rgb, "A"), "Could not connect red/green")
    require(unreal.MaterialEditingLibrary.connect_material_expressions(blue, "", rgb, "B"), "Could not connect blue")
    require(
        unreal.MaterialEditingLibrary.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
        "Could not connect path-line emissive color",
    )
    require(
        unreal.MaterialEditingLibrary.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY),
        "Could not connect path-line opacity",
    )
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def create_style(segment_mesh, marker_mesh, material):
    replace_asset(STYLE_PATH)
    style_class = require(
        unreal.load_class(None, "/Script/GridWorld.GridPathLineVisualStyle"),
        "UGridPathLineVisualStyle is unavailable; compile GridWorld before running this script",
    )
    style = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_GridRuntimePathLineStyle_Default",
            PRESENTATION_PATH,
            style_class,
            unreal.DataAssetFactory(),
        ),
        "Could not create the default GridWorld path-line style",
    )
    style.set_editor_property("segment_mesh", segment_mesh)
    style.set_editor_property("marker_mesh", marker_mesh)
    style.set_editor_property("segment_material", material)
    style.set_editor_property("marker_material", material)
    style.set_editor_property("line_width", 8.0)
    style.set_editor_property("line_thickness", 3.0)
    style.set_editor_property("marker_size", 14.0)
    style.set_editor_property("surface_offset", 7.0)
    style.set_editor_property("start_cull_distance", 5000)
    style.set_editor_property("end_cull_distance", 15000)
    style.set_editor_property("cast_shadow", False)
    unreal.EditorAssetLibrary.save_loaded_asset(style, only_if_is_dirty=False)
    return style


segment_asset = duplicate_mesh("/Engine/BasicShapes/Cube", SEGMENT_MESH_PATH)
marker_asset = duplicate_mesh("/Engine/BasicShapes/Sphere", MARKER_MESH_PATH)
material_asset = create_material()
style_asset = create_style(segment_asset, marker_asset, material_asset)
unreal.log(
    f"GridWorld path-line presentation assets created: {segment_asset.get_path_name()}, "
    f"{marker_asset.get_path_name()}, {material_asset.get_path_name()}, {style_asset.get_path_name()}"
)
