#pragma once
// shine::pages::Shell —— 应用外壳（refactor/phases.md P4）
//
// 尺寸全部照 design-spec.md §4（100% 缩放下）：
//   TopBar 46 / Rail 56 / SidePanel 240 / Inspector 280 / Dock 190 / StatusBar 26 / Crumbs 34
//
// 布局是自绘的绝对栅格（不是 ImGui 的 dock）：设计稿的每一档宽高都有出处，
// 用 ImGui 的自动布局反而对不上。外壳本身只占一层全屏 ImGui 窗口，
// 子区域全部用 kit::Rect 传给各自的绘制函数。
#pragma once

#include "ui/imgui/kit/Debug.h"
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/pages/Gallery.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "ui/imgui/theme/Theme.h"

#include "project/Project.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::pages {

// 6 个工作区 + 组件画廊（Rail 第 10 项，hidden，只在这一栏出现）。
enum class Workspace {
    Overview = 0,   // 总控
    Novel,          // 小说
    Assets,         // 资产
    Storyboard,     // 分镜
    ImageFlow,      // 出图
    VideoFlow,      // 出片
    Gallery,        // 组件画廊
};

inline constexpr int kWorkspaceCount = 7;

[[nodiscard]] const char* WorkspaceIcon(int workspace);
[[nodiscard]] const char* WorkspaceLabel(int workspace);
[[nodiscard]] const char* WorkspaceSubtitle(int workspace);
// 数据通路目标（refactor/README 的模块表），取证 manifest 里要写清楚。
[[nodiscard]] const char* WorkspaceTarget(int workspace);

// 布局持久化（P1.5）：%APPDATA%/ShineTVStudio/layout.dat，magic shinetv-layout-1
struct ShellLayout {
    int workspace = 0;
    bool sidePanelVisible = true;
    bool dockVisible = true;
    bool inspectorVisible = true;
    int sidePanelWidth = 240;
    int inspectorWidth = 280;
    int dockHeight = 190;
    int dockTab = 0;
    std::string projectName;
    // 当前工程根。空 = 未打开项目。打开/新建项目后由 SetProjectRoot 写入，
    // 布局持久化也带上它（P1.5），重开程序能回到同一个工程。
    std::filesystem::path projectRoot;
    std::string lastViewLabel;
    bool reduceMotion = false;
};

class Shell {
public:
    void DrawFrame(float dt);

    // 取证 / 快捷键用
    void SetWorkspace(int index);
    [[nodiscard]] int workspace() const { return layout_.workspace; }
    // 取证用：选中底栏页签（0 任务队列 / 1 日志 / 2 产物 / 3 校验报告）。
    // 底栏页签平时只能点 `Tabs` 切换，取证要复现"停在第 N 页"就得有这条入口。
    void SetDockTab(int tab);
    // 取证用：打开第 index 份校验报告的逐项详情模态（-1 = 关闭）。
    // 走的是列表点击同一条状态路径（reportDetail_），不是给模态开后门。
    void SetReportDetail(int index);
    // 取证用：报告扫描是否已经回投（列表非空且当前不在扫）。
    // ⚠️ 抓图前必须等它为 true —— worker 上那一次目录遍历没回投就抓图，
    //    拍到的是「尚无校验报告」空态，图名和内容对不上（phases.md P6 第 3 条）。
    [[nodiscard]] bool ReportsReady() const { return !reportScanning_ && !reports_.empty(); }
    // 取证用：直接开 / 关设置模态与命令面板。
    // ⚠️ 浮层的 z 序**只有截图能验** —— 它们与页面各自画在不同 draw list 上，
    //    画错时不会崩、不报错，只是被工作区内容盖住，manifest 照样记 saved。
    void SetSettingsOpen(bool open) { settingsOpen_ = open; }
    void SetCommandPaletteOpen(bool open) {
        if (open != paletteOpen_) {
            ToggleCommandPalette();
        }
    }
    void SetTheme(shine::theme::ThemeId id);
    void ToggleSidePanel();
    void ToggleDock();
    void ToggleInspector();
    void ToggleCommandPalette();
    [[nodiscard]] kit::Rect workspaceRect() const { return workspace_; }
    // 项目中心（webui Shell.jsx 的 onHub）：品牌标 / 项目胶囊点击都进它。
    // 进项目中心时整个外壳让位，Esc 或再点一次退出。
    void ToggleProjectHub();
    [[nodiscard]] bool projectHubOpen() const { return hubOpen_; }

