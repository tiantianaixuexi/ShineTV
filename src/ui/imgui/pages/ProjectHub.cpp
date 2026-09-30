// shine::pages —— P4.11 项目中心（全屏，不套外壳）
//
// 数据全部来自 shine::project，本页不再有任何编造的卡片：
//   最近列表 = ProjectService::Recent()  —— 读 %APPDATA%/ShineTVStudio/projects.json
//   卡片副行 = project.json 的 premise；封面标签 / meta = project.json 的 templateId
//   打开     = ProjectService::Open()    —— 真读 project.json + 置顶索引
//   新建     = ProjectService::Create()  —— 真落骨架（ValidateSpec + Materialize）
//   移除     = ProjectIndex::Remove()    —— 只摘登记，不删项目文件
// 业务层返回空就是空态，**不补假卡**。
//
// I/O 口径：ProjectService::Recent() 每次调用都重读索引文件（Project.h 有意如此），
// 所以卡片只在本文件首次进入 / 一次写操作之后刷新，不在每帧读盘。
//
// ⚠️ 这一页**不依赖** novel.db 快照（零 BookSide / Book() 调用）—— 它只共用
//    PageCommon 的版式常量与 LabelWidth。所以不 include BookData.h：把不存在的
//    依赖写进 include 列表，日后就分不清「真在用」和「顺手带上的」。
#include "ui/imgui/pages/PageCommon.h"

#include "core/Log.h"
#include "project/Project.h"
#include "project/ProjectIndex.h"
#include "project/ProjectTemplate.h"
#include "ui/imgui/kit/Overlays.h"
#include "util/Encoding.h"
#include "util/Shell.h"
#include "util/Strings.h"

#include <objbase.h>  // 必须在 windows.h（util/Encoding.h 已经带进来）之后
#include <shlobj.h>   // SHBrowseForFolderW：向导第 2 步的「浏览…」

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

namespace {

// 卡片上要用到、但 RecentEntry 里没有的字段（都来自各自的 project.json）。
struct HubCard {
    project::RecentEntry entry;
    std::string premise;   // project.json.premise（读不到就留空，不拿模板名顶替）
    std::string tplLabel;  // 封面短标签：小说 / 影视 / 空白
    std::string tplName;   // meta 行用的模板全名
    std::string when;      // lastOpened 的相对时间
    bool readable = false; // project.json 是否读得到
    int artSeed = 0;       // 封面种子：由项目 id 稳定派生（不是循环下标）
};

// DrawProjectHub 是自由函数，没有实例可挂交互态，状态放函数内 static
// （与本文件 g_imageFlowNeedsFit 同一手法）。
struct HubState {
    project::ProjectService service;
    std::vector<HubCard> cards; // 排序后的全量
    std::vector<int> shown;     // 搜索过滤后的下标
    bool loaded = false;
    std::string query;
    std::string sort = "r"; // Segmented 的 value：r=最近打开 / n=名称
    bool wizard = false;
    int step = 0;
    std::string tpl = "novel";
    std::string name;
    std::string dir;
    std::string idea;
    bool openDlg = false;
    bool confirm = false;
    // 「点面板外关闭」这一下点击**是否算数**。只在本帧之前对话框就已经开着时
    // 才为真 —— 见 DrawProjectHub 里的说明（不加这道闸门，弹窗会被打开它的那
    // 一下点击立刻关掉）。
    bool dismissArmed = false;
    std::string confirmTitle;
    std::string confirmBody;
    std::string confirmOk = "确定";
    std::string pendingId;
    std::string status;
    bool statusError = false;
};

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

HubState& Hub() {
    static HubState state;
    return state;
}

// 封面种子跟着项目 id 走：跨帧、跨排序、跨过滤都是同一张封面。
int SeedOf(std::string_view id) {
    std::uint32_t hash = 2166136261u;
    for (const char ch : id) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619u;
    }
    return static_cast<int>(hash % 12u); // Art 只有 12 组调色板
}

// project.json 的 templateId → 封面短标签（设计稿：小说 / 影视 / 空白）。
std::string TplLabel(std::string_view templateId) {
    if (templateId == "novel") {
        return "小说";
    }
    if (templateId == "film") {
        return "影视";
    }
    if (templateId == "blank") {
        return "空白";
    }
    return {};
}

// 模板 id → 图标（向导第 1 步每行一个）。
const char* TemplateIcon(std::string_view templateId) {
    if (templateId == "novel") {
        return "book";
    }
    if (templateId == "film") {
        return "film";
    }
    return "folder";
}

