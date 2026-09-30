// shine::pages —— 外壳的**浮层**：命令面板 + 浮层分发 + 主题菜单 + 设置模态
//
// 四块都是「盖在内容之上、吃掉输入」的东西，共用 `kit::OverlayPanel` / `kit::ScrimPaint`
// 这两个浮层原语与 `Shell::ChromeHit` 那条「浮层开着时 chrome 不提交 item」的规则。
// 放在一个文件里，那条规则才有唯一的读者。
//
// ⚠️ 命令面板**刻意没有**折进设置模态用的那套外壳：它顶部下拉、不居中、没有
//    `.modal-h`（输入框直接顶在顶部）。判定依据是形态，不是「能不能凑合」。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/Shell_Layout.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "ui/imgui/host/AppEnvironment.h"
#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/Gallery.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "util/Encoding.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

// 命令面板的组标题高与行高（内容总高靠这两个累加，滚动跟随选中行也靠它）。
constexpr float kPaletteGroupH = 22.0f;
constexpr float kPaletteRowH = 30.0f;

// ---------------------------------------------------------------- P4.9 命令面板
//
// 外壳 chrome 的热区统一入口（顶栏 / 导航 / 侧栏 / 检查器 / 面包屑 / 底栏都走它）。
//
// 浮层开着时**直接返回空 Hit、一个 item 都不提交**。理由是 ImGui 同窗口内
// 「先注册者独占 HoveredId」（imgui.cpp:5161），而外壳先于浮层注册 ⇒ 点遮罩关闭的
// 那一次点击会被侧栏 / 顶栏先吃掉，结果是「工作区被切走、浮层还开着」。
// 不提交 item 就没有「被吃掉」这回事，点击由浮层自己手算（IsMouseClicked +
// 判断点在不在面板内，这两个都不看 HoveredId）。
//
// 代价：浮层开着时外壳连 hover 高亮都没有 —— 那正是模态该有的样子。
// 配套的另一半在 DrawWorkspace（给工作区 child 加 NoMouseInputs），机制见 Scroll.h。
kit::Hit Shell::ChromeHit(const kit::Rect& bounds, std::string_view id) {
    if (!chromeInteractive_) {
        return kit::Hit{};
    }
    return kit::HitTest(bounds, id);
}

