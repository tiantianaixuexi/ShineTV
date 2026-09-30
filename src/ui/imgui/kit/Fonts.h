#pragma once
// shine::kit::Fonts —— 字体图集与 CJK（P2.5，本重构最硬的判据）
//
// 整个界面是中文（主题名就是「深空/薄暮/纸墨/水墨/极夜」），ImGui 默认
// 字体只有 ASCII —— 不解决就是满屏豆腐块，**而且不报错**。
//
// 三处必须做对：
//  1. msyh.ttc / simsun.ttc 是 TrueType Collection → 必须给 ImFontConfig::FontNo
//     选子字体。下标不是猜的：FindTtcIndex() 读 TTC 的 name 表按**族名**定位。
//  2. **不设 ImFontConfig::GlyphRanges**。1.92 起那是 legacy 路径：只有不声明
//     ImGuiBackendFlags_RendererHasTextures 的后端才会去读它（imgui_draw.cpp:3514
//     → :3568 是全工程唯一一处读取点）。本工程用 OpenGL3 后端，那条分支永不执行，
//     设了也只是存下来没人看。字形由**按需加载**补齐：小说正文里任意汉字只要
//     msyh.ttc 里有就会被自动烘焙，所以「字形集够不够」不是个问题。
//     换回 legacy 后端时必须重新接上，否则界面全是 '?'。
//  3. 图集格式保持 RGBA32，**不要**设 ImTextureFormat_Alpha8。core 支持，但
//     imgui_impl_opengl3 后端把格式硬编码成 GL_RGBA（:712/:733），而 :739 又按
//     `tex->BytesPerPixel` 算行跨度 —— Alpha8 下就是「按 1B/px 算跨度、按 4B/px
//     上传」，实测图集直接烘不出来。详见 Fonts.cpp 里 atlas->Clear() 上方那段。
//     ⚠️ 统计图集大小要用 ImTextureData::GetSizeInBytes()，它读 BytesPerPixel；
//     硬编码 `Width*Height*4` 会在换格式时虚报。
//
// 关于「图集预算」：按需加载下**没有固定上限** —— 图集随用户实际读到的汉字增长。
// 早先那个「总量 > 64MB 就 log::Error，并在日志里打印 vram=」是错的设计：
// 它量的是 **CPU 侧**暂存缓冲（ImTextureData::Pixels 是 unsigned char*）而不是
// 显存，GPU 侧只留一个 TexID；而且把一个正常现象标成 Error，长阅读下每帧刷屏。
// 现在是一次性快照 + 单帧增量 + 增长速率三档 Warn，去重按「事件」而非全局一次性。
//
// 另一条 1.92 起才成立、且不照做就一定糊的：**ImFontConfig::RasterizerDensity
// 必须 > 1**。本工程字号大量是半点（10.5/12.5/13.5…），而 `PixelSnapH = true`
// 会把 OversampleH 压成 1，**OversampleV 在 auto 模式下恒为 1**
// （imgui_draw.cpp:3525 没有 >36px 分支）⇒ 纵向没有任何超采样可用；而密度缺省
// 又只有 1.0 ⇒ stb_truetype 按整数高度出位图再缩放到半点尺寸，必然重采样。
// 细节与依据见 Fonts.cpp 里 kRasterizerDensity 上方那段注释。
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
