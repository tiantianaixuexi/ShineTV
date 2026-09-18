#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"

/**
 * 「Shine AI 贴图」编辑器面板的视觉样式。
 *
 * 统一收口配色、圆角画刷与按钮状态，避免各个 Build* 函数里散落魔法数字。
 * 基色沿用编辑器深色主题的明度阶梯（窗口 → 卡片 → 内凹区域），
 * 强调色用暖琥珀，和大纲/内容浏览器里「Shine AI 贴图」资产的类型色一致；
 * 遮罩相关的控件用冷蓝，和画布上的遮罩叠加层（橙=保护 / 绿=可重绘）区分开。
 */
namespace ShineAIPaintStyle
{
    // ---------- 调色板 ----------

    /** 品牌强调色（暖琥珀）。 */
    const FLinearColor& Accent();
    /** 强调色的低透明版本，用作徽标底 / 高亮底。 */
    const FLinearColor& AccentSoft();
    /** 压在强调色上的深色文字。 */
    const FLinearColor& OnAccent();
    /** 次级强调色（冷蓝），用于遮罩相关控件。 */
    const FLinearColor& AccentCool();
    const FLinearColor& AccentCoolSoft();
    const FLinearColor& OnAccentCool();

    const FLinearColor& Success();
    const FLinearColor& Warning();
    const FLinearColor& Danger();

    /** UE 变换行的轴色：X 红 / Y 绿 / Z 蓝。 */
    const FLinearColor& AxisX();
    const FLinearColor& AxisY();
    const FLinearColor& AxisZ();

    /** 主文字。 */
    const FLinearColor& TextPrimary();
    /** 正文 / 控件文字。 */
    const FLinearColor& TextSecondary();
    /** 说明文字。 */
    const FLinearColor& TextMuted();

    /** 面板底色。 */
    const FLinearColor& WindowBackground();
    /** 卡片底色。 */
    const FLinearColor& CardBackground();
    /** 卡片里的内凹区域（画布 / 预览井 / 状态条）。 */
    const FLinearColor& WellBackground();
    /** 1px 描边（浅白）。 */
    const FLinearColor& Hairline();
    /** 列表行悬停底。 */
    const FLinearColor& RowHover();
    /** 列表行选中底。 */
    const FLinearColor& RowSelected();
    /** 滑条轨道。 */
    const FLinearColor& SliderTrack();

    // ---------- 画刷 ----------

    /** 窗口底色（直角）。 */
    const FSlateBrush* WindowBrush();
    /** 卡片：圆角 + 描边。 */
    const FSlateBrush* CardBrush();
    /** 整块面板底（Details / 底部面板这种直角大面板）。 */
    const FSlateBrush* PanelBrush();
    /** 顶部工具条底色。 */
    const FSlateBrush* ToolbarBrush();
    /** Details 分组标题栏底色。 */
    const FSlateBrush* GroupHeaderBrush();
    /** 内凹区域：更深的圆角。 */
    const FSlateBrush* WellBrush();
    /** 强调色细条（章节标题左侧）。 */
    const FSlateBrush* AccentBarBrush();
    /** 冷色细条。 */
    const FSlateBrush* AccentCoolBarBrush();
    /** 强调色徽标底。 */
    const FSlateBrush* AccentBadgeBrush();
    /** 1px 分隔线。 */
    const FSlateBrush* HairlineBrush();
    /** 列表行底：常态（透明，露出下面的井底）。 */
    const FSlateBrush* ListRowBrush();
    /** 列表行底：悬停。 */
    const FSlateBrush* ListRowHoveredBrush();
    /** 列表行底：选中（浅琥珀）。 */
    const FSlateBrush* ListRowSelectedBrush();
    /** 圆角小方块：默认白色，用 BorderBackgroundColor 染成需要的颜色（色板 / 徽标）。 */
    const FSlateBrush* RoundedFillBrush();
    /** 纯色填充（圆角、无描边），默认白色，用 BorderBackgroundColor 染色（轴色条）。 */
    const FSlateBrush* SolidFillBrush();
    /** 数值输入框底：内凹圆角 + 细描边。 */
    const FSlateBrush* NumericFieldBrush();
    /** 章节标题栏底。 */
    const FSlateBrush* SectionHeaderBrush();
    /** 更细的 1px 分隔线（章节之间用）。 */
    const FSlateBrush* RuleBrush();

    // ---------- 字体 / 文本样式 ----------

    FSlateFontInfo TitleFont();
    FSlateFontInfo SectionFont();
    FSlateFontInfo BodyFont();
    FSlateFontInfo SmallFont();
    FSlateFontInfo ValueFont();

    const FTextBlockStyle& TitleTextStyle();
    const FTextBlockStyle& SectionTextStyle();
    const FTextBlockStyle& BodyTextStyle();
    const FTextBlockStyle& MutedTextStyle();
    const FTextBlockStyle& ValueTextStyle();
    /** 徽标（类型 / 标签）文字：小号 + 强调色。 */
    const FTextBlockStyle& BadgeTextStyle();

    // ---------- 数值输入 ----------

    /** 轴输入框里的文本框样式：无底、细内边距。 */
    const FEditableTextBoxStyle& NumericTextBox();

    // ---------- 按钮 ----------

    /** 主行动按钮：实心琥珀。 */
    const FButtonStyle& PrimaryButton();
    /** 次要按钮：半透明描边。 */
    const FButtonStyle& SubtleButton();
    /** 工具按钮（未选中）。 */
    const FButtonStyle& ToolButton();
    /** 工具按钮（选中，琥珀）。 */
    const FButtonStyle& ToolButtonActive();
    /** 工具按钮（选中，冷蓝，用于遮罩工具）。 */
    const FButtonStyle& ToolButtonActiveCool();
    /** 列表里的小圆角按钮（×）。 */
    const FButtonStyle& MiniButton();
}
