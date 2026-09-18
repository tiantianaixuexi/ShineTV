import unreal

MENU_OWNER = "ShineAI"
MENU_NAME = "LevelEditor.MainMenu"
SUB_MENU_NAME = "Shine AI"
SUB_MENU_LABEL = "Shine AI Tools"


def register_menu():
    menus = unreal.ToolMenus.get()

    main_menu = menus.find_menu(MENU_NAME)
    custom_menu = main_menu.add_sub_menu(MENU_OWNER, "Shine", SUB_MENU_NAME, SUB_MENU_LABEL)

    _add_python_entry(
        custom_menu,
        name="TestEntry",
        label="Hello",
        tool_tip="Write a log message",
        command="import shine_ai.actions as shine_ai_actions; shine_ai_actions.log_hello()",
    )
    _add_python_entry(
        custom_menu,
        name="DialogEntry",
        label="Open Dialog",
        tool_tip="Open a message dialog",
        command="import shine_ai.actions as shine_ai_actions; shine_ai_actions.show_message_window()",
    )
    _add_python_entry(
        custom_menu,
        name="InteractiveWindowEntry",
        label="Open Interactive Window",
        tool_tip="Open an Editor Utility Widget tab",
        command="import shine_ai.actions as shine_ai_actions; shine_ai_actions.open_interactive_window()",
    )

    menus.refresh_all_widgets()


def _add_python_entry(menu, name, label, tool_tip, command):
    entry = unreal.ToolMenuEntry(
        name=name,
        owner=unreal.ToolMenuOwner(MENU_OWNER),
        type=unreal.MultiBlockType.MENU_ENTRY,
        user_interface_action_type=unreal.UserInterfaceActionType.BUTTON,
    )
    entry.set_label(label)
    entry.set_tool_tip(tool_tip)
    entry.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", command)
    menu.add_menu_entry("", entry)