// 民用日期 → 天序（Hinnant）。只为算「今天 / 昨天 / N 天前」的真实天数差。
long long DayNumber(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const int yoe = year - static_cast<int>(era * 400);
    const int mp = month + (month > 2 ? -3 : 9);
    const int doy = (153 * mp + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// lastOpened 的相对时间：今天 HH:mm / 昨天 HH:mm / N 天前 / 更早给日期。
// 数据来自 RecentEntry::lastOpened（口径照 Qt 版 ProjectHubView.cpp:98）。
std::string RelativeTime(std::chrono::system_clock::time_point tp) {
    const std::time_t when = std::chrono::system_clock::to_time_t(tp);
    const std::tm* lt = std::localtime(&when);
    if (lt == nullptr) {
        return {};
    }
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::tm* ln = std::localtime(&now);
    const long long days =
        (ln != nullptr ? DayNumber(ln->tm_year + 1900, ln->tm_mon + 1, ln->tm_mday) : 0) -
        DayNumber(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    char buf[32] = {};
    if (days <= 0) {
        std::snprintf(buf, sizeof buf, "今天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days == 1) {
        std::snprintf(buf, sizeof buf, "昨天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days < 7) {
        std::snprintf(buf, sizeof buf, "%lld 天前", days);
    } else {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1,
                      lt->tm_mday);
    }
    return buf;
}

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

// UTF-8 字符数：设计稿的「200 字」上限按字符算，不是字节。
std::size_t CharCount(std::string_view text) {
    std::size_t count = 0;
    for (const char ch : text) {
        if ((static_cast<unsigned char>(ch) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

// 重新拉最近列表（Recent() 每次都重读索引，所以只在需要时调）。
void RefreshHub(HubState& hub) {
    hub.cards.clear();
    for (const project::RecentEntry& entry : hub.service.Recent()) {
        HubCard card;
        card.entry = entry;
        card.artSeed = SeedOf(entry.id);
        card.when = RelativeTime(entry.lastOpened);
        if (const std::expected<project::ProjectFile, project::Error> file =
                project::LoadProjectFile(entry.rootDir);
            file.has_value()) {
            card.readable = true;
            card.premise = std::string(util::Trim(file->premise));
            card.tplLabel = TplLabel(file->templateId);
            if (const project::ProjectTemplate* tpl = project::FindTemplate(file->templateId);
                tpl != nullptr) {
                card.tplName = tpl->name;
            }
        }
        hub.cards.push_back(std::move(card));
    }
    if (hub.sort == "n") { // 名称
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.name < b.entry.name;
        });
    } else { // 最近打开
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.lastOpened > b.entry.lastOpened;
        });
    }
    hub.loaded = true;
}

// 搜索过滤：项目名或一句话创意命中即可（webui 匹配 name + desc）。
void RefilterHub(HubState& hub) {
    const std::string key = LowerAscii(util::Trim(hub.query));
    hub.shown.clear();
    for (std::size_t i = 0; i < hub.cards.size(); ++i) {
        const HubCard& card = hub.cards[i];
        if (key.empty() || LowerAscii(card.entry.name).find(key) != std::string::npos ||
            LowerAscii(card.premise).find(key) != std::string::npos) {
            hub.shown.push_back(static_cast<int>(i));
        }
    }
}

// 打开项目：真 Open（读 project.json + 置顶索引），失败把业务层的中文 message 摆出来。
void OpenHubCard(HubState& hub, const HubCard& card) {
    const std::expected<project::ProjectRef, project::Error> ref =
        hub.service.Open(card.entry.rootDir);
    if (!ref) {
        hub.status = "打开项目失败：" + ref.error().message;
        hub.statusError = true;
        return;
    }
    hub.status = "已打开项目「" + ref->name + "」· " + util::PathToUtf8(ref->rootDir);
    hub.statusError = false;
    RefreshHub(hub);
}

// 从最近列表移除：ProjectService 没这个接口，走 ProjectIndex（只摘登记，不删文件）。
void RemoveHubCard(HubState& hub, const std::string& id) {
    project::ProjectIndex index;
    (void)index.Load(); // 坏文件也已回退空索引
    (void)index.Remove(id);
    if (const std::expected<void, project::Error> saved = index.Save(); !saved) {
        hub.status = "移除未落盘：" + saved.error().message;
        hub.statusError = true;
    } else {
        hub.status = "已从最近列表移除（项目文件未删除）";
        hub.statusError = false;
    }
    RefreshHub(hub);
}

// 主题轮转（pfoot 的 palette 按钮）。动作与 Shell::SetTheme 一致 ——
// 水墨换衬线族必须重建字体图集，否则中文缺字。
void CycleHubTheme(HubState& hub) {
    const auto it =
        std::find(theme::kAllThemes.begin(), theme::kAllThemes.end(), theme::CurrentThemeId());
    const std::size_t index =
        (it == theme::kAllThemes.end()) ? 0 : static_cast<std::size_t>(it - theme::kAllThemes.begin());
    const theme::ThemeId next = theme::kAllThemes[(index + 1) % theme::kAllThemes.size()];
    theme::ApplyTheme(next);
    if (theme::ThemeUsesSerif(next)) {
        if (!BuildFontAtlas(/*serif=*/true)) {
            shine::log::Error("serif font atlas rebuild failed — falling back to sans, 文字可能缺字");
        }
        theme::ApplyCurrentTheme();
    }
    (void)theme::PersistTheme(theme::DefaultThemeFile());
    hub.status = "主题已切到「" + std::string(theme::ThemeDisplayName(next)) + "」";
    hub.statusError = false;
}

// 向导第 2 步的「浏览…」：系统选目录（Win32 边界调用，按钮触发一次即返回）。
std::string PickFolder() {
    BROWSEINFOW info = {};
    info.hwndOwner = reinterpret_cast<HWND>(ImGui::GetMainViewport()->PlatformHandle);
    info.ulFlags = BIF_RETURNONLYFSDIRS;
    info.lpszTitle = L"选择项目位置";
    PIDLIST_ABSOLUTE picked = SHBrowseForFolderW(&info);
    if (picked == nullptr) {
        return {}; // 用户取消
    }
    wchar_t buffer[MAX_PATH] = {};
    const bool ok = SUCCEEDED(SHGetPathFromIDListW(picked, buffer));
    CoTaskMemFree(picked);
    return ok ? util::PathToUtf8(std::filesystem::path{buffer}) : std::string{};
}

// 项目将创建到的目录：<位置>\<项目名>（名字空就还没有目标）。
std::filesystem::path HubTargetDir(const HubState& hub) {
    if (util::Trim(hub.dir).empty() || util::Trim(hub.name).empty()) {
        return {};
    }
    return util::PathFromUtf8(util::Trim(hub.dir)) / util::PathFromUtf8(util::Trim(hub.name));
}

struct ModalBox {
    Rect frame;
    Rect header;
    Rect body;
    Rect footer;
};

// 弹窗外壳（ProjectHub.jsx 的 .modal）现在**直接走 kit::ModalFrameRect**：
// 遮罩 / 圆角 / 投影 / 头 / 体 / 脚 全部由 kit 画，几何与 kit::Modal 同源。
//
// 原来这里是页面层私有一份：scrim **不注册命中**（点外面关不掉）、头高写死 52
// （kit 的是 47）、页脚写死 60、标题自己排 Y。同一份逻辑两处实现，而这份
// 「算得对但零调用」的就在 kit 里躺着。
//
// 标题**由 kit 画**（这是 ModalFrameRect 的主路径）；页面层只在头部右侧补
// 自己的东西（向导那一步的 Steps 条），那才是页面层该管的部分。
// `dismiss` 出参：点遮罩（面板外）时置 true，由调用方关掉自己的状态。
// 为什么是出参而不是在函数里直接改 hub：三个对话框关的是**三个不同的字段**
// （wizard / openDlg / confirm），让外壳去认它们等于把状态所有权搅在一起。
ModalBox DrawModal(ImDrawList* draw, Rect area, std::string_view title, std::string_view icon,
                   float width, float height, bool* dismiss, int footerButtons = 2) {
    const kit::ModalFrame mf =
        kit::ModalFrameRect(draw, area, title, icon, width, height, footerButtons);
    // 点遮罩关闭：遮罩**不注册命中**（见 Overlays.cpp 的说明 —— 注册会抢
    // HoveredId，模态里每个按钮就永远点不动），所以在这里手算「点击是否落在
    // 面板外」。这与报告模态同一做法，两处共用的是同一份 kit 外壳。
    // ⚠️ 同样不能用 ImGui::IsMouseHoveringRect —— 在**本工程**它会 0xC0000005。
    if (dismiss != nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !mf.frame.contains(ImGui::GetIO().MousePos)) {
        *dismiss = true;
    }
    ModalBox box;
    box.frame = mf.frame;
    box.header = mf.header;
    box.body = mf.body;
    box.footer = mf.footer;
    return box;
}

struct ModalAction {
    std::string_view label;
    ButtonSpec spec;
    std::string_view id;
};

// 弹窗页脚：按钮右对齐（.modal-f 是「提示 + spacer + 按钮组」）。
// 返回每个按钮本帧是否被点。
std::vector<int> DrawModalFooter(ImDrawList* draw, Rect footer,
                                 const std::vector<ModalAction>& actions) {
    std::vector<int> hit(actions.size(), 0);
    float x = footer.max.x - 20.0f;
    for (std::size_t i = actions.size(); i-- > 0;) {
        const ModalAction& action = actions[i];
        const std::string label(action.label);
        const float textSize = action.spec.size == ButtonSize::Small ? 12.0f : 13.0f;
        ImFont* font = action.spec.variant == ButtonVariant::Primary ? FontBoldAt(textSize)
                                                                    : FontAt(textSize);
        const float iconW = action.spec.icon.empty()
                                ? 0.0f
                                : (action.spec.size == ButtonSize::Small ? 13.0f : 15.0f);
        const float width =
            ButtonWidth(action.spec.size, iconW, LabelWidth(font, textSize, label.c_str()));
        const float height = ButtonHeight(action.spec.size);
        const Rect bounds{x - width, footer.center().y - height * 0.5f, x,
                          footer.center().y + height * 0.5f};
        hit[i] = Button(draw, bounds, label, action.spec, action.id) ? 1 : 0;
        x -= width + 8.0f;
    }
    return hit;
}

// 卡片：整卡可点 + pfoot 的四个入口（打开 / 资源管理器 / 更多 / 主题）。
// 返回 true = 本帧通过整卡或「打开」按钮触发了打开。
bool DrawHubCard(ImDrawList* draw, HubState& hub, const HubCard& card, Rect bounds,
                 float bodyWidth) {
    // ⚠️ 整卡热区在**页脚四个按钮之后**才注册，方向不能反。
    //
    //    ImGui 同一窗口内是「**先注册者独占** HoveredId / ActiveId」：
    //    `ItemHoverable` 里 `if (g.HoveredId != 0 && g.HoveredId != id && !AllowOverlap)
    //    return false;`（imgui.cpp:5161），随后 `SetHoveredID(id)`。所以先注册整卡
    //    ⇒ 页脚四个按钮**永远 hovered=false / clicked=false**，而整卡在它们的像素上
    //    照样 clicked=true —— 症状是「点『…』不是弹移除确认，而是直接把项目打开」，
    //    另外三个键连 hover 底色都不出。**这里原来的注释断言「后命中的 item 优先」，
    //    与 ImGui 的规则正好相反**，所以那个 bug 一直没人看出来。
    //
    //    先注册按钮之后，落在页脚像素上的那次点击归按钮，落在卡片正文的归整卡 ——
    //    效果等价于 webui 里 pfoot 上的 e.stopPropagation()，而且是 ImGui 自己做的，
    //    不依赖下面那串 `!openPressed && …` 兜底（兜底仍保留，双保险）。
    //
    //    代价：边框的高亮要手算鼠标位置，不能用 hit.hovered（那时还没注册）。
    //    ⚠️ 只能用 bounds.contains() 手算，**不要**用 ImGui::IsMouseHoveringRect ——
    //    本工程调它会 0xC0000005（fault offset 0x8b8597），已改成手算比较。
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool cardHovered = bounds.contains(mouse);
    constexpr float radius = 10.0f;
    // `.card.proj-card.hoverable.glow`（ProjectHub.jsx:167）。命中的是
    // `.card.glow:hover`（ui.css:201-203 → border accent-glow + shadow-accent）；
    // `views.css:117` 的 `.proj-card:hover`（shadow-2）在**本元素上是死规则** ——
    // 特异度 0,2,0 输给 glow 的 0,3,0，与 CSS 引入顺序无关。
    DrawShadowed(draw, bounds.min, bounds.max, radius, ColorPanel(),
                 cardHovered ? ColorAccentGlow() : ColorLineSubtle(), 1.0f,
                 cardHovered ? theme::ShadowTier::Accent : theme::ShadowTier::None);

    // 封面满幅（views.css:82 .cover 无内缩）。这版 ImGui 没有 PushClipPath，
    // 卡片顶部的两个圆角用同色三角补掉。
    draw->PushClipRect(bounds.min, bounds.max, true);
    Art(draw, Rect{bounds.min.x, bounds.min.y, bounds.max.x, bounds.min.y + 120.0f}, card.artSeed,
        true);
    draw->PopClipRect();
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x + radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x - radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    if (!card.tplLabel.empty()) {
        Tag(draw, RectAt(bounds.min.x + 10.0f, bounds.min.y + 10.0f, TagWidth(card.tplLabel, true, false),
                         TagHeight(true)),
            card.tplLabel, theme::Tone::Accent, true);
    }

    const float bodyX = bounds.min.x + 14.0f;
    float y = bounds.min.y + 120.0f + 12.0f;
    // 项目名：索引里的 name（与 project.json 同源）
    DrawTextClipped(draw, FontBoldAt(14.5f), 14.5f, ImVec2(bodyX, y), bodyWidth, ColorText(),
                    card.entry.name);
    y += 21.0f;
    // 副行：一句话创意。没有就整行留空 —— 不拿模板名之类的字段顶替
    if (!card.premise.empty()) {
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX, y), bodyWidth, ColorTextMuted(),
                        card.premise);
    }
    y += 21.0f;
    // meta：模板名 · lastOpened 相对时间
    DrawIcon(draw, "book", ImVec2(bodyX, y + 2.0f), 12.0f, ColorTextMuted());
    const std::string meta =
        card.tplName.empty() ? card.when : (card.tplName + " · " + card.when);
    DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX + 20.0f, y), bodyWidth - 20.0f,
                    ColorTextMuted(), meta);
    y += 20.0f;
    DrawRoundRect(draw, ImVec2(bodyX, y), ImVec2(bounds.max.x - 14.0f, y + 1.0f), 0.0f,
                  ColorLineSubtle());
    y += 11.0f;

    bool open = false;
    ButtonSpec openSpec;
    openSpec.variant = ButtonVariant::Primary;
    openSpec.size = ButtonSize::Small;
    const float openW = ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
    const Rect openRect{bodyX, y, bodyX + openW, y + ButtonHeight(ButtonSize::Small)};
    const bool openPressed =
        Button(draw, openRect, "打开", openSpec, "hub-open-" + card.entry.id);

    float fx = openRect.max.x + 6.0f;
    const float iconSize = 22.0f;
    const float iconH = ButtonHeight(ButtonSize::Small);
    const bool revealPressed =
        IconButton(draw, RectAt(fx, y, iconSize, iconH), "folder", false, false,
                   "hub-reveal-" + card.entry.id, "在资源管理器中显示");
    fx += iconSize + 6.0f;
    const bool morePressed = IconButton(draw, RectAt(fx, y, iconSize, iconH), "dots", false, false,
                                        "hub-more-" + card.entry.id, "更多");
    const bool themePressed = IconButton(
        draw, RectAt(bounds.max.x - 14.0f - iconSize, y, iconSize, iconH), "palette", false, false,
        "hub-theme-" + card.entry.id, "切换主题");

    // ⚠️ 整卡热区**必须排在页脚四个按钮之后**（理由见函数开头那段）。
    const Hit hit = HitTest(bounds, "hub-card-" + card.entry.id);

    // 整卡点击要扣掉页脚按钮：ImGui 的 IsItemClicked 只看「光标在本 item 矩形内」，
    // 页脚按钮压在卡片上，不扣掉的话点「移除」会顺手把项目也打开
    // （等价 webui 里 pfoot 上的 e.stopPropagation）。
    // 改成「按钮先注册」之后 ImGui 自己就把这两次点击分开了，这串判定是双保险。
    open = hit.clicked && !openPressed && !revealPressed && !morePressed && !themePressed;

    if (revealPressed) {
        const std::string error = util::ShellReveal(card.entry.rootDir);
        hub.status = error.empty() ? ("已在资源管理器中显示「" + card.entry.name + "」") : error;
        hub.statusError = !error.empty();
    }
    if (morePressed) {
        hub.confirm = true;
        hub.confirmTitle = "从列表移除「" + card.entry.name + "」？";
        hub.confirmBody = "仅从最近项目列表移除，不会删除项目文件；之后可通过「打开…」重新加入。";
        hub.confirmOk = "移除";
        hub.pendingId = card.entry.id;
    }
    if (themePressed) {
        CycleHubTheme(hub);
    }
    return open || openPressed;
}

