// shine::pages —— P4.11 项目中心（全屏，不套外壳）· 页的**骨架**
//
// 数据全部来自 shine::project，本页不再有任何编造的卡片：
//   最近列表 = ProjectService::Recent()  —— 读 %APPDATA%/ShineTVStudio/projects.json
//   卡片副行 = project.json 的 premise；封面标签 / meta = project.json 的 templateId
//   打开     = ProjectService::Open()    —— 真读 project.json + 置顶索引
//   新建     = ProjectService::Create()  —— 真落骨架（ValidateSpec + Materialize）
//   移除     = ProjectIndex::Remove()    —— 只摘登记，不删项目文件
// 业务层返回空就是空态，**不补假卡**。
//
// 拆分后的形状（**纯结构，零行为变化**）：
//   Hub.h            这一页的私有契约（HubCard / HubState / IO 操作 / 四块子区域）
//   HubState.cpp     状态 + 索引 IO + 纯函数小工具（零绘制）
//   HubCard.cpp      项目卡
//   HubWizard.cpp    新建向导（4 步）
//   HubDialogs.cpp   「打开…」列表 + 移除确认框
//   ProjectHub.cpp（本文件）背景 / 品牌头 / 工具条 / 计数行 / 卡片栅格 / 空态 /
//                     浮层派发 / 自报栅格高度
//
// 本文件留下的**只有布局与时序**。特别是那两段 `Refilter` / `Refresh` 的调用位置：
// 搜索过滤必须排在工具条**之后**（本帧敲进来的字立刻生效），排序切换必须紧跟
// Segmented 的返回值 —— 这两条是时序，不是排版，所以留在派发层。
#include "ui/imgui/pages/PageCommon.h"

#include "ui/imgui/pages/Hub.h"
#include "util/Encoding.h"
#include "util/Strings.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

namespace {

// 三个对话框（向导 / 打开 / 确认）**有没有开着**的读数。
//
// ⚠️ 这个变量在一个**很长的**匿名 ns 里（HubCard / HubState 那一整段，到
//    DrawProjectHub 之前才关），所以 `HubDialogOpen()` 的**定义不能**写在它
//    旁边 —— 那样它也是内部链接，Shell.cpp 调它会得到
//    `undefined reference`，而**编译那一轮是过的**（只报链接错），很容易被
//    当成别的问题。定义放在文件末尾（`} // namespace` 之后）。
//
// 理由（为什么需要这个读数）见 WorkspacePages.h 的声明处。
bool g_hubDialogOpen = false;

} // namespace

