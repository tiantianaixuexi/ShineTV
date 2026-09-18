#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IAssetTypeActions;
class FSlateStyleSet;

/** 「Shine AI 贴图」功能的编辑器入口：样式、资产类型动作、菜单。 */
class FShineAIPaintEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterStyle();
    void UnregisterStyle();
    void RegisterAssetTypeActions();
    void UnregisterAssetTypeActions();
    void RegisterMenus();

    /** 新建并打开一个「Shine AI 贴图」资产。 */
    void CreateAIPaintAsset();

    uint32 RegisteredAssetCategory = 0;
    TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;
    TSharedPtr<FSlateStyleSet> StyleSet;
};