    // 打开/新建项目后登记工程根：顶栏胶囊、总控页、后续各工作区都从这里取。
    // root 为空表示「未打开项目」——总控页会显示真实空态，不编数字。
    void SetProjectRoot(std::filesystem::path root, std::string name);
    [[nodiscard]] const std::filesystem::path& projectRoot() const { return layout_.projectRoot; }
    // 取证要临时切工程根（SetProjectRoot 会立刻写 layout.dat），所以得能读回来还原，
    // 别让一次取证把用户登记的工程根覆盖成临时目录。
    [[nodiscard]] const std::string& projectName() const { return layout_.projectName; }
    [[nodiscard]] int dockTab() const { return layout_.dockTab; }
    // 从持久化的项目注册表里挑一个打开（项目中心「打开」按钮与命令面板都走它）。
    bool OpenProjectByName(const std::string& name);

    // 布局持久化
    [[nodiscard]] bool LoadLayout();
    [[nodiscard]] bool SaveLayout();

private:
    void DrawTopBar(kit::Rect area, ImDrawList* draw);
    void DrawRail(kit::Rect area, ImDrawList* draw);
    void DrawSidePanel(kit::Rect area, ImDrawList* draw);
    void DrawInspector(kit::Rect area, ImDrawList* draw);
    void DrawDock(kit::Rect area, ImDrawList* draw);
    void DrawStatusBar(kit::Rect area, ImDrawList* draw);
    void DrawBreadcrumbs(kit::Rect area, ImDrawList* draw);
    void DrawWorkspace(kit::Rect area, ImDrawList* draw);
    void DrawCommandPalette();
    void DrawOverlays(ImDrawList* draw);
    void ApplyShortcuts();
    // 往底栏「日志」页追加一行（"03:12:44 [info] 文本"）。
    void PushLog(std::string_view level, std::string_view message);

    // ---- P4.9 浮层：主题菜单 / 设置 / 项目中心 ----
    // 主题菜单锚在顶栏「主题」幽灵按钮下方（webui Shell.jsx:51-70）。
    void DrawThemeMenu(ImVec2 anchor, ImDrawList* draw);
    // 「设置 · 三步开工」模态：读 AppSettings 真值，不造假配置。
    void DrawSettingsModal();
    // 项目中心整屏（WorkspaceB 的 DrawProjectHub）。
    void DrawProjectHubScreen(kit::Rect area, ImDrawList* draw);
    // 底栏「产物」页：扫 <project>/output，IO 放 worker，回投后按路径判定要不要重扫。
    void RequestArtifactScan();
    void DrawDockArtifacts(kit::Rect body, ImDrawList* draw);
    // 底栏「任务队列」页：Comfy QueueModel 的真实快照。
    void DrawDockQueue(kit::Rect body, ImDrawList* draw);
    // 底栏「日志」页：运行期真实事件（PushLog 写入的行）。
    void DrawDockLogs(kit::Rect body, ImDrawList* draw);
    // 底栏「校验报告」页：扫 <project>/work/ch<NNN>/v08_continuity.json，IO 放 worker
    // （与 RequestArtifactScan 同一套模式，不发明第二份）。
    void RequestReportScan();
    // 底栏「校验报告」页 + 点开后的逐项详情模态。
    void DrawDockReports(kit::Rect body, ImDrawList* draw);
    void DrawReportModal();

    ShellLayout layout_;
    // 项目服务实例：新建 / 打开 / 最近列表都走它（前端持有自己的实例，见 Project.h:139）。
    project::ProjectService projects_;
    kit::Rect workspace_;
    float toastTimer_ = 0.0f;
    std::vector<std::string> logLines_;
    bool paletteOpen_ = false;
    char paletteQuery_[128] = {};
    int paletteSelected_ = 0;
    int paletteMatches_ = 0;
    // 列表滚动偏移与内容总高。16 条目 + 3 个组标题 = 546px，面板只有 358px 可用，
    // 不裁也不滚的话末尾几行会溢出面板、压在工作区上。
    float paletteScroll_ = 0.0f;
    float paletteContentH_ = 0.0f;
    float lastDelta_ = 0.0f;
    kit::DebugWindows debug_;
    // 当前字体图集是不是衬线族。Host 初始化时已按启动主题建过一次，
    // 这里存同一份状态，SetTheme 只在**族变了**时重建图集。
    bool atlasIsSerif_ = false;

    // ---- 浮层与面板的开关态（webui Shell.jsx:14 的 menu 状态）----
    bool hubOpen_ = false;        // 项目中心整屏（品牌标 / 项目胶囊点击进入）
    bool themeMenuOpen_ = false;  // 顶栏「主题」菜单
    bool settingsOpen_ = false;   // 「设置 · 三步开工」模态
    int reportDetail_ = -1;       // 底栏校验报告点开的条目下标，-1 = 未打开
    // 主题菜单锚点（顶栏「主题」按钮左下角），上一帧 DrawTopBar 写下。
    ImVec2 themeMenuAnchor_{0.0f, 0.0f};

