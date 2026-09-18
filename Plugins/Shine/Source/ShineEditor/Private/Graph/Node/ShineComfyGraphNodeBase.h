#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Graph/ShineComfyGraphTypes.h"
#include "ShineComfyGraphNodeBase.generated.h"

class UEdGraphSchema;
class SGraphNode;

UCLASS(Abstract)
class SHINEEDITOR_API UShineComfyGraphNodeBase : public UEdGraphNode
{
    GENERATED_BODY()

public:
    UShineComfyGraphNodeBase();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget();
    virtual FName GetNodePreset() const;
    virtual FText GetNodeSubtitle() const;
    const TArray<FShineComfyNodeParameter>& GetParameters() const;
    const FShineComfyNodeParameter* FindParameter(FName ParameterName) const;

    bool SetTextParameter(FName ParameterName, const FString& NewValue);
    bool SetFloatParameter(FName ParameterName, double NewValue);
    bool SetIntegerParameter(FName ParameterName, int32 NewValue);
    bool SetBoolParameter(FName ParameterName, bool bNewValue);
    virtual void RefreshNodeAfterParameterChange();

    /**
     * 生成结果图的本地路径（Preview / MultiImageGallery 这类显示节点用它把图直接画在节点里）。
     * 存在资产里，所以关掉编辑器再打开还能看到上一次的结果。
     */
    void SetResultImagePaths(const TArray<FString>& InPaths);
    int32 GetResultImageCount() const { return ResultImagePaths.Num(); }
    FString GetResultImagePath(int32 Index) const
    {
        return ResultImagePaths.IsValidIndex(Index) ? ResultImagePaths[Index] : FString();
    }

    virtual void AllocateDefaultPins() override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual FText GetTooltipText() const override;
    virtual bool CanUserDeleteNode() const override;
    virtual bool CanDuplicateNode() const override;
    virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const override;

protected:
    void SetNodePresentation(const FText& InNodeTitle, const FText& InNodeSubtitle, const FLinearColor& InAccentColor);
    void CreateNamedPin(EEdGraphPinDirection Direction, const FName& PinCategory, const FName& PinName);
    void NotifyNodeStateChanged(bool bRefreshGraph);

    virtual void BuildNodePins();
    virtual bool ShouldRefreshGraphOnParameterChanged(const FShineComfyNodeParameter& Parameter) const;

    UPROPERTY()
    FText NodeTitle;

    UPROPERTY()
    FText NodeSubtitle;

    UPROPERTY()
    FLinearColor AccentColor;

    UPROPERTY()
    TArray<FShineComfyNodeParameter> Parameters;

    /** 显示节点要画的结果图（磁盘绝对路径）。 */
    UPROPERTY()
    TArray<FString> ResultImagePaths;

private:
    FShineComfyNodeParameter* FindParameterMutable(FName ParameterName);
};