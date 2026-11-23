// shine::pages —— 外壳的**顶栏**（P4.2，2026-10-02 自 Shell_Chrome.cpp 拆出）
//
// 拆它的理由不是「文件太大」这一句：**顶栏是这一族里唯一带状态与动作的一块**。
// 另三块（导航栏 / 状态栏 / 面包屑）只读「当前工程 / 当前工作区」并把结果显示出来，
// 而顶栏拥有项目胶囊（含真工程名回退）、搜索触发器、运行 / 停止、主题与设置浮层的
// 锚点、以及 `RequestRunStart` / `RequestRunStop` 的**执行体**。把执行体留在画它
// 的那一块，是「一个动作只存一份」那条纪律的物理形态。
//
// ⚠️ 原文件头写的是「四块放一起是因为它们是同一族 …… 分成四个文件只会让那点
//    共享逻辑无处安放」。**那句理由不成立**：四块之间一个调用都没有，拆开之后
//    没有任何共享逻辑失去安放处。真正共享的是「Shell_Layout.h 的固定档」，而那一份
//    本来就是独立头文件 —— 真正的共性已经在文件边界之外了。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/Shell_Layout.h"

#include "comfy/ComfySession.h"
#include "core/Settings.h"
#include "pipeline/StageMachine.h"
#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "util/Encoding.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