// 新建项目向导：模板 / 命名 / 创意 / 确认。数据源 = AllTemplates / PreviewTree / Create。
void DrawHubWizard(HubState& hub, Rect area, ImDrawList* draw) {
    static const std::vector<std::string> stepNames{"模板", "命名", "创意", "确认"};
    bool dismissWizard = false;
    // dismissArmed 为假（= 本帧才被打开）时传 nullptr：这一下打开它的点击
    // 不能顺手把它关掉。理由见 DrawProjectHub 里 dismissArmed 的说明。
    const ModalBox box = DrawModal(draw, area, "新建项目", "sparkles", 600.0f, 420.0f,
                                   hub.dismissArmed ? &dismissWizard : nullptr);
    if (dismissWizard) {
        hub.wizard = false;
    }
    const float stepsW = StepsWidth(stepNames);
    Steps(draw, RectAt(box.header.max.x - 20.0f - stepsW, box.header.center().y - 10.0f, stepsW, 20.0f),
          stepNames, hub.step);

    const Rect body{box.body.min.x + 20.0f, box.body.min.y + 16.0f, box.body.max.x - 20.0f,
                    box.body.max.y - 8.0f};
    if (hub.step == 0) {
        float y = body.min.y;
        for (const project::ProjectTemplate& tpl : project::AllTemplates()) {
            const Rect row{body.min.x, y, body.max.x, y + 62.0f};
            // 模板行走 kit::ListCard：hover 投影档 / 选中 accent 边 / 双行块的
            // 垂直居中全在里面。原来这一段自己排 `row.min.y + 14` 与 `+ 34`，
            // 两行文字的**块**中心偏上 0.875px —— 两行各自都不居中，靠硬凑。
            kit::ListCardSpec spec;
            spec.id = "hub-tpl-" + std::string(tpl.id);
            spec.icon = TemplateIcon(tpl.id);
            spec.iconSize = 20.0f;
            spec.title = tpl.name;
            spec.description = tpl.description;
            spec.titleSize = 13.0f;
            spec.descSize = 11.5f;
            spec.textGap = 4.0f;
            spec.paddingX = 14.0f;
            // 文字原从 +60 起：图标 20 + 8 间距 + 14 padding = 42，再让 18 给
            // 右侧的「选中勾」⇒ 60。这里 paddingX 已经是 14，textInset 补到 60。
            spec.textInset = 60.0f - 14.0f - (20.0f + 8.0f);
            spec.selected = hub.tpl == tpl.id;
            if (kit::ListCard(draw, row, spec).clicked) {
                hub.tpl = tpl.id;
            }
            y += 70.0f;
        }
    } else if (hub.step == 1) {
        const Rect nameField =
            Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 48.0f}, "项目名称", {});
        Input(draw, nameField, hub.name, "例如：灯语回声", "hub-wiz-name");
        const Rect dirField = Field(
            draw, Rect{body.min.x, nameField.max.y + 18.0f, body.max.x, nameField.max.y + 66.0f},
            "位置", "项目目录将创建在该路径下");
        constexpr const char* browseLabel = "浏览…";
        const float browseW =
            ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontAt(13.0f), 13.0f, browseLabel));
        Input(draw, Rect{dirField.min.x, dirField.min.y, dirField.max.x - browseW - 8.0f,
                         dirField.max.y},
              hub.dir, "E:\\projects", "hub-wiz-dir");
        ButtonSpec browseSpec;
        browseSpec.variant = ButtonVariant::Secondary;
        browseSpec.icon = "folder";
        if (Button(draw, RectAt(dirField.max.x - browseW, dirField.min.y, browseW, 30.0f),
                   browseLabel, browseSpec, "hub-wiz-browse")) {
            const std::string picked = PickFolder();
            if (!picked.empty()) {
                hub.dir = picked;
            }
        }
        // 将创建目录：目标路径 + PreviewTree 给出的真实骨架清单
        const Rect preview{body.min.x, dirField.max.y + 16.0f, body.max.x, body.max.y};
        DrawRoundRect(draw, preview.min, preview.max, 10.0f, ColorFillMuted(), ColorLineSubtle(),
                      1.0f);
        const char* previewLabel = "将创建目录";
        draw->AddText(FontAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 10.0f),
                      ColorTextMuted(), previewLabel, previewLabel + std::strlen(previewLabel));
        const std::filesystem::path target = HubTargetDir(hub);
        const std::string targetText = target.empty() ? "<项目名>" : util::PathToUtf8(target);
        DrawTextClipped(draw, MonoAt(12.0f), 12.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 28.0f),
                        preview.width() - 28.0f, ColorText(), targetText);
        const std::vector<std::string> tree = project::PreviewTree(hub.tpl);
        float ty = preview.min.y + 50.0f;
        for (std::size_t i = 0; i < tree.size() && i < 6; ++i) {
            DrawTextClipped(draw, MonoAt(11.0f), 11.0f,
                            ImVec2(preview.min.x + 14.0f, ty), preview.width() - 28.0f,
                            ColorTextMuted(), tree[i]);
            ty += 15.0f;
        }
        if (tree.size() > 6) {
            const std::string more = "… 共 " + std::to_string(tree.size()) + " 项";
            draw->AddText(MonoAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, ty), ColorTextMuted(),
                          more.data(), more.data() + more.size());
        }
    } else if (hub.step == 2) {
        const std::string label = "一句话创意（" + std::to_string(CharCount(hub.idea)) + "/200）";
        const Rect ideaField = Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.max.y},
                                     label, "将写入 project.json，供 LLM 初始化参考；留空可跳过");
        TextArea(draw,
                 Rect{ideaField.min.x, ideaField.min.y, ideaField.max.x, ideaField.min.y + 120.0f},
                 hub.idea, 5, "hub-wiz-idea");
    } else {
        const Rect card{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f};
        DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
        Art(draw, Rect{card.min.x + 16.0f, card.min.y + 16.0f, card.min.x + 112.0f, card.min.y + 80.0f},
            7, false);
        const std::string title = util::Trim(hub.name).empty() ? "未命名项目"
                                                                    : std::string(util::Trim(hub.name));
        draw->AddText(FontBoldAt(17.0f), 17.0f, ImVec2(card.min.x + 128.0f, card.min.y + 16.0f),
                      ColorText(), title.data(), title.data() + title.size());
        const std::filesystem::path target = HubTargetDir(hub);
        const std::string pathText = target.empty() ? std::string{} : util::PathToUtf8(target);
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(card.min.x + 128.0f, card.min.y + 40.0f),
                        card.width() - 144.0f, ColorTextMuted(), pathText);
        const project::ProjectTemplate* tpl = project::FindTemplate(hub.tpl);
        const std::string tplName = tpl != nullptr ? tpl->name : std::string{};
        float tagX = card.min.x + 128.0f;
        if (!tplName.empty()) {
            const float tagW = TagWidth(tplName, true, false);
            Tag(draw, RectAt(tagX, card.min.y + 62.0f, tagW, TagHeight(true)), tplName,
                theme::Tone::Accent, true);
            tagX += tagW + 6.0f;
        }
        const char* ideaTag = util::Trim(hub.idea).empty() ? "无创意" : "含一句话创意";
        const float ideaW = TagWidth(ideaTag, true, false);
        Tag(draw, RectAt(tagX, card.min.y + 62.0f, ideaW, TagHeight(true)), ideaTag,
            theme::Tone::Idle, true);
        const char* note = "创建后将打开工作坊：总控 / 小说 / 资产 / 分镜 / 出图 / 出片 六个工作区可用。";
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(body.min.x, card.max.y + 14.0f), body.width(), ColorTextMuted(), note);
    }

    // 页脚左侧：本次操作的实际结果（错误时用 danger 色），没有就摆事实提示
    const char* hint = hub.status.empty() ? "项目骨架只写入本机目录" : nullptr;
    if (hub.status.empty()) {
        draw->AddText(FontAt(11.0f), 11.0f, ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f),
                      ColorTextMuted(), hint, hint + std::strlen(hint));
    } else {
        DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                        ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f), 300.0f,
                        hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextMuted(),
                        hub.status, true);
    }

    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec secondarySpec;
    secondarySpec.variant = ButtonVariant::Secondary;
    ButtonSpec primarySpec;
    primarySpec.variant = ButtonVariant::Primary;
    if (hub.step == 3) {
        primarySpec.icon = "sparkles";
    }
    std::vector<ModalAction> actions;
    actions.push_back({"取消", ghostSpec, "hub-wiz-cancel"});
    if (hub.step > 0) {
        actions.push_back({"上一步", secondarySpec, "hub-wiz-prev"});
    }
    actions.push_back({hub.step < 3 ? "下一步" : "创建", primarySpec,
                       hub.step < 3 ? "hub-wiz-next" : "hub-wiz-create"});
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);

    if (hit[0] != 0) {
        hub.wizard = false;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hub.step > 0 && hit[1] != 0) {
        hub.step -= 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hit.back() == 0) {
        return;
    }
    if (hub.step < 3) {
        // canNext：只有第 1 步（命名）会挡人，其余步都能过（设计稿 ProjectHub.jsx:22）
        if (hub.step == 1 && util::Trim(hub.name).empty()) {
            hub.status = "项目名称不能为空（ValidateSpec 也会拦，这里先提示一次）";
            hub.statusError = true;
            return;
        }
        if (hub.step == 2 && CharCount(hub.idea) > 200) {
            hub.status = "一句话创意超过 200 字，请删减后再继续";
            hub.statusError = true;
            return;
        }
        hub.step += 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }

    // 第 4 步：真建。ValidateSpec / Materialize 的中文错误直接摆给用户。
    project::ProjectSpec spec;
    spec.name = std::string(util::Trim(hub.name));
    spec.rootDir = HubTargetDir(hub);
    spec.templateId = hub.tpl;
    spec.premise = std::string(util::Trim(hub.idea));
    const std::expected<project::ProjectRef, project::Error> created = hub.service.Create(spec);
    if (!created) {
        hub.status = "创建失败：" + created.error().message;
        hub.statusError = true;
        return;
    }
    hub.wizard = false;
    hub.name.clear();
    hub.idea.clear();
    hub.step = 0;
    hub.status = "已创建并打开项目「" + created->name + "」· " + util::PathToUtf8(created->rootDir);
    hub.statusError = false;
    RefreshHub(hub);
}

