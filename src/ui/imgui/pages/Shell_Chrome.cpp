// shine::pages —— 外壳的**固定条带**：顶栏 / 导航栏 / 状态栏 / 面包屑
//
// 四块放一起是因为它们是同一族：贴边、常驻、不随内容滚动、尺寸全部来自
// Shell_Layout.h 的固定档。它们之间不互相调用，但都只读「当前工程 / 当前工作区」
// 这一份状态 —— 分成四个文件只会让那点共享逻辑无处安放。
//
// 侧栏 / 检查器 / 底栏是另外三族（各自有面板状态与列表视口），分别在
// Shell_Panels.cpp 与 Shell_Dock.cpp。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/Shell_Layout.h"

#include "comfy/ComfySession.h"
#include "core/Settings.h"
#include "pipeline/StageMachine.h"
#include "ui/imgui/kit/Anim.h"
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

namespace {

// T 链的阶段数 —— 状态栏那个「T{n}/N」的分母。
//
// ⚠️ 早先是硬编码字面量 `"/17"`（当年的 Shell.cpp 状态栏那一行）。**不能**改成
//    AllStages().size()：
//    那是 **28**（T1–T17 + V0–V11），而状态栏报的是**文本链**的进度，分母得是
//    chain == "text" 的条数 —— 与总控页 `OverviewState().chain` 同一口径（那边
//    同样只取 text 链 17 个）。写死 17 则是「哪天业务层加了 T18 就悄悄对不上」。
int TextStageCount() {
    static const int count = [] {
        int n = 0;
        for (const pipeline::StageDefinition& def : pipeline::AllStages()) {
            if (def.chain == "text") {
                ++n;
            }
        }
        return n;
    }();
    return count;
}

// 导航栏的六个入口。设计稿 shell.css 的 .rail 是一列 icon + label，顺序与
// 工作区下标一一对应。
struct RailEntry {
    const char* icon;
    const char* label;
    int workspace;
};

} // namespace

