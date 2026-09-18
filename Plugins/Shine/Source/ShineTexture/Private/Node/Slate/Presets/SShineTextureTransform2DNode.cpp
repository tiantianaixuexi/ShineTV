#include "Node/Slate/Presets/SShineTextureTransform2DNode.h"

#include "Node/Presets/ShineTextureTransform2DNode.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureTransform2DNode::Construct(const FArguments& InArgs, UShineTextureTransform2DNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureTransform2DNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureTransform2DNode", "OffsetLabel", "偏移 (Offset)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float>
                    {
                        return GetTransform2DNode() ? GetTransform2DNode()->GetOffset().X : 0.0f;
                    },
                    [this](float NewValue)
                    {
                        if (UShineTextureTransform2DNode* Node = GetTransform2DNode())
                        {
                            FVector2D Offset = Node->GetOffset();
                            Offset.X = NewValue;
                            Node->SetOffset(Offset);
                        }
                    },
                    FShineTextureFloatEntryOptions{ .MinSliderValue = -1.0f, .MaxSliderValue = 1.0f, .MinFractionalDigits = 2, .MaxFractionalDigits = 3 },
                    CreateSectionLabel(NSLOCTEXT("SShineTextureTransform2DNode", "OffsetXLabel", "X"))),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float>
                    {
                        return GetTransform2DNode() ? GetTransform2DNode()->GetOffset().Y : 0.0f;
                    },
                    [this](float NewValue)
                    {
                        if (UShineTextureTransform2DNode* Node = GetTransform2DNode())
                        {
                            FVector2D Offset = Node->GetOffset();
                            Offset.Y = NewValue;
                            Node->SetOffset(Offset);
                        }
                    },
                    FShineTextureFloatEntryOptions{ .MinSliderValue = -1.0f, .MaxSliderValue = 1.0f, .MinFractionalDigits = 2, .MaxFractionalDigits = 3 },
                    CreateSectionLabel(NSLOCTEXT("SShineTextureTransform2DNode", "OffsetYLabel", "Y"))))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureTransform2DNode", "ScaleLabel", "缩放 (Scale)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float>
                    {
                        return GetTransform2DNode() ? GetTransform2DNode()->GetScale().X : 1.0f;
                    },
                    [this](float NewValue)
                    {
                        if (UShineTextureTransform2DNode* Node = GetTransform2DNode())
                        {
                            FVector2D Scale = Node->GetScale();
                            Scale.X = NewValue;
                            Node->SetScale(Scale);
                        }
                    },
                    FShineTextureFloatEntryOptions(),
                    CreateSectionLabel(NSLOCTEXT("SShineTextureTransform2DNode", "ScaleXLabel", "X"))),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float>
                    {
                        return GetTransform2DNode() ? GetTransform2DNode()->GetScale().Y : 1.0f;
                    },
                    [this](float NewValue)
                    {
                        if (UShineTextureTransform2DNode* Node = GetTransform2DNode())
                        {
                            FVector2D Scale = Node->GetScale();
                            Scale.Y = NewValue;
                            Node->SetScale(Scale);
                        }
                    },
                    FShineTextureFloatEntryOptions{ .MinValue = 0.01f, .MinSliderValue = 0.01f, .MaxSliderValue = 8.0f, .MinFractionalDigits = 2, .MaxFractionalDigits = 3 },
                    CreateSectionLabel(NSLOCTEXT("SShineTextureTransform2DNode", "ScaleYLabel", "Y"))))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureTransform2DNode", "RotationLabel", "旋转 (Rotation)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float>
                    {
                        return GetTransform2DNode() ? GetTransform2DNode()->GetRotationDegrees() : 0.0f;
                    },
                    [this](float NewValue)
                    {
                        if (UShineTextureTransform2DNode* Node = GetTransform2DNode())
                        {
                            Node->SetRotationDegrees(NewValue);
                        }
                    },
                    FShineTextureFloatEntryOptions{ .MinValue = -180.0f, .MaxValue = 180.0f, .MinSliderValue = -180.0f, .MaxSliderValue = 180.0f, .MinFractionalDigits = 1, .MaxFractionalDigits = 2 },
                    CreateSectionLabel(NSLOCTEXT("SShineTextureTransform2DNode", "RotationUnitLabel", "deg"))))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .IsChecked(this, &SShineTextureTransform2DNode::GetTilingCheckState)
                .OnCheckStateChanged(this, &SShineTextureTransform2DNode::HandleTilingChanged)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ShineTextureSlateTheme::ToggleLabelSpacing(), 0.0f, 0.0f, 0.0f)
            .VAlign(VAlign_Center)
            [
                CreateSectionLabel(NSLOCTEXT("SShineTextureTransform2DNode", "TilingEnabledLabel", "平铺 (Tiling)"))
            ]
        ],
        196.0f);
}

void SShineTextureTransform2DNode::HandleTilingChanged(ECheckBoxState NewState) const
{
    if (UShineTextureTransform2DNode* Node = GetTransform2DNode())
    {
        Node->SetTilingEnabled(NewState == ECheckBoxState::Checked);
    }
}

ECheckBoxState SShineTextureTransform2DNode::GetTilingCheckState() const
{
    return GetTransform2DNode() && GetTransform2DNode()->GetTilingEnabled()
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

UShineTextureTransform2DNode* SShineTextureTransform2DNode::GetTransform2DNode() const
{
    return GraphNode ? CastChecked<UShineTextureTransform2DNode>(GraphNode) : nullptr;
}