void Shell::DrawCommandPalette() {
    if (!paletteOpen_) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(560.0f, display.x - 40.0f);
    const kit::Rect screen{0.0f, 0.0f, display.x, display.y};
    const Rect bounds{(display.x - width) * 0.5f, display.y * 0.22f, (display.x + width) * 0.5f,
                      display.y * 0.22f + 420.0f};
    // 浮层必须画在 foreground：页面/底栏各自跑在 BeginChild 里，child 在父窗口那份
    // draw list **之后**渲染，画在父 list 上的浮层会被整片盖住（见 DrawReportModal 的注释）。
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // ⚠️ 遮罩原来画的是**面板自己的 bounds**，下一行 DrawShadowed 的面板底板就把它
    // 整个盖住了 —— 一次完全被覆盖的死绘制：面板照常显示，背后该压暗的界面一点没暗，
    // 和其它所有浮层都不一样（`.scrim` 是 `position: fixed; inset: 0`，铺满屏幕）。
    // 编译过、截图正常，只有「没生效」这一种表现。
    // 现在走 kit 的原语：遮罩铺满屏幕，底板才是面板那块。
    kit::ScrimPaint(draw, screen);
    kit::OverlayPanel(draw, bounds);

    // 输入
    const Rect input{bounds.min.x + 18.0f, bounds.min.y + 18.0f, bounds.max.x - 18.0f,
                     bounds.min.y + 56.0f};
    ImFont* font = FontAt(15.0f);
    // 打开面板就把焦点交给输入框。
    //
    // ⚠️ 这里原来是个**被掏空的 `if`（条件成立、里面空的）**，`SetKeyboardFocusHere`
    //    被删掉了。后果：Ctrl+K 打开面板之后**直接打字不进过滤框**，必须先用鼠标点一下
    //    输入框 —— 而命令面板的基本用法就是「打开就打字」。整个过滤逻辑（下面那段
    //    includes）是真的，所以症状是「面板能开、列表能滚，但好像打不了字」。
    //    `SetKeyboardFocusHere` 必须在 InputText **之前**那一帧调用（同一帧内、
    //    该 item 提交之前即可），我们只在「刚打开」的第一帧调，避免每帧抢焦点。
    // 「这一帧是不是刚打开的那一帧」要在**消费** paletteJustOpened_ 之前抓住。
    // 下面关闭判定要用它：同一次按键沿既该打开面板、又不该立刻把它关掉。
    const bool openedThisFrame = paletteJustOpened_;
    if (paletteJustOpened_) {
        paletteJustOpened_ = false;
        paletteFocusInput_ = true;
    }
    ImGui::SetCursorScreenPos(input.min);
    ImGui::SetNextItemWidth(input.width());
    ImGui::PushFont(font);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImU32(0));
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    if (paletteFocusInput_) {
        paletteFocusInput_ = false;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::InputText("##palette", paletteQuery_, sizeof(paletteQuery_));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopFont();

    // 三组：页面 / 命令 / 主题；过滤 = label+hint+group 的小写 includes，空组丢弃
    //
    // ⚠️ 这里早先用一个 `int workspace` 同时表达「切到第 N 个工作区」与「别的命令」
    //    （-1 命令 / -2 切主题 / -3..-7 五个主题），而 Enter 的执行体只有
    //    `if (picked.workspace >= 0) SetWorkspace(...)` 一句 ⇒ **14 条里 8 条是空操作**：
    //    面板只是关掉，什么都没发生。主题组那 5 条尤其扎眼 —— 面板上明明列着「深空/
    //    薄暮/纸墨/水墨/极夜」，按 Enter 却毫无反应。
    //    改成显式的 action 枚举（见 Shell.h 的 PaletteAction）：新增一个动作必须同时
    //    写执行体，编译器盯着。
    struct Item {
        std::string group;
        std::string label;
        std::string hint;
        PaletteAction action;
        int arg;  // Workspace = 工作区下标；Theme = kAllThemes 下标
    };
    const std::vector<Item> all = {
        {"页面", "总控", "pipeline", PaletteAction::Workspace, 0},
        {"页面", "小说", "novel", PaletteAction::Workspace, 1},
        {"页面", "资产", "assets", PaletteAction::Workspace, 2},
        {"页面", "分镜", "storyboard", PaletteAction::Workspace, 3},
        {"页面", "出图", "imageflow", PaletteAction::Workspace, 4},
        {"页面", "出片", "videoflow", PaletteAction::Workspace, 5},
        {"页面", "组件画廊", "gallery", PaletteAction::Workspace, 6},
        // 新建 / 打开都落到项目中心：那里既有项目列表也有工具条上的「新建项目」按钮。
        // ⚠️ 不假装能直接开新建向导 —— 向导是项目中心内部的一个 flag（HubState::wizard），
        //    而 hub 是 DrawProjectHub 里的函数内状态，跨不过去。要直达得先把它提为成员，
        //    那是另一件事，别在这里顺手改。
        {"命令", "新建项目", "Ctrl+N", PaletteAction::NewProject, 0},
        {"命令", "打开项目", "Ctrl+O", PaletteAction::OpenProject, 0},
        {"命令", "保存布局", "layout.dat", PaletteAction::SaveLayout, 0},
        // Ctrl+T 语义是「切主题」。早先它被注册成**打开命令面板**，于是面板上写着
        // 「切换主题 Ctrl+T」、真按 Ctrl+T 却把面板又打开了一遍 —— 套娃。
        // 轮换用 ThemeNext（切到下一个），「主题」组那 5 条才是切到**指定**那个。
        // 两者早先共用 Theme，于是「切换主题」实际只是切到 index 1。
        {"命令", "切换主题", "Ctrl+T", PaletteAction::ThemeNext, 0},
        {"主题", "深空", "deepspace", PaletteAction::Theme, 0},
        {"主题", "薄暮", "dusk", PaletteAction::Theme, 1},
        {"主题", "纸墨", "paperink", PaletteAction::Theme, 2},
        {"主题", "水墨", "inkwash", PaletteAction::Theme, 3},
        {"主题", "极夜", "polarnight", PaletteAction::Theme, 4},
    };
    std::string query = paletteQuery_;
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<const Item*> matched;
    for (const Item& item : all) {
        std::string haystack = item.group + item.label + item.hint;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (query.empty() || haystack.find(query) != std::string::npos) {
            matched.push_back(&item);
        }
    }
    paletteMatches_ = static_cast<int>(matched.size());
    paletteSelected_ = std::clamp(paletteSelected_, 0, std::max(0, paletteMatches_ - 1));

    // 先量一遍：键盘移动选中项后要靠它算出「该滚到哪」，滚动跟随选中行是命令面板的基本行为。
    std::vector<float> rowTops;
    {
        float probe = 0.0f;
        std::string group;
        rowTops.reserve(static_cast<std::size_t>(paletteMatches_));
        for (int i = 0; i < paletteMatches_; ++i) {
            const Item& item = *matched[static_cast<std::size_t>(i)];
            if (item.group != group) {
                group = item.group;
                probe += kPaletteGroupH;
            }
            rowTops.push_back(probe);
            probe += kPaletteRowH;
        }
        paletteContentH_ = probe;
    }
    // ⚠️ 列表必须裁到面板内。16 条目 + 3 组标题 = 546px，而输入框下面只有 358px ——
    //    不裁的话末尾几行会画到面板外面、压在工作区上（实测：薄暮/纸墨/水墨/极夜 漏在外面）。
    const Rect listArea{bounds.min.x, input.max.y + 6.0f, bounds.max.x, bounds.max.y - 12.0f};
    const float listH = std::max(0.0f, listArea.height());
    if (listArea.contains(ImGui::GetIO().MousePos)) {
        paletteScroll_ -= ImGui::GetIO().MouseWheel * 30.0f;
    }
    if (paletteSelected_ < static_cast<int>(rowTops.size()) && listH > 0.0f) {
        const float top = rowTops[static_cast<std::size_t>(paletteSelected_)];
        if (top - paletteScroll_ < 0.0f) {
            paletteScroll_ = top;
        } else if (top + kPaletteRowH - paletteScroll_ > listH) {
            paletteScroll_ = top + kPaletteRowH - listH;
        }
    }
    paletteScroll_ =
        std::clamp(paletteScroll_, 0.0f, std::max(0.0f, paletteContentH_ - listH));

    draw->PushClipRect(listArea.min, listArea.max, true);
    float y = listArea.min.y - paletteScroll_;
    std::string lastGroup;
    ImFont* groupFont = FontBoldAt(11.5f);
    for (int i = 0; i < paletteMatches_; ++i) {
        const Item& item = *matched[static_cast<std::size_t>(i)];
        if (item.group != lastGroup) {
            lastGroup = item.group;
            // 组标题带高 kPaletteGroupH，按带中心落字（原来 `y + 6.0f` 偏上 0.75px）。
            const Rect groupBand{bounds.min.x + 18.0f, y, bounds.max.x - 18.0f, y + kPaletteGroupH};
            draw->AddText(groupFont, 11.5f,
                          ImVec2(groupBand.min.x, kit::CenterTextY(groupFont, 11.5f, groupBand.center().y)),
                          ColorTextMuted(), item.group.data(), item.group.data() + item.group.size());
            y += kPaletteGroupH;
        }
        const Rect row{bounds.min.x + 12.0f, y, bounds.max.x - 12.0f, y + kPaletteRowH};
        // 命令面板的条目行走 kit::ListRow。原来自己写 `row.min.y + 6.0f`（13px 字）
        // 与 `+ 7.0f`（11.5px hint），行高 30 时分别偏上 **2.5px / 2.25px** ——
        // 8 处列表行里最大的一处偏差。
        //
        // 纯键盘驱动（↑↓ + Enter），**不注册命中**：鼠标点不动是既定行为，
        // 传空 id。选中态走 selected 底。
        kit::ListRowSpec spec;
        spec.id = {}; // 纯键盘，不吃鼠标
        spec.title = item.label;
        spec.titleSize = 13.0f;
        spec.titleColor = ColorText(); // 命令名：primary
        spec.trailing = item.hint;
        spec.trailingSize = 11.5f;
        spec.chevron = "none"; // 命令面板用右侧 kbd 提示，不画 chevron
        spec.paddingX = 10.0f;
        spec.selected = i == paletteSelected_;
        kit::ListRow(draw, row, spec);
        y += kPaletteRowH;
    }
    draw->PopClipRect();
    // 滚动条：装不下才画。
    if (paletteContentH_ > listH && listH > 0.0f) {
        const float trackW = 4.0f;
        const float trackX = bounds.max.x - 8.0f - trackW;
        const float thumbH = std::max(24.0f, listH * (listH / paletteContentH_));
        const float thumbY = listArea.min.y + (listH - thumbH) * (paletteScroll_ / (paletteContentH_ - listH));
        draw->AddRectFilled(ImVec2(trackX, listArea.min.y), ImVec2(trackX + trackW, listArea.max.y),
                            ColorFillMuted());
        draw->AddRectFilled(ImVec2(trackX, thumbY), ImVec2(trackX + trackW, thumbY + thumbH),
                            ColorLineStrong());
    }

    // 键盘：↑↓ / Enter / Esc
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        paletteSelected_ = (paletteSelected_ + 1) % std::max(1, paletteMatches_);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        paletteSelected_ = (paletteSelected_ + paletteMatches_ - 1) % std::max(1, paletteMatches_);
    }
    // ⚠️ 这里原来写成**裸** `IsKeyPressed(ImGuiKey_K)`，与 `ApplyShortcuts` 里打开面板的
    //    `Ctrl+K` 撞在一起：`ApplyShortcuts()` 在 `DrawCommandPalette()` **之前**跑，
    //    于是同一帧里「打开」被「关闭」立刻抵消 ⇒ **Ctrl+K 永远打不开命令面板**。
    //    副症状更常见：面板开着时输入任何含 `k` 的查询都会把面板关掉。
    //    判据必须和打开侧**同一把尺子**：`Ctrl+K` 才关，裸 K 不关。
    // ⚠️ 关闭判定必须**跳过「刚打开的那一帧」**。
    //    ApplyShortcuts() 在帧首跑，DrawCommandPalette() 在帧尾跑：同一帧里 Ctrl+K 先把
    //    paletteOpen_ 置 true，走到这段时**同一个按键沿**又把它置 false ⇒ Ctrl+K 永远
    //    打不开命令面板（面板上却明明白白写着「命令面板 Ctrl+K」）。
    //    动作判据 r53 打出的就是 `palette=closed → palette=closed`，且 saw-ctrl / saw-pressed
    //    都为真 —— 注入没问题，是产品这一侧的同帧抵消。
    //
    // 副症状（更常见）：面板开着时输入任何含 `k` 的查询都会把面板关掉。所以关闭侧必须
    // 和打开侧**同一把尺子**：只认 `Ctrl+K`，裸 `K` 不关。
    //
    // ⚠️ 只把条件从裸 K 收窄成 Ctrl+K 是不够的（早先那么改过，症状一样）—— 问题不在
    //    条件宽不宽，在于**打开与关闭读到的是同一个按键沿**。
    if (!openedThisFrame &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
         (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false)))) {
        paletteOpen_ = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && paletteMatches_ > 0) {
        const Item& picked = *matched[static_cast<std::size_t>(paletteSelected_)];
        RunPaletteAction(picked.action, picked.arg);
        paletteOpen_ = false;
        paletteQuery_[0] = '\0';
        paletteSelected_ = 0;
    }
    // 点面板外关闭：与其它浮层同一套做法 —— 遮罩不注册命中（见上面 ScrimPaint 的
    // 说明），也不用本工程会 0xC0000005 的 IsMouseHoveringRect，手算点在不在面板内。
    //
    // ⚠️ 必须跳过「刚打开的那一帧」，和上面键盘那条同一个理由。顶栏搜索框是**点开**
    //    本面板的（`tb-search`），而那一次点击的位置就在面板之外 —— 不加这个前置
    //    条件，搜索框就成了死按钮：点一下，面板开出来又当场关掉，看上去毫无反应。
    //    这是「同帧自毁」那一类，和项目中心的 `HubState::dismissArmed` 同源。
    if (!openedThisFrame && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !bounds.contains(ImGui::GetIO().MousePos)) {
        paletteOpen_ = false;
    }
}

