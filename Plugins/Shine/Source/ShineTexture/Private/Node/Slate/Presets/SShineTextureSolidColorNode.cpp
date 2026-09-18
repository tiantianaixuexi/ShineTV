#include "Node/Slate/Presets/SShineTextureSolidColorNode.h"

#include "Node/Presets/ShineTextureSolidColorNode.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureSolidColorNode::Construct(const FArguments& InArgs, UShineTextureSolidColorNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureSolidColorNode::CreateNodeContent() const
{
    FShineTextureIntegerEntryOptions ColorChannelOptions;
    ColorChannelOptions.MinValue = 0;
    ColorChannelOptions.MaxValue = 255;
    ColorChannelOptions.MinSliderValue = 0;
    ColorChannelOptions.MaxSliderValue = 255;
    ColorChannelOptions.MinDesiredValueWidth = 56.0f;

    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(FMargin(0.0f))
        [
            CreatePreviewFrame(
                SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ContentPadding(FMargin(0.0f))
                .OnClicked(this, &SShineTextureSolidColorNode::HandleColorBlockClicked)
                [
                    SNew(SColorBlock)
                    .Color_Lambda([this]() { return GetSolidColorNode() ? GetSolidColorNode()->GetColorValue() : FLinearColor::White; })
                    .Size(FVector2D(96.0f, 22.0f))
                    .CornerRadius(FVector4(5.0f, 5.0f, 5.0f, 5.0f))
                ],
                1.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(0.0f, 0.0f, ShineTextureSlateTheme::FormDualValueSpacing(), 0.0f)
            [
                CreateIntegerEntryBox(
                    [this]() { return GetColorChannelValue(0); },
                    [this](int32 NewValue) { SetColorChannelValue(0, NewValue); },
                    ColorChannelOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSolidColorNode", "RedChannelLabel", "R")))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ShineTextureSlateTheme::FormDualValueSpacing(), 0.0f, 0.0f, 0.0f)
            [
                CreateIntegerEntryBox(
                    [this]() { return GetColorChannelValue(1); },
                    [this](int32 NewValue) { SetColorChannelValue(1, NewValue); },
                    ColorChannelOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSolidColorNode", "GreenChannelLabel", "G")))
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(0.0f, 0.0f, ShineTextureSlateTheme::FormDualValueSpacing(), 0.0f)
            [
                CreateIntegerEntryBox(
                    [this]() { return GetColorChannelValue(2); },
                    [this](int32 NewValue) { SetColorChannelValue(2, NewValue); },
                    ColorChannelOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSolidColorNode", "BlueChannelLabel", "B")))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(ShineTextureSlateTheme::FormDualValueSpacing(), 0.0f, 0.0f, 0.0f)
            [
                CreateIntegerEntryBox(
                    [this]() { return GetColorChannelValue(3); },
                    [this](int32 NewValue) { SetColorChannelValue(3, NewValue); },
                    ColorChannelOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSolidColorNode", "AlphaChannelLabel", "A")))
            ]
        ],
        132.0f);
}

TOptional<int32> SShineTextureSolidColorNode::GetColorChannelValue(int32 ChannelIndex) const
{
    const UShineTextureSolidColorNode* Node = GetSolidColorNode();
    if (!Node)
    {
        return TOptional<int32>();
    }

    const FLinearColor Color = Node->GetColorValue().GetClamped(0.0f, 1.0f);

    switch (ChannelIndex)
    {
    case 0:
        return FMath::Clamp(FMath::RoundToInt(Color.R * 255.0f), 0, 255);
    case 1:
        return FMath::Clamp(FMath::RoundToInt(Color.G * 255.0f), 0, 255);
    case 2:
        return FMath::Clamp(FMath::RoundToInt(Color.B * 255.0f), 0, 255);
    case 3:
        return FMath::Clamp(FMath::RoundToInt(Color.A * 255.0f), 0, 255);
    default:
        return TOptional<int32>();
    }
}

void SShineTextureSolidColorNode::SetColorChannelValue(int32 ChannelIndex, int32 ChannelValue) const
{
    UShineTextureSolidColorNode* Node = GetSolidColorNode();
    if (!Node)
    {
        return;
    }

    const float NormalizedValue = static_cast<float>(FMath::Clamp(ChannelValue, 0, 255)) / 255.0f;
    FLinearColor Color = Node->GetColorValue();

    switch (ChannelIndex)
    {
    case 0:
        Color.R = NormalizedValue;
        break;
    case 1:
        Color.G = NormalizedValue;
        break;
    case 2:
        Color.B = NormalizedValue;
        break;
    case 3:
        Color.A = NormalizedValue;
        break;
    default:
        return;
    }

    Node->SetColorValue(Color);
}

FReply SShineTextureSolidColorNode::HandleColorBlockClicked() const
{
    UShineTextureSolidColorNode* Node = GetSolidColorNode();
    if (!Node)
    {
        return FReply::Unhandled();
    }

    DestroyColorPicker();
    PendingColorPickerOriginalColor = Node->GetColorValue();

    Node->BeginInteractivePreviewChange();

    FColorPickerArgs PickerArgs;
    PickerArgs.bIsModal = false;
    PickerArgs.InitialColor = Node->GetColorValue();
    PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SShineTextureSolidColorNode::HandleColorPicked);
    PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(this, &SShineTextureSolidColorNode::HandleColorPickerCancelled);
    PickerArgs.OnColorPickerWindowClosed = FOnWindowClosed::CreateSP(this, &SShineTextureSolidColorNode::HandleColorPickerClosed);
    OpenColorPicker(PickerArgs);

    return FReply::Handled();
}

void SShineTextureSolidColorNode::HandleColorPicked(FLinearColor NewColor) const
{
    if (UShineTextureSolidColorNode* Node = GetSolidColorNode())
    {
        Node->SetColorValue(NewColor);
    }
}

void SShineTextureSolidColorNode::HandleColorPickerCancelled(FLinearColor OriginalColor) const
{
    if (PendingColorPickerOriginalColor.IsSet())
    {
        OriginalColor = PendingColorPickerOriginalColor.GetValue();
        PendingColorPickerOriginalColor.Reset();
    }

    if (UShineTextureSolidColorNode* Node = GetSolidColorNode())
    {
        Node->SetColorValue(OriginalColor);
    }
}

void SShineTextureSolidColorNode::HandleColorPickerClosed(const TSharedRef<SWindow>& Window) const
{
    PendingColorPickerOriginalColor.Reset();

    if (UShineTextureSolidColorNode* Node = GetSolidColorNode())
    {
        Node->EndInteractivePreviewChange();
    }
}

UShineTextureSolidColorNode* SShineTextureSolidColorNode::GetSolidColorNode() const
{
    return GraphNode ? CastChecked<UShineTextureSolidColorNode>(GraphNode) : nullptr;
}
