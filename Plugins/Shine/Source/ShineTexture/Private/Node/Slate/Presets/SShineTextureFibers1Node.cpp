#include "Node/Slate/Presets/SShineTextureFibers1Node.h"

#include "Node/Presets/ShineTextureFibers1Node.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SShineTextureFibers1Node::Construct(const FArguments& InArgs, UShineTextureFibers1Node* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureFibers1Node::CreateNodeContent() const
{
    const UShineTextureFibers1Node* Fibers1Node = GetFibers1Node();
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureFibers1Node", "TilingLabel", "平铺 (Tiling)"),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetFibers1Node() ? GetFibers1Node()->GetTiling() : 0; },
                    [this](int32 NewValue) { if (GetFibers1Node()) { GetFibers1Node()->SetTiling(NewValue); } }),
                10.0f)
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
                .IsChecked(this, &SShineTextureFibers1Node::GetNonSquareExpansionState)
                .OnCheckStateChanged(this, &SShineTextureFibers1Node::HandleNonSquareExpansionChanged)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ShineTextureSlateTheme::GetToggleLabelPadding())
            .VAlign(VAlign_Center)
            [
                CreateSectionLabel(NSLOCTEXT("SShineTextureFibers1Node", "NonSquareExpansionLabel", "非方形扩展 (Non Square Expansion)"))
            ]
        ],
        184.0f);
}

void SShineTextureFibers1Node::HandleNonSquareExpansionChanged(ECheckBoxState NewState) const
{
    if (UShineTextureFibers1Node* Fibers1Node = GetFibers1Node())
    {
        Fibers1Node->SetNonSquareExpansion(NewState == ECheckBoxState::Checked);
    }
}

ECheckBoxState SShineTextureFibers1Node::GetNonSquareExpansionState() const
{
    return GetFibers1Node() && GetFibers1Node()->GetNonSquareExpansion()
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

UShineTextureFibers1Node* SShineTextureFibers1Node::GetFibers1Node() const
{
    return GraphNode ? CastChecked<UShineTextureFibers1Node>(GraphNode) : nullptr;
}

