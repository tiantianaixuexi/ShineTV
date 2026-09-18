#include "Node/Slate/ShineTextureSlateTheme.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "EdGraph/EdGraphPin.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

namespace ShineTextureSlateTheme
{
    namespace
    {
        const FLinearColor GraphBackgroundColor(0.12f, 0.12f, 0.12f, 1.0f);
        const FLinearColor GridSmallColor(0.14f, 0.14f, 0.14f, 1.0f);
        const FLinearColor GridLargeColor(0.10f, 0.10f, 0.10f, 1.0f);

        const FLinearColor NodeBackgroundColor(0.13f, 0.13f, 0.13f, 0.95f);
        const FLinearColor NodeInnerBackgroundColor(0.16f, 0.16f, 0.16f, 1.0f);
        const FLinearColor NodeBorderColor(0.04f, 0.04f, 0.04f, 1.0f);

        const FLinearColor TextMainColor(0.85f, 0.85f, 0.85f, 1.0f);
        const FLinearColor TextSecondaryColor(0.65f, 0.65f, 0.65f, 1.0f);
        const FLinearColor TextDimColor(0.45f, 0.45f, 0.45f, 1.0f);

        const FLinearColor InputBackgroundColor(0.08f, 0.08f, 0.08f, 1.0f);
        const FLinearColor InputBorderColor(0.05f, 0.05f, 0.05f, 1.0f);
        const FLinearColor InputFocusColor(0.85f, 0.50f, 0.10f, 1.0f);

        const FLinearColor WireColorValue(0.85f, 0.85f, 0.85f, 0.95f);
        const FLinearColor SelectionOutlineColor(0.85f, 0.50f, 0.10f, 0.95f);
        constexpr float NumericEntryMinWidthValue = 72.0f;
        constexpr float FormSectionTopPaddingValue = 8.0f;
        constexpr float FormLabelSpacingValue = 3.0f;
        constexpr float FormDualValueSpacingValue = 3.0f;
        constexpr float FormSectionMinHeightValue = 24.0f;
        constexpr float ToggleLabelSpacingValue = 4.0f;
        constexpr float NodeContentMinWidthValue = 152.0f;
        constexpr float PreviewNodeContentMinWidthValue = 164.0f;
        constexpr float NodeSubtitleBottomSpacingValue = 8.0f;
        constexpr float NodeContentPinSpacingValue = 8.0f;
        constexpr float InlinePreviewPanelTopPaddingValue = 8.0f;
        constexpr float InlinePreviewPanelPaddingValue = 8.0f;
    }

    FLinearColor GraphBackground() noexcept { return GraphBackgroundColor; }
    FLinearColor GridSmall() noexcept { return GridSmallColor; }
    FLinearColor GridLarge() noexcept { return GridLargeColor; }

    FLinearColor NodeBackground() noexcept { return NodeBackgroundColor; }
    FLinearColor NodeInnerBackground() noexcept{ return NodeInnerBackgroundColor; }
    FLinearColor NodeBorder() noexcept { return NodeBorderColor; }

    FLinearColor TextMain() noexcept{ return TextMainColor; }
    FLinearColor TextSecondary() noexcept{ return TextSecondaryColor; }
    FLinearColor TextDim()noexcept { return TextDimColor; }

    FLinearColor InputBackground() noexcept { return InputBackgroundColor; }
    FLinearColor InputBorder() noexcept{ return InputBorderColor; }
    FLinearColor InputFocus()  noexcept{ return InputFocusColor; }

    FLinearColor WireColor()  noexcept { return WireColorValue; }
    FLinearColor SelectionOutline()  noexcept{ return SelectionOutlineColor; }

