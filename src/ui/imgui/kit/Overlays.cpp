#include "ui/imgui/kit/Overlays.h"

#include <algorithm>
#include <cstdlib>

namespace shine::kit {

namespace {
// .modal-h 的头高：padding 14 上下 + 标题 18 + 1px 下边（ui.css:955-965）。
// Modal 与 ModalFrameRect **共用**这一份 —— 原来两处各写一遍字面量，
// 改一处忘另一处就是两个模态的头高不一样，而那在界面上看不出来。
constexpr float kModalHeaderH = 14.0f * 2.0f + 18.0f + 1.0f;
// .modal-f 的页脚高：padding 12 上下 + 按钮高 + 1px 上边（ui.css:970-975）。
constexpr float kModalFooterPad = 12.0f * 2.0f + 1.0f;
} // namespace

// ------------------------------------------------------------ ScrimPaint
void ScrimPaint(ImDrawList* draw, Rect screen) {
    // `screen` 必须是**整个屏幕**。命令面板原来传的是面板自己的 bounds，
    // 于是这张遮罩下一行就被 DrawShadowed 的面板底板整个盖住 —— 一次
    // 完全被覆盖的死绘制：面板照常显示，而背后该压暗的界面一点没暗，
    // 和其它所有模态都不一样。编译过、截图正常，只有「没生效」这一种表现。
    draw->AddRectFilled(screen.min, screen.max, ColorScrim());
}

// ------------------------------------------------------------ OverlayPanel
void OverlayPanel(ImDrawList* draw, Rect frame, float radius) {
    // ui.css:943-946 `.modal` 的底板：bg-overlay + 1px line-normal + r-lg14 +
    // box-shadow: var(--shadow-2)（**常驻**，不是 hover 才有的）。
    // 抽屉用 radius 0（ui.css:652 的 .drawer 是直角）。
    //
    // 抽出来的理由：这段三行字面量原本在 Modal / ModalFrameRect / Drawer 与页面层
    // 的命令面板里各写一遍，共四处。改一次圆角或描边色就有地方漏，而漏掉的那处
    // 编译照过、截图正常，只有「和别的浮层差一点」这种说不清的观感。
    DrawShadowed(draw, frame.min, frame.max, radius, ColorOverlay(), ColorLineNormal(), 1.0f,
                 theme::ShadowTier::Overlay);
}

// ------------------------------------------------------------------ Scrim
bool Scrim(ImDrawList* draw, Rect screen, std::string_view id) {
    ScrimPaint(draw, screen);
    // 点遮罩本体 = 关闭浮层（Overlays.jsx:35/162 的 `e.target === e.currentTarget`）。
    // id 交给调用方：同一帧里若有嵌套浮层（命令面板套在 modal 上），
    // 固定 ID 会让两个遮罩抢同一个 item。
    return HitTest(screen, id).clicked;
}

// --------------------------------------------------------------- ModalFrame
ModalFrame ModalFrameRect(ImDrawList* draw, Rect screen, std::string_view title,
                          std::string_view icon, float width, float height,
                          int footerButtons) {
    // 遮罩：`ScrimPaint` —— **只画，不注册命中**。
    //
    // ⚠️ 这里绝不能用 `kit::Scrim`（它在 ScrimPaint 之上还会注册一个全屏热区）。
    //    注册全屏热区会与面板内每一个控件的热区**重叠**，而 ImGui 同窗口内
    //    先注册者独占 HoveredId（imgui.cpp:5161）—— 于是**谁先注册谁活**：
    //      · 外壳先画、按钮后画的浮层（报告模态：ModalFrameRect 在前，
    //        「导出 / ×」按钮在后）⇒ 遮罩抢走 HoveredId，按钮全部按不动；
    //      · 对话框最后画的（项目中心：卡片与工具条在前，ModalFrameRect 在后）
    //        ⇒ 遮罩自己拿不到 HoveredId，按钮仍然可点，但**重叠是真的**。
    //    两种都错，且「哪种」取决于绘制顺序 —— 靠读代码判断顺序极易搞反
    //    （第一版就以为项目中心也会中招，结果它不会）。
    //    症状全程沉默：遮罩和按钮**外观都画得好好的**，编译过、截图正常、
    //    manifest 记 saved，只有按钮按不动。
    //
    //    正确做法只有一条：**遮罩不参与命中**。「点外面关闭」由调用方拿返回的
    //    frame **手算**点击是否落在面板外 —— 既不抢 HoveredId，也避开了本工程
    //    会 0xC0000005 的 `ImGui::IsMouseHoveringRect`。
    //
    //    判据：`kit::DuplicateHitCount`（进 overall）。把这一行换成 Scrim 实测
    //    duplicate-hits 从 0 变 6232 —— 这条兜底就是为它准备的。
    ScrimPaint(draw, screen);
    const float w = width > 0.0f ? width : std::min(560.0f, screen.width() - 48.0f);
    const float h = height > 0.0f ? height : std::min(640.0f, screen.height() - 64.0f);
    const Rect frame = RectAt(screen.center().x - w * 0.5f, screen.center().y - h * 0.5f, w, h);
    OverlayPanel(draw, frame);

    float top = frame.min.y;
    const bool hasHeader = !title.empty();
    if (hasHeader) {
        const float iconSize = 17.0f;
        float x = frame.min.x + 18.0f;
        if (!icon.empty()) {
            DrawIcon(draw, icon, ImVec2(x, top + 15.0f), iconSize, ColorAccent());
            x += iconSize + 10.0f;
        }
        ImFont* font = FontBoldAt(15.0f);
        // 标题按**头部中心**落字（15px 字该落在 `top + 23.5 - 7.5`）——
        // 原来写死 `top + 14.0f` 是把 padding 当成了文字偏移，偏上 2.0px。
        DrawTextClipped(draw, font, 15.0f, ImVec2(x, CenterTextY(font, 15.0f, top + kModalHeaderH * 0.5f)),
                        frame.max.x - x - 18.0f, ColorText(), title);
        top += kModalHeaderH;
        draw->AddLine(ImVec2(frame.min.x, top - 0.5f), ImVec2(frame.max.x, top - 0.5f),
                      ColorLineSubtle(), 1.0f);
    }

    float bottom = frame.max.y;
    ModalFrame out;
    if (footerButtons > 0) {
        const float footerH = kModalFooterPad + ButtonHeight(ButtonSize::Medium);
        bottom -= footerH;
        out.footer = RectAt(frame.min.x + 18.0f, bottom + 12.0f, w - 36.0f,
                            ButtonHeight(ButtonSize::Medium));
        draw->AddLine(ImVec2(frame.min.x, bottom + 0.5f), ImVec2(frame.max.x, bottom + 0.5f),
                      ColorLineSubtle(), 1.0f);
    }

    out.frame = frame;
    out.header = RectAt(frame.min.x, frame.min.y, frame.width(), hasHeader ? kModalHeaderH : 0.0f);
    out.body = RectAt(frame.min.x + 18.0f, top + 18.0f, w - 36.0f,
                      std::max(0.0f, bottom - 18.0f - (top + 18.0f)));
    return out;
}

// ------------------------------------------------------------------ Drawer
float DrawerWidth() {
    // .drawer（ui.css:652）：width 390。
    return 390.0f;
}

Rect Drawer(ImDrawList* draw, Rect screen, std::string_view title, std::string_view icon,
            int footerButtons, Rect* footerOut) {
    ScrimPaint(draw, screen);
    const float w = std::min(DrawerWidth(), screen.width());
    const Rect frame = RectAt(screen.max.x - w, screen.min.y, w, screen.height());
    // ui.css:655 `.drawer` 的 box-shadow: var(--shadow-2)。
    OverlayPanel(draw, frame, 0.0f);

    // .drawer-h（ui.css:664-672）：padding 14/16 + gap 10 + 1px 下边，标题 14/700。
    const float iconSize = 17.0f;
    float x = frame.min.x + 16.0f;
    float top = frame.min.y;
    if (!icon.empty()) {
        DrawIcon(draw, icon, ImVec2(x, top + 15.0f), iconSize, ColorAccent());
        x += iconSize + 10.0f;
    }
    ImFont* font = FontBoldAt(14.0f);
    // 头高抽成常量（值与原来 `top += 14*2 + 18 + 1` 完全相同，不是改几何），
    // 这样标题按中心算时可以引用它，而不是写死 23.5。
    const float headerH = 14.0f * 2.0f + 18.0f + 1.0f;
    // 原来写死 `top + 14.0f` —— 那个 14 是 `.drawer-h` 的 **padding**，被当成了
    // 文字偏移：头高 47、14px 字该落在 `top + 23.5 - 7.0`，原值偏上 2.5px。
    DrawTextClipped(draw, font, 14.0f, ImVec2(x, CenterTextY(font, 14.0f, top + headerH * 0.5f)),
                    frame.max.x - x - 16.0f, ColorText(), title);
    top += headerH;
    draw->AddLine(ImVec2(frame.min.x, top - 0.5f), ImVec2(frame.max.x, top - 0.5f),
                  ColorLineSubtle(), 1.0f);

    // .drawer-f（ui.css:681-686）：padding 12/16 + gap 8 + 1px 上边。
    float bottom = frame.max.y;
    if (footerOut != nullptr) {
        const float footerH = 12.0f * 2.0f + ButtonHeight(ButtonSize::Medium) + 1.0f;
        bottom -= footerH;
        *footerOut = RectAt(frame.min.x + 16.0f, bottom + 12.0f, w - 32.0f,
                            ButtonHeight(ButtonSize::Medium));
        draw->AddLine(ImVec2(frame.min.x, bottom + 0.5f), ImVec2(frame.max.x, bottom + 0.5f),
                      ColorLineSubtle(), 1.0f);
        (void)footerButtons; // 按钮由调用方按 footerOut 摆，组件不代画
    }

    // .drawer-b（ui.css:673-680）：padding 16。
    return RectAt(frame.min.x + 16.0f, top + 16.0f, w - 32.0f,
                  std::max(0.0f, bottom - 16.0f - (top + 16.0f)));
}

// ------------------------------------------------------------------ Toast
float Toast(ImDrawList* draw, Rect screen, std::string_view text, theme::Tone tone,
            std::string_view icon, float above, float alpha) {
    const float padX = 14.0f;
    const float padY = 10.0f;
    const float borderLeft = 3.0f;
    ImFont* font = FontAt(12.5f);
    const float iconSize = 15.0f;
    const float textWidth =
        font->CalcTextSizeA(12.5f, 1e9f, 0.0f, text.data(), text.data() + text.size()).x;
    const float gap = icon.empty() ? 0.0f : iconSize + 10.0f;

    // min-width 260 / max-width 380（ui.css:991-992）。
    float w = textWidth + gap + padX * 2.0f + borderLeft;
    w = std::clamp(w, 260.0f, 380.0f);
    const float h = 12.5f + padY * 2.0f + 2.0f;
    // right 16 / bottom 40（ui.css:980-981）；above 用来往上叠多条。
    const Rect frame = RectAt(screen.max.x - kToastRight - w,
                              screen.max.y - kToastBottom - above - h, w, h);
    // ui.css:998 `.toast` 的 box-shadow: var(--shadow-2)。
    // alpha < 1 时**本体与投影一起淡**（阴影单独一层 alphaScale），否则尾巴
    // 淡出后影子还挂着 —— 那是页面层旧副本的做法，抽上来时保留。
    if (alpha >= 1.0f) {
        DrawShadowed(draw, frame.min, frame.max, 10.0f, ColorOverlay(), ColorLineNormal(), 1.0f,
                     theme::ShadowTier::Overlay);
    } else {
        DrawShadow(draw, frame.min, frame.max, 10.0f, theme::ShadowTier::Overlay, alpha);
        DrawRoundRect(draw, frame.min, frame.max, 10.0f, WithAlpha(ColorOverlay(), alpha),
                      WithAlpha(ColorLineNormal(), alpha), 1.0f);
    }
    // border-left: 3px <status>（ui.css:996 + .ok/.warn/.err 覆盖 ui.css:1003-1005）。
    // 画成一个 3px 宽的圆角矩形：Toast 左角是 r10 的圆，直接画直角条会戳出弧外，
    // 所以把条的上下各内缩 2px 让它落在圆角以内。
    const ImU32 accent = tone == theme::Tone::Idle ? ColorOf(theme::Current().accentInfo)
                                                    : ToneColor(tone);
    DrawRoundRect(draw, ImVec2(frame.min.x + 1.0f, frame.min.y + 2.0f),
                  ImVec2(frame.min.x + 1.0f + borderLeft, frame.max.y - 2.0f), 1.5f,
                  WithAlpha(accent, alpha));

    float x = frame.min.x + borderLeft + padX;
    if (!icon.empty()) {
        DrawIcon(draw, icon, ImVec2(x, frame.center().y - iconSize * 0.5f), iconSize,
                 WithAlpha(accent, alpha));
        x += gap;
    }
    DrawTextClipped(draw, font, 12.5f, ImVec2(x, frame.min.y + padY + 1.0f),
                    frame.max.x - x - padX, WithAlpha(ColorText(), alpha), text, /*wrap=*/true);
    return h;
}

} // namespace shine::kit
