#include "Node/Slate/Presets/SShineTextureClampNode.h"

#include "Node/Presets/Math/ShineTextureClampNode.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureClampNode::Construct(const FArguments& InArgs, UShineTextureClampNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureClampNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
            NSLOCTEXT("SShineTextureClampNode", "MinLabel", "Min"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetClampNode() ? GetClampNode()->GetMinValue() : 0.0f; },
                    [this](float NewValue) { if (GetClampNode()) { GetClampNode()->SetMinValue(NewValue); } }),
                8.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureClampNode", "MaxLabel", "Max"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetClampNode() ? GetClampNode()->GetMaxValue() : 1.0f; },
                    [this](float NewValue) { if (GetClampNode()) { GetClampNode()->SetMaxValue(NewValue); } }),
                6.0f)
        ],
        190.0f);
}

UShineTextureClampNode* SShineTextureClampNode::GetClampNode() const
{
    return GraphNode ? CastChecked<UShineTextureClampNode>(GraphNode) : nullptr;
}
