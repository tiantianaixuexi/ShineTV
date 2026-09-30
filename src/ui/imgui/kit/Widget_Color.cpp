#include "ui/imgui/kit/Widget_Color.h"

#include "ui/imgui/theme/Theme.h"

#include <algorithm>

namespace shine::kit {

// ------------------------------------------------------------------ 取色
ImU32 ColorOf(std::uint32_t rgba) { return theme::ToImU32((rgba)); }
ImU32 ColorText() { return ColorOf(theme::Current().textPrimary); }
ImU32 ColorTextSecondary() { return ColorOf(theme::Current().textSecondary); }
ImU32 ColorTextMuted() { return ColorOf(theme::Current().textMuted); }
ImU32 ColorTextInverse() { return ColorOf(theme::Current().textInverse); }
ImU32 ColorSurface() { return ColorOf(theme::Current().bgSurface); }
ImU32 ColorPanel() { return ColorOf(theme::Current().bgPanel); }
ImU32 ColorElevated() { return ColorOf(theme::Current().bgElevated); }
ImU32 ColorOverlay() { return ColorOf(theme::Current().bgOverlay); }
ImU32 ColorVoid() { return ColorOf(theme::Current().bgVoid); }
ImU32 ColorFillHover() { return ColorOf(theme::Current().fillHover); }
ImU32 ColorFillSelected() { return ColorOf(theme::Current().fillSelected); }
ImU32 ColorFillMuted() { return ColorOf(theme::Current().fillMuted); }
ImU32 ColorLineSubtle() { return ColorOf(theme::Current().lineSubtle); }
ImU32 ColorLineNormal() { return ColorOf(theme::Current().lineNormal); }
ImU32 ColorLineStrong() { return ColorOf(theme::Current().lineStrong); }
ImU32 ColorAccent() { return ColorOf(theme::Current().accentPrimary); }
ImU32 ColorAccentHover() { return ColorOf(theme::Current().accentPrimaryHover); }
ImU32 ColorAccentFg() { return ColorOf(theme::Current().accentPrimaryFg); }
ImU32 ColorAccentDim() { return ColorOf(theme::CurrentDerived().accentDim); }
ImU32 ColorAccentGlow() { return ColorOf(theme::CurrentDerived().accentGlow); }
ImU32 ColorFocusRing() { return ColorOf(theme::Current().lineFocus); }
ImU32 ColorScrim() { return ColorOf(theme::Current().shadowScrim); }

// 「不画」而不是「画黑色」：返回全 0，让 draw call 的 alpha 通道自己把它关掉。
//
// ⚠️ 这里**故意**不用 `IM_COL32(0,0,0,0)`：那正是 tools/check-colors.ps1 要拦的
//    硬编码颜色字面量，写在这里会让「唯一合规的出口」自己成为违规样本。
//    ImU32 的字节序宏（IMGUI_USE_BGRA_PACKED_COLOR 决定 R 在高位还是低位）对全 0
//    无所谓 —— 两种字节序下 0 都是 0，所以写字面量是安全的。
constexpr ImU32 kTransparent = 0u;
ImU32 ColorTransparent() { return kTransparent; }

// ImU32 的字节序是编译期宏（IMGUI_USE_BGRA_PACKED_COLOR 决定 R 在高位还是低位）。
// 这两个函数一律走 ImGui 自己的 float4 往返，**不手写位移** ——
// 手写过一次：默认字节序下 R 在最低字节，于是 alpha 被写进了红通道，
// 所有半透明叠层（tag 底 12%、gate 底 10%、KPI 光斑）其实全是 100% 不透明。
// ⚠️ 语义是**相乘调制**，不是「设为该 alpha」。
// 设成 1.0 就等于把颜色原本的 alpha 抹成全不透明，而预混派生色（tagBg 12%、
// gateBg 10%、ganttCell 12%）恰恰靠 alpha 表达"淡底"这个 CSS color-mix 语义
// （ui.css:139-144 `color-mix(in srgb, var(--ok) 12%, transparent)`）。
// 曾经按「设为」写，于是 Tag 传 busyPulse 的 1.0 → 12% 底变成 100% 实心，
// 绿字压在绿底上完全读不出来（assets 详情页的「已确认」）。
ImU32 WithAlpha(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

// 需要**覆盖** alpha（而不是调制）时用这个，名字自带"覆盖"语义。
// 只在明确知道目标 alpha 时用：阴影/scrim 叠层、以及给无 alpha 的实色补 alpha。
ImU32 WithAlphaSet(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w = std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

ImU32 LerpColorTo(ImU32 from, ImU32 to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    const ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                                a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}

} // namespace shine::kit
