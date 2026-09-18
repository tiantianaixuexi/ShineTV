#include "Editor/ShineAIPaintEditorStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace ShineAIPaintStyle
{
    namespace
    {
        // ---------- 调色板字面量（集中在这里，方便整体调色） ----------

        const FLinearColor GAccent(0.960f, 0.635f, 0.216f, 1.0f);
        const FLinearColor GAccentSoft(0.960f, 0.635f, 0.216f, 0.160f);
        const FLinearColor GOnAccent(0.090f, 0.066f, 0.031f, 1.0f);

        const FLinearColor GAccentCool(0.361f, 0.620f, 0.941f, 1.0f);
        const FLinearColor GAccentCoolSoft(0.361f, 0.620f, 0.941f, 0.160f);
        const FLinearColor GOnAccentCool(0.043f, 0.078f, 0.145f, 1.0f);

        const FLinearColor GSuccess(0.353f, 0.780f, 0.451f, 1.0f);
        const FLinearColor GWarning(0.960f, 0.722f, 0.259f, 1.0f);
        const FLinearColor GDanger(0.941f, 0.380f, 0.341f, 1.0f);

        // 和 UE 变换行一致的轴色。
        const FLinearColor GAxisX(0.905f, 0.298f, 0.235f, 1.0f);
        const FLinearColor GAxisY(0.521f, 0.792f, 0.290f, 1.0f);
        const FLinearColor GAxisZ(0.259f, 0.560f, 0.960f, 1.0f);

        const FLinearColor GTextPrimary(0.929f, 0.941f, 0.961f, 1.0f);
        const FLinearColor GTextSecondary(0.788f, 0.816f, 0.855f, 1.0f);
        const FLinearColor GTextMuted(0.525f, 0.557f, 0.612f, 1.0f);

        const FLinearColor GWindowBackground(0.043f, 0.047f, 0.055f, 1.0f);
        const FLinearColor GCardBackground(0.086f, 0.094f, 0.106f, 1.0f);
        const FLinearColor GWellBackground(0.055f, 0.061f, 0.071f, 1.0f);
        const FLinearColor GHairline(1.0f, 1.0f, 1.0f, 0.070f);
        const FLinearColor GSliderTrack(1.0f, 1.0f, 1.0f, 0.110f);
        const FLinearColor GRowHover(1.0f, 1.0f, 1.0f, 0.055f);
        const FLinearColor GRowSelected(0.960f, 0.635f, 0.216f, 0.140f);

        constexpr float GCardRadius = 6.0f;
        constexpr float GInsetRadius = 4.0f;
        constexpr float GButtonRadius = 4.0f;
        constexpr float GBadgeRadius = 3.0f;

        FSlateRoundedBoxBrush MakeRounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
        {
            return FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth);
        }

        FButtonStyle MakeButtonStyle(
            const FLinearColor& Normal,
            const FLinearColor& Hover,
            const FLinearColor& Pressed,
            const FLinearColor& Outline,
            float OutlineWidth,
            float Radius,
            const FMargin& Padding)
        {
            FButtonStyle Style;
            Style.SetNormal(MakeRounded(Normal, Radius, Outline, OutlineWidth));
            Style.SetHovered(MakeRounded(Hover, Radius, Outline, OutlineWidth));
            Style.SetPressed(MakeRounded(Pressed, Radius, Outline, OutlineWidth));

            FLinearColor Disabled = Normal;
            Disabled.A *= 0.45f;
            Style.SetDisabled(MakeRounded(Disabled, Radius, Outline, OutlineWidth));

            Style.SetNormalPadding(Padding);
            Style.SetPressedPadding(Padding);
            return Style;
        }
    }

    // ---------- 调色板 ----------

    const FLinearColor& Accent() { return GAccent; }
    const FLinearColor& AccentSoft() { return GAccentSoft; }
    const FLinearColor& OnAccent() { return GOnAccent; }
    const FLinearColor& AccentCool() { return GAccentCool; }
    const FLinearColor& AccentCoolSoft() { return GAccentCoolSoft; }
    const FLinearColor& OnAccentCool() { return GOnAccentCool; }
    const FLinearColor& Success() { return GSuccess; }
    const FLinearColor& Warning() { return GWarning; }
    const FLinearColor& Danger() { return GDanger; }
    const FLinearColor& AxisX() { return GAxisX; }
    const FLinearColor& AxisY() { return GAxisY; }
    const FLinearColor& AxisZ() { return GAxisZ; }
    const FLinearColor& TextPrimary() { return GTextPrimary; }
    const FLinearColor& TextSecondary() { return GTextSecondary; }
    const FLinearColor& TextMuted() { return GTextMuted; }
    const FLinearColor& WindowBackground() { return GWindowBackground; }
    const FLinearColor& CardBackground() { return GCardBackground; }
    const FLinearColor& WellBackground() { return GWellBackground; }
    const FLinearColor& Hairline() { return GHairline; }
    const FLinearColor& SliderTrack() { return GSliderTrack; }
    const FLinearColor& RowHover() { return GRowHover; }
    const FLinearColor& RowSelected() { return GRowSelected; }

    // ---------- 画刷 ----------

    const FSlateBrush* WindowBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GWindowBackground, 0.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* CardBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GCardBackground, GCardRadius, GHairline, 1.0f);
        return &Brush;
    }

    const FSlateBrush* PanelBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GCardBackground, 0.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* ToolbarBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor(0.067f, 0.075f, 0.086f, 1.0f), 0.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* GroupHeaderBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.035f), 0.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* WellBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GWellBackground, GInsetRadius, FLinearColor(1.0f, 1.0f, 1.0f, 0.045f), 1.0f);
        return &Brush;
    }

    const FSlateBrush* AccentBarBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GAccent, 2.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* AccentCoolBarBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GAccentCool, 2.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* AccentBadgeBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GAccentSoft, GBadgeRadius, GAccent, 1.0f);
        return &Brush;
    }

    const FSlateBrush* HairlineBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GHairline, 0.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* ListRowBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor::Transparent, GInsetRadius, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* ListRowHoveredBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GRowHover, GInsetRadius, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* ListRowSelectedBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(GRowSelected, GInsetRadius, GAccentSoft, 1.0f);
        return &Brush;
    }

    const FSlateBrush* RoundedFillBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor::White, 3.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 1.0f);
        return &Brush;
    }

    const FSlateBrush* SolidFillBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor::White, 2.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* NumericFieldBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.050f), GInsetRadius, FLinearColor(1.0f, 1.0f, 1.0f, 0.075f), 1.0f);
        return &Brush;
    }

    const FSlateBrush* SectionHeaderBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.028f), 0.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    const FSlateBrush* RuleBrush()
    {
        static const FSlateRoundedBoxBrush Brush = MakeRounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.045f), 0.0f, FLinearColor::Transparent, 0.0f);
        return &Brush;
    }

    // ---------- 字体 / 文本样式 ----------

    FSlateFontInfo TitleFont() { return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14); }
    FSlateFontInfo SectionFont() { return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11); }
    FSlateFontInfo BodyFont() { return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10); }
    FSlateFontInfo SmallFont() { return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9); }
    FSlateFontInfo ValueFont() { return FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 9); }

    const FTextBlockStyle& TitleTextStyle()
    {
        static const FTextBlockStyle Style = FTextBlockStyle()
            .SetFont(TitleFont())
            .SetColorAndOpacity(FSlateColor(GTextPrimary));
        return Style;
    }

    const FTextBlockStyle& SectionTextStyle()
    {
        static const FTextBlockStyle Style = FTextBlockStyle()
            .SetFont(SectionFont())
            .SetColorAndOpacity(FSlateColor(GTextPrimary));
        return Style;
    }

    const FTextBlockStyle& BodyTextStyle()
    {
        static const FTextBlockStyle Style = FTextBlockStyle()
            .SetFont(BodyFont())
            .SetColorAndOpacity(FSlateColor(GTextSecondary));
        return Style;
    }

    const FTextBlockStyle& MutedTextStyle()
    {
        static const FTextBlockStyle Style = FTextBlockStyle()
            .SetFont(SmallFont())
            .SetColorAndOpacity(FSlateColor(GTextMuted));
        return Style;
    }

    const FTextBlockStyle& ValueTextStyle()
    {
        static const FTextBlockStyle Style = FTextBlockStyle()
            .SetFont(ValueFont())
            .SetColorAndOpacity(FSlateColor(GTextSecondary));
        return Style;
    }

    const FTextBlockStyle& BadgeTextStyle()
    {
        static const FTextBlockStyle Style = FTextBlockStyle()
            .SetFont(SectionFont())
            .SetColorAndOpacity(FSlateColor(GAccent));
        return Style;
    }

    const FEditableTextBoxStyle& NumericTextBox()
    {
        static const FSlateRoundedBoxBrush Transparent = MakeRounded(FLinearColor::Transparent, 0.0f, FLinearColor::Transparent, 0.0f);
        static const FSlateRoundedBoxBrush Hovered = MakeRounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.070f), GInsetRadius, FLinearColor::Transparent, 0.0f);
        static const FSlateRoundedBoxBrush Focused = MakeRounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.100f), GInsetRadius, GAccentSoft, 1.0f);

        static const FEditableTextBoxStyle Style = FEditableTextBoxStyle()
            .SetBackgroundImageNormal(Transparent)
            .SetBackgroundImageHovered(Hovered)
            .SetBackgroundImageFocused(Focused)
            .SetBackgroundImageReadOnly(Transparent)
            .SetPadding(FMargin(4.0f, 1.0f))
            .SetFont(BodyFont())
            .SetForegroundColor(FSlateColor(GTextPrimary))
            .SetReadOnlyForegroundColor(FSlateColor(GTextMuted));
        return Style;
    }

    // ---------- 按钮 ----------

    const FButtonStyle& PrimaryButton()
    {
        static const FButtonStyle Style = MakeButtonStyle(
            GAccent,
            FLinearColor(1.0f, 0.710f, 0.318f, 1.0f),
            FLinearColor(0.836f, 0.522f, 0.153f, 1.0f),
            FLinearColor::Transparent,
            0.0f,
            GButtonRadius,
            FMargin(12.0f, 4.0f));
        return Style;
    }

    const FButtonStyle& SubtleButton()
    {
        static const FButtonStyle Style = MakeButtonStyle(
            FLinearColor(1.0f, 1.0f, 1.0f, 0.040f),
            FLinearColor(1.0f, 1.0f, 1.0f, 0.098f),
            FLinearColor(1.0f, 1.0f, 1.0f, 0.150f),
            FLinearColor(1.0f, 1.0f, 1.0f, 0.085f),
            1.0f,
            GButtonRadius,
            FMargin(10.0f, 4.0f));
        return Style;
    }

    const FButtonStyle& ToolButton()
    {
        static const FButtonStyle Style = MakeButtonStyle(
            FLinearColor(1.0f, 1.0f, 1.0f, 0.045f),
            FLinearColor(1.0f, 1.0f, 1.0f, 0.105f),
            FLinearColor(1.0f, 1.0f, 1.0f, 0.150f),
            FLinearColor(1.0f, 1.0f, 1.0f, 0.075f),
            1.0f,
            GButtonRadius,
            FMargin(8.0f, 5.0f));
        return Style;
    }

    const FButtonStyle& ToolButtonActive()
    {
        static const FButtonStyle Style = MakeButtonStyle(
            GAccent,
            FLinearColor(1.0f, 0.710f, 0.318f, 1.0f),
            FLinearColor(0.836f, 0.522f, 0.153f, 1.0f),
            FLinearColor::Transparent,
            0.0f,
            GButtonRadius,
            FMargin(8.0f, 7.0f));
        return Style;
    }

    const FButtonStyle& ToolButtonActiveCool()
    {
        static const FButtonStyle Style = MakeButtonStyle(
            GAccentCool,
            FLinearColor(0.478f, 0.706f, 0.980f, 1.0f),
            FLinearColor(0.278f, 0.510f, 0.831f, 1.0f),
            FLinearColor::Transparent,
            0.0f,
            GButtonRadius,
            FMargin(8.0f, 7.0f));
        return Style;
    }

    const FButtonStyle& MiniButton()
    {
        static const FButtonStyle Style = MakeButtonStyle(
            FLinearColor(1.0f, 1.0f, 1.0f, 0.000f),
            FLinearColor(0.941f, 0.380f, 0.341f, 0.900f),
            FLinearColor(0.800f, 0.290f, 0.259f, 0.900f),
            FLinearColor(1.0f, 1.0f, 1.0f, 0.070f),
            1.0f,
            3.0f,
            FMargin(6.0f, 1.0f));
        return Style;
    }
}