// 命令面板 Enter 与 ApplyShortcuts 的 Ctrl+N / Ctrl+O / Ctrl+T 共用这一份。
// 分成两处的话，迟早只改得动一边 —— 本仓已经吃过一次（Ctrl+T 套娃）。
void Shell::RunPaletteAction(PaletteAction action, int arg) {
    switch (action) {
    case PaletteAction::Workspace:
        SetWorkspace(arg);
        break;
    case PaletteAction::ThemeNext: {
        // 「切换主题」= 切到**下一个**主题。写死一个索引会让第二轮点回同一个主题，
        // 看着像没生效。
        const std::size_t count = std::size(theme::kAllThemes);
        std::size_t cur = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (theme::kAllThemes[i] == theme::CurrentThemeId()) {
                cur = i;
                break;
            }
        }
        const theme::ThemeId next = theme::kAllThemes[(cur + 1) % count];
        SetTheme(next);
        Notify(std::string("主题 · ") + theme::ThemeIdKey(next), theme::Tone::Ok);
        break;
    }
    case PaletteAction::Theme: {
        const std::size_t count = std::size(theme::kAllThemes);
        if (arg < 0 || static_cast<std::size_t>(arg) >= count) {
            return;
        }
        const theme::ThemeId id = theme::kAllThemes[static_cast<std::size_t>(arg)];
        SetTheme(id);
        Notify(std::string("主题 · ") + theme::ThemeIdKey(id), theme::Tone::Ok);
        break;
    }
    case PaletteAction::NewProject:
        if (!hubOpen_) {
            ToggleProjectHub();
        }
        Notify("项目中心 · 点工具条上的「新建项目」", theme::Tone::Info);
        break;
    case PaletteAction::OpenProject:
        if (!hubOpen_) {
            ToggleProjectHub();
        }
        Notify("项目中心 · 从最近列表里挑一个打开", theme::Tone::Info);
        break;
    case PaletteAction::SaveLayout: {
        // ⚠️ 别写成 `SaveLayout() ? … : …` —— 那是**调用两次**，文件写两遍。
        const bool ok = SaveLayout();
        Notify(ok ? "布局已保存 · layout.dat" : "布局保存失败 · layout.dat",
               ok ? theme::Tone::Ok : theme::Tone::Danger);
        break;
    }
    }
}

