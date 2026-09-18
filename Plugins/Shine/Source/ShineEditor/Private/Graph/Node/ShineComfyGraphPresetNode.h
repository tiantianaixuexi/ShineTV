#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "ShineComfyGraphPresetNode.generated.h"

UCLASS(Abstract)
class SHINEEDITOR_API UShineComfyGraphPresetNode : public UShineComfyGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineComfyGraphPresetNode();

    virtual FName GetNodePreset() const override;

protected:
    void SetPresetName(FName InNodePreset);
    void ResetPresetParameters();
    void AddTextParameter(const FName& Name, const TCHAR* Label, const TCHAR* DefaultValue);
    void AddFloatParameter(const FName& Name, const TCHAR* Label, double DefaultValue);
    void AddIntegerParameter(const FName& Name, const TCHAR* Label, int32 DefaultValue);
    void AddBoolParameter(const FName& Name, const TCHAR* Label, bool bDefaultValue);

private:
    UPROPERTY()
    FName NodePreset;
};