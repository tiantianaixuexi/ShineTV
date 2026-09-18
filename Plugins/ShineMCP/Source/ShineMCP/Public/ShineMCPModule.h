#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FShineMCPModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void RegisterBuiltinTools();

    bool bToolsRegistered = false;
};