// ---------------------------------------------------------------- P4.2 顶栏
void Shell::DrawTopBar(Rect area, ImDrawList* draw) {
    // 玻璃降级：用 --glass 实色（R4 已知降级）
    DrawRoundRect(draw, area.min, area.max, 0.0f, GlassColor());
    draw->AddLine(ImVec2(area.min.x, area.max.y - 0.5f), ImVec2(area.max.x, area.max.y - 0.5f),
                  ColorLineSubtle(), 1.0f);

    float x = area.min.x + 16.0f;
    // 品牌标 22×22 / r6 / grad-accent + shadow-accent + 内嵌 12px play
    const Rect brand{area.min.x + 16.0f, 0.5f * (area.min.y + area.max.y) - 11.0f,
                     area.min.x + 38.0f, 0.5f * (area.min.y + area.max.y) + 11.0f};
    const Rect halo = Rect(brand.min.x, brand.min.y - 2.0f, brand.max.x, brand.max.y + 2.0f);
    DrawRoundRect(draw, halo.min, halo.max, 8.0f, 0, ColorAccentGlow(), 2.0f);
    DrawDiagGradient(draw, brand.min, brand.max, 6.0f, ColorAccent(), ColorAccentHover());
    DrawIconCentered(draw, "play", brand.center(), 12.0f, ColorAccentFg());

    ImFont* brandFont = FontBoldAt(13.5f);
    const std::string_view prefix = "ShineTV ";
    const std::string_view suffix = "Studio";
    x = brand.max.x + 10.0f;
    draw->AddText(brandFont, 13.5f, ImVec2(x, 0.5f * (area.min.y + area.max.y) - 6.75f),
                  ColorText(), prefix.data(), prefix.data() + prefix.size());
    x += brandFont->CalcTextSizeA(13.5f, 1e9f, 0.0f, prefix.data(), prefix.data() + prefix.size())
             .x;
    // 「Studio」是渐变字：分两段画，中间色过渡
    const float suffixW =
        brandFont->CalcTextSizeA(13.5f, 1e9f, 0.0f, suffix.data(), suffix.data() + suffix.size()).x;
    draw->AddText(brandFont, 13.5f, ImVec2(x, 0.5f * (area.min.y + area.max.y) - 6.75f),
                  ColorAccent(), suffix.data(), suffix.data() + suffix.size());
    x += suffixW + 20.0f;

    // 品牌标整块可点：同样回项目中心（webui Shell.jsx:26 的 onHub）。
    {
        const Rect hit{area.min.x + 16.0f, area.min.y, x - 20.0f, area.max.y};
        if (ChromeHit(hit, "tb-brand").clicked) {
            ToggleProjectHub();
        }
    }

    // 项目胶囊 h28 r6：8×8 渐变点 + 名字（max-w 140）+ 10px 下箭头
    const std::string& project =
        layout_.projectName.empty() ? std::string("未打开项目") : layout_.projectName;
    ImFont* capFont = FontAt(13.0f);
    const float capTextW = std::min(
        140.0f, capFont->CalcTextSizeA(13.0f, 1e9f, 0.0f, project.data(), project.data() + project.size()).x);
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
    DrawTextClipped(draw, capFont, 13.0f,
                    ImVec2(capsule.min.x + 24.0f, capsule.center().y - 6.5f), capTextW, ColorText(),
                    project);
    DrawIcon(draw, "chevdown", ImVec2(capsule.max.x - 18.0f, capsule.center().y - 5.0f), 10.0f,
             ColorTextMuted());
    // 项目胶囊可点：回项目中心（webui Shell.jsx:31 的 onHub）。早先只是画了个带箭头的死胶囊。
    if (ChromeHit(capsule, "tb-projchip").clicked) {
        ToggleProjectHub();
    }
    x = capsule.max.x + 16.0f;

    // 搜索触发器 260×28 胶囊 + 右侧 Kbd "Ctrl K"
    const Rect search{x, 0.5f * (area.min.y + area.max.y) - 14.0f, x + 260.0f,
                      0.5f * (area.min.y + area.max.y) + 14.0f};
    DrawRoundRect(draw, search.min, search.max, 14.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "search", ImVec2(search.min.x + 10.0f, search.center().y - 7.0f), 14.0f,
             ColorTextMuted());
    // ⚠️ 长度一律用 sizeof(字面量) - 1。本树曾有 6 处把字节数写死在 AddText 的
    //    text_end 上，而 4 个汉字在 UTF-8 里是 12 字节 —— 结果每处都少画 1 个字；
    //    另有一处比实际多 5 字节，直接读到字面量池外面。搜索提示「搜索命令」就属于前者。
    static constexpr char kSearchHint[] = "搜索命令";
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(search.min.x + 30.0f, search.center().y - 6.25f),
                  ColorTextMuted(), kSearchHint, kSearchHint + sizeof(kSearchHint) - 1);
    const float kbdW = KbdWidth("Ctrl K");
    Kbd(draw, RectAt(search.max.x - kbdW - 6.0f, search.center().y - 9.0f, kbdW, 18.0f), "Ctrl K");
    // 搜索框可点：开命令面板（webui Shell.jsx:36）。
    if (ChromeHit(search, "tb-search").clicked) {
        ToggleCommandPalette();
    }

    // 右侧：运行 / 停止 / 主题 / 设置 / Comfy 状态
    // 排布照 webui Shell.jsx:44-78（自右向左）：Comfy 状态项 · 设置 · 主题 · 运行/停止
    const float cy = 0.5f * (area.min.y + area.max.y);
    float rx = area.max.x - 16.0f;

    // Comfy 状态项（shell.css:458 的 .sb-item）：状态点 + 文字。
    // 早先这里只画一个恒为 Idle 的裸点，没有任何文字，也点不动。
    // 现在读 ComfySession 的真实连接状态 —— 没配地址是"未配置"，
    // 配了但连不上是 LastError，非空即报"未连接"。不编造"已连接"。
    {
        comfy::ComfySession& comfy = comfy::ComfySession::Instance();
        const bool configured = !Settings().comfyBaseUrl.empty();
        const std::string err = comfy.LastError();
        const bool connected = configured && err.empty();
        std::string text;
        theme::Tone tone = theme::Tone::Idle;
        if (!configured) {
            text = "Comfy 未配置";
        } else if (!connected) {
            text = "Comfy 未连接";
            tone = theme::Tone::Warn;
        } else {
            text = "Comfy 已连接";
            tone = theme::Tone::Ok;
        }
        ImFont* f = FontAt(11.5f);
        const float tw =
            f->CalcTextSizeA(11.5f, 1e9f, 0.0f, text.data(), text.data() + text.size()).x;
        const float w = 8.0f + 11.0f + 6.0f + tw + 16.0f;
        const Rect item{rx - w, cy - 10.0f, rx, cy + 10.0f};
        const Hit hit = ChromeHit(item, "tb-comfy");
        if (hit.hovered) {
            DrawRoundRect(draw, item.min, item.max, 4.0f, ColorFillHover());
        }
        StatusDot(draw, ImVec2(item.min.x + 8.0f + 3.5f, cy), tone, false);
        draw->AddText(f, 11.5f, ImVec2(item.min.x + 8.0f + 11.0f + 6.0f, cy - 5.75f),
                      ColorTextSecondary(), text.data(), text.data() + text.size());
        if (hit.clicked) {
            // 点开设置模态看连接细节，而不是像设计稿那样凭空把连接状态翻个面。
            ToggleSettingsModal(true);
        }
        rx -= w + 6.0f;
    }

    rx -= 28.0f;
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
        ImFont* f = FontAt(13.0f);
        const float bw = ButtonWidth(ButtonSize::Medium, 15.0f,
                                     f->CalcTextSizeA(13.0f, 1e9f, 0.0f, label.data(),
                                                      label.data() + label.size())
                                             .x);
        rx -= bw;
        themeMenuAnchor_ = ImVec2(rx, cy + 15.0f);
        if (Button(draw, RectAt(rx, cy - 15.0f, bw, 30.0f), label, themeSpec, "tb-theme")) {
            themeMenuOpen_ = !themeMenuOpen_;
            if (themeMenuOpen_) {
                settingsOpen_ = false;
            }
        }
    }
    rx -= 12.0f;
    // 运行 / 停止是**二选一**（webui Shell.jsx:44-48：`run.active ? 停止 : 运行`）。
    // 早先两个按钮常驻且「停止」恒 disabled，界面上等于挂了一个永远按不动的控件。
    ImFont* topFont = FontBoldAt(13.0f);
    const auto labelWidth = [&](std::string_view label) {
        return topFont->CalcTextSizeA(13.0f, 1e9f, 0.0f, label.data(),
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

// ---------------------------------------------------------------- P4.3 导航栏
void Shell::DrawRail(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.max.x - 0.5f, area.min.y), ImVec2(area.max.x - 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 6 个工作区（设计稿序），第 6 项后是分隔线 + 3 个面板开关 + 弹性空隙 + 画廊
    float y = area.min.y + 10.0f;
    for (int i = 0; i < 6; ++i) {
        const bool on = layout_.workspace == i;
        const Rect item{area.min.x, y, area.max.x, y + 48.0f};
        const ImVec2 center = ImVec2(area.center().x, item.center().y);
        if (on) {
            DrawRoundRect(draw, ImVec2(item.min.x + 6.0f, item.min.y + 2.0f),
                          ImVec2(item.max.x - 6.0f, item.max.y - 2.0f), 10.0f, ColorFillSelected());
            // 左侧 2.5px accent 竖条：left:-8px，上下内缩 9px，r3
            const Rect bar{area.min.x - 8.0f, item.min.y + 9.0f, area.min.x - 5.5f,
                           item.max.y - 9.0f};
            DrawRoundRect(draw, bar.min, bar.max, 3.0f, 0, ColorAccentGlow(), 6.0f);
            DrawRoundRect(draw, bar.min, bar.max, 3.0f, ColorAccent());
        }
        // ⚠️ 一次 HitTest 拿走 hovered + clicked。早先这里先 Hovered("rail-hover-N")
        // 再 Clicked("rail-ws-N") —— 两个 InvisibleButton 落在**同一矩形**上，
        // ImGui 的 ItemHoverable 只让先注册的那个拿到 HoveredId（HoveredAllowOverlap 默认 false），
        // 第二个永远 hovered=false / clicked=false，整个导航栏点不动。
        const Hit hit = ChromeHit(item, "rail-ws-" + std::to_string(i));
        DrawIconCentered(draw, WorkspaceIcon(i), center, 19.0f,
                         on ? ColorAccent() : (hit.hovered ? ColorText() : ColorTextMuted()));
        if (hit.clicked) {
            SetWorkspace(i);
        }
        y += 48.0f;
    }

    // 分隔线 24×1
    y += 4.0f;
    draw->AddLine(ImVec2(area.center().x - 12.0f, y), ImVec2(area.center().x + 12.0f, y),
                  ColorLineSubtle(), 1.0f);
    y += 8.0f;

    struct Toggle {
        const char* icon;
        bool* state;
        const char* tip;
    };
    const Toggle toggles[] = {
        {"panelL", &layout_.sidePanelVisible, "侧栏 Ctrl+B"},
        {"terminal", &layout_.dockVisible, "底栏 Ctrl+J"},
        {"panel", &layout_.inspectorVisible, "检查器 Ctrl+I"},
    };
    // 三个面板开关。⚠️ 设计稿这里**没有**常驻高亮：
    // webui Shell.jsx:100-108 给侧栏/检查器写的是 `panels.x ? '' : ' active'`，
    // 但整个 CSS 里只有 `.icon-btn.active`（ui.css:102），**根本没有 .rail-btn.active 规则**
    // —— 那个 active 类是死类，不产生任何视觉。早先这里画了一层常驻 fill-hover，
    // 等于凭空多出一个设计稿没有的常亮态；只保留 hover（shell.css 的 .rail-btn:hover）。
    for (const Toggle& toggle : toggles) {
        const Rect item{area.min.x, y, area.max.x, y + 44.0f};
        // ⚠️ 一帧里只 HitTest 一次。同一个 id 注册两个 InvisibleButton 会让 hover 整个
        //    失效（实测：hover-jump-btn 与静息态逐像素零差异）。hovered / clicked 从
        //    同一次命中测试里取。
        const kit::Hit hit = ChromeHit(item, "rail-tg-" + std::string(toggle.icon));
        if (hit.hovered) {
            DrawRoundRect(draw, ImVec2(item.min.x + 6.0f, item.min.y), ImVec2(item.max.x - 6.0f, item.max.y),
                          10.0f, ColorFillHover());
        }
        DrawIconCentered(draw, toggle.icon, ImVec2(area.center().x, item.center().y), 19.0f,
                         hit.hovered ? ColorText() : ColorTextMuted());
        if (hit.clicked) {
            *toggle.state = !*toggle.state;
        }
        y += 44.0f;
    }

    // 弹性空隙后固定「组件画廊」（hidden: true，只在这一栏出现）
    const Rect gallery{area.min.x, area.max.y - 58.0f, area.max.x, area.max.y - 10.0f};
    if (layout_.workspace == 6) {
        DrawRoundRect(draw, ImVec2(gallery.min.x + 6.0f, gallery.min.y + 2.0f),
                      ImVec2(gallery.max.x - 6.0f, gallery.max.y - 2.0f), 10.0f, ColorFillSelected());
    }
    DrawIconCentered(draw, "grid", ImVec2(area.center().x, gallery.center().y), 19.0f,
                     layout_.workspace == 6 ? ColorAccent() : ColorTextMuted());
    if (ChromeClicked(gallery, "rail-gallery")) {
        SetWorkspace(6);
    }
}


// ---------------------------------------------------------------- P4.7 状态栏
void Shell::DrawStatusBar(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x, area.min.y + 0.5f), ImVec2(area.max.x, area.min.y + 0.5f),
                  ColorLineSubtle(), 1.0f);

    const float cy = area.center().y;
    float x = area.min.x + 10.0f;
    const float h = 20.0f;

    // 6 项：Comfy 状态点 + 文字 / LLM 就绪 / 队列 N / 弹性 / 阶段名 / 96px 细进度 + T{n}/17 / 主题 / 版本
    // ⚠️ 三项都读**真实状态**，不再写死 "Comfy 未连接 / LLM 就绪 / 队列 3"。
    //    LLM 的「就绪」看 AppSettings 里 provider 对应的 API Key 有没有配；
    //    队列数取 comfy::QueueModel 的 Counts；Comfy 同顶栏。
    {
        const AppSettings& s = Settings();
        const bool llmOk = !s.openaiApiKey.empty() || !s.mimoApiKey.empty() ||
                           !s.minimaxApiKey.empty();
        const comfy::ComfySession& session = comfy::ComfySession::Instance();
        const bool comfyConfigured = !s.comfyBaseUrl.empty();
        const bool comfyUp = comfyConfigured && session.LastError().empty();
        const comfy::QueueModel::Counts counts = session.Queue().CountsSnapshot();
        const int queued = counts.pending + counts.running;

        const std::string queueText = "队列 " + std::to_string(queued);
        struct Item {
            const char* text;
            theme::Tone tone;
        };
        const Item items[3] = {
            {comfyConfigured ? (comfyUp ? "Comfy 已连接" : "Comfy 未连接") : "Comfy 未配置",
             comfyUp ? theme::Tone::Ok
                     : (comfyConfigured ? theme::Tone::Warn : theme::Tone::Idle)},
            {llmOk ? "LLM 就绪" : "LLM 未配置", llmOk ? theme::Tone::Ok : theme::Tone::Idle},
            // ⚠️ 队列数要活到本函数返回：早先写成 ("队列 " + to_string(n)).c_str()，
            //    那个临时 string 在完整表达式结束就析构，指针当场悬空。
            {queueText.c_str(), theme::Tone::Idle},
        };
        for (const Item& item : items) {
            const float w =
                FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, item.text,
                                              item.text + std::strlen(item.text))
                    .x +
                16.0f;
            DrawRoundRect(draw, ImVec2(x, cy - 10.0f), ImVec2(x + w, cy + 10.0f), 4.0f, 0);
            StatusDot(draw, ImVec2(x + 7.5f, cy), item.tone, item.tone == theme::Tone::Busy);
            draw->AddText(FontAt(11.5f), 11.5f, ImVec2(x + 16.0f, cy - 5.75f),
                          ColorTextSecondary(), item.text, item.text + std::strlen(item.text));
            x += w + 6.0f;
        }
    }

    const std::string themeName = std::string("主题：") +
                                  std::string(theme::ThemeDisplayName(theme::CurrentThemeId()));
    const float themeW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, themeName.data(), themeName.data() + themeName.size()).x;
    // 版本号取 CMake 的 `SHINE_VERSION`（CMakeLists 里 shine_core 的 PUBLIC 编译定义）。
    // 早先这里是写死的 `const char* version = "v0.2.0"` —— 改版本时不会跟着动，
    // 界面上会一直显示一个与实际构建版本无关的号。
    const std::string version = std::string("v") + SHINE_VERSION;
    const float versionW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, version.data(), version.data() + version.size()).x;
    float rightX = area.max.x - 10.0f - versionW - 16.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX + 8.0f, cy - 5.75f), ColorTextMuted(),
                  version.data(), version.data() + version.size());
    rightX -= themeW + 16.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX + 8.0f, cy - 5.75f), ColorTextSecondary(),
                  themeName.data(), themeName.data() + themeName.size());
    // 96px 细进度 + 运行态文案。
    //
    // ⚠️ 早先这里是 `"T" + (runStageIndex_+1) + "/17 · " + runPercent_ + "%"`，而
    //    runStageIndex_ / runPercent_ **全文件只有「写 0」与「读出来显示」**，没有任何
    //    自增点 —— 那是一个结构上不可能前进的进度条，注释却写着「这里是真实运行态」。
    //    执行体未接入时如实说「执行体未接入」，不做进度条。
    //    另外那个 "17" 是硬编码字面量（T1..T17 恰好 17 个），全仓库没有 kStageCount。
    std::string progress;
    ImU32 progressColor = ColorTextSecondary();
    if (runActive_) {
        progress = "T" + std::to_string(runStageIndex_ + 1) + "/" +
                   std::to_string(TextStageCount()) + " · " + std::to_string(runPercent_) + "%";
    } else if (runFinished_) {
        progress = "已完成";
    } else if (!pages::OverviewPipelineWired()) {
        progress = "阶段执行体未接入";
        progressColor = ColorOf(theme::Current().statusWarn);
    } else {
        progress = "未运行";
    }
    const float progressW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, progress.data(), progress.data() + progress.size()).x;
    rightX -= 96.0f + 8.0f + progressW + 8.0f;
    // 进度条宽度也跟状态走：未接入/未运行时画一条 0% 的空轨道，是在暗示「进度是 0」
    // 而不是「没有进度可言」。
    Progress(draw, Rect{rightX, cy - 2.0f, rightX + 96.0f, cy + 2.0f},
             runActive_ ? static_cast<float>(runPercent_) : 0.0f, runActive_, true);
    rightX -= progressW + 8.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX, cy - 5.75f), progressColor,
                  progress.data(), progress.data() + progress.size());
}