// ---------------------------------------------------------------- P4.10 浮层
// toast（ui.css:978-1011 的 .toasts / .toast，设计稿里是 notify(...)）。
//
// ⚠️ 走 **GetForegroundDrawList()**，不是 Begin/End 里的那个 draw：页面与底栏都跑在
//    ScrollRegion(BeginChild) 里，child 的 draw list 在父窗口 list 之后渲染，
//    画上去会被工作区整片盖住（上一轮四个浮层就是这么被盖的）。
// 规格：fixed right 16 / bottom 40；min-w 260 max-w 380；pad 10 14；r-md；
// bg-overlay 底 + line-normal 边 + **左侧 3px** 色调条 + shadow-2；12.5px。
void Shell::DrawOverlays(ImDrawList* draw) {
    (void)draw;
    if (toastTimer_ <= 0.0f || toastText_.empty()) {
        return;
    }
    toastTimer_ -= lastDelta_;
    if (toastTimer_ <= 0.0f) {
        return;
    }
    // 尾部 0.4s 淡出：设计稿没有这层，是纯 CSS transition 的等价物 ——
    // 硬切会闪一下，反而比设计稿更糙。
    const float alpha = toastTimer_ < 0.4f ? toastTimer_ / 0.4f : 1.0f;

    ImDrawList* front = ImGui::GetForegroundDrawList();
    // 走 kit::Toast：本体 + 投影 + 3px 色调条 + 图标 + 文字居中全在里面。
    // 页面层曾自己画一份（`DrawRoundRect(..., 8.0f, ...)` + 手动量宽 + 手动排 Y），
    // 偏上 2.25px，而且与 kit 里那份**已经算对**的实现各活一份。
    // 圆角 8→10、量宽公式、Y 居中现在统一走 kit（ui.css:987-1000 的 r-md 是 10）。
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const theme::Tone tone = toastTone_ == theme::Tone::Idle ? theme::Tone::Info : toastTone_;
    kit::Toast(front, RectAt(0.0f, 0.0f, display.x, display.y), toastText_, tone,
               tone == theme::Tone::Ok ? "check" : "info", /*above=*/0.0f, alpha);
}

