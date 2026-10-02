"""Create missing native hacking assets; preserve existing designer-authored assets."""
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
action_path = "/Game/Data/GameplayActions/DA_ParadoxHackTerminal"
smart_path = "/Game/Data/Puzzles/SOD_ParadoxHackingTerminalSmartObject"
action = unreal.load_asset(action_path) if unreal.EditorAssetLibrary.does_asset_exist(action_path) else None
if action is None:
    cls = unreal.load_class(None, "/Script/Paradox.ParadoxHackTerminalActionDefinition")
    if cls is None:
        raise RuntimeError("Build ParadoxEditor before creating hacking assets")
    action = tools.create_asset("DA_ParadoxHackTerminal", "/Game/Data/GameplayActions", cls, unreal.DataAssetFactory())
smart = unreal.load_asset(smart_path) if unreal.EditorAssetLibrary.does_asset_exist(smart_path) else None
if smart is None:
    source = unreal.load_asset("/Game/Data/Inventory/DA_ParadoxItemSlotSmartObject")
    if source is None:
        raise RuntimeError("Missing standard Item Slot SmartObject")
    smart = tools.duplicate_asset("SOD_ParadoxHackingTerminalSmartObject", "/Game/Data/Puzzles", source)
for asset in (action, smart):
    if asset is None or not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Could not create/save hacking asset")
    unreal.log("HACKING_ASSET_CREATED: " + asset.get_path_name())
