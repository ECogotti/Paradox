"""Create or validate the designer-owned Common UI Widget Blueprint shell.

Run through Unreal's Python commandlet after the TacticalPause module has compiled.
The asset intentionally contains no designer tree. Its owner supplies layout and style,
including the required Common UI buttons declared with BindWidget by the native class.
"""

import unreal


ASSET_PATH = "/TacticalPause/UI/WBP_TacticalPauseControls_Default"
PARENT_CLASS_PATH = "/Script/TacticalPause.TacticalPauseControlsWidget"


def create_or_validate_default_widget():
    """Return the valid default widget asset, creating and saving it when absent."""
    parent_class = unreal.load_class(None, PARENT_CLASS_PATH)
    if not parent_class:
        raise RuntimeError(f"Unable to load Tactical Pause widget parent class: {PARENT_CLASS_PATH}")

    asset_library = unreal.EditorAssetLibrary
    if asset_library.does_asset_exist(ASSET_PATH):
        asset = asset_library.load_asset(ASSET_PATH)
        if not asset:
            raise RuntimeError(f"Existing widget asset could not be loaded: {ASSET_PATH}")
        unreal.log(f"Validated existing Tactical Pause widget asset: {ASSET_PATH}")
        return asset

    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = asset_tools.create_asset(
        "WBP_TacticalPauseControls_Default",
        "/TacticalPause/UI",
        unreal.WidgetBlueprint,
        factory,
    )
    if not asset:
        raise RuntimeError(f"Widget Blueprint creation failed: {ASSET_PATH}")
    if not asset_library.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Widget Blueprint save failed: {ASSET_PATH}")
    unreal.log(f"Created Tactical Pause widget asset: {ASSET_PATH}")
    return asset


create_or_validate_default_widget()