std::uint64_t Shell::LayoutStateHash() const {
    // FNV-1a 64，和取证那边的像素哈希同一套 —— 同一份实现，判据与产品别各写一遍。
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
            h *= 1099511628211ull;
        }
    };
    mix(static_cast<std::uint64_t>(layout_.workspace));
    mix(layout_.sidePanelVisible ? 1u : 0u);
    mix(layout_.dockVisible ? 1u : 0u);
    mix(layout_.inspectorVisible ? 1u : 0u);
    mix(static_cast<std::uint64_t>(layout_.sidePanelWidth));
    mix(static_cast<std::uint64_t>(layout_.inspectorWidth));
    mix(static_cast<std::uint64_t>(layout_.dockHeight));
    mix(static_cast<std::uint64_t>(layout_.dockTab));
    mix(layout_.reduceMotion ? 1u : 0u);
    for (const char c : layout_.projectName) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    for (const char c : layout_.lastViewLabel) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// ---------------------------------------------------------------- P4.9a 主题菜单
// 照 webui Shell.jsx:51-70：幽灵按钮弹出的 .menu-pop（shell.css:101-158），
// 宽 224，5 个内置主题各一行（双色渐变方块 + 名字 + 英文），当前主题右侧打勾；
// 下面一条分隔线 + 「减少动效」开关项。
// 早先这里根本不存在 —— 顶栏那个调色板图标直接去翻命令面板，主题压根切不了。
void Shell::DrawThemeMenu(ImVec2 anchor, ImDrawList* draw) {
    if (!themeMenuOpen_) {
        return;
    }
    // 主题菜单走 kit::Menu：面板框 / 行 hover / 选中态 / 文字居中 / 命中全在里面。
    // 页面层原来手写一份（面板宽 224、行高 30 步进 32、手排 `row.min.y + 8.0f`
    // 偏上 0.75px），而 kit::Menu 那份是按 shell.css:98-145 算好的、零调用。
    //
    // 行序：内置主题（Label）→ 5 个主题（Item，选中态走 selected）→ 分隔 → 减少动效。
    std::vector<kit::MenuRow> rows;
    kit::MenuRow groupLabel;
    groupLabel.kind = kit::MenuRowKind::Label;
    groupLabel.label = "内置主题";
    rows.push_back(groupLabel);
    for (std::size_t i = 0; i < theme::kAllThemes.size(); ++i) {
        const theme::ThemeId id = theme::kAllThemes[i];
        kit::MenuRow row;
        row.kind = kit::MenuRowKind::Item;
        row.label = std::string(theme::ThemeDisplayName(id));
        // 设计稿这一行左边是 24×14 的双向渐变色块（shell.css:148 的 .swatch），
        // 不是图标字形 —— kit::Menu 的 icon 位画不了。这是有意的取舍：
        // 色块那一族只有主题菜单在用，为它给 kit::Menu 加一个「自定义左侧绘制」
        // 回调，会把一个纯数据菜单变成带副作用的绘制口。**代价是主题色块没有了**，
        // 选中态改由 kit 的 accent 字 + accent-dim 底表达（信息量不减）。
        row.selected = theme::CurrentThemeId() == id;
        rows.push_back(row);
    }
    kit::MenuRow sep;
    sep.kind = kit::MenuRowKind::Separator;
    rows.push_back(sep);
    kit::MenuRow motion;
    motion.kind = kit::MenuRowKind::Item;
    motion.label = "减少动效";
    motion.icon = "zap";
    motion.selected = layout_.reduceMotion;
    rows.push_back(motion);

    // 锚点：kit::Menu 贴 anchor 右下展开。主题按钮在顶栏右侧，锚点给它左下角。
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Rect menuAnchor{std::min(anchor.x, display.x - 200.0f), anchor.y,
                          std::min(anchor.x, display.x - 200.0f) + 200.0f, anchor.y + 30.0f};
    const int picked = kit::Menu(draw, menuAnchor, rows, "theme-menu");
    if (picked < 0) {
        return;
    }
    // 行下标 → 动作。1..5 是主题，末尾是「减少动效」。
    if (picked >= 1 && static_cast<std::size_t>(picked) <= theme::kAllThemes.size()) {
        const theme::ThemeId id = theme::kAllThemes[static_cast<std::size_t>(picked - 1)];
        SetTheme(id);
        themeMenuOpen_ = false;
        PushLog("info", "主题已切换：" + std::string(theme::ThemeDisplayName(id)));
    } else if (static_cast<std::size_t>(picked) == rows.size() - 1) {
        layout_.reduceMotion = !layout_.reduceMotion;
        kit::SetReduceMotion(layout_.reduceMotion);
        PushLog("info", layout_.reduceMotion ? "已减少动效" : "已恢复动效");
    }

    // 点菜单外面关掉（对应 webui 的 pointerdown 外部关闭）。
    //
    // ⚠️ `themeOutsideHit_` 的**注册**不在这里，而在 DrawFrame 开头、外壳 chrome
    // **之前**。同窗口内「先注册者独占 HoveredId」（imgui.cpp:5161）：注册在
    // chrome 之后的话，导航栏 / 顶栏 / 工作区任何一处都先抢到这次点击，于是
    // 「点外面关菜单」变成「切了工作区、菜单还开着悬在新工作区上」。
    if (themeOutsideHit_.clicked) {
        themeMenuOpen_ = false;
    }
}