    float NodeRadius()  noexcept { return 4.0f; }
    float HeaderHeight() noexcept { return 22.0f; }
    float BodyPadding() noexcept { return 6.0f; }
    FSlateFontInfo BodyFont() noexcept { return FCoreStyle::GetDefaultFontStyle("Regular", 9); }
    float NodeMinWidth() noexcept { return 120.0f; }
    float NumericEntryMinWidth() noexcept { return NumericEntryMinWidthValue; }
    float FormSectionTopPadding() noexcept { return FormSectionTopPaddingValue; }
    float FormLabelSpacing() noexcept { return FormLabelSpacingValue; }
    float FormDualValueSpacing() noexcept{ return FormDualValueSpacingValue; }
    float FormSectionMinHeight() noexcept { return FormSectionMinHeightValue; }
    float ToggleLabelSpacing() noexcept { return ToggleLabelSpacingValue; }
    float NodeContentMinWidth() noexcept { return NodeContentMinWidthValue; }
    float PreviewNodeContentMinWidth() noexcept { return PreviewNodeContentMinWidthValue; }
    float NodeSubtitleBottomSpacing() noexcept { return NodeSubtitleBottomSpacingValue; }
    float NodeContentPinSpacing() noexcept { return NodeContentPinSpacingValue; }
    float InlinePreviewPanelTopPadding() noexcept { return InlinePreviewPanelTopPaddingValue; }
    float InlinePreviewPanelPadding() noexcept { return InlinePreviewPanelPaddingValue; }
    const FMargin& GetTopSectionPadding()
    {
        static const FMargin Padding(0.0f, FormSectionTopPaddingValue, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetBodySpacingPadding()
    {
        static const FMargin Padding(0.0f, BodyPadding(), 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetTightRowPadding()
    {
        static const FMargin Padding(0.0f, FormLabelSpacingValue, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetFormLabelSlotPadding()
    {
        static const FMargin Padding(0.0f, FormLabelSpacingValue, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetFormDualValueSlotPadding()
    {
        static const FMargin Padding(FormDualValueSpacingValue, 0.0f, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetToggleLabelPadding()
    {
        static const FMargin Padding(ToggleLabelSpacingValue, 0.0f, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetMaskTopRowLeftPadding()
    {
        static const FMargin Padding(0.0f, 0.0f, FormDualValueSpacingValue, ToggleLabelSpacingValue);
        return Padding;
    }
    const FMargin& GetMaskTopRowRightPadding()
    {
        static const FMargin Padding(FormDualValueSpacingValue, 0.0f, 0.0f, ToggleLabelSpacingValue);
        return Padding;
    }
    const FMargin& GetMaskBottomRowLeftPadding()
    {
        static const FMargin Padding(0.0f, 0.0f, FormDualValueSpacingValue, 0.0f);
        return Padding;
    }
    const FMargin& GetMaskBottomRowRightPadding()
    {
        static const FMargin Padding(FormDualValueSpacingValue, 0.0f, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetNodeHeaderAccentPadding()
    {
        static const FMargin Padding(BodyPadding(), 0.0f, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetNodeHeaderTitlePadding()
    {
        static const FMargin Padding(ToggleLabelSpacingValue, 0.0f, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetNodeHeaderActionPadding()
    {
        static const FMargin Padding(0.0f, 0.0f, BodyPadding(), 0.0f);
        return Padding;
    }
    const FMargin& GetNodeSubtitleSlotPadding()
    {
        static const FMargin Padding(0.0f, 0.0f, 0.0f, NodeSubtitleBottomSpacingValue);
        return Padding;
    }
    const FMargin& GetNodeContentPinPaddingMargin()
    {
        static const FMargin Padding(NodeContentPinSpacingValue, 0.0f);
        return Padding;
    }
    const FMargin& GetInlinePreviewPanelSlotPadding()
    {
        static const FMargin Padding(0.0f, InlinePreviewPanelTopPaddingValue, 0.0f, 0.0f);
        return Padding;
    }
    const FMargin& GetInlinePreviewPanelContentPadding()
    {
        static const FMargin Padding(InlinePreviewPanelPaddingValue);
        return Padding;
    }
    const FMargin& GetPinSlotPadding()
    {
        static const FMargin Padding(0.0f, FormLabelSpacingValue);
        return Padding;
    }

    const FSlateBrush* GetNodeCardBrush()
    {
        static const FSlateRoundedBoxBrush Brush(NodeBackgroundColor, NodeRadius(), NodeBorderColor, 1.0f);
        return &Brush;
    }

    const FSlateBrush* GetNodeSelectionBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, NodeRadius(), SelectionOutlineColor, 1.2f);
        return &Brush;
    }

    const FSlateBrush* GetNodeHoverBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, NodeRadius(), FLinearColor(1.0f, 1.0f, 1.0f, 0.09f), 1.0f);
        return &Brush;
    }

    const FSlateBrush* GetHeaderFillBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor::White, FVector4(NodeRadius(), NodeRadius(), 0.0f, 0.0f));
        return &Brush;
    }

    const FSlateBrush* GetBodyFillBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor::White, FVector4(0.0f, 0.0f, NodeRadius(), NodeRadius()));
        return &Brush;
    }

    const FSlateBrush* GetPanelBrush()
    {
        static const FSlateRoundedBoxBrush Brush(NodeInnerBackgroundColor, 6.0f, NodeBorderColor, 1.0f);
        return &Brush;
    }

    const FSlateBrush* GetPreviewBrush()
    {
        static const FSlateRoundedBoxBrush Brush(InputBackgroundColor, 6.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.70f), 1.0f);
        return &Brush;
    }

    const FSlateBrush* GetPinDisconnectedBrush()
    {
        static const FSlateRoundedBoxBrush Brush(FLinearColor(0.1f, 0.1f, 0.1f, 1.0f), 5.0f, FLinearColor(0.5f, 0.5f, 0.5f, 1.0f), 1.0f, FVector2f(10.0f, 10.0f));
        return &Brush;
    }

    const FSlateBrush* GetPinConnectedBrush()
    {
        static const FSlateRoundedBoxBrush Brush(WireColorValue, 5.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), 1.0f, FVector2f(10.0f, 10.0f));
        return &Brush;
    }

    const FEditableTextBoxStyle& GetNumericEntryTextBoxStyle()
    {
        static FEditableTextBoxStyle Style = []
        {
            FEditableTextBoxStyle Result = FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
            Result
                .SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f, InputBorderColor, 1.0f))
                .SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f))
                .SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f, InputFocusColor, 1.0f))
                .SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f, InputBorderColor.CopyWithNewOpacity(0.55f), 1.0f))
                .SetPadding(FMargin(6.0f, 2.0f))
                .SetForegroundColor(TextMainColor)
                .SetBackgroundColor(InputBackgroundColor);

            FTextBlockStyle TextStyle = Result.TextStyle;
            TextStyle.SetColorAndOpacity(TextMainColor);
            Result.SetTextStyle(TextStyle);
            return Result;
        }();

        return Style;
    }