// ---------------------------------------------------------------- P4.8 面包屑
//
// 设计稿 `Crumbs()` 的第三段：小说页取「第 N 章 · 标题」，其余走 `defaultTabName()`。
// ⚠️ 而那份 `defaultTabName` 表里 assets 的「实体 · 林晚」、image 的「分镜图_v3」
//    **本身就是 mock 字符串** —— 照抄等于把假名字写死在界面上。
// 规则：能从真状态推的推，推不出来的用**结构性**标签，一个假名都不编。
std::string Shell::DerivedViewLabel() const {
    switch (layout_.workspace) {
    case static_cast<int>(pages::Workspace::Overview):
        return "总控台";
    case static_cast<int>(pages::Workspace::Novel): {
        const pages::BookSideView& s = pages::BookSide();
        if (!s.bound || s.chapters.empty()) {
            return "章节";
        }
        const int idx = std::clamp(s.selectedChapter, 0, static_cast<int>(s.chapters.size()) - 1);
        const pages::BookChapterView& ch = s.chapters[static_cast<std::size_t>(idx)];
        if (ch.title.empty()) {
            return "第 " + std::to_string(ch.ord) + " 章";
        }
        return "第 " + std::to_string(ch.ord) + " 章 · " + ch.title;
    }
    case static_cast<int>(pages::Workspace::Assets): {
        const pages::BookSideView& s = pages::BookSide();
        if (!s.bound || s.assets.empty()) {
            return "实体";
        }
        const int idx = std::clamp(s.selectedAsset, 0, static_cast<int>(s.assets.size()) - 1);
        const pages::BookAssetView& a = s.assets[static_cast<std::size_t>(idx)];
        // 实体名是空的就不写「实体 · 」（设计稿的「实体 · 林晚」是 mock，不是格式要求）。
        return a.name.empty() ? std::string("实体")
                              : std::string("实体 · ") + a.name;
    }
    case static_cast<int>(pages::Workspace::Storyboard):
        return "镜头表";
    case static_cast<int>(pages::Workspace::ImageFlow):
        return "分镜图";
    case static_cast<int>(pages::Workspace::VideoFlow):
        return "H3 视频";
    case static_cast<int>(pages::Workspace::Gallery):
        return "组件画廊";
    default:
        return layout_.lastViewLabel.empty() ? "总览" : layout_.lastViewLabel;
    }
}

