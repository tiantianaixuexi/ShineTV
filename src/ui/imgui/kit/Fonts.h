#pragma once
// shine::kit::Fonts —— 字体图集与 CJK（P2.5，本重构最硬的判据）
//
// 整个界面是中文（主题名就是「深空/薄暮/纸墨/水墨/极夜」），ImGui 默认
// 字体只有 ASCII —— 不解决就是满屏豆腐块，**而且不报错**。
//
// 三处必须做对：
//  1. msyh.ttc / simsun.ttc 是 TrueType Collection → 必须给 ImFontConfig::FontNo
//     选子字体。下标不是猜的：FindTtcIndex() 读 TTC 的 name 表按**族名**定位。
//  2. 字形范围用 GetGlyphRangesChineseSimplifiedCommon()（约 2500 常用字）。
//     用 ChineseFull()（21000 字）图集会直接爆显存。
//  3. 图集显存占用必须打日志，硬判据 < 64 MB。
//
// 另一条 1.92 起才成立、且不照做就一定糊的：**ImFontConfig::RasterizerDensity
// 必须 > 1**。本工程字号大量是半点（10.5/12.5/13.5…），而 `PixelSnapH = true`
// 会把 OversampleH/V 强制压成 1、密度缺省又只有 1.0 ⇒ stb_truetype 按整数高度
// 出位图再缩放到半点尺寸，必然重采样。细节与依据见 Fonts.cpp 里
// kRasterizerDensity 上方那段注释。
//
// ⚠️ 字号「看起来是半点」不等于绘制尺寸就是半点：自绘路径
//    `ImDrawList::AddText(font, size, ...)` **不取整**（`scale = size / baked->Size`），
//    而上下文路径 `ImGui::PushFont` 走 `GetRoundedFontSize` = `IM_ROUND` 取整。
//    同一个 `FontAt(12.5f)` 在两条路上会落到不同的 baked 尺寸上，后者是一次
//    on-demand 重烘焙。预烘焙字号清单 `kUiSizes` 保持半点正是为了配合自绘路径，
//    不要"顺手"把它改成整数。
//
// 水墨是唯一换字体的主题（衬线族，tokens.css:195）→ 切主题时重建图集。
#pragma once

#include <imgui.h>

#include <string>
#include <unordered_map>

namespace shine::kit {

// 载入并构建图集。serif=true 用宋体族（水墨主题）。
// 返回 false 表示字体文件缺失且已退回 ImGui 内置字体（界面会有豆腐块）。
[[nodiscard]] bool BuildFontAtlas(bool serif);

// 图集显存占用（字节）。首帧之后有效。
[[nodiscard]] std::size_t FontAtlasBytes();
[[nodiscard]] int FontAtlasTextureCount();
// 首帧之后调一次：后端此时已烘焙并上传纹理，打印显存占用并校验 64 MB 硬判据。
void LogFontAtlasIfNeeded();

// 按像素取字体；没建过的尺寸向上取最近的一档。
[[nodiscard]] ImFont* FontAt(float pixelSize);
[[nodiscard]] ImFont* MonoAt(float pixelSize);
[[nodiscard]] ImFont* BaseFont();

// 粗体字重。设计稿大量用 600/700/800（控件标签 / 卡片标题 / 视图大标题），
// 单一个字重会让「1:1」在字重上整体偏轻。msyhbd.ttc 是雅黑 Bold，TTC 同样要
// 按族名定位子字体；取不到就退回常规字重（退回只偏细，不出豆腐块）。
[[nodiscard]] ImFont* FontBoldAt(float pixelSize);
[[nodiscard]] bool HasBoldFace();

// TTC 子字体定位：在 .ttc 里找族名匹配的下标；单字体文件恒返回 0。
[[nodiscard]] int FindTtcIndex(const std::string& path, const std::string& familyName);

} // namespace shine::kit
