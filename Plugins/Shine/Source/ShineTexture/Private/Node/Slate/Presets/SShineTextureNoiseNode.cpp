#include "Node/Slate/Presets/SShineTextureNoiseNode.h"

#include "Node/Presets/ShineTextureNoiseNode.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureNoiseNode::Construct(const FArguments& InArgs, UShineTextureNoiseNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureNoiseNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureNoiseNode", "ScaleLabel", "缩放 (Scale)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetNoiseNode() ? GetNoiseNode()->GetScale() : 0.0f; },
                    [this](float NewValue) { if (GetNoiseNode()) { GetNoiseNode()->SetScale(NewValue); } }),
                10.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureNoiseNode", "OctavesLabel", "八度 (Octaves)"),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetNoiseNode() ? GetNoiseNode()->GetOctaves() : 0; },
                    [this](int32 NewValue) { if (GetNoiseNode()) { GetNoiseNode()->SetOctaves(NewValue); } }),
                6.0f)
        ]);
}

UShineTextureNoiseNode* SShineTextureNoiseNode::GetNoiseNode() const
{
    return GraphNode ? CastChecked<UShineTextureNoiseNode>(GraphNode) : nullptr;
}

