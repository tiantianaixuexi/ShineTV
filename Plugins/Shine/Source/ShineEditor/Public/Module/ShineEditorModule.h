#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IAssetTypeActions;
class FSlateStyleSet;
struct FGraphPanelNodeFactory;
struct FGraphPanelPinFactory;

class FShineEditorModule : public IModuleInterface
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

    uint32 RegisteredAssetCategory = 0;
    TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;
    TSharedPtr<FSlateStyleSet> StyleSet;
    TSharedPtr<FGraphPanelNodeFactory> GraphNodeFactory;
    TSharedPtr<FGraphPanelPinFactory> GraphPinFactory;
};