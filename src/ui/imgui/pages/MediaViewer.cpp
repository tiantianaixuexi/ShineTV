#include "ui/imgui/pages/MediaViewer.h"

#include "ui/imgui/kit/Image.h"
#include "ui/imgui/kit/Overlays.h"  // ScrimPaint / OverlayPanel（Widgets.h 不带它）
#include "ui/imgui/pages/PageCommon.h"       // LabelWidth / kDash
#include "ui/imgui/pages/WorkspacePages.h"   // WorkspaceToast / ShotCode

#include <algorithm>
#include <cmath>
#include <string>

namespace shine::pages {

using namespace shine::kit;

namespace {

// ---- 几何：逐条对着 FileViewer.jsx 的内联 style 与 ui.css ----
//
// ⚠️ JSX 内联样式会**覆盖** CSS（本仓库反复踩过），所以两处都要看：
//   `.modal`            宽 min(560,100vw-48) / 高 max min(640,100vh-64)（ui.css:943-954）
//   FileViewer.jsx:155  **内联覆写**成 width 720 / height min(78vh,700px)
constexpr float kViewerWidth = 720.0f;
constexpr float kViewerMaxHeight = 700.0f;
constexpr float kViewerHeightRatio = 0.78f;
// `.modal-h`：padding 14 上下 + 行盒 18 + 1px 下边（ui.css:955-965）
constexpr float kHeaderH = 14.0f * 2.0f + 18.0f + 1.0f;
// `.modal-f`：padding 12 上下 + 行盒 18 + 1px 上边（ui.css:970-975）
constexpr float kFooterH = 12.0f * 2.0f + 18.0f + 1.0f;
constexpr float kBodyPad = 18.0f;  // `.modal-b` padding 18

// `.fv-stage`（ui.css:567-578）：16/10 / max-width 620 / r-md / 1px line-normal
constexpr float kStageMaxW = 620.0f;
constexpr float kStageAspect = 16.0f / 10.0f;
constexpr float kStageRadius = 10.0f;  // --r-md
// `.fv-tools`（ui.css:590-603）：right 10 / bottom 10 / padding 4 / gap 2 / 按钮 22
constexpr float kToolsInset = 10.0f;
constexpr float kToolsPad = 4.0f;
constexpr float kToolsGap = 2.0f;
constexpr float kToolsBtn = 22.0f;
// `.fv-lens`（ui.css:579-589）：160 圆 / 2px accent 边 + 3px accent-dim 外圈 / 内容 3×
constexpr float kLensR = 80.0f;
constexpr float kLensZoom = 3.0f;

constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 8.0f;
constexpr float kZoomStep = 1.12f;
constexpr float kPanMargin = 24.0f;

// `.sheet`（ui.css:604-636）
constexpr float kSheetPad = 12.0f;
constexpr float kSheetGap = 10.0f;
constexpr float kSheetBarPadY = 10.0f;
constexpr float kSheetBarPadX = 14.0f;
constexpr float kSheetLabelPadY = 6.0f;
constexpr float kSheetLabelPadX = 10.0f;
constexpr float kSheetLabelFont = theme::font::kTiny;  // `.sheet-label` 12px/700（ui.css:633 附近）

struct ViewerState {
    bool open = false;
    bool sheet = false;
    bool exportButtons = false;
    std::string name;  // 联系表的标题（图片模式下头部显示**当前页**的文件名）
    std::vector<ViewerImage> gallery;
    std::vector<ViewerSheetCell> cells;
    int index = 0;
    // 打开它的那一帧（`ImGui::GetFrameCount()`）。「点外面关闭」要靠它区分
    // 「用户点了遮罩」与「用户点的是那个入口，而入口本来就在面板之外」。
    int openFrame = -1;
    float zoom = 1.0f;  // 相对「适应窗口」（= `.fv-stage` 铺满那一档）
    float panX = 0.0f;
    float panY = 0.0f;
    bool lens = false;
    // 上一帧的几何。判据与输入都读这一份 —— 不许在外面照 720/min(78vh,700) 重算
    // （`SettingsCloseRect()` 那次分叉的教训）。
    Rect frame{};
    Rect closeR{};
    Rect prevR{};
    Rect nextR{};
    Rect stage{};
    Rect tools{};
};

ViewerState& State() {
    static ViewerState s;
    return s;
}

int PageCount(const ViewerState& s) {
    if (s.sheet) {
        return s.cells.empty() ? 0 : 1;
    }
    return static_cast<int>(s.gallery.size());
}

void ClampIndex(ViewerState& s) {
    const int n = PageCount(s);
    s.index = n <= 0 ? 0 : std::clamp(s.index, 0, n - 1);
}

void ResetView(ViewerState& s) {
    s.zoom = 1.0f;
    s.panX = 0.0f;
    s.panY = 0.0f;
}

// 平移上限：图比舞台小时最多挪到「离边 24px」；比舞台大时允许再出去 1/4 舞台。
// 判据与 `gallery::viewer::ClampPan` 同一套语义（那边是模型层，这边是工程内文件那一支）。
void ClampPanView(ViewerState& s) {
    const float drawW = s.stage.width() * s.zoom;
    const float drawH = s.stage.height() * s.zoom;
    const float maxX = drawW <= s.stage.width()
                           ? (s.stage.width() - drawW) * 0.5f + kPanMargin
                           : (drawW - s.stage.width()) * 0.5f + s.stage.width() * 0.25f;
    const float maxY = drawH <= s.stage.height()
                           ? (s.stage.height() - drawH) * 0.5f + kPanMargin
                           : (drawH - s.stage.height()) * 0.5f + s.stage.height() * 0.25f;
    s.panX = std::clamp(s.panX, -maxX, maxX);
    s.panY = std::clamp(s.panY, -maxY, maxY);
}

// 当前页要画的矩形：以舞台中心为锚，按 zoom 缩放再平移。
Rect DrawnRect(const ViewerState& s) {
    const float w = s.stage.width() * s.zoom;
    const float h = s.stage.height() * s.zoom;
    return RectAt(s.stage.center().x - w * 0.5f + s.panX, s.stage.center().y - h * 0.5f + s.panY, w,
                  h);
}

// 缩放**围绕舞台中心**（滚轮那支围绕光标，见 DrawStage 里那段）。
void ZoomBy(ViewerState& s, float factor) {
    const float z = std::clamp(s.zoom * factor, kZoomMin, kZoomMax);
    if (z == s.zoom) {
        return;
    }
    const float k = z / s.zoom;
    s.panX *= k;
    s.panY *= k;
    s.zoom = z;
    ClampPanView(s);
}

std::string CurrentName(const ViewerState& s) {
    if (s.sheet) {
        return s.name.empty() ? std::string(kDash) : s.name;
    }
    if (s.index >= 0 && s.index < static_cast<int>(s.gallery.size())) {
        const std::string& n = s.gallery[static_cast<std::size_t>(s.index)].name;
        if (!n.empty()) {
            return n;
        }
    }
    return s.name.empty() ? std::string(kDash) : s.name;
}

// ---- 头部：icon + 等宽 14 文件名 + 类型 Tag + 翻页器 + 复制/另存为 + × ----
// （FileViewer.jsx:156-171 的 `.modal-h`）
void DrawHeader(ViewerState& s, ImDrawList* draw) {
    const Rect header = RectAt(s.frame.min.x, s.frame.min.y, s.frame.width(), kHeaderH);
    const float cy = header.center().y;
    draw->AddLine(ImVec2(header.min.x, header.max.y - 0.5f), ImVec2(header.max.x, header.max.y - 0.5f),
                  ColorLineSubtle(), 1.0f);

    // 右侧从右往左摆：× → 另存为 → 复制 → 翻页器
    float rx = header.max.x - 18.0f;
    s.closeR = RectAt(rx - 22.0f, cy - 11.0f, 22.0f, 22.0f);
    if (IconButton(draw, s.closeR, "x", false, false, "viewer-close", "关闭 (Esc)")) {
        CloseMediaViewer();
    }
    rx -= 22.0f + 8.0f;

    // 「复制 / 另存为」是设计稿里的 ghost sm 按钮。业务层没有「复制到剪贴板 / 写出
    // 图片文件」的接口，所以点了只发 toast 说清楚 —— 与出图页「批量出图」同一条纪律：
    // 画一个点了没反应的主按钮是最差的一种，而把原因写死在别处又看不见。
    struct HeaderButton {
        const char* id;
        const char* label;
        const char* icon;
        const char* toast;
    };
    static const HeaderButton kHeaderButtons[2] = {
        {"viewer-save", "另存为", "upload", "另存为：业务层还没有「写出图片文件」的接口（演示）"},
        {"viewer-copy", "复制", "download", "复制到剪贴板：业务层没有这条接口（演示）"},
    };
    for (const HeaderButton& def : kHeaderButtons) {
        ButtonSpec spec;
        spec.variant = ButtonVariant::Ghost;
        spec.size = ButtonSize::Small;
        spec.icon = def.icon;
        const float w = ButtonWidth(
            ButtonSize::Small, 13.0f,
            LabelWidth(FontBoldAt(theme::font::kTiny), theme::font::kTiny, def.label));
        const Rect r = RectAt(rx - w, cy - 12.0f, w, 24.0f);
        if (Button(draw, r, def.label, spec, def.id)) {
            WorkspaceToast(def.toast);
        }
        rx -= w + 8.0f;
    }

    const int count = PageCount(s);
    s.prevR = Rect{};
    s.nextR = Rect{};
    if (!s.sheet && count > 1) {
        // `.mono.tiny.dim` 的 `i/N`（FileViewer.jsx:164）。11.5 = kLabel 档（等宽小字）。
        ImFont* mono = MonoAt(theme::font::kLabel);
        const std::string page = kit::Fmt("{}/{}", s.index + 1, count);
        const float pw = mono->CalcTextSizeA(theme::font::kLabel, 1e9f, 0.0f, page.data(),
                                             page.data() + page.size())
                             .x;
        s.nextR = RectAt(rx - 22.0f, cy - 11.0f, 22.0f, 22.0f);
        if (IconButton(draw, s.nextR, "chevron", false, false, "viewer-next", "下一张 (→)")) {
            MediaViewerStep(1);
        }
        rx -= 22.0f + 2.0f;
        // R3 收编：裸 AddText → kit::Text（token + 字面 + 色全在 style 里，
        // scale=1.0 时与原先的 AddText 逐位一致 —— Text.h 的像素不变契约）。
        const TextStyle pageStyle{.size = theme::font::kLabel, .mono = true,
                                  .color = ColorTextMuted()};
        kit::Text(draw, ImVec2(rx - pw, CenterTextY(mono, theme::font::kLabel, cy)), page,
                  pageStyle);
        rx -= pw + 2.0f;
        // ⚠️ 设计稿这里用 CSS `transform: rotate(180deg)` 把同一个 chevron 掉头；
        //    本版 Icon 不支持旋转（`DrawIcon` 是按 24 网格直接描路径），所以退回
        //    **另一个方向的** chevron 顶点。两者都是「指向左」，视觉一致。
        s.prevR = RectAt(rx - 22.0f, cy - 11.0f, 22.0f, 22.0f);
        if (IconButton(draw, s.prevR, "chevron-left", false, false, "viewer-prev", "上一张 (←)")) {
            MediaViewerStep(-1);
        }
        rx -= 22.0f + 8.0f;
    }

    float x = s.frame.min.x + 18.0f;
    DrawIcon(draw, s.sheet ? "grid" : "image", ImVec2(x, cy - 8.5f), 17.0f, ColorAccent());
    x += 17.0f + 10.0f;

    const char* tagLabel = s.sheet ? "联系表" : "图片";
    const float tagH = TagHeight(/*small=*/true);
    const float tagW = TagWidth(tagLabel, /*small=*/true, /*dot=*/false);
    ImFont* mono = MonoAt(theme::font::kTitle);  // FileViewer.jsx:158 的 fontFamily: var(--font-mono) / 14
    const float textMax = std::max(0.0f, rx - x - 10.0f - tagW - 10.0f);
    const std::string title = CurrentName(s);
    // 窗口窄到连标题都放不下时（左边的 icon + 右边的按钮已经占满）**只画图标**，
    // 不画标题与类型 Tag —— 画出去会插进那排按钮里，比不画更糟。
    if (textMax > 40.0f) {
        const float tw = MeasureClipped(mono, theme::font::kTitle, textMax, title);
        DrawTextClipped(draw, mono, theme::font::kTitle,
                        ImVec2(x, CenterTextY(mono, theme::font::kTitle, cy)), textMax, ColorText(),
                        title);
        if (textMax - tw > tagW + 10.0f) {
            Tag(draw, RectAt(x + tw + 10.0f, cy - tagH * 0.5f, tagW, tagH), tagLabel,
                s.sheet ? theme::Tone::Accent : theme::Tone::Ok, /*small=*/true, /*dot=*/false);
        }
    }
}

// ---- 图片舞台：`.fv-stage` + 缩放/平移 + `.fv-tools` + `.fv-lens` ----
void DrawStage(ViewerState& s, ImDrawList* draw) {
    const ViewerImage& cur = s.gallery[static_cast<std::size_t>(s.index)];
    // ⚠️ **每帧无条件请求**（`kit/Image.h` 约束 1）：只在 `State==NotLoaded` 时请求
    //    的话，纹理被 GPU LRU 淘汰后这张大图**永远不会再出现** —— 界面上表现为
    //    「图闪过一次就没了」，不报错、也不崩。
    const ImageSlot slot = RequestImageFull(cur.path);
    const ImGuiIO& io = ImGui::GetIO();
    const bool overTools = s.tools.contains(io.MousePos);
    const bool overStage = s.stage.contains(io.MousePos) && !overTools;

    // 滚轮：以**光标**为锚点缩放（设计稿 `.fv-stage` 的 onWheel，FileViewer.jsx:72-84）
    if (overStage && io.MouseWheel != 0.0f) {
        const float factor = io.MouseWheel > 0.0f ? kZoomStep : 1.0f / kZoomStep;
        const float z = std::clamp(s.zoom * factor, kZoomMin, kZoomMax);
        if (z != s.zoom) {
            const float cx = io.MousePos.x - s.stage.center().x - s.panX;
            const float cyy = io.MousePos.y - s.stage.center().y - s.panY;
            const float k = z / s.zoom - 1.0f;
            s.panX -= cx * k;
            s.panY -= cyy * k;
            s.zoom = z;
            ClampPanView(s);
        }
    }
    // 拖动平移。⚠️ 只看「当前帧鼠标在舞台内」——设计稿用的是 pointer capture，
    //    本版不注入捕获事件，于是拖出舞台边界就停手（比捕获少一点手感，不会错位）。
    if (overStage && io.MouseDown[0] && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        s.panX += io.MouseDelta.x;
        s.panY += io.MouseDelta.y;
        ClampPanView(s);
    }

    // 底 + 1px 描边（`.fv-stage` 的 border: 1px solid var(--line-normal) + r-md）
    DrawRoundRect(draw, s.stage.min, s.stage.max, kStageRadius, ColorFillMuted(), ColorLineNormal(),
                  1.0f);

    const Rect dr = DrawnRect(s);
    // 矩形裁剪：`.fv-stage` 的 overflow:hidden。⚠️ 本版 ImGui 没有 push clip path
    //    （design-spec §9.3），所以四个圆角裁不掉 —— 与资产卡 / HubCard 封面同一个
    //    已知降级。
    draw->PushClipRect(s.stage.min, s.stage.max, true);
    if (slot.ready()) {
        DrawImage(draw, dr, slot, 0.0f, ImageFit::Cover);
    } else {
        // 解码还没落地（或文件坏了）：画设计稿那张 `Art` 占位（`seed={cur.art}`），
        // 与资产网格卡 / 派生链节点同一条「真图优先」的次序。
        Art(draw, dr, cur.seed, /*cover=*/true);
    }

    // `.fv-lens`：160px 圆、3× 放大、跟着光标。**画在舞台裁剪之内** —— 设计稿的
    // `.fv-lens` 在 `.fv-stage` 里，圆到边会被 stage 的 overflow:hidden 切掉。
    if (s.lens && slot.ready() && s.stage.contains(io.MousePos)) {
        ImVec2 a;
        ImVec2 b;
        ImageUvForFit(slot, dr, ImageFit::Cover, a, b);
        const float halfPx = kLensR / kLensZoom;  // 屏幕坐标下要采样的半宽
        const float hu = (b.x - a.x) * (halfPx / dr.width());
        const float hv = (b.y - a.y) * (halfPx / dr.height());
        const float cu = a.x + (io.MousePos.x - dr.min.x) / dr.width() * (b.x - a.x);
        const float cv = a.y + (io.MousePos.y - dr.min.y) / dr.height() * (b.y - a.y);
        const Rect lens = RectAt(io.MousePos.x - kLensR, io.MousePos.y - kLensR, kLensR * 2.0f,
                                 kLensR * 2.0f);
        // 圆：本版没有圆形裁剪路径，但 `AddImageRounded` 的 rounding 取**一半边长**
        // 画出来就是一个圆（圆角矩形退化成圆）。
        const ImTextureRef ref(slot.texture);
        draw->AddImageRounded(ref, lens.min, lens.max, ImVec2(cu - hu, cv - hv),
                              ImVec2(cu + hu, cv + hv), IM_COL32_WHITE, kLensR);
        // border: 2px accent + box-shadow: 0 0 0 3px accent-dim（ui.css:586-587）
        draw->AddCircle(io.MousePos, kLensR - 1.0f, ColorAccent(), 0, 2.0f);
        draw->AddCircle(io.MousePos, kLensR + 2.5f, ColorAccentDim(), 0, 3.0f);
    }
    draw->PopClipRect();

    // `.fv-tools`：right/bottom 10、pad 4、gap 2。宽度里给百分比留**固定**一档 ——
    // 让它按实际位数伸缩的话，缩放时整条工具条会左右跳。
    ImFont* mono = MonoAt(theme::font::kLabel);  // 百分比：等宽 11.5（kLabel 档），与翻页器同档
    const char* pctProbe = "8888%";
    const float pctW =
        mono->CalcTextSizeA(theme::font::kLabel, 1e9f, 0.0f, pctProbe, pctProbe + 5).x + 12.0f;
    const float toolsH = kToolsBtn + kToolsPad * 2.0f;
    const float toolsW = kToolsPad * 2.0f + kToolsBtn * 4.0f + kToolsGap * 3.0f + pctW;
    s.tools = RectAt(s.stage.max.x - kToolsInset - toolsW, s.stage.max.y - kToolsInset - toolsH,
                     toolsW, toolsH);
    // `--glass` 实色（backdrop-filter 是已知降级）+ 1px line-subtle + r-md
    DrawRoundRect(draw, s.tools.min, s.tools.max, kStageRadius, GlassColor(), ColorLineSubtle(),
                  1.0f);

    float bx = s.tools.min.x + kToolsPad;
    const float by = s.tools.center().y - kToolsBtn * 0.5f;
    if (IconButton(draw, RectAt(bx, by, kToolsBtn, kToolsBtn), "plus", false, false,
                   "viewer-zoom-in", "放大")) {
        ZoomBy(s, kZoomStep);
    }
    bx += kToolsBtn + kToolsGap;
    // ⚠️ 设计稿这里用的是 `x` 当减号（FileViewer.jsx:111）—— 那是设计稿自身的缺陷，
    //    `Icon.jsx` 里 `x` 是叉。这里用真的 `minus`，别照抄。
    if (IconButton(draw, RectAt(bx, by, kToolsBtn, kToolsBtn), "minus", false, false,
                   "viewer-zoom-out", "缩小")) {
        ZoomBy(s, 1.0f / kZoomStep);
    }
    bx += kToolsBtn + kToolsGap;
    if (IconButton(draw, RectAt(bx, by, kToolsBtn, kToolsBtn), "target", false, false,
                   "viewer-fit", "适应窗口")) {
        ResetView(s);
    }
    bx += kToolsBtn + kToolsGap;
    if (IconButton(draw, RectAt(bx, by, kToolsBtn, kToolsBtn), "search", s.lens, false,
                   "viewer-lens", "放大镜 3×")) {
        s.lens = !s.lens;
    }
    bx += kToolsBtn + kToolsGap;
    const std::string pct = kit::Fmt("{}%", static_cast<int>(std::lround(s.zoom * 100.0f)));
    const float pctTextW =
        mono->CalcTextSizeA(theme::font::kLabel, 1e9f, 0.0f, pct.data(), pct.data() + pct.size()).x;
    // R3 收编：裸 AddText → kit::Text（同翻页器那一处，逐位等价）。
    const TextStyle pctStyle{.size = theme::font::kLabel, .mono = true,
                             .color = ColorTextMuted()};
    kit::Text(draw,
              ImVec2(bx + (pctW - pctTextW) * 0.5f,
                     CenterTextY(mono, theme::font::kLabel, s.tools.center().y)),
              pct, pctStyle);
}

// ---- 联系表：`.sheet` + `.sheet-bar` + `.sheet-grid`（2 列）+ `.sheet-cell` ----
void DrawSheet(ViewerState& s, ImDrawList* draw, const Rect body) {
    const int n = static_cast<int>(s.cells.size());
    if (n <= 0) {
        return;
    }
    const int columns = 2;  // `grid-template-columns: 1fr 1fr`（ui.css:620）
    const int rows = (n + columns - 1) / columns;

    ImFont* labelFont = FontBoldAt(kSheetLabelFont);
    const float labelH = kSheetLabelPadY * 2.0f + LineBox(kSheetLabelFont) + 1.0f;
    // 面板高度是死的（720 × min(78vh,700)），所以格子里的图高要**按剩余空间收口**，
    // 不能让第二行溢出面板 —— 那正是「列表按容器高静默截断」的同一族问题。
    const float barH = kSheetBarPadY * 2.0f + LineBox(theme::font::kHint) + 1.0f;
    const float footH = s.exportButtons ? (ButtonHeight(ButtonSize::Small) + 12.0f) : 0.0f;
    const float avail = body.height() - barH - footH;
    const float cellInner = (avail - kSheetPad * 2.0f - kSheetGap * static_cast<float>(rows - 1)) /
                            static_cast<float>(rows);
    const float thumbH = std::clamp(cellInner - labelH, 60.0f, 150.0f);
    const float cellH = thumbH + labelH;

    const float gridW = body.width();
    const float cellW = (gridW - kSheetPad * 2.0f - kSheetGap * static_cast<float>(columns - 1)) /
                        static_cast<float>(columns);
    const float sheetH = barH + kSheetPad * 2.0f + static_cast<float>(rows) * cellH +
                         kSheetGap * static_cast<float>(rows - 1);
    // ⚠️ 设计稿这块是 `.modal-b`（flex 列）里的 `<div style={{ margin:'auto', width:'100%' }}>`
    //    —— `margin:auto` 在 flex 列里是**垂直居中**，脚部那两个按钮跟着它一起居中
    //    （FileViewer.jsx:192-198）。原来这里是贴顶摆的，与设计稿差一截。
    const float buttonH = ButtonHeight(ButtonSize::Small);
    const float blockH = sheetH + (s.exportButtons ? (12.0f + buttonH) : 0.0f);
    const float blockTop = body.min.y + std::max(0.0f, (body.height() - blockH) * 0.5f);
    const Rect sheet = RectAt(body.min.x, blockTop, gridW, sheetH);
    // 底 + 1px line-normal + r-md
    DrawRoundRect(draw, sheet.min, sheet.max, kStageRadius, ColorPanel(), ColorLineNormal(), 1.0f);

    // `.sheet-bar`：fill-muted 底 + padding 10/14 + 1px 下边
    const Rect bar = RectAt(sheet.min.x, sheet.min.y, sheet.width(), barH);
    draw->AddRectFilled(bar.min, ImVec2(bar.max.x, bar.max.y - 1.0f), ColorFillMuted());
    draw->AddLine(ImVec2(bar.min.x, bar.max.y - 0.5f), ImVec2(bar.max.x, bar.max.y - 0.5f),
                  ColorLineNormal(), 1.0f);
    ImFont* barFont = FontBoldAt(theme::font::kHint);
    const ImVec2 barTextPos(bar.min.x + kSheetBarPadX,
                            CenterTextY(barFont, theme::font::kHint, bar.min.y + barH * 0.5f));
    DrawTextClipped(draw, barFont, theme::font::kHint, barTextPos, bar.width() * 0.55f, ColorText(),
                    CurrentName(s));
    // 右侧 `.tiny.dim`（FileViewer.jsx:49，11px = kMicro 档）：写的是设计稿那份整版
    // 尺寸文案。我们的整版是**按需拼的格子**，没有「一张 1536×1024 的成品」这回事 —— 照实说。
    DrawTextClipped(draw, FontAt(theme::font::kMicro), theme::font::kMicro,
                    ImVec2(bar.min.x + bar.width() * 0.55f,
                           CenterTextY(FontAt(theme::font::kMicro), theme::font::kMicro,
                                       bar.min.y + barH * 0.5f)),
                    bar.width() * 0.45f - kSheetBarPadX, ColorTextMuted(),
                    "ShineTV Studio · 设定集整版 · 逐层取证");

    for (int i = 0; i < n; ++i) {
        const ViewerSheetCell& cell = s.cells[static_cast<std::size_t>(i)];
        const int col = i % columns;
        const int row = i / columns;
        const Rect box = RectAt(sheet.min.x + kSheetPad + static_cast<float>(col) * (cellW + kSheetGap),
                                bar.max.y + kSheetPad + static_cast<float>(row) * (cellH + kSheetGap),
                                cellW, cellH);
        // `.sheet-cell`：fill-muted 底 + 1px line-subtle + r-sm + overflow hidden
        DrawRoundRect(draw, box.min, box.max, 6.0f, ColorFillMuted(), ColorLineSubtle(), 1.0f);
        const Rect thumb = RectAt(box.min.x, box.min.y, box.width(), thumbH);
        // 格子里的图走**缩略图那一档**（512）：格子最宽 ~330px，缩略图够用，
        // 而且网格页早就请求过同一批路径，这里不会多解一张。
        const ImageSlot slot = RequestImage(cell.path);
        if (slot.ready()) {
            DrawImage(draw, thumb, slot, 0.0f, ImageFit::Cover);
        } else {
            Art(draw, thumb, cell.seed, /*cover=*/true);
        }
        // `.sheet-label`：padding 6/10 + 12px/700 secondary + 1px 上边
        const Rect label = RectAt(box.min.x, thumb.max.y, box.width(), labelH);
        draw->AddLine(ImVec2(label.min.x, label.min.y + 0.5f), ImVec2(label.max.x, label.min.y + 0.5f),
                      ColorLineSubtle(), 1.0f);
        DrawTextClipped(draw, labelFont, kSheetLabelFont,
                        ImVec2(label.min.x + kSheetLabelPadX,
                               CenterTextY(labelFont, kSheetLabelFont, label.center().y)),
                        label.width() - kSheetLabelPadX * 2.0f, ColorTextSecondary(), cell.label);
    }

    // 表格下方的两个按钮（FileViewer.jsx:194-197，`marginTop: 12`）。与头部那两颗
    // 同一条纪律：业务层没有「拼版导出 / 重生成整版」的接口，点了只发 toast 说清楚。
    if (s.exportButtons) {
        struct SheetButton {
            const char* id;
            const char* label;
            const char* icon;
            bool primary;
            const char* toast;
        };
        static const SheetButton kSheetButtons[2] = {
            {"viewer-export", "导出 PNG", "download", true,
             "导出整版：业务层还没有「拼版写出 PNG」的接口（演示）"},
            {"viewer-regenerate", "重新生成整版", "refresh", false,
             "重新生成整版：要提交 V0 派生链重跑，业务层没有这条前端接口（演示）"},
        };
        float totalW = 0.0f;
        float widths[2] = {0.0f, 0.0f};
        for (int i = 0; i < 2; ++i) {
            widths[i] = ButtonWidth(
                ButtonSize::Small, 13.0f,
                LabelWidth(FontBoldAt(theme::font::kTiny), theme::font::kTiny,
                           kSheetButtons[i].label));
            totalW += widths[i];
        }
        totalW += 8.0f;
        float bx = sheet.center().x - totalW * 0.5f;
        const float by = sheet.max.y + 12.0f;  // `marginTop: 12`
        for (int i = 0; i < 2; ++i) {
            ButtonSpec spec;
            spec.variant = kSheetButtons[i].primary ? ButtonVariant::Primary : ButtonVariant::Secondary;
            spec.size = ButtonSize::Small;
            spec.icon = kSheetButtons[i].icon;
            const Rect r = RectAt(bx, by, widths[i], ButtonHeight(ButtonSize::Small));
            if (Button(draw, r, kSheetButtons[i].label, spec, kSheetButtons[i].id)) {
                WorkspaceToast(kSheetButtons[i].toast);
            }
            bx += widths[i] + 8.0f;
        }
    }
}

void DrawFooter(ViewerState& s, ImDrawList* draw) {
    const Rect footer = RectAt(s.frame.min.x, s.frame.max.y - kFooterH, s.frame.width(), kFooterH);
    draw->AddLine(ImVec2(footer.min.x, footer.min.y + 0.5f), ImVec2(footer.max.x, footer.min.y + 0.5f),
                  ColorLineSubtle(), 1.0f);
    const char* hint = s.sheet ? "联系表 · 每格是该层当前的产出图"
                               : "滚轮缩放 · 拖动平移 · ←→ 切图 · 放大镜 3×";
    // 页脚一行是 11px 最细档（kMicro）：FileViewer.jsx:205 的页脚说明。
    DrawTextClipped(draw, FontAt(theme::font::kMicro), theme::font::kMicro,
                    ImVec2(footer.min.x + 18.0f,
                           CenterTextY(FontAt(theme::font::kMicro), theme::font::kMicro,
                                       footer.center().y)),
                    footer.width() * 0.6f, ColorTextMuted(), hint);
    // 右侧：`Esc 关闭`（FileViewer.jsx:205）。Kbd 的盒高是 18（ui.css:44-46 的 h18），
    // 组件没有导出 KbdHeight，这里按同一条 CSS 量。
    constexpr float kKbdH = 18.0f;
    const float kbdW = KbdWidth("Esc");
    const float closeW = LabelWidth(FontAt(theme::font::kMicro), theme::font::kMicro, "关闭");
    const float x0 = footer.max.x - 18.0f - closeW - 6.0f - kbdW;
    Kbd(draw, RectAt(x0, footer.center().y - kKbdH * 0.5f, kbdW, kKbdH), "Esc");
    DrawTextClipped(draw, FontAt(theme::font::kMicro), theme::font::kMicro,
                    ImVec2(x0 + kbdW + 6.0f,
                           CenterTextY(FontAt(theme::font::kMicro), theme::font::kMicro,
                                       footer.center().y)),
                    closeW + 2.0f, ColorTextMuted(), "关闭");
}

} // namespace

// ------------------------------------------------------------------ 对外 API

void OpenMediaViewer(ViewerRequest request, int initialIndex) {
    ViewerState& s = State();
    s.gallery = std::move(request.gallery);
    s.cells = std::move(request.cells);
    s.name = std::move(request.name);
    s.sheet = !s.cells.empty();
    s.exportButtons = request.exportButtons;
    s.index = std::max(0, initialIndex);
    s.lens = false;
    s.open = true;
    s.openFrame = ImGui::GetFrameCount();
    ResetView(s);
    ClampIndex(s);
}

void CloseMediaViewer() {
    ViewerState& s = State();
    s.open = false;
    s.lens = false;
}

bool MediaViewerOpen() { return State().open; }
bool MediaViewerSheet() { return State().sheet; }
int MediaViewerIndex() { return State().index; }
int MediaViewerCount() { return PageCount(State()); }
std::string MediaViewerName() { return CurrentName(State()); }

void MediaViewerStep(int dir) {
    ViewerState& s = State();
    if (s.sheet || dir == 0) {
        return;
    }
    const int n = static_cast<int>(s.gallery.size());
    if (n <= 1) {
        return;
    }
    s.index = (s.index + (dir > 0 ? 1 : n - 1)) % n;
    // 设计稿在换图时把缩放复位（FileViewer.jsx:136 的 useEffect）。
    ResetView(s);
}

Rect MediaViewerRect() { return State().frame; }
Rect MediaViewerCloseRect() { return State().closeR; }
Rect MediaViewerNextRect() { return State().nextR; }
Rect MediaViewerPrevRect() { return State().prevR; }

// AssetViewerRequest / ShotViewerRequest / AssetSheetViewerRequest 三位构造器
// 住在 MediaViewer_Request.cpp：它们只读 `Book()` 拼请求，不碰 ViewerState 也不画 ——
// 与本文件的「状态 + 绘制」是两个读者（第二十二轮按职责拆文件）。

// ------------------------------------------------------------------ 绘制

void DrawMediaViewer(ImDrawList* draw) {
    ViewerState& s = State();
    if (!s.open || draw == nullptr) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Rect screen{0.0f, 0.0f, display.x, display.y};
    const float w = std::min(kViewerWidth, display.x - 48.0f);
    const float h = std::min(display.y * kViewerHeightRatio, kViewerMaxHeight);
    s.frame = RectAt(screen.center().x - w * 0.5f, screen.center().y - h * 0.5f, w, h);

    // ⚠️ 遮罩**只画不注册**（`kit::ScrimPaint`）：注册全屏热区会与面板里每个控件抢
    //    同一个 `HoveredId`（ImGui 先注册者独占），症状是「浮层里按钮点不动」——
    //    外观全对、编译过、静息截图零覆盖。「点外面关闭」手算 `contains`。
    ScrimPaint(draw, screen);
    OverlayPanel(draw, s.frame);

    // `.modal-b`：整条铺 `--fill-muted`（FileViewer.jsx:172 的内联 background）
    const Rect bodyStrip{s.frame.min.x + 1.0f, s.frame.min.y + kHeaderH, s.frame.max.x - 1.0f,
                         s.frame.max.y - kFooterH};
    draw->AddRectFilled(bodyStrip.min, bodyStrip.max, ColorFillMuted());
    const Rect body = RectAt(bodyStrip.min.x + kBodyPad, bodyStrip.min.y + kBodyPad,
                             bodyStrip.width() - kBodyPad * 2.0f,
                             bodyStrip.height() - kBodyPad * 2.0f);

    DrawHeader(s, draw);

    if (s.sheet) {
        s.stage = Rect{};
        s.tools = Rect{};
        DrawSheet(s, draw, body);
    } else if (!s.gallery.empty() && body.width() > 80.0f && body.height() > 60.0f) {
        // `.fv-stage`：16/10、max-width 620、居中（`margin:auto`）。
        // ⚠️ 窗口被拖到极小时 `body` 会退化成零/负尺寸，那时**不画**舞台 ——
        //    否则 `RectAt` 出来的是反向矩形（`max < min`），整块不画又进
        //    `NoteInvertedRect` 计数，看着像产品缺陷。
        float sw = std::min(kStageMaxW, body.width());
        float sh = sw / kStageAspect;
        if (sh > body.height()) {
            sh = body.height();
            sw = sh * kStageAspect;
        }
        s.stage = RectAt(body.center().x - sw * 0.5f, body.center().y - sh * 0.5f, sw, sh);
        // ⚠️ `s.tools` 是**上一帧**的矩形，这一帧的输入先按它排除「鼠标在工具条上」；
        //    画完工具条后被覆盖成这一帧的。差一帧只在「工具条刚好从鼠标下移进来」时
        //    才可能多吃一次滚轮，代价是一格缩放，不值得为它引入两遍布局。
        DrawStage(s, draw);
    }

    DrawFooter(s, draw);

    // Esc / ←→：键盘入口。Esc 由 `Shell::ApplyShortcuts` 收（那边是本工程唯一一份
    // 「浮层关闭键」的执行体，查看器排在它链子的**第一个**），这里只收 ←→。
    if (!s.sheet && PageCount(s) > 1) {
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
            MediaViewerStep(1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            MediaViewerStep(-1);
        }
    }

    // 「点外面关闭」（`e.target === e.currentTarget` 语义，Overlays.jsx:35/162）。
    // 手算落在面板外 —— 本工程的 `ImGui::IsMouseHoveringRect` 会 0xC0000005。
    //
    // ⚠️ **必须排除「打开它的那一帧」**：查看器的入口（派生链节点 / 基线图 /
    //    导出整版按钮）本来就在面板之外，而 `io.MouseClicked` 是**整帧**为真的。
    //    少了这道闸门，用户点一下入口 → 查看器在**同一帧**里被自己关掉，症状是
    //    「闪一下就没了」。这与命令面板的 `paletteJustOpened_` 是同一条坑
    //    （那边点顶栏搜索框打开面板，而那一次点击也在面板之外）。
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::GetFrameCount() != s.openFrame && !s.frame.contains(io.MousePos) &&
        io.MouseClicked[0]) {
        CloseMediaViewer();
    }
}

} // namespace shine::pages
