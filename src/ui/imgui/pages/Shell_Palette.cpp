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
#include "comfy/ComfySession.h"  // ComfyUI 端点即改即生效（SetBaseUrl）
#include "llm/OpenAIProvider.h"  // provider/protocol 枚举的 id 串（存取用，不另造词表）
#include "ui/imgui/host/AppEnvironment.h"
#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Fmt.h"
#include "ui/imgui/kit/Hash.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/kit/Text.h"
#include "ui/imgui/pages/Gallery.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "util/Encoding.h"

#include <algorithm>
#include <optional>
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
    return kit::detail::HitTestItem(bounds, id, true);
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
    // 浮层原先画在 foreground（防页面 child 盖住），但**查询框是真 InputText**：
    // 它的框线 / 文字 / 光标画在所属窗口自己的 draw list（imgui_widgets.cpp 的
    // RenderFrame / RenderText 走 window->DrawList），而 foreground 在所有窗口
    // list 之上 ⇒ 面板壳把输入框的字整个压在下面 ——「打字看不见」（设置模态同款坑，
    // 见 DrawSettingsModal 的长注释）。从本轮起与设置模态同款：**帧尾全屏 child**
    // 作模态层，面板壳 / 列表 / InputText 同一条 list、按提交顺序画 ⇒ 控件在壳后可见。
    // 全屏 child 吃掉 hover = 底下点不动（模态语义）；它不是 ScrollRegion，不读
    // OverlayMouseGate。其它浮层（报告模态 / 主题菜单 / 查看器）没有真控件，照旧
    // foreground，z 序不变（DrawCommandPalette 仍在 DrawSettingsModal 之前调）。
    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("palette-modal-layer", display, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // ⚠️ 遮罩原来画的是**面板自己的 bounds**，下一行 DrawShadowed 的面板底板就把它
    // 整个盖住了 —— 一次完全被覆盖的死绘制：面板照常显示，背后该压暗的界面一点没暗，
    // 和其它所有浮层都不一样（`.scrim` 是 `position: fixed; inset: 0`，铺满屏幕）。
    // 编译过、截图正常，只有「没生效」这一种表现。
    // 现在走 kit 的原语：遮罩铺满屏幕，底板才是面板那块。
    kit::ScrimPaint(draw, screen);
    kit::OverlayPanel(draw, bounds);

    const float inputBottom = bounds.min.y + 56.0f;
    const float inputCenterY = (bounds.min.y + inputBottom) * 0.5f;
    constexpr float kSearchIconSize = 16.0f;
    constexpr float kKeycapHeight = 18.0f;
    const float searchX = bounds.min.x + 18.0f;
    const float escWidth = KbdWidth("Esc");
    const float escX = bounds.max.x - 18.0f - escWidth;
    DrawIcon(draw, "search", ImVec2(searchX, inputCenterY - kSearchIconSize * 0.5f),
             kSearchIconSize, ColorTextMuted());
    Kbd(draw, RectAt(escX, inputCenterY - kKeycapHeight * 0.5f, escWidth, kKeycapHeight), "Esc");
    draw->AddLine(ImVec2(bounds.min.x, inputBottom), ImVec2(bounds.max.x, inputBottom),
                  ColorLineSubtle());

    // 开启时的点击或 Ctrl+K 不能被下面的关闭逻辑再处理一次。
    const bool openedThisFrame = paletteJustOpened_;
    paletteJustOpened_ = false;
    const bool acceptingFocus = paletteFocusInput_ && !openedThisFrame;
    paletteFocusInput_ = openedThisFrame;
    if (openedThisFrame) {
        paletteQuery_[0] = '\0';
    }

    // ImGui 在键盘焦点生效的那一帧会丢弃字符队列。用输入框的回调接住这批字符。
    const auto& io = ImGui::GetIO();
    std::string firstText;
    if (acceptingFocus && (!io.KeyCtrl || io.KeyAlt)) {
        std::wstring characters;
        for (ImWchar c : io.InputQueueCharacters) {
            if (c >= L' ' && c != L'\x7f' && (c < L'\uE000' || c > L'\uF8FF')) {
                characters.push_back(static_cast<wchar_t>(c));
            }
        }
        firstText = util::Utf16ToUtf8(characters);
    }
    ImGui::PushFont(FontAt(theme::font::kModal), theme::font::kModal);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ColorTransparent());
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    const float inputX = searchX + kSearchIconSize + 10.0f;
    ImGui::SetCursorScreenPos(ImVec2(inputX, inputCenterY - ImGui::GetFrameHeight() * 0.5f));
    ImGui::SetNextItemWidth(escX - 10.0f - inputX);
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_None;
    if (openedThisFrame) {
        ImGui::SetKeyboardFocusHere();
        // 避免快速重开时，ImGui 把上次失焦的内容写回刚清空的查询。
        inputFlags |= ImGuiInputTextFlags_ReadOnly;
    }
    if (!firstText.empty()) {
        inputFlags |= ImGuiInputTextFlags_CallbackAlways;
    }
    ImGui::InputTextWithHint("##palette", "输入命令搜索…", paletteQuery_, sizeof(paletteQuery_),
                             inputFlags, [](ImGuiInputTextCallbackData* data) {
                                 if (data->EventActivated) {
                                     const auto& text = *static_cast<const std::string*>(data->UserData);
                                     data->InsertChars(data->CursorPos, text.data(), text.data() + text.size());
                                 }
                                 return 0;
                             }, &firstText);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
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
    // 条目表恒定：提 static（每帧不再重建 16 条 Item，第二十轮波 3）。
    static const std::vector<Item> all = {
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
    // 条目表 / 检索 haystack / 匹配结果 / 行顶表全部与查询一一对应：
    // 查询没变就整帧复用（第二十轮波 3 —— 面板打开期间原先每帧重建 16 条
    // Item、每条一个 haystack 串，以及两个 vector）。UI 单线程 ⇒ 函数级 static。
    static const std::vector<std::string> haystacks = [] {
        std::vector<std::string> hs;
        hs.reserve(all.size());
        for (const Item& item : all) {
            std::string h = item.group + item.label + item.hint;
            std::transform(h.begin(), h.end(), h.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            hs.push_back(std::move(h));
        }
        return hs;
    }();
    static std::optional<std::string> cachedQuery;
    static std::vector<const Item*> matched;
    static std::vector<float> rowTops;
    if (!cachedQuery || *cachedQuery != paletteQuery_) {
        cachedQuery = paletteQuery_;
        std::string query = *cachedQuery;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        matched.clear();
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (query.empty() || haystacks[i].find(query) != std::string::npos) {
                matched.push_back(&all[i]);
            }
        }
        // 先量一遍：键盘移动选中项后要靠它算出「该滚到哪」，滚动跟随选中行是命令面板的基本行为。
        rowTops.clear();
        rowTops.reserve(matched.size());
        float probe = 0.0f;
        std::string group;
        for (const Item* item : matched) {
            if (item->group != group) {
                group = item->group;
                probe += kPaletteGroupH;
            }
            rowTops.push_back(probe);
            probe += kPaletteRowH;
        }
    }
    paletteMatches_ = static_cast<int>(matched.size());
    paletteContentH_ = rowTops.empty() ? 0.0f : rowTops.back() + kPaletteRowH;
    paletteSelected_ = std::clamp(paletteSelected_, 0, std::max(0, paletteMatches_ - 1));
    // 列表独立裁剪和滚动，给操作说明留出固定页脚。
    const float footerTop = bounds.max.y - 36.0f;
    const Rect listArea{bounds.min.x, inputBottom + 6.0f, bounds.max.x, footerTop};
    const float listH = std::max(0.0f, listArea.height());
    const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
    const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
    if (paletteMatches_ > 0 && (up || down)) {
        paletteSelected_ = (paletteSelected_ + (down ? 1 : -1) + paletteMatches_) % paletteMatches_;
    }
    const Rect track{ImVec2(listArea.max.x - 10.0f, listArea.min.y),
                     ImVec2(listArea.max.x - 2.0f, listArea.max.y)};
    OverlayScrollbar(draw, track, kPaletteRowH, paletteContentH_, listH,
                     paletteScroll_, "palette-scrollbar", listArea);
    if ((up || down) && paletteSelected_ < static_cast<int>(rowTops.size()) && listH > 0.0f) {
        const float top = rowTops[static_cast<std::size_t>(paletteSelected_)];
        if (top - paletteScroll_ < 0.0f) {
            paletteScroll_ = top;
        } else if (top + kPaletteRowH - paletteScroll_ > listH) {
            paletteScroll_ = top + kPaletteRowH - listH;
        }
    }
    paletteScroll_ =
        std::clamp(paletteScroll_, 0.0f, std::max(0.0f, paletteContentH_ - listH));

    draw->PushClipRect(listArea.min, ImVec2(track.min.x - 2.0f, listArea.max.y), true);
    if (matched.empty()) {
        Text(draw, ImVec2(searchX, listArea.min.y + 8.0f), "没有匹配的命令，换个关键词试试",
             TextStyle{.size = theme::font::kHint, .color = ColorTextSecondary()});
    }
    int activated = -1;
    float y = listArea.min.y - paletteScroll_;
    std::string lastGroup;
    const TextStyle groupStyle{.size = theme::font::kLabel, .bold = true, .color = ColorTextMuted()};
    for (int i = 0; i < paletteMatches_; ++i) {
        const Item& item = *matched[static_cast<std::size_t>(i)];
        if (item.group != lastGroup) {
            lastGroup = item.group;
            // 组标题带高 kPaletteGroupH，按带中心落字（原来 `y + 6.0f` 偏上 0.75px）。
            const Rect groupBand{bounds.min.x + 18.0f, y, bounds.max.x - 18.0f, y + kPaletteGroupH};
            Text(draw, ImVec2(groupBand.min.x, CenterY(groupBand.center().y, groupStyle)),
                 item.group, groupStyle);
            y += kPaletteGroupH;
        }
        const Rect row{bounds.min.x + 12.0f, y, bounds.max.x - 12.0f, y + kPaletteRowH};
        // 命令面板的条目行走 kit::ListRow。原来自己写 `row.min.y + 6.0f`（13px 字）
        // 与 `+ 7.0f`（11.5px hint），行高 30 时分别偏上 **2.5px / 2.25px** ——
        // 8 处列表行里最大的一处偏差。
        kit::ListRowSpec spec;
        const std::string rowId = Fmt("palette-item-{}", i);
        spec.id = rowId;
        spec.suppressed = row.min.y < listArea.min.y || row.max.y > listArea.max.y;
        spec.title = item.label;
        spec.titleSize = 13.0f;
        spec.titleColor = ColorText(); // 命令名：primary
        spec.trailing = item.hint;
        spec.trailingSize = 11.5f;
        spec.chevron = "none"; // 命令面板用右侧 kbd 提示，不画 chevron
        spec.paddingX = 10.0f;
        spec.selected = i == paletteSelected_;
        if (kit::ListRow(draw, row, spec).clicked) {
            activated = i;
        }
        y += kPaletteRowH;
    }
    draw->PopClipRect();
    draw->AddLine(ImVec2(bounds.min.x, footerTop), ImVec2(bounds.max.x, footerTop),
                  ColorLineSubtle());
    const TextStyle footerStyle{.size = theme::font::kMicro, .color = ColorTextMuted()};
    TextClipped(draw, ImVec2(searchX, CenterY((footerTop + bounds.max.y) * 0.5f, footerStyle)),
                bounds.width() - 36.0f, "↑↓ 选择 · Enter 执行 · Esc 关闭", footerStyle);
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
    if (paletteOpen_ &&
        (activated >= 0 || (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && paletteMatches_ > 0))) {
        const int selected = activated >= 0 ? activated : paletteSelected_;
        const Item& picked = *matched[static_cast<std::size_t>(selected)];
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
    ImGui::EndChild();  // palette-modal-layer
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
static std::string_view ToastIcon(theme::Tone tone) {
    switch (tone) {
    case theme::Tone::Ok: return "check";
    case theme::Tone::Warn:
    case theme::Tone::Danger: return "alert";
    default: return "info"; // Info、Busy 与其他状态沿用信息图标。
    }
}

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

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const theme::Tone tone = toastTone_ == theme::Tone::Idle ? theme::Tone::Info : toastTone_;
    Rect frame;
    kit::Toast(ImGui::GetForegroundDrawList(), RectAt(0.0f, 0.0f, display.x, display.y),
               toastText_, tone, ToastIcon(tone), /*above=*/0.0f, alpha, &frame);

    // 前景 draw list 只绘制。用独立的透明窗口接收点击，避免点到下方的控件。
    const ImVec2 frameSize(frame.width(), frame.height());
    ImGui::SetNextWindowPos(frame.min);
    ImGui::SetNextWindowSize(frameSize);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##shine-toast", nullptr, flags)) {
        ImGui::SetCursorScreenPos(frame.min);
        if (ImGui::InvisibleButton("dismiss", frameSize)) {
            toastTimer_ = 0.0f;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }
    ImGui::End();
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
    const bool openedThisFrame = themeMenuJustOpened_;
    themeMenuJustOpened_ = false;
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
    const int picked = kit::Menu(draw, menuAnchor, rows, "theme-menu", &themeMenuPanel_);
    // 全屏热区会抢走菜单行的 HoveredId。直接用面板矩形判断外部点击，
    // 跳过打开菜单的那一帧，避免顶栏点击把刚打开的菜单关掉。
    if (!openedThisFrame && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !themeMenuPanel_.contains(ImGui::GetIO().MousePos)) {
        themeMenuOpen_ = false;
        return;
    }
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
    //
    // ⚠️ 但**本模态从本轮起不画在 foreground**，改画在一个全屏 child 的 draw list 里。
    //    原因是 LLM 区块引入了 `kit::Input`（真正的 ImGui 控件）：它的框线 / 文字 /
    //    光标画在**所属窗口自己的** draw list（imgui_widgets.cpp 的 RenderFrame /
    //    RenderText 走 window->DrawList），而 foreground 在所有窗口 list **之上**
    //    —— 面板壳画 foreground 会把输入框的字整个压在下面（画了等于没画）。
    //    全屏 child 排在工作区所有 child **之后**提交，draw list 按提交顺序渲染 ⇒
    //    它在最上层；面板壳 / KV 行 / 输入控件又都在这一条 list 里按提交顺序画，
    //    控件在面板壳之后 ⇒ 可见。命令面板的查询框曾是同款坑，已同款修复
    //    （见 DrawCommandPalette 的 modal-layer 注释）。
    //    别的浮层（报告模态 / 主题菜单 / 查看器 / toast）没有真控件，照旧 foreground，
    //    z 序不变（它们仍在本层之上，与改动前的调用顺序一致）。
    //    这个 child 就是**模态层**：全屏吃掉 hover = 底下点不动（模态语义）；它不是
    //    ScrollRegion，不读 OverlayMouseGate。
    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("settings-modal-layer", display, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 遮罩 / 面板框 / 头部 / 内容区一次拿全。遮罩**只画不注册命中**：注册全屏热区
    // 会先拿到 HoveredId，本模态里的按钮就永远按不动（先注册者独占）。
    //
    // 面板高度以紧凑摘要和 LLM 表单为准。小窗口由正文滚动区承载，关闭按钮随面板定位。
    const kit::ModalFrame mf =
        kit::ModalFrameRect(draw, screen, "项目与服务设置", "settings", 680.0f, 732.0f);
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

    // 可变引用：下方 LLM 区块要往里写（改完按函数末尾的时机 SaveSettings）。
    AppSettings& s = Settings();
    std::optional<ScrollRegion> scroll;
    scroll.emplace("settings-content", mf.body, false, false, false);
    const Rect body = scroll->content();
    draw = scroll->drawList();
    const float ix = body.min.x;
    const float iw = body.width();

    // 三组 KV 的显示缓存（第二十轮波 3）：指纹覆盖全部字符串输入（Fnv1a 不分配），
    // 工程根用 filesystem::path 的 == 比较（也不分配）；变了才重建 11 行。
    std::uint64_t kvKey = HashMix(0, Fnv1a(layout_.projectName));
    kvKey = HashMix(kvKey, Fnv1a(s.llmProvider));
    kvKey = HashMix(kvKey, Fnv1a(s.openaiModelDefault));
    kvKey = HashMix(kvKey, Fnv1a(s.openaiModelWriter));
    kvKey = HashMix(kvKey, Fnv1a(s.openaiModelCritic));
    kvKey = HashMix(kvKey, Fnv1a(s.comfyBaseUrl));
    kvKey = HashMix(kvKey, Fnv1a(s.gallerySource));
    kvKey = HashMix(kvKey, Fnv1a(s.videoOutputDir));
    kvKey = HashMix(kvKey, static_cast<std::uint64_t>(s.galleryThumbSize));
    if (!settingsKvValid_ || settingsKvKey_ != kvKey ||
        settingsKvRootPath_ != layout_.projectRoot) {
        settingsKvValid_ = true;
        settingsKvKey_ = kvKey;
        settingsKvRootPath_ = layout_.projectRoot;
        settingsKvRows_ = {
            {"当前工程", layout_.projectName.empty() ? "未打开项目" : layout_.projectName},
            {"工程根", layout_.projectRoot.empty() ? "—" : util::PathToUtf8(layout_.projectRoot)},
            {"数据目录", util::PathToUtf8(shine::app::EnvironmentPath(L"APPDATA"))},
            {"LLM 供应商", s.llmProvider},
            {"默认模型", s.openaiModelDefault},
            {"写作模型", s.openaiModelWriter.empty() ? "沿用默认模型" : s.openaiModelWriter},
            {"评审模型", s.openaiModelCritic.empty() ? "沿用默认模型" : s.openaiModelCritic},
            {"ComfyUI", s.comfyBaseUrl.empty() ? "未配置" : s.comfyBaseUrl},
            {"图库来源", s.gallerySource},
            {"缩略图尺寸", kit::Fmt("{}", s.galleryThumbSize)},
            {"产物目录", s.videoOutputDir.empty() ? "未配置" : s.videoOutputDir},
        };
    }
    kit::Stack rows(body, 8.0f);
    const auto drawSummaryGroup = [&](std::size_t offset, std::size_t count) {
        const Rect group = rows.Next(KeyValuesHeight(count), theme::space::kSteps[4]);
        KeyValues(draw, group, settingsKvRows_.data() + offset, count);
    };
    drawSummaryGroup(0, 3);
    drawSummaryGroup(3, 5);
    drawSummaryGroup(8, 3);

    static constexpr char kSettingsNote[] =
        "以上取自 %APPDATA%/ShineTVStudio/settings.json · API Key 不在此显示";
    Text(draw, rows.Next(24.0f, 0.0f).min, std::string_view(kSettingsNote, sizeof(kSettingsNote) - 1),
         TextStyle{.size = theme::font::kSmall, .color = ColorTextMuted()});

    // ---------------- LLM 配置（可编辑区块）----------------
    // 供应商（4 值）/ 协议（3 值）枚举 + 端点三行 + 按角色模型 + 两个开关。
    // 枚举值一律用 llm::ProviderId / ProtocolId 的 **id 串**存取（不另造词表）；
    // key 只写 settings.json，任何日志都不打（SaveSettings 也只记字节数）。
    const TextStyle llmHeadStyle{.size = theme::font::kTiny, .bold = true,
                                 .color = ColorTextSecondary()};
    const Rect llmHead = rows.Next(24.0f, 0.0f);
    Text(draw, ImVec2(ix, CenterY(llmHead.min.y + 9.0f, llmHeadStyle)), "LLM 配置", llmHeadStyle);

    // 行标签口径照抄 KeyValues 的「键列」（Widget_Status.cpp）：12.5 常规 muted，
    // 列宽 = 最宽标签、夹在 45% 之内；键值间距 14。每帧量宽但不分配（TextWidth 不造串）。
    const TextStyle llmKeyStyle{.size = theme::font::kHint, .color = ColorTextMuted()};
    static constexpr std::string_view kLlmLabels[] = {
        "供应商", "协议", "Base URL", "API Key", "模型",
        "规划模型", "写作模型", "评审模型", "提取模型",
        "MiMo Token Plan", "MiniMax 关思考", "ComfyUI",
    };
    float labelW = 0.0f;
    for (const std::string_view lab : kLlmLabels) {
        labelW = std::max(labelW, TextWidth(lab, llmKeyStyle));
    }
    labelW = std::min(labelW, iw * 0.45f);
    const float inX = ix + labelW + 14.0f;   // 单列行控件左沿
    const float colW = (iw - 12.0f) * 0.5f;  // 两列版式（按角色模型 / 开关），列间 12
    const float colBX = ix + colW + 12.0f;
    // 本帧有没有编辑动作。落盘时机统一在函数**末尾**（点遮罩关闭之后）按指纹兜底。
    bool llmEdited = false;

    const auto rowLabel = [&](float x, float rowTop, float rowH, std::string_view text) {
        Text(draw, ImVec2(x, CenterY(rowTop + rowH * 0.5f, llmKeyStyle)), text, llmKeyStyle);
    };

    // 枚举选项表恒定 ⇒ 提 static（每帧零堆分配，照 Gallery.cpp:160 的口径）。
    // value 用 ProviderId / ProtocolId 的字面量（静态存储，寿命长过本次调用）。
    static const std::vector<SegmentOption> kProviderSeg = {
        {llm::ProviderId(llm::Provider::OpenAi), llm::ProviderLabel(llm::Provider::OpenAi)},
        {llm::ProviderId(llm::Provider::MiMo), llm::ProviderLabel(llm::Provider::MiMo)},
        {llm::ProviderId(llm::Provider::MiniMax), llm::ProviderLabel(llm::Provider::MiniMax)},
        {llm::ProviderId(llm::Provider::Custom), llm::ProviderLabel(llm::Provider::Custom)},
    };
    static const std::vector<SegmentOption> kProtocolSeg = {
        {llm::ProtocolId(llm::Protocol::ChatCompletions),
         llm::ProtocolId(llm::Protocol::ChatCompletions)},
        {llm::ProtocolId(llm::Protocol::Responses), llm::ProtocolId(llm::Protocol::Responses)},
        {llm::ProtocolId(llm::Protocol::Anthropic), llm::ProtocolId(llm::Protocol::Anthropic)},
    };

    // 供应商（4 值）。⚠️ Segmented 不持状态、**返回值必须存回**，丢弃 = 点不动
    //    （Gallery.cpp:140 那条注释踩过的坑）；存回先拷 std::string 再赋值 ——
    //    没点击时返回的 view 指回 Settings 自己那条 string，直接 assign 是自引用。
    // 行推进统一走 kit::Stack（R4 三件套）——此前这里手写 `y += rh + 8.0f` 被
    // find-manual-list 当场抓成新增违规（布局游标就是防「改高度漏改推进」的）。
    // Next(h, gap) 与原手写推进逐位等价：32+8 / 30+8 / 26+0 / 22+0。
    {
        const float rh = 32.0f;
        const Rect row = rows.Next(rh);
        rowLabel(ix, row.min.y, rh, "供应商");
        const std::string_view picked =
            Segmented(draw, RectAt(inX, row.min.y, SegmentedWidth(kProviderSeg), rh), kProviderSeg,
                      s.llmProvider, "set-llm-prov");
        if (picked != std::string_view(s.llmProvider)) {
            s.llmProvider = std::string(picked);
            llmEdited = true;
        }
    }
    // 协议（3 值）：chat_completions | responses | anthropic（标签就是 id 串本身）。
    {
        const float rh = 32.0f;
        const Rect row = rows.Next(rh);
        rowLabel(ix, row.min.y, rh, "协议");
        const std::string_view picked =
            Segmented(draw, RectAt(inX, row.min.y, SegmentedWidth(kProtocolSeg), rh), kProtocolSeg,
                      s.llmProtocol, "set-llm-proto");
        if (picked != std::string_view(s.llmProtocol)) {
            s.llmProtocol = std::string(picked);
            llmEdited = true;
        }
    }

    // 端点三行按当前 provider 绑字段（openai&custom 共用 openai* 三字段）。
    std::string* fBase = &s.openaiBaseUrl;
    std::string* fKey = &s.openaiApiKey;
    std::string* fModel = &s.openaiModelDefault;
    if (const llm::Provider p = llm::ParseProvider(s.llmProvider); p == llm::Provider::MiMo) {
        fBase = &s.mimoBaseUrl;
        fKey = &s.mimoApiKey;
        fModel = &s.mimoModel;
    } else if (p == llm::Provider::MiniMax) {
        fBase = &s.minimaxBaseUrl;
        fKey = &s.minimaxApiKey;
        fModel = &s.minimaxModel;
    }
    // 单元格 = 标签（labelW）+ 14 间距 + 输入框（补满 cellW）。id 全部 ≤15 字符
    // （kit::Input 的 PushID 走 std::string，短串走 SSO 不分配）。
    const auto inputCell = [&](float x, float rowTop, float cellW, std::string_view lab,
                               std::string& value, std::string_view hint, std::string_view id,
                               ImGuiInputTextFlags flags = ImGuiInputTextFlags_None) {
        rowLabel(x, rowTop, 30.0f, lab);
        // ⚠️ kit::Input 的返回值**必须吃**（EnterReturnsTrue：回车才为真）；文本本身
        //    打字即写回 Settings 字段，落盘时机见函数末尾的指纹兜底。
        if (Input(draw, RectAt(x + labelW + 14.0f, rowTop, cellW - labelW - 14.0f, 30.0f), value,
                  hint, id, flags)) {
            llmEdited = true;
        }
    };
    inputCell(ix, rows.Next(30.0f).min.y, iw, "Base URL", *fBase, "空 = 默认端点", "set-llm-base");
    inputCell(ix, rows.Next(30.0f).min.y, iw, "API Key", *fKey, "仅存本地 · 不打日志",
              "set-llm-key", ImGuiInputTextFlags_Password);
    inputCell(ix, rows.Next(30.0f).min.y, iw, "模型", *fModel, "模型 id", "set-llm-model");

    // 按角色模型（可选行，空 = 跟默认模型）：openaiModelPlanner/Writer/Critic/Extractor。
    {
        const float rowY = rows.Next(30.0f).min.y;
        inputCell(ix, rowY, colW, "规划模型", s.openaiModelPlanner, "空 = 跟默认模型",
                  "set-llm-plan");
        inputCell(colBX, rowY, iw - colW - 12.0f, "写作模型", s.openaiModelWriter,
                  "空 = 跟默认模型", "set-llm-write");
    }
    {
        const float rowY = rows.Next(30.0f).min.y;
        inputCell(ix, rowY, colW, "评审模型", s.openaiModelCritic, "空 = 跟默认模型",
                  "set-llm-crit");
        inputCell(colBX, rowY, iw - colW - 12.0f, "提取模型", s.openaiModelExtractor,
                  "空 = 跟默认模型", "set-llm-extr");
    }

    // 顺带两个开关（同一区块内，保持简单）：mimoTokenPlan / minimaxDisableThinking。
    // ⚠️ Switch 的 bool 是**按值**传的，返回「本帧被翻转」—— 返回值丢弃 = 点不动
    //    （Gallery.cpp:198 那条注释踩过的坑），必须拿它翻状态。
    {
        const Rect row = rows.Next(26.0f, 0.0f);
        const float swY = row.min.y + (26.0f - 19.0f) * 0.5f;
        rowLabel(ix, row.min.y, 26.0f, "MiMo Token Plan");
        if (Switch(draw, RectAt(inX, swY, 34.0f, 19.0f), s.mimoTokenPlan, "set-llm-tp")) {
            s.mimoTokenPlan = !s.mimoTokenPlan;
            llmEdited = true;
        }
        rowLabel(colBX, row.min.y, 26.0f, "MiniMax 关思考");
        if (Switch(draw, RectAt(colBX + labelW + 14.0f, swY, 34.0f, 19.0f),
                   s.minimaxDisableThinking, "set-llm-think")) {
            s.minimaxDisableThinking = !s.minimaxDisableThinking;
            llmEdited = true;
        }
    }
    // ComfyUI 端点：空 = **未配置**（不探测、不重连）。行内打字即写回 s.comfyBaseUrl，
    // 真正生效在下方落盘时机块里推给 `ComfySession::SetBaseUrl` —— 即改即生效，
    // 不用重启（SetBaseUrl 幂等：规范化后与当前值相同就什么都不做）。
    {
        const float rh = 30.0f;
        const Rect row = rows.Next(rh);
        inputCell(ix, row.min.y, iw, "ComfyUI", s.comfyBaseUrl,
                  "空 = 未配置（不探测）· 本机默认 http://127.0.0.1:8188", "set-comfy-url");
    }
    {
        const Rect row = rows.Next(22.0f, 0.0f);
        Text(draw, ImVec2(ix, row.min.y + 2.0f),
             "改动即写回 settings.json · API Key 仅存本地，不打进日志",
             TextStyle{.size = theme::font::kSmall, .color = ColorTextMuted()});
    }

    // 点遮罩关闭：既不注册 item（免得遮罩先拿到 HoveredId，把模态里的按钮全变成
    // 死键），也不用 ImGui::IsMouseHoveringRect（本工程会 0xC0000005），
    // 改成手算点击是否落在面板内。面板矩形用上面存下的 settingsFrame_。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !settingsFrame_.contains(ImGui::GetIO().MousePos)) {
        settingsOpen_ = false;
    }

    // ---- LLM 区块的落盘时机（这块的保存习惯就定在这一条）----
    // kit::Input 的文本**打字即写回** Settings 字段（ImGui 直改 string），但
    // EnterReturnsTrue 下「changed」只在回车为真 —— 只按 changed 落盘会把
    // 「打了字没回车就点走」的编辑丢在内存里。所以按**指纹**判「有没有没落盘的
    // 改动」（Fnv1a 串接不分配），三类时机各落一次：
    //   · 本帧有编辑动作（分段 / 开关被点、输入框回车，llmEdited）→ 当帧；
    //   · 文本编辑态结束（IsAnyItemActive() 转假，即失焦那一帧）→ 当帧；
    //   · 模态被关掉（× / 点遮罩，上面两处已把 settingsOpen_ 置 false）→ 关闭帧兜底。
    // 首帧只登记指纹不落盘（免得没编辑也重写一遍 settings.json）。
    // ⚠️ 全程不打印 key：SaveSettings 只记字节数，这里也不加任何 PushLog。
    std::uint64_t llmKey = Fnv1a(s.llmProvider);
    llmKey = HashMix(llmKey, Fnv1a(s.llmProtocol));
    llmKey = HashMix(llmKey, Fnv1a(s.openaiBaseUrl));
    llmKey = HashMix(llmKey, Fnv1a(s.openaiApiKey));
    llmKey = HashMix(llmKey, Fnv1a(s.openaiModelDefault));
    llmKey = HashMix(llmKey, Fnv1a(s.openaiModelPlanner));
    llmKey = HashMix(llmKey, Fnv1a(s.openaiModelWriter));
    llmKey = HashMix(llmKey, Fnv1a(s.openaiModelCritic));
    llmKey = HashMix(llmKey, Fnv1a(s.openaiModelExtractor));
    llmKey = HashMix(llmKey, Fnv1a(s.mimoBaseUrl));
    llmKey = HashMix(llmKey, Fnv1a(s.mimoApiKey));
    llmKey = HashMix(llmKey, Fnv1a(s.mimoModel));
    llmKey = HashMix(llmKey, Fnv1a(s.minimaxBaseUrl));
    llmKey = HashMix(llmKey, Fnv1a(s.minimaxApiKey));
    llmKey = HashMix(llmKey, Fnv1a(s.minimaxModel));
    llmKey = HashMix(llmKey, static_cast<std::uint64_t>(s.mimoTokenPlan ? 1 : 0));
    llmKey = HashMix(llmKey, static_cast<std::uint64_t>(s.minimaxDisableThinking ? 1 : 0));
    llmKey = HashMix(llmKey, Fnv1a(s.comfyBaseUrl)); // ComfyUI 端点同落盘时机（见下）
    if (!settingsLlmKeyValid_) {
        settingsLlmKeyValid_ = true;
        settingsLlmKey_ = llmKey;
    } else if (llmKey != settingsLlmKey_ &&
               (llmEdited || !ImGui::IsAnyItemActive() || !settingsOpen_)) {
        settingsLlmKey_ = llmKey;
        SaveSettings();
        // ComfyUI 端点改动**即改即生效**（2026-11-24 接上）：同一落盘时机推给会话 ——
        // 换地址立即重连，清空立即停止探测。SetBaseUrl 幂等，多推无害。
        // ⚠️ 之前 `SetBaseUrl` 全仓零调用点：改地址只落盘、要重启才生效。
        comfy::ComfySession::Instance().SetBaseUrl(s.comfyBaseUrl);
    }

    scroll->setContentHeight(rows.Cur() - body.min.y + 8.0f);
    scroll.reset();
    ImGui::EndChild();  // settings-modal-layer（与开头的 BeginChild 配对）
}


} // namespace shine::pages
