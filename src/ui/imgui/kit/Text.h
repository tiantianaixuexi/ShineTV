#pragma once
// shine::kit::Text —— 文本原语（R3，refactor/记忆.md（原 记忆.md（原 component-reuse.md）） §4.2 三件套之一）
//
// 收编页面层的两类手写：
//   * 裸 `draw->AddText(...)`（81 处 / 21 个文件）—— 换行、裁剪、基线各写各的；
//   * `FontAt(11.5f)` 这类**数字字面量字号**（208 处 / 24 个文件）—— 它让
//     「字号全局可调整」结构上不可能：改一次要动 208 处。
//
// 页面只传两样东西：`theme::font::*` 字号 token + 主题取色入口的返回值。
// 行盒、基线、字面（粗/等宽）、折行宽度全部在这里算，页面不写算式。
//
// ⚠️ **像素不变契约**（R3 的验收判据）：scale = 1.0 时 `Text()` 就是
//    `draw->AddText(Font[Bold|Mono]At(size), size, pos, col, begin, end, wrap)`
//    这一行调用的封装 —— 不加任何额外变换、不取整、不重排，所以收编前后
//    屏幕逐位一致。基线算式 `AscentOf` 原先散在 PageKpi.cpp（本地 static 函数），
//    这里是同一份常量与同一个 ceil，不是新算式。
//
// 布局推进（Row/Stack/Grid）是 R4 的 kit::Layout，不在这里；本原语只管
// 「一段文字怎么画、多宽」，不管「画在哪一行的哪个位置」。
#pragma once

#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/theme/Tokens.h"

#include <imgui.h>

#include <string_view>

namespace shine::kit {

// 行盒高 = 字号 × 1.6（base.css:16 的全局 line-height；设计稿里 .kpi 一族
// 没有覆盖它）。PageKpi 的卡高算式从这条推，不再本地手写 `11.5f * 1.6f`。
[[nodiscard]] constexpr float LineBox(float fontToken) { return fontToken * 1.6f; }

// ImGui 的 Ascent 口径：`ceil(字号 × A/(A−D))`。
//   `imgui_draw.cpp:4783` 的 ScaleFactor 来自 `stbtt_ScaleForPixelHeight`，其定义
//   （`imstb_truetype.h:2665-2669`）是 `height / (hhea.ascent − hhea.descent)`
//   —— 即「把 A−D 跨度映射到字号像素」，不是「em 盒 = 字号」。
//   msyh 的 A/(A−D) 由 `kit/Fonts.cpp` 顶部那条实测记录反推（13px → asc 11）= 0.8017。
// 文字要落在 CSS 基线上：`pos.y = 基线 − Ascent(字号)`。
// ⚠️ 0.8017 是 msyh 的度量，与 `PageKpi.cpp` 原来那份本地实现逐位一致；
//    换字体主题（水墨走宋体）时这里仍是同一近似 —— 半像素级，量过不追。
[[nodiscard]] float AscentOf(float fontToken);
// 基线（相对某个行盒/卡顶）→ ImGui 的 pos.y。
[[nodiscard]] float BaselineToY(float baseline, float fontToken);

// 一段文字的样式。size 必须是 token；色必须来自主题（0 = ColorText() 兜底）。
struct TextStyle {
    float size = theme::font::kBody;
    bool bold = false;      // 600/700/800 全落 msyh Bold（本仓只有两档字重）
    bool mono = false;      // --font-mono（拉丁等宽 + 中文回落雅黑）
    ImU32 color = 0;        // 0 = ColorText()；一般显式传 ColorTextMuted() 等
    float wrapWidth = 0.0f; // >0 按此宽度折行（ImGui AddText 的 wrap 语义）
};

// 量宽（含折行处理，与绘制同口径）。
[[nodiscard]] ImVec2 TextSize(std::string_view text, const TextStyle& style);

// 画一段文字。pos 与 `draw->AddText` 同语义（行盒左上角，不是基线）。
void Text(ImDrawList* draw, ImVec2 pos, std::string_view text, const TextStyle& style);

// 基线版：y 由 `baseline − Ascent(size)` 推。设计稿给了基线（或由 flex
// baseline 对齐推出）时用它，别手算「行盒顶 + 半行距」—— 那套在数值行
// 会差 1.8px（PageKpi 第十一轮的实测记录）。
void TextAtBaseline(ImDrawList* draw, float x, float baseline, std::string_view text,
                    const TextStyle& style);

// ---- 第二十二/二十三轮补的三族：把「取字面 + 传字号」这个双参数从页面层彻底拿掉 ----
//
// 动机不是「少写几个字」。页面层原先要画一段**会截断**的文字，唯一的入口是
// `DrawTextClipped(draw, FontAt(N), N, pos, maxW, col, text)` —— 同一个 `N` 出现
// **两遍**，而两遍是**两次独立的字面量**：改一处的字号很容易漏掉另一处，字号就
// 画在 B 档、量宽在 A 档（`DrawTextClipped` 用 font 量、`AddText` 用 size 画）。
// 收编后这些点只出现**一个** token，且那个 token 同时决定画与量。
//
// ⚠️ 三族全部是**逐位转发**到 `Draw.h` 的既有原语（`DrawTextClipped` /
//    `MeasureClipped` / `CenterTextY`），没有新算式、没有取整 —— 所以本页顶那条
//    「像素不变契约」在这里同样成立：改前改后屏上逐位一致。
//
// 单行返回省略后的实际绘制宽度，与 TextClippedWidth 同口径。
// wrap=true 时返回多行占用的总行高，供页面推进下一段。
float TextClipped(ImDrawList* draw, ImVec2 pos, float maxWidth, std::string_view text,
                  const TextStyle& style, bool wrap = false);

// `TextClipped` 的纯量版：超宽时返回**截断后**的宽度（不是原文宽度）。
[[nodiscard]] float TextClippedWidth(float maxWidth, std::string_view text,
                                     const TextStyle& style);

// 单行文字的像素宽（不截断）。页面层 `LabelWidth` 的 token 版：同样只传一次字号。
[[nodiscard]] float TextWidth(std::string_view text, const TextStyle& style);

// 把 ImGui 的行盒摆到 `centerY` 上（`CenterTextY` 的 token 版）。
//
// ⚠️ 语义写在 `Draw.h` 的 `CenterTextY` 注释里，**别改这个算式**：`ImFont::RenderText`
//    的 `line_height = size`，所以行盒高**就等于请求字号**，`centerY - size/2` 居中
//    的是行盒且是对的。改成「按字体真实 Ascent/Descent 算」会让每一行都偏。
[[nodiscard]] constexpr float CenterY(float centerY, const TextStyle& style) {
    return centerY - style.size * 0.5f;
}

} // namespace shine::kit
