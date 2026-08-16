"""Create the native Item Slot interaction Definitions and standard Smart Object asset."""

import unreal


GAMEPLAY_ACTION_FOLDER = "/Game/Data/GameplayActions"
INVENTORY_FOLDER = "/Game/Data/Inventory"
SOURCE_SMART_OBJECT = f"{INVENTORY_FOLDER}/DA_ParadoxPickupableSmartObject"
TARGET_SMART_OBJECT = f"{INVENTORY_FOLDER}/DA_ParadoxItemSlotSmartObject"


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def replace_data_asset(name, class_path):
    asset_path = f"{GAMEPLAY_ACTION_FOLDER}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        require(
            unreal.EditorAssetLibrary.delete_asset(asset_path),
            f"Could not replace {asset_path}",
        )
    asset_class = require(
        unreal.load_class(None, class_path),
        f"Compiled class is unavailable: {class_path}",
    )
    asset = require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name,
            GAMEPLAY_ACTION_FOLDER,
            asset_class,
            unreal.DataAssetFactory(),
        ),
        f"Could not create {asset_path}",
    )
    require(
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
        f"Could not save {asset_path}",
    )
    return asset


source_smart_object = require(
    unreal.load_asset(SOURCE_SMART_OBJECT),
    f"Missing standard four-direction Smart Object {SOURCE_SMART_OBJECT}",
)
if unreal.EditorAssetLibrary.does_asset_exist(TARGET_SMART_OBJECT):
    require(
        unreal.EditorAssetLibrary.delete_asset(TARGET_SMART_OBJECT),
        f"Could not replace {TARGET_SMART_OBJECT}",
    )
slot_smart_object = require(
    unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(
        "DA_ParadoxItemSlotSmartObject",
        INVENTORY_FOLDER,
        source_smart_object,
    ),
    f"Could not create {TARGET_SMART_OBJECT}",
)
require(
    unreal.EditorAssetLibrary.save_loaded_asset(
        slot_smart_object, only_if_is_dirty=False
    ),
    f"Could not save {TARGET_SMART_OBJECT}",
)

insert_definition = replace_data_asset(
    "DA_ParadoxInsertItem",
    "/Script/Paradox.ParadoxInsertItemInteractionActionDefinition",
)
pickup_definition = replace_data_asset(
    "DA_ParadoxPickupFromItemSlot",
    "/Script/Paradox.ParadoxPickupFromItemSlotInteractionActionDefinition",
)

unreal.log(
    "Created Item Slot assets: "
    f"{slot_smart_object.get_path_name()}, "
    f"{insert_definition.get_path_name()}, "
    f"{pickup_definition.get_path_name()}"
)
