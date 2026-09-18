import unreal

WIDGET_DIRECTORY = "/Game/ShineAI"
WIDGET_ASSET_NAME = "EUW_ShineAI"
WIDGET_ASSET_PATH = f"{WIDGET_DIRECTORY}/{WIDGET_ASSET_NAME}.{WIDGET_ASSET_NAME}"


def log_hello():
    unreal.log("Shine AI Tools Menu Clicked")


def show_message_window():
    unreal.EditorDialog.show_message(
        "Shine AI",
        "Hello from Shine AI window.",
        unreal.AppMsgType.OK,
        unreal.AppReturnType.OK,
    )


def open_interactive_window():
    widget_asset = _ensure_editor_utility_widget()
    if not widget_asset:
        unreal.log_error("Failed to create or load the Editor Utility Widget.")
        return

    subsystem = unreal.get_editor_subsystem(unreal.EditorUtilitySubsystem)
    if not subsystem:
        unreal.log_error("EditorUtilitySubsystem is not available.")
        return

    subsystem.spawn_and_register_tab(widget_asset)


def _ensure_editor_utility_widget():
    if unreal.EditorAssetLibrary.does_asset_exist(WIDGET_ASSET_PATH):
        return unreal.EditorAssetLibrary.load_asset(WIDGET_ASSET_PATH)

    if not unreal.EditorAssetLibrary.does_directory_exist(WIDGET_DIRECTORY):
        unreal.EditorAssetLibrary.make_directory(WIDGET_DIRECTORY)

    factory = unreal.EditorUtilityWidgetBlueprintFactory()
    factory.set_editor_property("parent_class", unreal.EditorUtilityWidget)
    factory.set_editor_property("edit_after_new", False)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    widget_asset = asset_tools.create_asset(
        WIDGET_ASSET_NAME,
        WIDGET_DIRECTORY,
        unreal.EditorUtilityWidgetBlueprint,
        factory,
    )
    if not widget_asset:
        return None

    unreal.EditorAssetLibrary.save_loaded_asset(widget_asset)
    unreal.log_warning(
        f"Created blank Editor Utility Widget at {WIDGET_ASSET_PATH}. Open it in UMG Designer to add controls."
    )
    return widget_asset