// ---------------------------------------------------------------- P4.2 顶栏
void Shell::DrawTopBar(Rect area, ImDrawList* draw) {
    // 玻璃降级：用 --glass 实色（R4 已知降级）
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorPanel());
    draw->AddLine(ImVec2(area.min.x, area.max.y - 0.5f), ImVec2(area.max.x, area.max.y - 0.5f),
                  ColorLineSubtle(), 1.0f);

    // shell.css:25 `.topbar { padding: 0 12px }` —— 左右内边距都是 12，不是 16。
    float x = area.min.x + 12.0f;
    // 品牌标 22×22 / r6 / grad-accent + shadow-accent + 内嵌 12px play
    const Rect brand{area.min.x + 12.0f, 0.5f * (area.min.y + area.max.y) - 11.0f,
                     area.min.x + 34.0f, 0.5f * (area.min.y + area.max.y) + 11.0f};
    // 品牌标统一走 `kit::LogoMark`（同一份实现也给项目中心的 `.hub-logo` 用）。
    // ⚠️ 它接管了三件事：`--shadow-accent` 柔光、`--grad-accent` 斜渐变、
    //    以及**水墨主题下这两个都换成「纯 accent 实底 + 无投影」**。
    //    原来这里是手写的「`accent-glow` 描边环 ±2px」当投影 —— 环不是虚化，
    //    形状与 CSS 的 `0 4px 20px` 无关，而且水墨下还会照画不误。
    LogoMark(draw, brand, 6.0f, 12.0f);

    // R3 收编：品牌两段走 kit::Text（画与量共用同一份 TextStyle，字号只传 token）。
    const TextStyle brandStyle{.size = theme::font::kBodyLarge, .bold = true,
                               .color = ColorText()};
    const TextStyle brandSuffixStyle{.size = theme::font::kBodyLarge, .bold = true,
                                     .color = ColorAccent()};
    const std::string_view prefix = "ShineTV ";
    const std::string_view suffix = "Studio";
    // shell.css:34 `.brand { gap: 8px }` —— 标与字之间是 8px，不是顶栏的 10px。
    x = brand.max.x + 8.0f;
    Text(draw, ImVec2(x, 0.5f * (area.min.y + area.max.y) - brandStyle.size * 0.5f), prefix,
         brandStyle);
    x += TextSize(prefix, brandStyle).x;
    // 「Studio」是渐变字：分两段画，中间色过渡
    Text(draw, ImVec2(x, 0.5f * (area.min.y + area.max.y) - brandSuffixStyle.size * 0.5f),
         suffix, brandSuffixStyle);
    // shell.css:24 `.topbar { gap: 10px }` —— 顶栏子项之间一律 10px。
    x += TextSize(suffix, brandSuffixStyle).x + 10.0f;

    // 品牌标整块可点：同样回项目中心（webui Shell.jsx:26 的 onHub）。
    {
        const Rect hit{area.min.x + 12.0f, area.min.y, x - 10.0f, area.max.y};
        if (ChromeHit(hit, "tb-brand").clicked) {
            ToggleProjectHub();
        }
    }

    // 项目胶囊 h28 r6：8×8 渐变点 + 名字（max-w 140）+ 10px 下箭头
    const std::string& project =
        layout_.projectName.empty() ? std::string("未打开项目") : layout_.projectName;
    ImFont* capFont = FontAt(theme::font::kBody);
    const float capTextW = std::min(
        140.0f, capFont->CalcTextSizeA(theme::font::kBody, 1e9f, 0.0f, project.data(), project.data() + project.size()).x);
    const Rect capsule{x, 0.5f * (area.min.y + area.max.y) - 14.0f, x + 12.0f + 8.0f + capTextW + 10.0f + 10.0f + 8.0f,
                      0.5f * (area.min.y + area.max.y) + 14.0f};
    DrawRoundRect(draw, capsule.min, capsule.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
    // .proj-chip .pdot 的 `0 0 6px var(--accent-glow)`（shell.css:78）：光晕用
    // accent-glow 自身的 alpha，不再额外乘 0.6（乘完比设计稿更淡）。
    DrawRoundRect(draw, ImVec2(capsule.min.x + 10.0f, capsule.center().y - 4.0f),
                  ImVec2(capsule.min.x + 18.0f, capsule.center().y + 4.0f), 4.0f,
                  ColorAccentGlow());
    DrawHGradient(draw, ImVec2(capsule.min.x + 10.0f, capsule.center().y - 4.0f),
                  ImVec2(capsule.min.x + 18.0f, capsule.center().y + 4.0f), 4.0f, ColorAccent(),
                  ColorAccentHover());
    DrawTextClipped(draw, capFont, theme::font::kBody,
                    ImVec2(capsule.min.x + 24.0f, capsule.center().y - 6.5f), capTextW, ColorText(),
                    project);
    DrawIcon(draw, "chevdown", ImVec2(capsule.max.x - 18.0f, capsule.center().y - 5.0f), 10.0f,
             ColorTextMuted());
    // 项目胶囊可点：回项目中心（webui Shell.jsx:31 的 onHub）。早先只是画了个带箭头的死胶囊。
    if (ChromeHit(capsule, "tb-projchip").clicked) {
        ToggleProjectHub();
    }
    x = capsule.max.x + 10.0f;

    // 右侧：运行 / 停止 / 主题 / 设置 / Comfy 状态
    // 排布照 webui Shell.jsx:44-78（自右向左）：Comfy 状态项 · 设置 · 主题 · 运行/停止
    const float cy = 0.5f * (area.min.y + area.max.y);
    float rx = area.max.x - 12.0f;

    // Comfy 状态项（shell.css:458 的 .sb-item）：状态点 + 文字。
    // 早先这里只画一个恒为 Idle 的裸点，没有任何文字，也点不动。
    // 现在读 ComfySession 的真实连接状态 —— 没配地址是"未配置"，
    // 配了但连不上是 LastError，非空即报"未连接"。不编造"已连接"。
    if (area.width() >= 930.0f) {
        comfy::ComfySession& comfy = comfy::ComfySession::Instance();
        const bool configured = !Settings().comfyBaseUrl.empty();
        const auto state = comfy.State();
        std::string text;
        theme::Tone tone = theme::Tone::Idle;
        if (!configured) {
            text = "Comfy 未配置";
        } else if (state == comfy::ConnectionState::Connecting) {
            text = "Comfy 连接中";
            tone = theme::Tone::Busy;
        } else if (state != comfy::ConnectionState::Connected) {
            text = "Comfy 未连接";
            tone = theme::Tone::Warn;
        } else {
            text = "Comfy 已连接";
            tone = theme::Tone::Ok;
        }
        const TextStyle comfyStyle{.size = theme::font::kLabel, .color = ColorTextSecondary()};
        const float tw = TextSize(text, comfyStyle).x;
        const float w = 8.0f + 11.0f + 6.0f + tw + 16.0f;
        const Rect item{rx - w, cy - 10.0f, rx, cy + 10.0f};
        const Hit hit = ChromeHit(item, "tb-comfy");
        if (hit.hovered) {
            DrawRoundRect(draw, item.min, item.max, 4.0f, ColorFillHover());
        }
        StatusDot(draw, ImVec2(item.min.x + 8.0f + 3.5f, cy), tone, false);
        Text(draw, ImVec2(item.min.x + 8.0f + 11.0f + 6.0f, cy - comfyStyle.size * 0.5f), text,
             comfyStyle);
        if (hit.clicked) {
            // 点开设置模态看连接细节，而不是像设计稿那样凭空把连接状态翻个面。
            ToggleSettingsModal(true);
        }
        rx -= w + 10.0f;
    }

    // 28 是图标钮的宽，另一个 10 是 `.topbar{gap:10px}` —— 减掉一个只会让两者贴死。
    rx -= 28.0f + 10.0f;
    if (IconButton(draw, RectAt(rx, cy - 14.0f, 28.0f, 28.0f), "settings", false, false,
                   "tb-settings", "设置")) {
        ToggleSettingsModal(!settingsOpen_);
    }

    // 「主题」是**幽灵按钮 + 文字**（Shell.jsx:51），点开的是主题菜单，
    // 不是命令面板 —— 早先这里是个纯图标键且直接翻 paletteOpen_，两处都错。
    {
        ButtonSpec themeSpec;
        themeSpec.variant = ButtonVariant::Ghost;
        themeSpec.icon = "palette";
        const std::string_view label = "主题";
        ImFont* f = FontAt(theme::font::kBody);
        const float bw = ButtonWidth(ButtonSize::Medium, 15.0f,
                                     f->CalcTextSizeA(theme::font::kBody, 1e9f, 0.0f, label.data(),
                                                      label.data() + label.size())
                                             .x);
        rx -= bw;
        themeMenuAnchor_ = ImVec2(rx, cy + 15.0f);
        if (Button(draw, RectAt(rx, cy - 15.0f, bw, 30.0f), label, themeSpec, "tb-theme")) {
            themeMenuOpen_ = !themeMenuOpen_;
            themeMenuJustOpened_ = themeMenuOpen_;
            if (themeMenuOpen_) {
                settingsOpen_ = false;
            }
        }
    }
    rx -= 10.0f;
    // 运行 / 停止是**二选一**（webui Shell.jsx:44-48：`run.active ? 停止 : 运行`）。
    // 早先两个按钮常驻且「停止」恒 disabled，界面上等于挂了一个永远按不动的控件。
    ImFont* topFont = FontBoldAt(theme::font::kBody);
    const auto labelWidth = [&](std::string_view label) {
        return topFont->CalcTextSizeA(theme::font::kBody, 1e9f, 0.0f, label.data(),
                                      label.data() + label.size())
            .x;
    };
    if (runActive_) {
        const std::string_view stopLabel = "停止";
        const float stopW = ButtonWidth(ButtonSize::Medium, 15.0f, labelWidth(stopLabel));
        rx -= stopW;
        ButtonSpec stopSpec;
        stopSpec.variant = ButtonVariant::Secondary;
        stopSpec.icon = "stop";
        if (Button(draw, RectAt(rx, 0.5f * (area.min.y + area.max.y) - 15.0f, stopW, 30.0f),
                   stopLabel, stopSpec, "tb-stop")) {
            RequestRunStop();
        }
    } else {
        const std::string_view runLabel = "运行";
        const float runW = ButtonWidth(ButtonSize::Medium, 15.0f, labelWidth(runLabel));
        rx -= runW;
        ButtonSpec runSpec;
        runSpec.variant = ButtonVariant::Primary;
        runSpec.icon = "play";
        // 执行体未接入时**不禁用**按钮：点了会弹 toast 说明为什么跑不了，
        // 这比一个按不动的灰按钮有用 —— 用户能问出原因。
        // 总控页那侧才禁用（那里离原因更远，页面上直接写了禁用说明）。
        if (Button(draw, RectAt(rx, 0.5f * (area.min.y + area.max.y) - 15.0f, runW, 30.0f),
                   runLabel, runSpec, "tb-run")) {
            RequestRunStart();
        }
    }
    // 搜索入口拿左右两组实际占宽之间的余量，长项目名和状态不会压住运行按钮。
    const float available = rx - x - 12.0f;
    if (available >= 138.0f) {
        const float width = std::min(260.0f, available);
        const Rect search = RectAt(x, cy - 15.0f, width, 30.0f);
        DrawRoundRect(draw, search.min, search.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
        DrawIcon(draw, "search", ImVec2(search.min.x + 10.0f, cy - 7.0f), 14.0f, ColorTextSecondary());
        const TextStyle hint{.size = theme::font::kBody, .color = ColorTextSecondary()};
        const float kbdW = KbdWidth("Ctrl K");
        Kbd(draw, RectAt(search.max.x - kbdW - 10.0f, cy - 9.0f, kbdW, 18.0f), "Ctrl K");
        TextClipped(draw, ImVec2(search.min.x + 32.0f, CenterY(cy, hint)),
                    width - kbdW - 52.0f, "搜索命令", hint);
        if (ChromeHit(search, "tb-search").clicked) {
            ToggleCommandPalette();
        }
    } else if (available >= 30.0f &&
               IconButton(draw, RectAt(x, cy - 15.0f, 30.0f, 30.0f), "search", false, false,
                          "tb-search", "搜索命令 Ctrl+K")) {
        ToggleCommandPalette();
    }

}

// 运行 / 停止的执行体提成函数：顶栏按钮与 Ctrl+Enter 走**同一份**。
// 写成两处的话，迟早只改得动一边 —— 这正是「一个动作只存一份」那条纪律。
//
// ⚠️ 阶段执行体**未接入**，所以「运行」不做任何假装在跑的事。理由见
//    WorkspaceA.cpp 的 OverviewPipelineWired()：Runner 的执行体拿不到，真执行体在
//    novel/ 且签名与 StageExecutor 不同形状。
//    早先这里会置 `runActive_ = true; runStageIndex_ = 0; runPercent_ = 0`，于是状态栏
//    稳定输出「T1/17 · 0%」—— 一个**永远不会前进**的进度条。那三个字段全文件只有
//    「写 0」和「读出来显示」两处，结构上就不可能前进，不是「跑到一半卡住」。
//    界面看起来在跑、实际上什么都不会发生，这比显示「未接入」糟得多。
void Shell::RequestRunStart() {
    if (!pages::OverviewPipelineWired()) {
        PushLog("warn", "运行请求被拒绝：阶段执行体未接入");
        Notify(pages::StageExecutorMissingReason(), theme::Tone::Warn);
        return;
    }
    runActive_ = true;
    runFinished_ = false;
    runStageIndex_ = 0;
    runPercent_ = 0;
    PushLog("info", "流水线开始运行");
}

void Shell::RequestRunStop() {
    runActive_ = false;
    PushLog("warn", "流水线已请求停止");
}


} // namespace shine::pages
