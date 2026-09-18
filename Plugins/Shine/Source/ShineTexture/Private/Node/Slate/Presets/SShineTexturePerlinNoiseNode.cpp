#include "Node/Slate/Presets/SShineTexturePerlinNoiseNode.h"

#include "Node/Presets/ShineTexturePerlinNoiseNode.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"

void SShineTexturePerlinNoiseNode::Construct(const FArguments& InArgs, UShineTexturePerlinNoiseNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTexturePerlinNoiseNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTexturePerlinNoiseNode", "ScaleLabel", "缩放 (Scale)"),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetPerlinNoiseNode() ? GetPerlinNoiseNode()->GetScale() : 0; },
                    [this](int32 NewValue) { if (GetPerlinNoiseNode()) { GetPerlinNoiseNode()->SetScale(NewValue); } },
                    FShineTextureIntegerEntryOptions{ .MinValue = 1, .MaxValue = 256, .MinSliderValue = 1, .MaxSliderValue = 256 }),
                10.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTexturePerlinNoiseNode", "DisorderLabel", "紊乱 (Disorder)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetPerlinNoiseNode() ? GetPerlinNoiseNode()->GetDisorder() : 0.0f; },
                    [this](float NewValue) { if (GetPerlinNoiseNode()) { GetPerlinNoiseNode()->SetDisorder(NewValue); } },
                    FShineTextureFloatEntryOptions{ .MinValue = 0.0f, .MaxValue = 1.0f, .MinSliderValue = 0.0f, .MaxSliderValue = 1.0f, .MinFractionalDigits = 2, .MaxFractionalDigits = 3 }),
                8.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTexturePerlinNoiseNode", "DisorderSpeedLabel", "紊乱速度 (Disorder Speed)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float> { return GetPerlinNoiseNode() ? GetPerlinNoiseNode()->GetDisorderSpeed() : 0.0f; },
                    [this](float NewValue) { if (GetPerlinNoiseNode()) { GetPerlinNoiseNode()->SetDisorderSpeed(NewValue); } },
                    FShineTextureFloatEntryOptions{ .MinValue = 0.0f, .MinSliderValue = 0.0f, .MaxSliderValue = 10.0f, .MinFractionalDigits = 2, .MaxFractionalDigits = 3 }),
                8.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTexturePerlinNoiseNode", "TileOffsetLabel", "平铺偏移 (Tile Offset)"),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float>
                    {
                        return GetPerlinNoiseNode() ? GetPerlinNoiseNode()->GetTileOffset().X : 0.0f;
                    },
                    [this](float NewValue)
                    {
                        if (UShineTexturePerlinNoiseNode* Node = GetPerlinNoiseNode())
                        {
                            FVector2D Offset = Node->GetTileOffset();
                            Offset.X = NewValue;
                            Node->SetTileOffset(Offset);
                        }
                    },
                    FShineTextureFloatEntryOptions{ .MinSliderValue = -4.0f, .MaxSliderValue = 4.0f, .MinFractionalDigits = 2, .MaxFractionalDigits = 3 },
                    CreateSectionLabel(NSLOCTEXT("SShineTexturePerlinNoiseNode", "TileOffsetXLabel", "X"))),
                CreateFloatEntryBox(
                    [this]() -> TOptional<float>
                    {
                        return GetPerlinNoiseNode() ? GetPerlinNoiseNode()->GetTileOffset().Y : 0.0f;
                    },
                    [this](float NewValue)
                    {
                        if (UShineTexturePerlinNoiseNode* Node = GetPerlinNoiseNode())
                        {
                            FVector2D Offset = Node->GetTileOffset();
                            Offset.Y = NewValue;
                            Node->SetTileOffset(Offset);
                        }
                    },
                    FShineTextureFloatEntryOptions{ .MinSliderValue = -4.0f, .MaxSliderValue = 4.0f, .MinFractionalDigits = 2, .MaxFractionalDigits = 3 },
                    CreateSectionLabel(NSLOCTEXT("SShineTexturePerlinNoiseNode", "TileOffsetYLabel", "Y"))),
                8.0f)
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
                .IsChecked(this, &SShineTexturePerlinNoiseNode::GetNonSquareExpansionState)
                .OnCheckStateChanged(this, &SShineTexturePerlinNoiseNode::HandleNonSquareExpansionChanged)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ShineTextureSlateTheme::GetToggleLabelPadding())
            .VAlign(VAlign_Center)
            [
                CreateSectionLabel(NSLOCTEXT("SShineTexturePerlinNoiseNode", "NonSquareExpansionLabel", "非方形扩展 (Non Square Expansion)"))
            ]
        ],
        220.0f);
}

void SShineTexturePerlinNoiseNode::HandleNonSquareExpansionChanged(ECheckBoxState NewState) const
{
    if (UShineTexturePerlinNoiseNode* PerlinNoiseNode = GetPerlinNoiseNode())
    {
        PerlinNoiseNode->SetNonSquareExpansion(NewState == ECheckBoxState::Checked);
    }
}

ECheckBoxState SShineTexturePerlinNoiseNode::GetNonSquareExpansionState() const
{
    return GetPerlinNoiseNode() && GetPerlinNoiseNode()->GetNonSquareExpansion()
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

UShineTexturePerlinNoiseNode* SShineTexturePerlinNoiseNode::GetPerlinNoiseNode() const
{
    return GraphNode ? CastChecked<UShineTexturePerlinNoiseNode>(GraphNode) : nullptr;
}