    const FSpinBoxStyle& GetNumericEntrySpinBoxStyle()
    {
        static FSpinBoxStyle Style = []
        {
            FSpinBoxStyle Result = FAppStyle::Get().GetWidgetStyle<FSpinBoxStyle>("NumericEntrySpinBox");
            Result
                .SetBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f, InputBorderColor, 1.0f))
                .SetActiveBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f, InputFocusColor, 1.0f))
                .SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f))
                .SetActiveFillBrush(FSlateRoundedBoxBrush(InputBackgroundColor, 4.0f))
                .SetHoveredFillBrush(FSlateRoundedBoxBrush(InputBackgroundColor, 4.0f))
                .SetInactiveFillBrush(FSlateRoundedBoxBrush(InputBackgroundColor, 4.0f))
                .SetForegroundColor(TextMainColor)
                .SetTextPadding(FMargin(6.0f, 2.0f));
            return Result;
        }();

        return Style;
    }

    FLinearColor ResolvePinColor(const UEdGraphPin* Pin)
    {
        if (!Pin)
        {
            return TextDimColor;
        }

        if (Pin->PinType.PinCategory == UShineTextureGraphNodeBase::ColorPinCategory)
        {
            return FLinearColor(0.85f, 0.65f, 0.15f, 1.0f);
        }

        if (Pin->PinType.PinCategory == UShineTextureGraphNodeBase::Vector2PinCategory)
        {
            return FLinearColor(0.20f, 0.78f, 0.92f, 1.0f);
        }

        if (Pin->PinType.PinCategory == UShineTextureGraphNodeBase::ScalarPinCategory)
        {
            return FLinearColor(0.82f, 0.82f, 0.82f, 1.0f);
        }

        if (Pin->PinType.PinCategory == UShineTextureGraphNodeBase::IntPinCategory)
        {
            return FLinearColor(0.93f, 0.86f, 0.34f, 1.0f);
        }

        if (Pin->PinType.PinCategory == UShineTextureGraphNodeBase::BoolPinCategory)
        {
            return FLinearColor(0.32f, 0.84f, 0.48f, 1.0f);
        }

        return FLinearColor(0.7f, 0.7f, 0.7f, 1.0f);
    }
}