void Shell::DrawBreadcrumbs(Rect area, ImDrawList* draw) {
    // shell.css:223-234 `.crumbs`：height 34 / padding 0 16 / gap 7 / font-size 12 /
    // color text-muted / border-bottom 1px solid line-subtle /
    // background color-mix(in srgb, var(--bg-void) 60%, var(--bg-surface))。
    // 内容与 JSX 逐项对得上（Shell.jsx:124-139）：`.c`(text-secondary / 500) ·
    // `.sep`(opacity .55) · `.c.here`(text-primary / 600) · `.spacer` · `.tiny.dim` 提示。
    //
    // ⚠️ 底色与下边框原先**整条都没有**：这个函数原来只 AddText，于是面包屑在界面上
    //    是「浮在 bg-void 上的一串字」，而设计稿里它是一条独立的浅色横条。
    //    而底色**早就算好了** —— `theme::Derived::crumbBg`（`Theme.cpp:99` 的
    //    `MixSrgb(c.bgVoid, c.bgSurface, 60.0f)`，字段注释直接写着 shell.css:233），
    //    此前**零绘制消费点**，只有 `check-theme` 的自检在读它。
    //    派生色算出来没人用，和没算一样 —— 门禁也管不到（它不是硬编码颜色）。
    draw->AddRectFilled(area.min, area.max, ColorOf(theme::CurrentDerived().crumbBg));
    // border-box（`base.css:2-6` 的 `*` 全局）⇒ 这 1px 边框**含在 34px 高度里**，
    // 内容区少 1px，文字的垂直中心跟着上移 0.5px。
    draw->AddLine(ImVec2(area.min.x, area.max.y - 0.5f), ImVec2(area.max.x, area.max.y - 0.5f),
                  ColorLineSubtle(), 1.0f);
    const float cy = area.center().y - 0.5f;
    // ⚠️ 字号原先写 12.5f，而 12.5 **不在字体档位里**（`Tokens.h:110` 的 kSizes 只有
    //    12/13/14/16/20/28），`LookupNearest` 又**向上**取档 ⇒ 实际渲染在 13px，
    //    比设计稿大一号。写 12.0f 才真的落在 xs 档上。
    constexpr float kCrumbsFont = 12.0f;  // shell.css:230 / base.css:120 的 .tiny
    constexpr float kCrumbsGap = 7.0f;    // shell.css:228 的 flex gap
    // padding: 0 16px（原先左边 24px）
    float x = area.min.x + 16.0f;
    // ⚠️ 第三段以前恒为 "总览"：`SetWorkspace` 无条件写 `lastViewLabel = "总览"`，
    // 而 `lastViewLabel` 全仓没有任何别的地方会改它 ⇒ 切到小说页，面包屑照样是
    // 「项目 › 小说 › 总览」。状态字段没跟动作走，和「只显示不联动」是同一类。
    const std::string derived = DerivedViewLabel();
    const std::string viewLabel = derived.empty() ? std::string("总览") : derived;
    const char* crumbs[] = {"项目", WorkspaceLabel(layout_.workspace), viewLabel.c_str()};
    for (int i = 0; i < 3; ++i) {
        const bool current = (i == 2);
        ImFont* face = current ? FontBoldAt(kCrumbsFont) : FontAt(kCrumbsFont);
        const std::size_t len = std::strlen(crumbs[i]);
        const float w = face->CalcTextSizeA(kCrumbsFont, 1e9f, 0.0f, crumbs[i], crumbs[i] + len).x;
        draw->AddText(face, kCrumbsFont, ImVec2(x, cy - kCrumbsFont * 0.5f),
                      current ? ColorText() : ColorTextSecondary(), crumbs[i], crumbs[i] + len);
        x += w + kCrumbsGap;
        if (i < 2) {
            // ⚠️ 长度用 sizeof() - 1，**不要**写死字节数。
            //    这里原来写的是一个单角引号 U+203A 加 "+ 3"：它在 UTF-8 里正好 3 字节，
            //    当时对得上；但字面量一旦被改写成一个 1 字节的字符（编码往返、编辑器保存），
            //    那个 3 就会越界多读 2 字节 —— 实测面包屑上画成了两个问号
            //    （? 后面跟的是字符串池里恰好相邻的字节，纯属巧合，不报错、不崩，
            //    只是永远画不对）。
            static constexpr char kSep[] = "›";
            const std::size_t sepLen = sizeof(kSep) - 1;
            ImFont* sepFace = FontAt(kCrumbsFont);
            // 分隔符的步进原先写死 12.0f（≈ 7px gap + 5px 字宽，碰巧接近），
            // 现在按设计稿的 flex 语义真去量它的宽度。
            const float sw =
                sepFace->CalcTextSizeA(kCrumbsFont, 1e9f, 0.0f, kSep, kSep + sepLen).x;
            draw->AddText(sepFace, kCrumbsFont, ImVec2(x, cy - kCrumbsFont * 0.5f),
                          WithAlpha(ColorTextMuted(), 0.55f), kSep, kSep + sepLen);
            x += sw + kCrumbsGap;
        }
    }

    // 右侧提示串：JSX 是 `<span className="tiny dim">`（Shell.jsx:138），
    // `.tiny`=12px、`.dim`=text-muted（base.css:118/120）。
    const char* hint = "Ctrl+B 侧栏 · Ctrl+J 底栏 · Ctrl+I 检查器 · Ctrl+K 命令";
    ImFont* font = FontAt(kCrumbsFont);
    const float w =
        font->CalcTextSizeA(kCrumbsFont, 1e9f, 0.0f, hint, hint + std::strlen(hint)).x;
    draw->AddText(font, kCrumbsFont, ImVec2(area.max.x - 16.0f - w, cy - kCrumbsFont * 0.5f),
                  ColorTextMuted(), hint, hint + std::strlen(hint));
}


} // namespace shine::pages
