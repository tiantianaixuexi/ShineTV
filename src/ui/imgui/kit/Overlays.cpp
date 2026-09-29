#include "ui/imgui/kit/Overlays.h"

#include <algorithm>

namespace shine::kit {

// ------------------------------------------------------------------ Scrim
bool Scrim(ImDrawList* draw, Rect screen, std::string_view id) {
    draw->AddRectFilled(screen.min, screen.max, ColorScrim());
    // 点遮罩本体 = 关闭浮层（Overlays.jsx:35/162 的 `e.target === e.currentTarget`）。
    // id 交给调用方：同一帧里若有嵌套浮层（命令面板套在 modal 上），
    // 固定 ID 会让两个遮罩抢同一个 item。
    return HitTest(screen, id).clicked;
}

// ------------------------------------------------------------------ Modal
Rect Modal(ImDrawList* draw, Rect screen, std::string_view title, std::string_view icon,
           float width, std::string_view id) {
    Scrim(draw, screen, std::string(id) + "#scrim");
    // width: min(560, 100vw - 48)（ui.css:948）；0 = 用默认值。
    const float w = width > 0.0f ? width : std::min(560.0f, screen.width() - 48.0f);
    // max-height: min(640, 100vh - 64)（ui.css:949）。这里按内容自适应，
    // 只把上限当硬约束 —— 内容少的时候不该留一块空白。
    const float maxH = std::min(640.0f, screen.height() - 64.0f);
    const float headerH = title.empty() ? 0.0f : 14.0f * 2.0f + 18.0f + 1.0f;
    // 内容高度：调用方在返回的矩形里画，所以这里给一个「尽量高但不超过上限」
    // 的值，让 Modal 变成一个占位式 API（内容画完可能不满，实际以内容为准）。
    const float bodyH = std::max(0.0f, maxH - headerH - 18.0f * 2.0f);
    const float h = std::min(maxH, headerH + bodyH);
    const Rect frame = RectAt(screen.center().x - w * 0.5f, screen.center().y - h * 0.5f, w, h);
    DrawShadowed(draw, frame.min, frame.max, 14.0f, ColorOverlay(), ColorLineNormal(), 1.0f);

    float top = frame.min.y;
    if (!title.empty()) {
        // .modal-h（ui.css:955-965）：padding 14/18 + gap 10 + 1px 下边；标题 15/700。
        const float iconSize = 17.0f;
        float x = frame.min.x + 18.0f;
        if (!icon.empty()) {
            DrawIcon(draw, icon, ImVec2(x, top + 15.0f), iconSize, ColorAccent());
            x += iconSize + 10.0f;
        }
        ImFont* font = FontBoldAt(15.0f);
        DrawTextClipped(draw, font, 15.0f, ImVec2(x, top + 14.0f), frame.max.x - x - 18.0f,
                        ColorText(), title);
        top += headerH;
        draw->AddLine(ImVec2(frame.min.x, top - 0.5f), ImVec2(frame.max.x, top - 0.5f),
                      ColorLineSubtle(), 1.0f);
    }
    // .modal-b（ui.css:966-969）：padding 18 + overflow-y auto（滚动由 ScrollRegion 负责）。
    return RectAt(frame.min.x + 18.0f, top + 18.0f, w - 36.0f,
                  std::max(0.0f, frame.max.y - 18.0f - (top + 18.0f)));
}

// ------------------------------------------------------------------ Drawer
float DrawerWidth() {
    // .drawer（ui.css:652）：width 390。
    return 390.0f;
}

Rect Drawer(ImDrawList* draw, Rect screen, std::string_view title, std::string_view icon,
            int footerButtons, Rect* footerOut, std::string_view id) {
    Scrim(draw, screen, std::string(id) + "#scrim");
    const float w = std::min(DrawerWidth(), screen.width());
    const Rect frame = RectAt(screen.max.x - w, screen.min.y, w, screen.height());
    DrawShadowed(draw, frame.min, frame.max, 0.0f, ColorOverlay(), ColorLineNormal(), 1.0f);

    // .drawer-h（ui.css:664-672）：padding 14/16 + gap 10 + 1px 下边，标题 14/700。
    const float iconSize = 17.0f;
    float x = frame.min.x + 16.0f;
    float top = frame.min.y;
    if (!icon.empty()) {
        DrawIcon(draw, icon, ImVec2(x, top + 15.0f), iconSize, ColorAccent());
        x += iconSize + 10.0f;
    }
    ImFont* font = FontBoldAt(14.0f);
    DrawTextClipped(draw, font, 14.0f, ImVec2(x, top + 14.0f), frame.max.x - x - 16.0f,
                    ColorText(), title);
    top += 14.0f * 2.0f + 18.0f + 1.0f;
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
            std::string_view icon, float above) {
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
    DrawShadowed(draw, frame.min, frame.max, 10.0f, ColorOverlay(), ColorLineNormal(), 1.0f);
    // border-left: 3px <status>（ui.css:996 + .ok/.warn/.err 覆盖 ui.css:1003-1005）。
    // 画成一个 3px 宽的圆角矩形：Toast 左角是 r10 的圆，直接画直角条会戳出弧外，
    // 所以把条的上下各内缩 2px 让它落在圆角以内。
    const ImU32 accent = tone == theme::Tone::Idle ? ColorOf(theme::Current().accentInfo)
                                                    : ToneColor(tone);
    DrawRoundRect(draw, ImVec2(frame.min.x + 1.0f, frame.min.y + 2.0f),
                  ImVec2(frame.min.x + 1.0f + borderLeft, frame.max.y - 2.0f), 1.5f, accent);

    float x = frame.min.x + borderLeft + padX;
    if (!icon.empty()) {
        DrawIcon(draw, icon, ImVec2(x, frame.center().y - iconSize * 0.5f), iconSize, accent);
        x += gap;
    }
    DrawTextClipped(draw, font, 12.5f, ImVec2(x, frame.min.y + padY + 1.0f),
                    frame.max.x - x - padX, ColorText(), text, /*wrap=*/true);
    return h;
}

} // namespace shine::kit