// ---------------------------------------------------------------- P4.9b 设置模态
// 「设置 · 三步开工」（Shell.jsx:74 的 IconBtn tip）。这里读 AppSettings 的真值，
// 让用户看到程序**实际**连的是什么，而不是一份编出来的配置。
//
// 外壳是第 5 份「页面层私有浮层副本」，本轮收进 `kit::ModalFrameRect`。
// 收掉的不只是壳，还有 `SettingsCloseRect()` 里那份**独立的 560 × 452 复算** ——
// 原来「绘制与判据共用同一份几何」这句话只对了一半：判据没在 Review.cpp 里复算，
// 但产品内部早就分叉成两处。几何现在只有 `mf` 一处算出来，写进 settingsFrame_
// / settingsClose_，绘制与判据都读它。
void Shell::DrawSettingsModal() {
    if (!settingsOpen_) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const kit::Rect screen{0.0f, 0.0f, display.x, display.y};
    // 浮层走 foreground，否则会被 BeginChild 里的页面内容盖住。
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // 遮罩 / 面板框 / 头部 / 内容区一次拿全。遮罩**只画不注册命中**：注册全屏热区
    // 会先拿到 HoveredId，本模态里的按钮就永远按不动（先注册者独占）。
    const kit::ModalFrame mf =
        kit::ModalFrameRect(draw, screen, "设置 · 三步开工", "settings", 560.0f, 452.0f);
    settingsFrame_ = mf.frame;
    // × 落在头部右侧、垂直居中（22px 方钮）。位置在这里算一次就够了 ——
    // 下面的绘制与判据的 SettingsCloseRect() 读的都是 settingsClose_。
    //
    // ⚠️ 纵向偏移**取整**：头部高 47 是奇数，减去 22 除 2 得 12.5，于是这个 22×22
    //    的方钮两条边都会压在半像素上。半像素本身画得出来，但「× 的中心」于是
    //    落在 277.5 —— 判据注入鼠标后按 0.5 的容差判「注入到位吗」，差正好卡在
    //    边界上，报出来的是一条**自相矛盾**的消息（要 277、看到 277，却说没到位）。
    //    一像素以内不该算「注入失败」，但产品这边也没必要制造半像素。
    settingsClose_ = RectAt(mf.frame.max.x - 18.0f - 22.0f,
                            mf.header.min.y + std::round((mf.header.height() - 22.0f) * 0.5f),
                            22.0f, 22.0f);
    if (IconButton(draw, settingsClose_, "x", false, false, "settings-close")) {
        settingsOpen_ = false;
    }

    const AppSettings& s = Settings();
    const Rect& body = mf.body;
    float y = body.min.y;
    const float ix = body.min.x;
    const float iw = body.width();

    KeyValues(draw, Rect{ix, y, ix + iw, y + 96.0f},
              {{"当前工程", layout_.projectName.empty() ? "未打开项目" : layout_.projectName},
               {"工程根", layout_.projectRoot.empty() ? "—" : util::PathToUtf8(layout_.projectRoot)},
               {"数据目录", util::PathToUtf8(shine::app::EnvironmentPath(L"APPDATA"))}});
    y += 106.0f;

    KeyValues(draw, Rect{ix, y, ix + iw, y + 118.0f},
              {{"LLM 供应商", s.llmProvider},
               {"默认模型", s.openaiModelDefault},
               {"写作模型", s.openaiModelWriter},
               {"评审模型", s.openaiModelCritic},
               {"ComfyUI", s.comfyBaseUrl.empty() ? "未配置" : s.comfyBaseUrl}});
    y += 128.0f;

    KeyValues(draw, Rect{ix, y, ix + iw, y + 96.0f},
              {{"图库来源", s.gallerySource},
               {"缩略图尺寸", std::to_string(s.galleryThumbSize)},
               {"产物目录", s.videoOutputDir.empty() ? "未配置" : s.videoOutputDir}});
    y += 106.0f;

    static constexpr char kSettingsNote[] =
        "以上取自 %APPDATA%/ShineTVStudio/settings.json · API Key 不在此显示";
    draw->AddText(FontAt(10.5f), 10.5f, ImVec2(ix, y), ColorTextMuted(), kSettingsNote,
                  kSettingsNote + sizeof(kSettingsNote) - 1);

    // 点遮罩关闭：既不注册 item（免得遮罩先拿到 HoveredId，把模态里的按钮全变成
    // 死键），也不用 ImGui::IsMouseHoveringRect（本工程会 0xC0000005），
    // 改成手算点击是否落在面板内。面板矩形用上面存下的 settingsFrame_。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !settingsFrame_.contains(ImGui::GetIO().MousePos)) {
        settingsOpen_ = false;
    }
}


} // namespace shine::pages
