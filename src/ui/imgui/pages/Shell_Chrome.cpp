// shine::pages —— 外壳的**固定条带**：导航栏 / 状态栏 / 面包屑
//
// 2026-10-02 自本文件拆出顶栏（连同 RequestRunStart / RequestRunStop 的执行体，
// 去了 Shell_TopBar.cpp —— 理由写在那份的头注释里）。留下的三块仍是同一族：
// 贴边、常驻、不随内容滚动、尺寸全部来自 Shell_Layout.h 的固定档，
// 且都只读「当前工程 / 当前工作区」这一份状态。
//
// 侧栏 / 检查器 / 底栏是另外三族（各自有面板状态与列表视口），分别在
// Shell_Sidebar.cpp、Shell_Inspector.cpp 与 Shell_Dock.cpp。
#include "ui/imgui/pages/Shell.h"
#include <optional>

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

namespace {

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

// 导航栏的七个入口。设计稿 shell.css 的 .rail 是一列 icon + label，顺序与
// 工作区下标一一对应。
struct RailEntry {
    const char* icon;
    const char* label;
    int workspace;
};

} // namespace

// ---------------------------------------------------------------- P4.3 导航栏
void Shell::DrawRail(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.max.x - 0.5f, area.min.y), ImVec2(area.max.x - 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 7 个工作区（设计稿序），第 7 项后是分隔线 + 3 个面板开关 + 弹性空隙 + 画廊
    //
    // shell.css:157-183：`.rail{gap:4px;padding:10px 0}` + `.rail-btn{40×40;r10}`
    // ⇒ 行距固定 44（= 40 + 4），行高 40 —— 不是 48/44 混着来。
    // ⚠️ 三处曾按「铺满 56px 栏宽」画，全部订正：
    //    ① 选中底画成 44×44（应 40×40，左右各留 8px 栏内边距）；
    //    ② hover 底压根没画，而 CSS 有 `.rail-btn:hover{background:var(--fill-hover)}`；
    //    ③ 左侧 2.5px accent 竖条按 `area.min.x - 8` 落在**栏外**（x = -8..-5.5），
    //       被裁掉 ⇒ 设计稿里最显眼的「当前工作区」标识一直是不可见的。
    //       `.rail-btn.on::before{left:-8px}` 的 -8 是相对 **40px 按钮盒**的左边；
    //       按钮在栏内居中 ⇒ 盒左沿 = 栏中线 - 20，竖条绝对 x = 栏中线 - 28 .. -25.5。
    constexpr float kBtn = 40.0f;
    constexpr float kPitch = kBtn + 5.0f;
    constexpr float kRailContentHeight = kPitch * 11.0f + 26.0f;
    const bool compact = area.height() < kRailContentHeight;
    std::optional<ScrollRegion> scroll;
    if (compact) {
        scroll.emplace("rail-scroll", area);
        area = scroll->content();
        draw = scroll->drawList();
        scroll->setContentHeight(kRailContentHeight);
    }
    const float btnLeft = area.center().x - 0.5f * kBtn;
    float y = area.min.y + 10.0f;
    // 7 个 id 恒定：提 static 表，循环内不再 `"rail-ws-" + to_string(i)`（第二十轮波 3）。
    static const char* const kRailIds[7] = {"rail-ws-0", "rail-ws-1", "rail-ws-2", "rail-ws-3",
                                            "rail-ws-4", "rail-ws-5", "rail-ws-6"};
    for (int i = 0; i < 7; ++i) {
        const bool on = layout_.workspace == i;
        const Rect item{btnLeft, y, btnLeft + kBtn, y + kBtn};
        // ⚠️ 一次 HitTest 拿走 hovered + clicked。早先这里先 Hovered("rail-hover-N")
        // 再 Clicked("rail-ws-N") —— 两个 InvisibleButton 落在**同一矩形**上，
        // ImGui 的 ItemHoverable 只让先注册的那个拿到 HoveredId（HoveredAllowOverlap 默认 false），
        // 第二个永远 hovered=false / clicked=false，整个导航栏点不动。
        // 命中测试必须排在画底之前：hover 底要用 hit.hovered，而它只从这一次测试里来。
        const Hit hit = ChromeHit(item, kRailIds[i]);
        // `.rail-btn.on`（两个类，权重 0,2,0）与 `.rail-btn:hover`（类+伪类，同样 0,2,0）
        // 同权重，而 `.on` 在 CSS 里更靠后 ⇒ 选中压过 hover，两个底不会同时画。
        if (on) {
            DrawRoundRect(draw, item.min, item.max, 10.0f, ColorFillSelected());
        } else if (hit.hovered) {
            DrawRoundRect(draw, item.min, item.max, 10.0f, ColorFillHover());
        }
        if (on) {
            const Rect bar{area.center().x - 28.0f, y + 9.0f, area.center().x - 25.5f,
                           y + kBtn - 9.0f};
            DrawRoundRect(draw, bar.min, bar.max, 3.0f, 0, ColorAccentGlow(), 6.0f);
            DrawRoundRect(draw, bar.min, bar.max, 3.0f, ColorAccent());
        }
        const ImU32 foreground = on ? ColorAccent() : ColorTextSecondary();
        DrawIconCentered(draw, WorkspaceIcon(i), ImVec2(item.center().x, item.min.y + 12.0f),
                         16.0f, foreground);
        const TextStyle labelStyle{.size = theme::font::kSmall, .bold = on, .color = foreground};
        const char* label = WorkspaceLabel(i);
        Text(draw, ImVec2(item.center().x - TextWidth(label, labelStyle) * 0.5f, item.min.y + 24.0f),
             label, labelStyle);
        if (hit.clicked) {
            SetWorkspace(i);
        }
        y += kPitch;
    }

    // 分隔线 24×1：`.rail-sep{margin:5px 0}` ⇒ 上 5px + 线 1px + 下 5px
    y += 5.0f;
    draw->AddLine(ImVec2(area.center().x - 12.0f, y), ImVec2(area.center().x + 12.0f, y),
                  ColorLineSubtle(), 1.0f);
    y += 6.0f;

    struct Toggle {
        const char* icon;
        const char* tip;
        void (Shell::*action)();
    };
    const Toggle toggles[] = {
        {"panelL", "侧栏 Ctrl+B", &Shell::ToggleSidePanel},
        {"terminal", "底栏 Ctrl+J", &Shell::ToggleDock},
        {"panel", "检查器 Ctrl+I", &Shell::ToggleInspector},
    };
    // 三个面板开关。⚠️ 设计稿这里**没有**常驻高亮：
    // webui Shell.jsx:100-108 给侧栏/检查器写的是 `panels.x ? '' : ' active'`，
    // 但整个 CSS 里只有 `.icon-btn.active`（ui.css:102），**根本没有 .rail-btn.active 规则**
    // —— 那个 active 类是死类，不产生任何视觉。早先这里画了一层常驻 fill-hover，
    // 等于凭空多出一个设计稿没有的常亮态；只保留 hover（shell.css 的 .rail-btn:hover）。
    for (const Toggle& toggle : toggles) {
        const Rect item{btnLeft, y, btnLeft + kBtn, y + kBtn};
        // ⚠️ 一帧里只 HitTest 一次。同一个 id 注册两个 InvisibleButton 会让 hover 整个
        //    失效（实测：hover-jump-btn 与静息态逐像素零差异）。hovered / clicked 从
        //    同一次命中测试里取。
        const kit::Hit hit = ChromeHit(item, "rail-tg-" + std::string(toggle.icon));
        if (hit.hovered) {
            DrawRoundRect(draw, item.min, item.max, 10.0f, ColorFillHover());
            ImGui::SetTooltip("%s", toggle.tip);
        }
        DrawIconCentered(draw, toggle.icon, item.center(), 19.0f,
                         hit.hovered ? ColorText() : ColorTextMuted());
        if (hit.clicked) {
            (this->*toggle.action)();
        }
        y += kPitch;
    }

    // 弹性空隙后固定「组件画廊」（hidden: true，只在这一栏出现）。
    // `.rail{padding:10px 0}` ⇒ 底沿留 10px；按钮仍是 40×40，所以是 58→50 那一段。
    {
        const float galleryY = compact ? y : area.max.y - 10.0f - kBtn;
        const Rect gallery = RectAt(btnLeft, galleryY, kBtn, kBtn);
        const Hit hit = ChromeHit(gallery, "rail-gallery");
        const bool on = layout_.workspace == 6;
        if (on) {
            DrawRoundRect(draw, gallery.min, gallery.max, 10.0f, ColorFillSelected());
        } else if (hit.hovered) {
            DrawRoundRect(draw, gallery.min, gallery.max, 10.0f, ColorFillHover());
        }
        DrawIconCentered(draw, "grid", gallery.center(), 19.0f,
                         on ? ColorAccent() : (hit.hovered ? ColorText() : ColorTextMuted()));
        if (hit.clicked) {
            SetWorkspace(6);
        }
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
        const auto connection = session.State();
        const bool comfyUp = comfyConfigured && connection == comfy::ConnectionState::Connected;
        const bool comfyConnecting = comfyConfigured && connection == comfy::ConnectionState::Connecting;
        const comfy::QueueModel::Counts counts = session.Queue().CountsSnapshot();
        const int queued = counts.pending + counts.running;

        // 「队列 N」只随 queued 变：值没变就复用（第二十轮波 3）。
        static int cachedQueued = -1;
        static std::string queueText;
        if (cachedQueued != queued) {
            cachedQueued = queued;
            queueText = kit::Fmt("队列 {}", queued);
        }
        struct Item {
            const char* text;
            theme::Tone tone;
        };
        const Item items[3] = {
            {comfyUp ? "Comfy 已连接" : comfyConnecting ? "Comfy 连接中"
                        : comfyConfigured ? "Comfy 未连接" : "Comfy 未配置",
             comfyUp ? theme::Tone::Ok : comfyConnecting ? theme::Tone::Busy
                       : comfyConfigured ? theme::Tone::Warn : theme::Tone::Idle},
            {llmOk ? "LLM 就绪" : "LLM 未配置", llmOk ? theme::Tone::Ok : theme::Tone::Idle},
            // ⚠️ 队列数要活到本函数返回：早先写成 ("队列 " + to_string(n)).c_str()，
            //    那个临时 string 在完整表达式结束就析构，指针当场悬空。
            {queueText.c_str(), theme::Tone::Idle},
        };
        // 状态栏全部 11.5 档（theme::font::kLabel）—— 一份 style 伺候画与量。
        const TextStyle sbStyle{.size = theme::font::kTiny, .color = ColorTextSecondary()};
        for (const Item& item : items) {
            const float w = TextSize(std::string_view{item.text, std::strlen(item.text)}, sbStyle).x +
                            16.0f;
            DrawRoundRect(draw, ImVec2(x, cy - 10.0f), ImVec2(x + w, cy + 10.0f), 4.0f, 0);
            StatusDot(draw, ImVec2(x + 7.5f, cy), item.tone, item.tone == theme::Tone::Busy);
            Text(draw, ImVec2(x + 16.0f, cy - sbStyle.size * 0.5f),
                 std::string_view{item.text, std::strlen(item.text)}, sbStyle);
            x += w + 6.0f;
        }
    }

    // 主题名只随主题切换变（第二十轮波 3）。
    static int cachedThemeId = -1;
    static std::string themeName;
    const int themeId = static_cast<int>(theme::CurrentThemeId());
    if (cachedThemeId != themeId) {
        cachedThemeId = themeId;
        themeName = std::string("主题：") + theme::ThemeDisplayName(theme::CurrentThemeId());
    }
    // 版本号取 CMake 的 `SHINE_VERSION`（CMakeLists 里 shine_core 的 PUBLIC 编译定义）。
    // 早先这里是写死的 `const char* version = "v0.2.0"` —— 改版本时不会跟着动，
    // 界面上会一直显示一个与实际构建版本无关的号。恒定 ⇒ static。
    static const std::string version = std::string("v") + SHINE_VERSION;
    const TextStyle sbMuted{.size = theme::font::kTiny, .color = ColorTextMuted()};
    const TextStyle sbSecondary{.size = theme::font::kTiny, .color = ColorTextSecondary()};
    const float themeW = TextSize(themeName, sbSecondary).x;
    const float versionW = TextSize(version, sbMuted).x;
    float rightX = area.max.x - 10.0f - versionW - 16.0f;
    Text(draw, ImVec2(rightX + 8.0f, cy - sbMuted.size * 0.5f), version, sbMuted);
    rightX -= themeW + 16.0f;
    Text(draw, ImVec2(rightX + 8.0f, cy - sbSecondary.size * 0.5f), themeName, sbSecondary);
    // 96px 细进度 + 运行态文案。
    //
    // ⚠️ 早先这里是 `"T" + (runStageIndex_+1) + "/17 · " + runPercent_ + "%"`，而
    //    runStageIndex_ / runPercent_ **全文件只有「写 0」与「读出来显示」**，没有任何
    //    自增点 —— 那是一个结构上不可能前进的进度条，注释却写着「这里是真实运行态」。
    //    执行体未接入时如实说「执行体未接入」，不做进度条。
    //    另外那个 "17" 是硬编码字面量（T1..T17 恰好 17 个），全仓库没有 kStageCount。
    // 进度文案只随 (运行态, 阶段, 百分比) 变：状态键没变就复用（第二十轮波 3）。
    // runPercent_ 是 0..100 的整数 ⇒ 键不会撞。
    const int runKey = runActive_ ? 1000000 + runStageIndex_ * 1000 + runPercent_
                                  : (runFinished_ ? 2 : (pages::OverviewPipelineWired() ? 1 : 0));
    static int cachedRunKey = -1;
    static std::string progress;
    ImU32 progressColor = ColorTextSecondary();
    if (cachedRunKey != runKey) {
        cachedRunKey = runKey;
        if (runActive_) {
            progress = kit::Fmt("T{}/{} · {}%", runStageIndex_ + 1, TextStageCount(), runPercent_);
        } else if (runFinished_) {
            progress = "已完成";
        } else if (!pages::OverviewPipelineWired()) {
            progress = "阶段执行体未接入";
        } else {
            progress = "未运行";
        }
    }
    if (!runActive_ && !runFinished_ && !pages::OverviewPipelineWired()) {
        progressColor = ColorOf(theme::Current().statusWarn);
    }
    const TextStyle progressStyle{.size = theme::font::kTiny, .color = progressColor};
    const float progressW = TextSize(progress, progressStyle).x;
    const float textRight = rightX - 8.0f;
    const float progressBarWidth = (runActive_ && textRight - x > progressW + 118.0f) ? 96.0f : 0.0f;
    rightX = textRight - progressBarWidth;
    // 进度条宽度也跟状态走：未接入/未运行时画一条 0% 的空轨道，是在暗示「进度是 0」
    // 而不是「没有进度可言」。
    if (progressBarWidth > 0.0f) {
        Progress(draw, RectAt(rightX, cy - 2.0f, progressBarWidth, 4.0f),
                 static_cast<float>(runPercent_), true, true);
        rightX -= 8.0f;
    }
    const float progressX = std::max(x, rightX - progressW);
    TextClipped(draw, ImVec2(progressX, CenterY(cy, progressStyle)), rightX - progressX,
                progress, progressStyle);
}

