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

#include "ui/imgui/kit/Text.h"
#include "ui/imgui/kit/Image.h"
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

    const ImVec2 viewPos = ImGui::GetWindowPos();
    const ImVec2 viewSize = ImGui::GetWindowSize();
    const Rect viewport = RectAt(viewPos.x, viewPos.y, viewSize.x, viewSize.y);
    draw->AddRectFilled(viewport.min, viewport.max, ColorVoid());

    const float outerW = std::min(1280.0f, area.width() - 56.0f);
    const float left = area.min.x + (area.width() - outerW) * 0.5f;
    const Rect column = RectAt(left, area.min.y + 20.0f, outerW, area.height());
    const TextStyle title{.size = theme::font::kHubBrand, .bold = true, .color = ColorText()};
    Text(draw, column.min, "项目中心", title);
    const float barY = area.min.y + 24.0f;
    ImGui::BeginDisabled(wasOpen);
    ButtonSpec openProjectSpec;
    openProjectSpec.variant = ButtonVariant::Secondary;
    openProjectSpec.icon = "folder";
    const char* openLabel = "打开…";
    const float openW = ButtonWidth(
        ButtonSize::Medium, 15.0f, TextWidth(openLabel, TextStyle{.size = theme::font::kBody}));
    ButtonSpec newProject;
    newProject.variant = ButtonVariant::Primary;
    newProject.icon = "plus";
    const char* newLabel = "新建项目";
    const float newW = ButtonWidth(
        ButtonSize::Medium, 15.0f,
        TextWidth(newLabel, TextStyle{.size = theme::font::kBody, .bold = true}));
    // ⚠️ 次序按设计稿（Pixso `13:11`/`13:12`）：**主按钮「新建项目」最靠右**
    //    （右沿 = 列内容右沿 1468），「打开…」在其左、间距 10（1355 → 1365）。
    //    DOM 顺序也是 `打开…` 在前、`新建项目` 在后（ProjectHub.jsx:153-154）——
    //    原来把「打开…」钉在最右，两个按钮正好互换。
    const bool newClicked =
        Button(draw, RectAt(column.max.x - newW, barY, newW, 30.0f), newLabel, newProject,
               "hub-new");
    const bool openClicked =
        Button(draw, RectAt(column.max.x - newW - 10.0f - openW, barY, openW, 30.0f), openLabel,
               openProjectSpec, "hub-open");
    if (openClicked) {
        hub.openDlg = true;
    }
    if (newClicked) {
        hub.wizard = true;
        hub.step = 0;
        hub.status.clear();
        hub.statusError = false;
    }
    Stack sections{Rect{ImVec2(left, column.min.y + 56.0f), column.max}};
    if (hub.cards.empty()) {
        constexpr char kWelcomeTitle[] = "还没有项目";
        constexpr char kWelcomeBody[] = "新建项目，或打开磁盘上的已有项目";
        const float height = EmptyHeight(sections.area.width(), kWelcomeTitle, kWelcomeBody);
        const Rect welcome = sections.Next(std::max(144.0f, height), 20.0f);
        DrawRoundRect(draw, welcome.min, welcome.max, 8.0f, ColorPanel(), ColorLineNormal(), 1.0f);
        Empty(draw, welcome, "folder", kWelcomeTitle, kWelcomeBody);
    } else {
        const auto recent = std::max_element(hub.cards.begin(), hub.cards.end(),
            [](const hub::HubCard& a, const hub::HubCard& b) {
                return a.entry.lastOpened < b.entry.lastOpened;
            });
        const Rect feature = sections.Next(232.0f, 24.0f);
        DrawRoundRect(draw, feature.min, feature.max, 8.0f, ColorPanel(), ColorLineNormal(), 1.0f);
        const float coverW = feature.width() * 0.52f;
        const Rect cover = RectAt(feature.min.x + 1.0f, feature.min.y + 1.0f,
                                  coverW - 1.0f, feature.height() - 2.0f);
        const auto path = recent->entry.coverThumb;
        const auto preview = RequestImage(path.empty() ? path : recent->entry.rootDir / path);
        if (preview.ready()) {
            DrawImage(draw, cover, preview, 5.0f, ImageFit::Cover);
        } else {
            DrawRoundRect(draw, cover.min, cover.max, 5.0f, ColorSurface());
            DrawIconCentered(draw, hub::TemplateIcon(recent->tplLabel), cover.center(), 48.0f,
                             ColorTextSecondary());
        }
        const float x = cover.max.x + 24.0f;
        const float maxW = feature.max.x - 20.0f - x;
        kit::Text(draw, ImVec2(x, feature.min.y + 28.0f), "最近打开",
                  TextStyle{.size = theme::font::kTiny, .color = ColorTextSecondary()});
        kit::TextClipped(draw, ImVec2(x, feature.min.y + 56.0f), maxW, recent->entry.name,
                         TextStyle{.size = theme::font::kValue, .bold = true, .color = ColorText()});
        const std::string description = recent->premise.empty()
            ? util::PathToUtf8(recent->entry.rootDir) : recent->premise;
        kit::TextClipped(draw, ImVec2(x, feature.min.y + 101.0f), maxW, description,
                         TextStyle{.size = theme::font::kBody, .color = ColorTextSecondary()});
        if (Button(draw, RectAt(x, feature.max.y - 58.0f, 112.0f, 34.0f), "继续项目",
                   ButtonSpec{.variant = ButtonVariant::Primary}, "hub-continue")) {
            hub::Open(hub, *recent);
        }
    }

    const Rect tools = sections.Next(34.0f, 16.0f);
    static const std::vector<SegmentOption> sortOptions{{"r", "最近打开"}, {"n", "名称"}};
    const float sortWidth = SegmentedWidth(sortOptions);
    const float searchW = std::min(344.0f, tools.width() - sortWidth - 16.0f);
    Input(draw, RectAt(tools.min.x, tools.min.y, searchW, 34.0f), hub.query,
          "搜索项目名称或创意…", "hub-search");
    const std::string_view sort = Segmented(draw, RectAt(tools.min.x + searchW + 12.0f,
                                            tools.min.y, sortWidth, 34.0f), sortOptions,
                                            hub.sort, "hub-sort");
    if (!sort.empty() && sort != hub.sort) {
        hub.sort = std::string(sort);
        hub::Refresh(hub);
    }
    hub::Refilter(hub);

    const Rect tableHead = sections.Next(34.0f);
    DrawRoundRect(draw, tableHead.min, tableHead.max, 0.0f, ColorSurface());
    const TextStyle label{.size = theme::font::kTiny, .bold = true, .color = ColorTextSecondary()};
    Text(draw, ImVec2(tableHead.min.x + 16.0f, CenterY(tableHead.center().y, label)), "最近项目", label);
    Text(draw, ImVec2(tableHead.max.x - 180.0f, CenterY(tableHead.center().y, label)), "操作", label);

    int openIndex = -1;
    for (const int index : hub.shown) {
        const Rect row = sections.Next(58.0f);
        if (row.max.y < viewport.min.y || row.min.y > viewport.max.y) {
            continue;
        }
        if (hub::DrawCard(draw, hub, hub.cards[static_cast<std::size_t>(index)], row)) {
            openIndex = index;
        }
    }
    if (openIndex >= 0) {
        hub::Open(hub, hub.cards[static_cast<std::size_t>(openIndex)]);
    }
    if (!hub.query.empty() && hub.shown.empty()) {
        constexpr char kNoMatchTitle[] = "没有匹配的项目";
        constexpr char kNoMatchBody[] = "请换个搜索词";
        Empty(draw, sections.Next(EmptyHeight(sections.area.width(), kNoMatchTitle, kNoMatchBody)),
              "search", kNoMatchTitle, kNoMatchBody);
    }
    if (!hub.status.empty()) {
        const Rect status = sections.Next(32.0f);
        TextClipped(draw, ImVec2(status.min.x, status.min.y + 8.0f), status.width(),
                    hub.status, TextStyle{.size = theme::font::kTiny,
                        .color = hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextSecondary()});
    }
    pages::SetPageContentHeight(sections.Cur() - area.min.y + 24.0f);
    ImGui::EndDisabled();

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
        hub::DrawConfirmDialog(hub, viewport, draw);
    }
    if (hub.wizard) {
        hub::DrawWizard(hub, viewport, draw);
    }
    if (hub.openDlg) {
        hub::DrawOpenDialog(hub, viewport, draw);
    }
}

// ⚠️ 定义必须在**匿名命名空间之外**（理由见本文件里 g_hubDialogOpen 那段注释），
//    否则 Shell.cpp 链接不到，而编译那一轮是过的。
bool HubDialogOpen() { return g_hubDialogOpen; }

} // namespace shine::pages
