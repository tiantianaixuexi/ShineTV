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

// TTC 子字体定位：在 .ttc 里找族名匹配的下标；单字体文件恒返回 0。
[[nodiscard]] int FindTtcIndex(const std::string& path, const std::string& familyName);

} // namespace shine::kit
