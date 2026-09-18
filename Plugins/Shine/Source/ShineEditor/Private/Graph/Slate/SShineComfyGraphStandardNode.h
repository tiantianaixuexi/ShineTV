#pragma once

#include "CoreMinimal.h"
#include "Graph/ShineComfyGraphTypes.h"
#include "Graph/Slate/SShineComfyGraphNodeBase.h"

class UShineComfyGraphNodeBase;
enum class ECheckBoxState : uint8;
template <typename OptionType> class SComboBox;

class SShineComfyGraphStandardNode : public SShineComfyGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineComfyGraphStandardNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode);

    virtual void UpdateGraphNode() override;
    virtual void CreatePinWidgets() override;
    virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

protected:
    void PopulateParameterWidgets();
    TSharedRef<SWidget> CreateParameterWidget(const FShineComfyNodeParameter& Parameter);

    /**
     * 子类可以往"标题栏"和"参数区"之间塞一块自己的内容。
     *
     * 默认返回空控件。视频工作台的节点用它放缩略图、逐段播放器和状态条；
     * 放在标题下面、参数上面，是因为这些内容才是"这个节点是什么"的主要信息，
     * 而参数是细节。
     */
    virtual TSharedRef<SWidget> BuildExtraBodyWidget();
    TArray<TSharedPtr<FString>>& GetOrBuildTextOptions(FName ParameterName);
    TSharedRef<SWidget> GenerateTextOptionWidget(TSharedPtr<FString> Option) const;
    FText GetTextParameterValue(FName ParameterName) const;
    TSharedPtr<FString> GetSelectedTextOption(FName ParameterName) const;
    float GetFloatParameterValue(FName ParameterName) const;
    int32 GetIntegerParameterValue(FName ParameterName) const;
    ECheckBoxState GetBoolParameterValue(FName ParameterName) const;
    TOptional<float> GetFloatMinValue(FName ParameterName) const;
    TOptional<float> GetFloatMaxValue(FName ParameterName) const;
    TOptional<float> GetFloatSliderMinValue(FName ParameterName) const;
    TOptional<float> GetFloatSliderMaxValue(FName ParameterName) const;
    TOptional<int32> GetIntegerMinValue(FName ParameterName) const;
    TOptional<int32> GetIntegerMaxValue(FName ParameterName) const;
    TOptional<int32> GetIntegerSliderMinValue(FName ParameterName) const;
    TOptional<int32> GetIntegerSliderMaxValue(FName ParameterName) const;
    float GetFloatDelta(FName ParameterName) const;
    int32 GetIntegerDelta(FName ParameterName) const;
    void HandleTextParameterCommitted(const FText& NewText, ETextCommit::Type CommitType, FName ParameterName);
    void HandleTextParameterSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, FName ParameterName);
    void HandleFloatParameterChanged(float NewValue, FName ParameterName);
    void HandleFloatParameterCommitted(float NewValue, ETextCommit::Type CommitType, FName ParameterName);
    void HandleIntegerParameterChanged(int32 NewValue, FName ParameterName);
    void HandleIntegerParameterCommitted(int32 NewValue, ETextCommit::Type CommitType, FName ParameterName);
    void HandleBoolParameterChanged(ECheckBoxState NewState, FName ParameterName);

private:
    TMap<FName, TArray<TSharedPtr<FString>>> ParameterOptionItems;
};