// 「打开…」对话框：列最近项目，点了就 Open。
void DrawHubOpenDialog(HubState& hub, Rect area, ImDrawList* draw) {
    bool dismissOpen = false;
    const ModalBox box = DrawModal(draw, area, "打开项目", "folder", 520.0f, 440.0f,
                                   hub.dismissArmed ? &dismissOpen : nullptr);
    if (dismissOpen) {
        hub.openDlg = false;
    }
    // 右上角标出列表的真实来源（索引文件所在目录）
    const std::string indexDir = util::PathToUtf8(project::DefaultIndexFile().parent_path());
    const float indexW = LabelWidth(FontAt(11.5f), 11.5f, indexDir.c_str());
    DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                    ImVec2(box.header.max.x - 20.0f - indexW, box.header.center().y - 5.75f),
                    indexW + 1.0f, ColorTextMuted(), indexDir);

    if (hub.cards.empty()) {
        Empty(draw, Rect{box.body.min.x, box.body.min.y, box.body.max.x, box.body.max.y}, "folder",
              "还没有可打开的项目", "先新建一个项目，或把已有项目目录登记进来");
    } else {
        float y = box.body.min.y + 6.0f;
        for (const HubCard& card : hub.cards) {
            const Rect row{box.body.min.x + 12.0f, y, box.body.max.x - 12.0f, y + 62.0f};
            // 「打开…」列表行走 kit::ListCard（`.card.hoverable`，ProjectHub.jsx:226）。
            // 两行文字块原来自己排 `row.min.y + 12` 与 `+ 34`，块中心偏上 1.875px。
            //
            // ⚠️ 「打开」按钮必须由 ListCard 的 footer 回调画 —— 原代码把它排在
            // 整行 HitTest **之后**，而 ImGui 同窗口内先注册者独占 HoveredId
            //（imgui.cpp:5161），于是那个按钮永远 clicked=false。它当时没暴露，
            // 只因为外面还写了 `|| hit.clicked` 兜底，整卡点击把功能兜住了 ——
            // 按钮画得出来、按下去没反应。footer 在整卡命中**之后**调用，顺序对了。
            const std::string meta =
                card.tplName.empty() ? ("最近 " + card.when) : (card.tplName + " · " + card.when);
            bool openPressed = false;
            kit::ListCardSpec spec;
            spec.id = "hub-open-" + std::string(card.entry.id);
            spec.icon = {}; // 缩略图走 Art，不是 kit 图标
            spec.title = card.entry.name;
            spec.description = meta;
            spec.titleSize = 13.0f;
            spec.descSize = 11.5f;
            spec.paddingX = 12.0f;
            // 缩略图 64 宽 + 10 左内边距 + 12 间距 ⇒ 文字从 +86 起（原代码的 nameX）。
            spec.textInset = 64.0f + 10.0f + 12.0f;
            const Rect rowFinal = row;
            // 缩略图 + 类型 Tag + 「打开」按钮都排在 footer 里，而 footer 由
            // ListCard 在整卡命中**之后**调用（顺序反了按钮永远点不动）。
            const kit::Hit cardHit = kit::ListCard(
                draw, row, spec, [&](ImDrawList* d, Rect footer) {
                // 封面：程序化插画（无外部资源），64×42。
                Art(d, Rect{rowFinal.min.x + 10.0f, rowFinal.min.y + 10.0f, rowFinal.min.x + 74.0f,
                            rowFinal.min.y + 52.0f},
                    card.artSeed, false);
                // 类型 Tag 贴在标题右侧（标题行，不是卡心）。
                if (!card.tplLabel.empty()) {
                    const theme::Tone tone = card.tplLabel == "小说"  ? theme::Tone::Accent
                                              : card.tplLabel == "影视" ? theme::Tone::Info
                                                                        : theme::Tone::Idle;
                    const float tagW = TagWidth(card.tplLabel, true, false);
                    const float nameW = LabelWidth(FontBoldAt(13.0f), 13.0f, card.entry.name.c_str());
                    Tag(d, RectAt(rowFinal.min.x + 86.0f + nameW + 8.0f,
                                 rowFinal.min.y + 10.0f, tagW, TagHeight(true)),
                        card.tplLabel, tone, true);
                }
                ButtonSpec openSpec;
                openSpec.variant = ButtonVariant::Primary;
                openSpec.size = ButtonSize::Small;
                const float openW =
                    ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
                const Rect openRect{rowFinal.max.x - 14.0f - openW, rowFinal.center().y - 12.0f,
                                    rowFinal.max.x - 14.0f, rowFinal.center().y + 12.0f};
                openPressed = Button(d, openRect, "打开", openSpec,
                                     "hub-open-btn-" + std::string(card.entry.id));
            });
            // 整卡点击走 ListCard **返回的同一个 Hit**，不能再补一次 HitTest：
            // 同窗口内先注册者独占 HoveredId，第二次永远 clicked=false。
            if (openPressed || cardHit.clicked) {
                // 先按值取出来：OpenHubCard 会刷新列表，hub.cards 随即重建
                const HubCard target = card;
                hub.openDlg = false;
                OpenHubCard(hub, target);
                return;
            }
            y += 70.0f;
        }
    }

    const char* hint = "单击卡片直接打开";
    draw->AddText(FontAt(11.0f), 11.0f, ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f),
                  ColorTextMuted(), hint, hint + std::strlen(hint));
    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec folderSpec;
    folderSpec.variant = ButtonVariant::Ghost;
    folderSpec.icon = "folder";
    folderSpec.disabled = hub.cards.empty();
    const std::vector<ModalAction> actions{
        {"打开所在文件夹", folderSpec, "hub-open-reveal"},
        {"关闭", ghostSpec, "hub-open-close"}};
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
    if (hit[0] != 0) {
        const std::string error = util::ShellReveal(hub.cards.front().entry.rootDir);
        hub.status = error.empty() ? "已在资源管理器中显示项目目录" : error;
        hub.statusError = !error.empty();
    }
    if (hit[1] != 0) {
        hub.openDlg = false;
    }
}

} // namespace

