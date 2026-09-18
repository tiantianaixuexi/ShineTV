#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogShineTools, Log, All);

class FShineToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void ActivateBoundsFacesModeAndGenerate();

private:
	void RegisterEditorMode();
	void UnregisterEditorMode();
	void RegisterMenus();
	void RegisterToolbar();
	void UnregisterMenusAndToolbar();

	TSharedPtr<class FExtender> LevelEditorToolbarExtender;
};
