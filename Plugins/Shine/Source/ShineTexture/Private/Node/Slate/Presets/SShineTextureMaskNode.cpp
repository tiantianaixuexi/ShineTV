#include "Node/Slate/Presets/SShineTextureMaskNode.h"

#include "Node/Presets/Math/ShineTextureMaskNode.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

void SShineTextureMaskNode::Construct(const FArguments& InArgs, UShineTextureMaskNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureMaskNode::CreateNodeContent() const
{
    auto CreateMaskToggle = [this](const FText& Label, TFunction<bool()> Getter, TFunction<void(bool)> Setter) -> TSharedRef<SWidget>
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([Getter]() { return Getter() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([Setter](ECheckBoxState NewState) { Setter(NewState == ECheckBoxState::Checked); })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(ShineTextureSlateTheme::GetToggleLabelPadding())
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(Label)
            ];
    };

    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureMaskNode", "ChannelsLabel", "Channels"),
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(ShineTextureSlateTheme::GetMaskTopRowLeftPadding())
                    [
                        CreateMaskToggle(
                            NSLOCTEXT("SShineTextureMaskNode", "ChannelXLabel", "X"),
                            [this]() { return GetMaskNode() ? GetMaskNode()->GetMaskX() : false; },
                            [this](bool bValue) { if (GetMaskNode()) { GetMaskNode()->SetMaskX(bValue); } })
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(ShineTextureSlateTheme::GetMaskTopRowRightPadding())
                    [
                        CreateMaskToggle(
                            NSLOCTEXT("SShineTextureMaskNode", "ChannelYLabel", "Y"),
                            [this]() { return GetMaskNode() ? GetMaskNode()->GetMaskY() : false; }, 
                            [this](bool bValue) { if (GetMaskNode()) { GetMaskNode()->SetMaskY(bValue); } })
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(ShineTextureSlateTheme::GetMaskBottomRowLeftPadding())
                    [
                        CreateMaskToggle(
                            NSLOCTEXT("SShineTextureMaskNode", "ChannelZLabel", "Z"),
                            [this]() { return GetMaskNode() ? GetMaskNode()->GetMaskZ() : false; },
                            [this](bool bValue) { if (GetMaskNode()) { GetMaskNode()->SetMaskZ(bValue); } })
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(ShineTextureSlateTheme::GetMaskBottomRowRightPadding())
                    [
                        CreateMaskToggle(
                            NSLOCTEXT("SShineTextureMaskNode", "ChannelWLabel", "W"),
                            [this]() { return GetMaskNode() ? GetMaskNode()->GetMaskW() : false; },
                            [this](bool bValue) { if (GetMaskNode()) { GetMaskNode()->SetMaskW(bValue); } })
                    ]
                ],
                8.0f)
        ],
        190.0f);
}

UShineTextureMaskNode* SShineTextureMaskNode::GetMaskNode() const
{
    return GraphNode ? CastChecked<UShineTextureMaskNode>(GraphNode) : nullptr;
}