// 全屏，不套外壳。居中列 max-w 1080，底两层径向渐变 + 240px 高 ArtInk 带。
void DrawProjectHub(Rect area, ImDrawList* draw) {
    hub::HubState& hub = hub::S();
    // 帧首快照：这一帧**开始时**有没有对话框开着。两个用途。
    //
    // ⚠️ `dismissArmed` 是必须的，不是保险：点工具条上「打开项目」的那一下点击，
    //    在**同一帧**里 `hub.openDlg` 才被置 true、对话框才被画出来，而
    //    `ImGui::IsMouseClicked()` 在那一帧**仍然是真**、鼠标仍然停在工具条上
    //    （对话框矩形之外）⇒ 「点面板外关闭」当场把刚开的对话框关掉。
    //    症状是「点『打开项目』→ 闪一下就没了」：编译过、截图正常、打开之前
    //    一切正常，**只有真的点一下才看得见**。是浮层按钮判据抓到的。
    //
    // 读数也用这个帧首值：它回答的是「这一帧开始时对话框开着吗」，而不是
    // 「这一帧处理完还开着吗」—— 后者会把「刚开又关」报成「一直开着」。
    const bool wasOpen = hub.wizard || hub.openDlg || hub.confirm;
    g_hubDialogOpen = wasOpen;
    hub.dismissArmed = wasOpen;
    if (!hub.loaded) {
        if (util::Trim(hub.dir).empty()) {
            // 位置默认给当前工作目录：ValidateSpec 要求父目录真实存在，
            // 设计稿里写死的 E:\projects 在别的机器上不一定有。
            std::error_code ec;
            hub.dir = util::PathToUtf8(std::filesystem::current_path(ec));
            if (ec) {
                hub.dir = ".";
            }
        }
        hub::Refresh(hub);
    }

    DrawVGradient(draw, area.min, area.max, 0.0f, ColorOf(theme::Current().bgVoid),
                  ColorOf(theme::CurrentDerived().accentDim));
    ArtInk(draw, Rect{area.min.x, area.max.y - 240.0f, area.max.x, area.max.y}, 3, 0.3f);

    const float columnW = std::min(1080.0f, area.width() - 64.0f);
    const Rect column{(area.width() - columnW) * 0.5f, 40.0f, (area.width() + columnW) * 0.5f,
                      area.max.y - 40.0f};

    const float float_ = ReduceMotion() ? 0.0f : (Pulse(5.0f) - 0.5f) * 8.0f;
    const Rect logo{column.min.x, column.min.y + float_, column.min.x + 52.0f,
                    column.min.y + 52.0f + float_};
    DrawRoundRect(draw, ImVec2(logo.min.x, logo.min.y - 4.0f), ImVec2(logo.max.x, logo.max.y + 4.0f),
                  18.0f, 0, ColorAccentGlow(), 8.0f);
    DrawDiagGradient(draw, logo.min, logo.max, 14.0f, ColorAccent(), ColorAccentHover());
    DrawIconCentered(draw, "play", logo.center(), 26.0f, ColorAccentFg());
    const char* brand = "ShineTV Studio";
    draw->AddText(FontBoldAt(26.0f), 26.0f, ImVec2(logo.max.x + 16.0f, column.min.y + 8.0f),
                  ColorText(), brand, brand + std::strlen(brand));
    const char* subtitle = "把小说变成画面";
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(logo.max.x + 16.0f, column.min.y + 40.0f),
                  ColorTextMuted(), subtitle, subtitle + std::strlen(subtitle));

    // ---- 工具条：搜索 / 排序 / 打开… / 新建项目 ----
    const float barY = column.min.y + 76.0f;
    const Rect search{column.min.x, barY, column.min.x + 280.0f, barY + 30.0f};
    // 设计稿的搜索框是「图标 + 输入」合一（.search）。kit::Input 的左内边距只有
    // 10px、塞不下 14px 图标，所以输入框右移 24px，再在没 hover/没聚焦时把它
    // 自己的左边框擦掉 —— 两个控件合起来仍然是一个 280px 的圆角搜索框。
    const Rect searchInput{search.min.x + 24.0f, search.min.y, search.max.x, search.max.y};
    Input(draw, searchInput, hub.query, "搜索项目…", "hub-search");
    if (!ImGui::IsItemFocused() && !ImGui::IsItemHovered()) {
        draw->AddRectFilled(searchInput.min, ImVec2(searchInput.min.x + 1.0f, searchInput.max.y),
                            ColorFillMuted());
    }
    DrawIcon(draw, "search", ImVec2(search.min.x + 8.0f, search.center().y - 7.0f), 14.0f,
             ColorTextMuted());

    const std::vector<SegmentOption> sortOptions{{"r", "最近打开"}, {"n", "名称"}};
    const std::string_view picked = Segmented(
        draw, RectAt(search.max.x + 10.0f, barY, SegmentedWidth(sortOptions), 30.0f), sortOptions,
        hub.sort, "hub-sort");
    if (picked != hub.sort) {
        hub.sort = std::string(picked);
        hub::Refresh(hub); // 排序换了要真重排（Refresh 按 hub.sort 排）
    }

    ButtonSpec openProjectSpec;
    openProjectSpec.variant = ButtonVariant::Secondary;
    openProjectSpec.icon = "folder";
    const char* openLabel = "打开…";
    const float openW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontAt(13.0f), 13.0f, openLabel));
    const bool openClicked =
        Button(draw, RectAt(column.max.x - openW, barY, openW, 30.0f), openLabel, openProjectSpec,
               "hub-open");
    ButtonSpec newProject;
    newProject.variant = ButtonVariant::Primary;
    newProject.icon = "plus";
    const char* newLabel = "新建项目";
    const float newW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, newLabel));
    const bool newClicked =
        Button(draw, RectAt(column.max.x - openW - 10.0f - newW, barY, newW, 30.0f), newLabel,
               newProject, "hub-new");
    if (openClicked) {
        hub.openDlg = true;
    }
    if (newClicked) {
        hub.wizard = true;
        hub.step = 0;
        hub.status.clear();
        hub.statusError = false;
    }
    // 搜索过滤放在工具条之后：本帧输入框里刚敲进来的字立刻生效
    hub::Refilter(hub);

    // ---- 计数行：搜索结果条数 + 上一次操作的结果 ----
    const Rect count{column.min.x, barY + 30.0f + 22.0f, column.max.x, barY + 30.0f + 22.0f + 16.0f};
    const std::string countText = "最近项目 · 共 " + std::to_string(hub.shown.size()) + " 个项目";
    // 原来写死 `count.min.y + 1.0f`：计数行高 16，正确中心是 +8，字盒中心落在 7，
    // **偏上 1.0px**。计数与右侧状态共用同一个 Rect，一起按容器中心算。
    draw->AddText(FontBoldAt(12.0f), 12.0f,
                  ImVec2(count.min.x, kit::CenterTextY(FontBoldAt(12.0f), 12.0f, count.center().y)),
                  ColorTextMuted(), countText.data(), countText.data() + countText.size());
    if (!hub.status.empty()) {
        const float maxW = column.width() * 0.5f;
        const float statusW = std::min(LabelWidth(FontAt(12.0f), 12.0f, hub.status.c_str()), maxW);
        // 同上：原来 `count.min.y + 1.0f`，**偏上 1.0px**。右对齐但 Y 走同一个中心线。
        DrawTextClipped(draw, FontAt(12.0f), 12.0f,
                        ImVec2(count.max.x - statusW,
                               kit::CenterTextY(FontAt(12.0f), 12.0f, count.center().y)),
                        statusW, hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextMuted(),
                        hub.status);
    }

    // ---- 卡片栅格 / 空态 ----
    const Rect grid{column.min.x, count.max.y + 10.0f, column.max.x, column.max.y - 8.0f};
    if (hub.shown.empty()) {
        const Rect emptyCard{grid.min.x, grid.min.y, grid.max.x, grid.min.y + 260.0f};
        DrawShadowed(draw, emptyCard.min, emptyCard.max, 10.0f, ColorPanel(), ColorLineSubtle(),
                     1.0f);
        const bool filtering = !util::Trim(hub.query).empty();
        const char* emptyTitle = filtering ? "没有匹配的项目" : "还没有项目";
        const char* emptyBody =
            filtering ? "换个关键词试试，或新建一个项目开始你的第一部作品"
                      : "点「新建项目」开始你的第一部作品";
        Empty(draw, Rect{emptyCard.min.x, emptyCard.min.y + 60.0f, emptyCard.max.x,
                         emptyCard.max.y - 60.0f},
              "folder", emptyTitle, emptyBody);
    } else {
        constexpr float cardH = 250.0f; // 120 封面 + pbody(12+19+21+21+20+11+24+13)
        const int columns = std::max(1, AutoFillCols(grid.width(), 240.0f, 14.0f));
        const float cardW = (grid.width() - 14.0f * static_cast<float>(columns - 1)) /
                            static_cast<float>(columns);
        // 打开会重排列表（lastOpened 变了），所以只记下标、循环外再执行 ——
        // 循环里刷新会让 hub.cards 重建，后面几张卡读到的是错位的条目。
        int openIndex = -1;
        for (std::size_t i = 0; i < hub.shown.size(); ++i) {
            const hub::HubCard& card = hub.cards[static_cast<std::size_t>(hub.shown[i])];
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;
            const Rect bounds{grid.min.x + (cardW + 14.0f) * static_cast<float>(column),
                              grid.min.y + (cardH + 14.0f) * static_cast<float>(row), cardW, cardH};
            if (hub::DrawCard(draw, hub, card, bounds, cardW - 28.0f)) {
                openIndex = static_cast<int>(i);
            }
        }
        if (openIndex >= 0) {
            hub::Open(hub, hub.cards[static_cast<std::size_t>(
                                hub.shown[static_cast<std::size_t>(openIndex)])]);
        }
    }

    // ---- 自报栅格实际高度（Shell 的 `hub-scroll` 靠它才能滚）----
    //
    // ⚠️ 不报的话项目卡超过一屏就被裁掉**且滚不到**（1080 高窗口约 6 张，第 7 张
    //    起够不着）。上一轮 Shell 那侧写的是 `setContentHeight(region.content().height())`
    //    —— 上报值恰好等于视口高，等于上报了 0 行，ScrollMaxY 恒为 0。**看着加了、
    //    实际等于没加**。Shell 侧现在有一次性哨兵会把这个报出来。
    // 高度按栅格公式算（行数 = ceil(条目数/列数)），与上面画卡用的是同一套列宽算法。
    {
        float usedBottom;
        if (hub.shown.empty()) {
            usedBottom = grid.min.y + 260.0f;
        } else {
            constexpr float kHubCardH = 250.0f;
            const int cols = std::max(1, AutoFillCols(grid.width(), 240.0f, 14.0f));
            const int rowCount =
                (static_cast<int>(hub.shown.size()) + cols - 1) / cols; // 向上取整
            usedBottom = grid.min.y + (kHubCardH + 14.0f) * static_cast<float>(rowCount) - 14.0f;
        }
        // 与列首/计数行保持一致的下边界，向上多留 8px 收尾。
        pages::SetPageContentHeight(usedBottom - area.min.y + 8.0f);
    }

    // ---- 弹窗：确认 > 向导 > 打开项目 ----
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (hub.confirm) {
            hub.confirm = false;
        } else if (hub.wizard) {
            hub.wizard = false;
        } else if (hub.openDlg) {
            hub.openDlg = false;
        }
    }
    if (hub.confirm) {
        hub::DrawConfirmDialog(hub, area, draw);
    }
    if (hub.wizard) {
        hub::DrawWizard(hub, area, draw);
    }
    if (hub.openDlg) {
        hub::DrawOpenDialog(hub, area, draw);
    }
}

// ⚠️ 定义必须在**匿名命名空间之外**（理由见本文件里 g_hubDialogOpen 那段注释），
//    否则 Shell.cpp 链接不到，而编译那一轮是过的。
bool HubDialogOpen() { return g_hubDialogOpen; }

} // namespace shine::pages