// ---------------------------------------------------------------- P4.8 面包屑
//
// 设计稿 `Crumbs()` 的第三段：小说页取「第 N 章 · 标题」，其余走 `defaultTabName()`。
// ⚠️ 而那份 `defaultTabName` 表里 assets 的「实体 · 林晚」、image 的「分镜图_v3」
//    **本身就是 mock 字符串** —— 照抄等于把假名字写死在界面上。
// 规则：能从真状态推的推，推不出来的用**结构性**标签，一个假名都不编。
const std::string& Shell::DerivedViewLabel() const {
    // 指纹覆盖全部分支用到的输入（第二十轮波 3）：工作区、lastViewLabel、
    // 绑定态、选中章（ord/标题）、选中实体名。变了才重建，否则整帧复用缓存。
    std::uint64_t key = HashMix(0, static_cast<std::uint64_t>(layout_.workspace));
    key = HashMix(key, Fnv1a(layout_.lastViewLabel));
    const pages::BookSideView& bs = pages::BookSide();
    key = HashMix(key, bs.bound ? 1ull : 0ull);
    key = HashMix(key, static_cast<std::uint64_t>(bs.chapters.size()));
    key = HashMix(key, static_cast<std::uint64_t>(bs.selectedChapter));
    if (!bs.chapters.empty()) {
        const int chIdx =
            std::clamp(bs.selectedChapter, 0, static_cast<int>(bs.chapters.size()) - 1);
        const pages::BookChapterView& ch = bs.chapters[static_cast<std::size_t>(chIdx)];
        key = HashMix(key, static_cast<std::uint64_t>(ch.ord));
        key = HashMix(key, Fnv1a(ch.title));
    }
    key = HashMix(key, static_cast<std::uint64_t>(bs.assets.size()));
    key = HashMix(key, static_cast<std::uint64_t>(bs.selectedAsset));
    if (!bs.assets.empty()) {
        const int aIdx = std::clamp(bs.selectedAsset, 0, static_cast<int>(bs.assets.size()) - 1);
        key = HashMix(key, Fnv1a(bs.assets[static_cast<std::size_t>(aIdx)].name));
    }
    if (key == viewLabelKey_) {
        return viewLabelCache_;
    }
    viewLabelKey_ = key;
    switch (layout_.workspace) {
    case static_cast<int>(pages::Workspace::Overview):
        viewLabelCache_ = "总控台";
        break;
    case static_cast<int>(pages::Workspace::Novel): {
        if (!bs.bound || bs.chapters.empty()) {
            viewLabelCache_ = "章节";
            break;
        }
        const int chIdx =
            std::clamp(bs.selectedChapter, 0, static_cast<int>(bs.chapters.size()) - 1);
        const pages::BookChapterView& ch = bs.chapters[static_cast<std::size_t>(chIdx)];
        viewLabelCache_ = ch.title.empty() ? kit::Fmt("第 {} 章", ch.ord)
                                           : kit::Fmt("第 {} 章 · {}", ch.ord, ch.title);
        break;
    }
    case static_cast<int>(pages::Workspace::Assets): {
        if (!bs.bound || bs.assets.empty()) {
            viewLabelCache_ = "实体";
            break;
        }
        const int aIdx = std::clamp(bs.selectedAsset, 0, static_cast<int>(bs.assets.size()) - 1);
        const pages::BookAssetView& a = bs.assets[static_cast<std::size_t>(aIdx)];
        // 实体名是空的就不写「实体 · 」（设计稿的「实体 · 林晚」是 mock，不是格式要求）。
        viewLabelCache_ = a.name.empty() ? std::string("实体") : "实体 · " + a.name;
        break;
    }
    case static_cast<int>(pages::Workspace::Storyboard):
        viewLabelCache_ = "镜头表";
        break;
    case static_cast<int>(pages::Workspace::ImageFlow):
        viewLabelCache_ = "分镜图";
        break;
    case static_cast<int>(pages::Workspace::VideoFlow):
        viewLabelCache_ = "H3 视频";
        break;
    case static_cast<int>(pages::Workspace::Gallery):
        viewLabelCache_ = "组件画廊";
        break;
    default:
        viewLabelCache_ =
            layout_.lastViewLabel.empty() ? std::string("总览") : layout_.lastViewLabel;
        break;
    }
    return viewLabelCache_;
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
    // ⚠️ 字号原先写 12.5f，而 12.5 **不在字体档位里**（`LookupNearest` 向上取档）
    //    ⇒ 实际渲染在 13px，比设计稿大一号。落 12.0（theme::font::kTiny）才对。
    //    R3 起字号只写 token：kTiny 就 .tiny 这一档的唯一定义处。
    constexpr float kCrumbsFont = theme::font::kTiny;
    constexpr float kCrumbsGap = 7.0f;    // shell.css:228 的 flex gap
    // padding: 0 16px（原先左边 24px）
    float x = area.min.x + 16.0f;
    // ⚠️ 第三段以前恒为 "总览"：`SetWorkspace` 无条件写 `lastViewLabel = "总览"`，
    // 而 `lastViewLabel` 全仓没有任何别的地方会改它 ⇒ 切到小说页，面包屑照样是
    // 「项目 › 小说 › 总览」。状态字段没跟动作走，和「只显示不联动」是同一类。
    // DerivedViewLabel 返回缓存引用：这里只取引用不再拷贝（第二十轮波 3）。
    static const std::string kOverviewFallback = "总览";
    const std::string& derived = DerivedViewLabel();
    const std::string& viewLabel = derived.empty() ? kOverviewFallback : derived;
    // 第一段是**真实工程名**，不是字面量「项目」：webui Shell.jsx:126 写的是
    // `project?.name || '无项目'`（顶栏胶囊那里才是 `|| '未打开项目'`，两处回退串不同，
    // 不要统一）。早先这里是死的 "项目"，于是界面上永远看不出打开的是哪个工程。
    const std::string rootLabel =
        layout_.projectName.empty() ? std::string("无项目") : layout_.projectName;
    // Shell.jsx:128-136：当第 2 段的文字与第 3 段**恰好相等**时只渲染两段
    // （否则组件画廊页会出现「组件画廊 › 组件画廊」这种重复）。
    std::vector<std::string> parts;
    parts.push_back(rootLabel);
    if (WorkspaceLabel(layout_.workspace) != viewLabel) {
        parts.push_back(WorkspaceLabel(layout_.workspace));
    }
    parts.push_back(viewLabel);

    const TextStyle crumbStyle{.size = kCrumbsFont, .color = ColorTextSecondary()};
    const TextStyle currentStyle{.size = kCrumbsFont, .bold = true, .color = ColorText()};
    const std::string_view separator = "›";
    const float sepWidth = TextWidth(separator, crumbStyle);
    const float sepSpan = sepWidth + 2.0f * kCrumbsGap;
    const std::string_view hint = "Ctrl+B 侧栏 · Ctrl+J 底栏 · Ctrl+I 检查器 · Ctrl+K 命令";
    const float hintWidth = TextWidth(hint, crumbStyle);
    const float right = area.max.x - 16.0f;
    const bool showHint = right - x >= hintWidth + 420.0f;
    const float contentRight = showHint ? right - hintWidth - 20.0f : right;
    const float viewWidth = TextWidth(parts.back(), currentStyle);
    const float prefixLimit = contentRight - std::min(viewWidth, (contentRight - x) * 0.55f);
    const std::size_t last = parts.size() - 1;
    for (std::size_t i = 0; i < last; ++i) {
        const float limit = (prefixLimit - x) / static_cast<float>(last - i) - sepSpan;
        if (limit < 32.0f) {
            break;
        }
        x += TextClipped(draw, ImVec2(x, CenterY(cy, crumbStyle)), limit, parts[i], crumbStyle);
        x += kCrumbsGap;
        Text(draw, ImVec2(x, CenterY(cy, crumbStyle)), separator, crumbStyle);
        x += sepWidth + kCrumbsGap;
    }
    TextClipped(draw, ImVec2(x, CenterY(cy, currentStyle)), contentRight - x, parts.back(), currentStyle);
    if (showHint) {
        Text(draw, ImVec2(right - hintWidth, CenterY(cy, crumbStyle)), hint, crumbStyle);
    }
}


} // namespace shine::pages
