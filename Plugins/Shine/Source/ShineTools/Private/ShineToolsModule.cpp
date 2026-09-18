#include "ShineToolsModule.h"

#include "EditMode/ShineToolsEdMode.h"
#include "Tools/SixFaceCamera/ShineTools_SixFaceTool.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "LevelEditor.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "ShineToolsModule"

DEFINE_LOG_CATEGORY(LogShineTools);

void FShineToolsModule::StartupModule()
{
	RegisterEditorMode();

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FShineToolsModule::RegisterMenus));

	RegisterToolbar();
	
	static const FName PropertyEditor("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
	TSharedRef<FPropertySection> Section = PropertyModule.FindOrCreateSection("Object", "Shine", LOCTEXT("Shine", "Shine"));
	Section->AddCategory("Shine");
}

void FShineToolsModule::ShutdownModule()
{
	UnregisterMenusAndToolbar();
	UnregisterEditorMode();
}

void FShineToolsModule::RegisterEditorMode()
{
	FEditorModeRegistry::Get().RegisterMode<FShineToolsEdMode>(
		FShineToolsEdMode::EM_ShineTools,
		LOCTEXT("ShineToolsEd", "ShineTools"),
		FSlateIcon(),
		true,
		550);
}

void FShineToolsModule::UnregisterEditorMode()
{
	FEditorModeRegistry::Get().UnregisterMode(FShineToolsEdMode::EM_ShineTools);
}

void FShineToolsModule::ActivateBoundsFacesModeAndGenerate()
{
	if (!GLevelEditorModeTools().IsModeActive(FShineToolsEdMode::EM_ShineTools))
	{
		GLevelEditorModeTools().ActivateMode(FShineToolsEdMode::EM_ShineTools);
	}

	if (FShineToolsEdMode* EdMode = FShineToolsEdMode::Get())
	{
		if (FShineTools_SixFaceTool* SixFaceTool = EdMode->GetTool<FShineTools_SixFaceTool>())
		{
			SixFaceTool->GenerateForSelection();
		}
	}
}

void FShineToolsModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection("ShineTools");
	Section.AddMenuEntry(
		"ShineTools",
		LOCTEXT("ShineToolsMenuLabel", "ShineTools"),
		LOCTEXT(
			"ShineToolsMenuTooltip",
			"Open ShineTools"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FShineToolsModule::ActivateBoundsFacesModeAndGenerate)));
}

void FShineToolsModule::RegisterToolbar()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		"Settings",
		EExtensionHook::After,
		nullptr,
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateRaw(this, &FShineToolsModule::ActivateBoundsFacesModeAndGenerate)),
				NAME_None,
				LOCTEXT("ShineToolsToolBarLavel", "ShineTools"),
				LOCTEXT(
					"ShineToolbarTooltip",
					"Open ShineTools "),
				FSlateIcon());
		}));

	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	LevelEditorToolbarExtender = ToolbarExtender;
}

void FShineToolsModule::UnregisterMenusAndToolbar()
{
	if (LevelEditorToolbarExtender.IsValid())
	{
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
		{
			LevelEditorModule->GetToolBarExtensibilityManager()->RemoveExtender(LevelEditorToolbarExtender);
		}
		LevelEditorToolbarExtender.Reset();
	}

	if (UToolMenus::TryGet())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShineToolsModule, ShineTools)
