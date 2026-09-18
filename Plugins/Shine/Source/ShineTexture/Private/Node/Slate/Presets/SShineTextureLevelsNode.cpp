#include "Node/Slate/Presets/SShineTextureLevelsNode.h"

#include "Node/Presets/Math/ShineTextureLevelsNode.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureLevelsNode::Construct(const FArguments& InArgs, UShineTextureLevelsNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureLevelsNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureLevelsNode", "InputRangeLabel", "输入上下"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetLevelsNode() ? GetLevelsNode()->GetInputLow() : 0.0f; },
                    [this](float NewValue) { if (GetLevelsNode()) { GetLevelsNode()->SetInputLow(NewValue); } }),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetLevelsNode() ? GetLevelsNode()->GetInputHigh() : 1.0f; },
                    [this](float NewValue) { if (GetLevelsNode()) { GetLevelsNode()->SetInputHigh(NewValue); } }),
                8.0f)
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureLevelsNode", "GammaLabel", "伽马(Gamma)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetLevelsNode() ? GetLevelsNode()->GetGamma() : 1.0f; },
                    [this](float NewValue) { if (GetLevelsNode()) { GetLevelsNode()->SetGamma(NewValue); } }),
                6.0f)
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureLevelsNode", "OutputRangeLabel", "输出上下"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetLevelsNode() ? GetLevelsNode()->GetOutputLow() : 0.0f; },
                    [this](float NewValue) { if (GetLevelsNode()) { GetLevelsNode()->SetOutputLow(NewValue); } }),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetLevelsNode() ? GetLevelsNode()->GetOutputHigh() : 1.0f; },
                    [this](float NewValue) { if (GetLevelsNode()) { GetLevelsNode()->SetOutputHigh(NewValue); } }),
                6.0f)
        ],
        210.0f);
}

UShineTextureLevelsNode* SShineTextureLevelsNode::GetLevelsNode() const
{
    return GraphNode ? CastChecked<UShineTextureLevelsNode>(GraphNode) : nullptr;
}
