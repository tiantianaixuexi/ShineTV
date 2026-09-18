#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IAssetTypeActions;

class FShineTextureEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterEditorFeatures();
    void RegisterAssetTypeActions();
    void UnregisterAssetTypeActions();

    TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;
    TSharedPtr<struct FGraphPanelNodeFactory> GraphNodeFactory;
    FDelegateHandle PostEngineInitHandle;
    uint32 RegisteredAssetCategory = 0;
};