// 全屏，不套外壳。居中列 max-w 1080，底两层径向渐变 + 240px 高 ArtInk 带。
void DrawProjectHub(Rect area, ImDrawList* draw) {
    HubState& hub = Hub();
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
        RefreshHub(hub);
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
        RefreshHub(hub); // 排序换了要真重排（RefreshHub 按 hub.sort 排）
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
    RefilterHub(hub);

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
            const HubCard& card = hub.cards[static_cast<std::size_t>(hub.shown[i])];
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;
            const Rect bounds{grid.min.x + (cardW + 14.0f) * static_cast<float>(column),
                              grid.min.y + (cardH + 14.0f) * static_cast<float>(row), cardW, cardH};
            if (DrawHubCard(draw, hub, card, bounds, cardW - 28.0f)) {
                openIndex = static_cast<int>(i);
            }
        }
        if (openIndex >= 0) {
            OpenHubCard(hub, hub.cards[static_cast<std::size_t>(
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
        bool dismissConfirm = false;
        const ModalBox box = DrawModal(draw, area, hub.confirmTitle, "info", 460.0f, 210.0f,
                                       hub.dismissArmed ? &dismissConfirm : nullptr);
        if (dismissConfirm) {
            hub.confirm = false;
        }
        DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                        ImVec2(box.body.min.x + 18.0f, box.body.min.y + 18.0f),
                        box.body.width() - 36.0f, ColorTextSecondary(), hub.confirmBody, true);
        ButtonSpec ghostSpec;
        ghostSpec.variant = ButtonVariant::Ghost;
        ButtonSpec dangerSpec;
        dangerSpec.variant = ButtonVariant::Danger;
        const std::vector<ModalAction> actions{{"取消", ghostSpec, "hub-confirm-cancel"},
                                               {hub.confirmOk, dangerSpec, "hub-confirm-ok"}};
        const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
        if (hit[0] != 0) {
            hub.confirm = false;
        }
        if (hit[1] != 0) {
            RemoveHubCard(hub, hub.pendingId);
            hub.confirm = false;
        }
    }
    if (hub.wizard) {
        DrawHubWizard(hub, area, draw);
    }
    if (hub.openDlg) {
        DrawHubOpenDialog(hub, area, draw);
    }
}

bool HubDialogOpen() { return g_hubDialogOpen; }

} // namespace shine::pages