    // ---- 底栏「产物」页的真实文件列表 ----
    // IO 走 worker：UI 线程只读这个缓存。scanRoot_ 记上次扫的是哪个工程根，
    // 换工程或点刷新才重扫（避免每帧 std::filesystem）。
    struct ArtifactRow {
        std::string name;
        std::filesystem::path path;
        std::uintmax_t bytes = 0;
        std::string kind;  // 扩展名
    };
    std::vector<ArtifactRow> artifacts_;
    std::filesystem::path artifactRoot_;
    std::filesystem::path artifactScanRoot_;
    bool artifactScanning_ = false;
    float artifactRefreshAt_ = 0.0f;

    // ---- 底栏「校验报告」页：<project>/work/ch<NNN>/v08_continuity.json 的真实报告 ----
    //
    // 真值源是 novelcore::RunContinuityChecks（V8 连续性，pipeline T12 / T15 的产出）落盘的那份
    // JSON，字段见 NovelContinuity.cpp:467。IO 走 worker，UI 线程只读缓存。
    // 早先这一页只给诚实空态，而 DrawReportModal 更糟：它是个**永远打不开的空壳** ——
    // 驱动它的 reportDetail_ 全代码只有 `= -1` 初始化与复位，从没有任何地方把它设成 ≥0。
    // 现在列表按章列真报告，点行开逐项详情。
    struct ReportIssue {
        std::string code;      // "C1".."C12"
        std::string severity;  // high | medium | low
        std::string detail;    // 中文，可直接显示
    };
    struct ChapterReport {
        int chapterOrd = 0;  // ch<NNN> 的序号（文件名给的；JSON 里只有库内的 chapter_id）
        int chapterId = 0;   // novel.db 的 chapter_id
        int shots = 0;
        int pairs = 0;
        int rulesChecked = 0;
        int failed = 0;
        int unverified = 0;
        std::vector<ReportIssue> issues;
        std::vector<std::string> notes;  // unverified 的原因，每条一次
        std::string path;                 // 报告落盘位置
    };
    // 模态里那张 compact 表的一行。设计稿那张表实际渲染 5 个 td 而表头只写了 4 个标题
    // （Shell.jsx:285 vs 289-293，设计稿自身缺陷）：表头照设计稿写 4 个，name 列留空 ——
    // 业务层的 ContinuityIssue 本来就没有独立的名称字段，不编一个出来。
    struct CheckRow {
        std::string code;
        std::string level;   // high | medium | low；未核对为空
        theme::Tone levelTone = theme::Tone::Idle;
        std::string verdict;  // 过 / 未过 / 提示 / 未核对
        theme::Tone verdictTone = theme::Tone::Idle;
        std::string detail;
    };
    std::vector<ChapterReport> reports_;
    std::filesystem::path reportScanRoot_;
    bool reportScanning_ = false;
    float reportRefreshAt_ = 0.0f;
    // 模态主体的纵向滚动偏移。12 条 issue 装不满时用滚轮滚。
    float reportScroll_ = 0.0f;

    // severity 串 → 色调（设计稿 Shell.jsx:291：high→danger / medium→warn / low→idle）。
    static theme::Tone SeverityTone(std::string_view severity);
    // 整条报告的三态：failed > unverified > 通过。unverified 是「数据不足」，
    // 给 Warn 而不是 Ok —— 照实显示，不粉饰成通过。
    static theme::Tone ReportTone(const ChapterReport& report);
    // 行尾 Tag 的文案，例如「1 未过 · 2 未核对 · 12 镜对」。
    static std::string ReportSummary(const ChapterReport& report);
    // issues + notes 铺成模态那张表的行。notes 形如「C3：数据不足…」，
    // 冒号前是规则码，取出来当「检查」列。
    static std::vector<CheckRow> BuildCheckRows(const ChapterReport& report);
    // 解析一个 v08_continuity.json。false = 文件缺失 / 非法 JSON / 缺 stage 字段（跳过该目录）。
    static bool ParseContinuityReport(const std::filesystem::path& file, ChapterReport& out);
    // 模态标题栏「导出 JSON」：把这一份报告写到 <project>/output/，写盘放 worker。
    void ExportReport(int index);

    // 流水线运行态（顶栏「运行 / 停止」二选一，webui Shell.jsx:44-48）。
    // Runner 是同步阻塞的，真正的执行放 worker；这里只存 UI 侧的状态。
    bool runActive_ = false;
    bool runFinished_ = false;
    int runStageIndex_ = 0; // 0..16，对应 T1..T17
    int runPercent_ = 0;

    // 六个工作区（各自持有交互态：选中的镜头/标签/页签）
    NovelPage novel_;
    AssetsPage assets_;
    StoryboardPage storyboard_;
    ImageFlowPage imageflow_;
    VideoFlowPage videoflow_;
    GalleryPage gallery_;
};

} // namespace shine::pages
