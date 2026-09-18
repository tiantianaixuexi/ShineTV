#pragma once

#include "CoreMinimal.h"

struct FEditableTextBoxStyle;
struct FSlateBrush;
struct FSpinBoxStyle;
class UEdGraphPin;

namespace ShineTextureSlateTheme
{
    SHINETEXTUREEDITOR_API FLinearColor GraphBackground() noexcept; 
    SHINETEXTUREEDITOR_API FLinearColor GridSmall() noexcept;
    SHINETEXTUREEDITOR_API FLinearColor GridLarge() noexcept;

    SHINETEXTUREEDITOR_API FLinearColor NodeBackground() noexcept;
    SHINETEXTUREEDITOR_API FLinearColor NodeInnerBackground() noexcept;
    SHINETEXTUREEDITOR_API FLinearColor NodeBorder() noexcept;

    SHINETEXTUREEDITOR_API FLinearColor TextMain() noexcept;
    SHINETEXTUREEDITOR_API FLinearColor TextSecondary() noexcept;
    SHINETEXTUREEDITOR_API FLinearColor TextDim() noexcept;

    SHINETEXTUREEDITOR_API FLinearColor InputBackground() noexcept;
    SHINETEXTUREEDITOR_API FLinearColor InputBorder() noexcept;
    SHINETEXTUREEDITOR_API FLinearColor InputFocus() noexcept;

    SHINETEXTUREEDITOR_API FLinearColor WireColor() noexcept ;
    SHINETEXTUREEDITOR_API FLinearColor SelectionOutline() noexcept;

    SHINETEXTUREEDITOR_API float NodeRadius() noexcept;
    SHINETEXTUREEDITOR_API float HeaderHeight() noexcept;
    SHINETEXTUREEDITOR_API float BodyPadding() noexcept;
    SHINETEXTUREEDITOR_API FSlateFontInfo BodyFont() noexcept;
    SHINETEXTUREEDITOR_API float NodeMinWidth() noexcept;
    SHINETEXTUREEDITOR_API float NumericEntryMinWidth() noexcept;
    SHINETEXTUREEDITOR_API float FormSectionTopPadding() noexcept;
    SHINETEXTUREEDITOR_API float FormLabelSpacing() noexcept;
    SHINETEXTUREEDITOR_API float FormDualValueSpacing() noexcept;
    SHINETEXTUREEDITOR_API float FormSectionMinHeight() noexcept;
    SHINETEXTUREEDITOR_API float ToggleLabelSpacing() noexcept;
    SHINETEXTUREEDITOR_API float NodeContentMinWidth() noexcept;
    SHINETEXTUREEDITOR_API float PreviewNodeContentMinWidth() noexcept;
    SHINETEXTUREEDITOR_API float NodeSubtitleBottomSpacing() noexcept;
    SHINETEXTUREEDITOR_API float NodeContentPinSpacing() noexcept;
    SHINETEXTUREEDITOR_API float InlinePreviewPanelTopPadding() noexcept;
    SHINETEXTUREEDITOR_API float InlinePreviewPanelPadding() noexcept;
    SHINETEXTUREEDITOR_API const FMargin& GetTopSectionPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetBodySpacingPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetTightRowPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetFormLabelSlotPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetFormDualValueSlotPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetToggleLabelPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetMaskTopRowLeftPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetMaskTopRowRightPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetMaskBottomRowLeftPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetMaskBottomRowRightPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetNodeHeaderAccentPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetNodeHeaderTitlePadding();
    SHINETEXTUREEDITOR_API const FMargin& GetNodeHeaderActionPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetNodeSubtitleSlotPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetNodeContentPinPaddingMargin();
    SHINETEXTUREEDITOR_API const FMargin& GetInlinePreviewPanelSlotPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetInlinePreviewPanelContentPadding();
    SHINETEXTUREEDITOR_API const FMargin& GetPinSlotPadding();

    SHINETEXTUREEDITOR_API const FSlateBrush* GetNodeCardBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetNodeSelectionBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetNodeHoverBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetHeaderFillBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetBodyFillBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetPanelBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetPreviewBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetPinDisconnectedBrush();
    SHINETEXTUREEDITOR_API const FSlateBrush* GetPinConnectedBrush();
    SHINETEXTUREEDITOR_API const FEditableTextBoxStyle& GetNumericEntryTextBoxStyle();
    SHINETEXTUREEDITOR_API const FSpinBoxStyle& GetNumericEntrySpinBoxStyle();

    SHINETEXTUREEDITOR_API FLinearColor ResolvePinColor(const UEdGraphPin* Pin);
}
