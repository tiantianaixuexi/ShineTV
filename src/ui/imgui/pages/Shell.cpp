// shine::pages —— 外壳的**本体**：帧循环 / 工作区分发 / 快捷键 / 布局持久化 /
// 工程打开 / 项目中心整屏
//
// 拆完之后这一份只留「谁在什么时候被调用」这一层：
//   * DrawFrame        每帧的总调度（先外壳 chrome、再页面、最后浮层）
//   * DrawWorkspace    把当前工作区页分发出去，并收它的内容高度
//   * ApplyShortcuts   快捷键 → 状态
//   * SaveLayout/LoadLayout   layout.dat（P1.5 的契约）
//   * SetProjectRoot / OpenProjectByName
//   * DrawProjectHubScreen    项目中心整屏
//
// 画出来的那几块（顶栏 / 侧栏 / 检查器 / 底栏 / 浮层）与报告解析分别在
// Shell_Chrome.cpp / Shell_Panels.cpp / Shell_Dock.cpp / Shell_Palette.cpp /
// Shell_Reports.cpp；外壳的固定档在 Shell_Layout.h。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/Shell_Layout.h"

#include "comfy/ComfySession.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "pipeline/StageMachine.h"
#include "ui/imgui/host/AppEnvironment.h"
#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/BookData.h"
#include "ui/imgui/pages/Gallery.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Shell.h"

#include <yyjson.h>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

namespace {

// layout.dat 的 magic 与 Qt 侧一致（P1.5 的契约）
constexpr char kLayoutMagic[16] = {'s', 'h', 'i', 'n', 'e', 't', 'v', '-', 'l', 'a', 'y', 'o',
                                   'u', 't', '-', '1'};

std::filesystem::path LayoutFile() {
    if (const std::filesystem::path dir = shine::app::EnvironmentPath(L"APPDATA");
        !dir.empty()) {
        return dir / L"ShineTVStudio" / L"layout.dat";
    }
    return {};
}

} // namespace

const char* WorkspaceIcon(int workspace) {
    switch (workspace) {
    case 0: return "gauge";
    case 1: return "book";
    case 2: return "masks";
    case 3: return "clapper";
    case 4: return "image";
    case 5: return "film";
    default: break;
    }
    return "grid";
}

const char* WorkspaceLabel(int workspace) {
    switch (workspace) {
    case 0: return "总控";
    case 1: return "小说";
    case 2: return "资产";
    case 3: return "分镜";
    case 4: return "出图";
    case 5: return "出片";
    default: break;
    }
    return "组件画廊";
}

const char* WorkspaceSubtitle(int workspace) {
    switch (workspace) {
    case 0: return "全流程总控台";
    case 1: return "小说生产工作区";
    case 2: return "视觉资产工作区";
    case 3: return "分镜工作区";
    case 4: return "出图工作区";
    case 5: return "出片工作区";
    default: break;
    }
    return "组件画廊";
}

const char* WorkspaceTarget(int workspace) {
    switch (workspace) {
    case 0: return "pipeline";
    case 1: return "novel";
    case 2: return "assets";
    case 3: return "storyboard";
    case 4: return "imageflow";
    case 5: return "videoflow";
    default: break;
    }
    return "gallery";
}

// ---------------------------------------------------------------- 快捷键
void Shell::ApplyShortcuts() {
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        ToggleSidePanel();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_J, false)) {
        ToggleDock();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) {
        ToggleInspector();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        ToggleCommandPalette();
    }
    // Ctrl+Enter = 运行（phases.md:286 的契约，设计稿顶栏「运行 / 下一阶段」也在这条
    // 快捷键的语义上）。以前**没注册**：全树的 IsKeyPressed 只有面板的 ↑↓/Enter/Esc/K
    // 和上面这三条，phases.md 要求的这条一直缺着。
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (runActive_) {
            RequestRunStop();
        } else {
            RequestRunStart();
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        // ⚠️ 这里原来写的是 `paletteOpen_ = !paletteOpen_`，而命令面板里那条
        // 「切换主题 / Ctrl+T」承诺的是**切主题**。于是按 Ctrl+T 把命令面板打开一遍，
        // 面板上那条 Ctrl+T 又只能把面板再开关一次 —— 套娃，永远切不到主题。
        // 改成真的轮换主题；面板里那条走 RunPaletteAction(ThemeNext)，同一份逻辑。
        RunPaletteAction(PaletteAction::ThemeNext, 0);
    }
    // ⚠️ 这两条以前**根本没注册**：命令面板上写着「新建项目 / Ctrl+N」「打开项目 /
    //    Ctrl+O」，但 ApplyShortcuts 里只有 B / J / I / K / T。面板里那条早先也是空操作，
    //    于是这行提示从头到尾是**两处都死的**。
    // 执行体走 RunPaletteAction —— 与面板 Enter 那条共用一份，不在这里重写一遍。
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        RunPaletteAction(PaletteAction::NewProject, 0);
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        RunPaletteAction(PaletteAction::OpenProject, 0);
    }
    // 浮层的关闭键。设计稿的三处都写了 Esc 关闭（校验报告 modal 的 footer、
    // 设置向导、命令面板），补齐后浮层才算真正可用。
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (paletteOpen_) {
            paletteOpen_ = false;
        } else if (settingsOpen_) {
            settingsOpen_ = false;
        } else if (reportDetail_ >= 0) {
            reportDetail_ = -1;
        } else if (themeMenuOpen_) {
            themeMenuOpen_ = false;
        } else if (hubOpen_) {
            hubOpen_ = false;
        }
    }
}

void Shell::SetWorkspace(int index) {
    layout_.workspace = std::clamp(index, 0, kWorkspaceCount - 1);
    // 第三段面包屑改成**按工作区现算**（DerivedViewLabel），这里不再无条件写死
    // "总览" —— 写了它就永远盖住真状态。字段保留只为 layout.dat 的向后兼容。
}

void Shell::SetDockTab(int tab) { layout_.dockTab = std::clamp(tab, 0, 3); }

void Shell::SetReportDetail(int index) {
    reportDetail_ = index;
    reportScroll_ = 0.0f;  // 换一份报告就从顶上读，别留着上一份的滚动位置
    if (reportDetail_ >= static_cast<int>(reports_.size())) {
        reportDetail_ = -1;  // 没那么多报告就别开 —— 模态只认列表里真实存在的下标
    }
}

void Shell::SetTheme(shine::theme::ThemeId id) {
    theme::ApplyTheme(id);
    // 水墨是唯一换衬线族的主题 → 字体图集要重建，其余主题只换 ImGuiStyle。
    //
    // ⚠️ 双向都要判，且要记住当前图集是哪一个族：
    //   * 只在「切到水墨」时重建 → 切离水墨后图集还停在宋体，其他主题的字全是宋体观感
    //   * 不记状态连续重建 → 每调一次 SetTheme(ink) 就重烘一遍图集（16 档 ×
    //     两族字形集），取证跑 5 套主题时会连续烘 5 次，卡顿且无意义
    const bool wantSerif = theme::ThemeUsesSerif(id);
    if (wantSerif != atlasIsSerif_) {
        if (!BuildFontAtlas(/*serif=*/wantSerif)) {
            shine::log::Error("font atlas rebuild failed for {} family — 文字可能缺字",
                              wantSerif ? "serif" : "sans");
        }
        atlasIsSerif_ = wantSerif;
        // 重建后 io.FontDefault 变了，必须把 Style 的字体色/尺寸基线重刷一遍
        theme::ApplyCurrentTheme();
    }
    (void)theme::PersistTheme(theme::DefaultThemeFile());
}

void Shell::ToggleSidePanel() { layout_.sidePanelVisible = !layout_.sidePanelVisible; }
void Shell::ToggleDock() { layout_.dockVisible = !layout_.dockVisible; }
void Shell::ToggleInspector() { layout_.inspectorVisible = !layout_.inspectorVisible; }
void Shell::ToggleCommandPalette() {
    paletteOpen_ = !paletteOpen_;
    if (paletteOpen_) {
        // 每次打开从顶上开始，并选中第一项 —— 沿用上次的滚动位置会让人以为列表被过滤过。
        paletteScroll_ = 0.0f;
        paletteSelected_ = 0;
        // 焦点交给输入框：「打开就打字」才是命令面板的基本用法。早先这里没有这一句，
        // 打开后必须先用鼠标点一下输入框才能打字（SetKeyboardFocusHere 被删过）。
        paletteJustOpened_ = true;
    }
}

void Shell::PushLog(std::string_view level, std::string_view message) {
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    char stamp[16];
    std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d", now.wHour, now.wMinute, now.wSecond);
    logLines_.emplace_back(std::string(stamp) + " [" + std::string(level) + "] " +
                           std::string(message));
    // 只留最近 400 行：底栏日志是滚动视图，不裁会一直涨
    if (logLines_.size() > 400) {
        logLines_.erase(logLines_.begin(), logLines_.begin() + 200);
    }
}

// ---------------------------------------------------------------- 工程打开/新建
void Shell::SetProjectRoot(std::filesystem::path root, std::string name) {
    layout_.projectRoot = std::move(root);
    layout_.projectName = std::move(name);
    // 总控页的 Runner 要靠工程根才知道 work/ 与 ledger 的位置。
    // 不绑 = 页面显示真实空态，而不是编一份假账本出来。
    pages::BindOverviewProject(layout_.projectRoot);
    // 小说 / 资产 / 分镜三页共用同一份工程快照，任绑一个即可（见 WorkspacePages.h）。
    pages::BindNovelProject(layout_.projectRoot);
    (void)SaveLayout();
}

bool Shell::OpenProjectByName(const std::string& name) {
    // 在最近列表里按名字找根目录，再交给 ProjectService 真正打开
    std::filesystem::path root;
    for (const project::RecentEntry& entry : projects_.Recent()) {
        if (entry.name == name) {
            root = entry.rootDir;
            break;
        }
    }
    if (root.empty()) {
        PushLog("warn", "未找到工程：" + name);
        return false;
    }
    auto opened = projects_.Open(root);
    if (!opened) {
        PushLog("error", "打开工程失败：" + name + " · " + std::string(opened.error().message));
        return false;
    }
    SetProjectRoot(root, name);
    PushLog("info", "已打开工程：" + name);
    return true;
}

// ---------------------------------------------------------------- P1.5 布局持久化
bool Shell::SaveLayout() {
    const std::filesystem::path file = LayoutFile();
    if (file.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    // 定长定序：magic + 版本 + 布局块。损坏 → LoadLayout 走默认值，不崩。
    struct Header {
        char magic[16];
        std::uint32_t version;
        std::uint32_t workspace;
        std::uint32_t sidePanelVisible;
        std::uint32_t dockVisible;
        std::uint32_t inspectorVisible;
        std::uint32_t sidePanelWidth;
        std::uint32_t inspectorWidth;
        std::uint32_t dockHeight;
        std::uint32_t dockTab;
        std::uint32_t reduceMotion;
        std::uint32_t nameLength;
        std::uint32_t viewLength;
    } header{};
    std::memcpy(header.magic, kLayoutMagic, sizeof(kLayoutMagic));
    header.version = 1;
    header.workspace = static_cast<std::uint32_t>(layout_.workspace);
    header.sidePanelVisible = layout_.sidePanelVisible ? 1u : 0u;
    header.dockVisible = layout_.dockVisible ? 1u : 0u;
    header.inspectorVisible = layout_.inspectorVisible ? 1u : 0u;
    header.sidePanelWidth = static_cast<std::uint32_t>(layout_.sidePanelWidth);
    header.inspectorWidth = static_cast<std::uint32_t>(layout_.inspectorWidth);
    header.dockHeight = static_cast<std::uint32_t>(layout_.dockHeight);
    header.dockTab = static_cast<std::uint32_t>(layout_.dockTab);
    header.reduceMotion = layout_.reduceMotion ? 1u : 0u;
    header.nameLength = static_cast<std::uint32_t>(layout_.projectName.size());
    header.viewLength = static_cast<std::uint32_t>(layout_.lastViewLabel.size());
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(layout_.projectName.data(), static_cast<std::streamsize>(layout_.projectName.size()));
    out.write(layout_.lastViewLabel.data(),
              static_cast<std::streamsize>(layout_.lastViewLabel.size()));
    return out.good();
}

bool Shell::LoadLayout() {
    const std::filesystem::path file = LayoutFile();
    if (file.empty() || !std::filesystem::exists(file)) {
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    struct Header {
        char magic[16];
        std::uint32_t version;
        std::uint32_t workspace;
        std::uint32_t sidePanelVisible;
        std::uint32_t dockVisible;
        std::uint32_t inspectorVisible;
        std::uint32_t sidePanelWidth;
        std::uint32_t inspectorWidth;
        std::uint32_t dockHeight;
        std::uint32_t dockTab;
        std::uint32_t reduceMotion;
        std::uint32_t nameLength;
        std::uint32_t viewLength;
    } header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || std::memcmp(header.magic, kLayoutMagic, sizeof(kLayoutMagic)) != 0 ||
        header.version != 1) {
        return false; // 损坏 → 保持默认布局，不崩（照 Qt 侧 MainWindow 的行为）
    }
    layout_.workspace = std::clamp(static_cast<int>(header.workspace), 0, kWorkspaceCount - 1);
    layout_.sidePanelVisible = header.sidePanelVisible != 0;
    layout_.dockVisible = header.dockVisible != 0;
    layout_.inspectorVisible = header.inspectorVisible != 0;
    layout_.sidePanelWidth = static_cast<int>(header.sidePanelWidth);
    layout_.inspectorWidth = static_cast<int>(header.inspectorWidth);
    layout_.dockHeight = static_cast<int>(header.dockHeight);
    layout_.dockTab = static_cast<int>(header.dockTab);
    layout_.reduceMotion = header.reduceMotion != 0;
    kit::SetReduceMotion(layout_.reduceMotion);
    layout_.projectName.resize(header.nameLength);
    layout_.lastViewLabel.resize(header.viewLength);
    in.read(layout_.projectName.data(), header.nameLength);
    in.read(layout_.lastViewLabel.data(), header.viewLength);
    return true;
}

// ---------------------------------------------------------------- 帧
void Shell::DrawFrame(float dt) {
    lastDelta_ = dt;
    // 过渡补间池只在第一次进来时预分配一次。放在这里而不是 AppEntry：
    // Shell 构造完成、主题 JSON 也加载完之后才开始画，预分配跟着第一帧走最自然。
    if (!animPoolReserved_) {
        animPoolReserved_ = true;
        // 容量按「一屏里可能同时在飞的补间数」估：每页几十个 hover 通道，
        // 加上列表行长尾，给到几百条，避免第一次划过列表时才扩容。
        kit::ReserveTweenPool(512, 128, 256, 64, 512);
    }
    kit::TickAnimation(dt);
    ApplyShortcuts();

    // Comfy 会话：ImGui 前端原先**从没初始化过**它（AppEntry 只 Init 了 log/async/gallery），
    // 所以顶栏状态点与底栏队列只能画假的。这里按 AppSettings 的地址起一次会话，
    // 每帧只跑 ComfySession::Tick —— 它内部是纯定时器 + 非阻塞 socket，不碰同步 HTTP。
    static bool comfyInited = false;
    if (!comfyInited) {
        comfyInited = true;
        comfy::ComfySession::Instance().Init(Settings().comfyBaseUrl);
    }
    comfy::ComfySession::Instance().Tick(dt);

    // 页面层的 toast 通道：页面不认识 Shell（那是外壳的活），所以由外壳注入自己的
    // Notify。首帧之后页面才可能有按钮被点，注入放在这里一次就够。
    // ⚠️ 捕获 this 而不是裸函数指针：Notify 是成员函数。
    static bool toastWired = false;
    if (!toastWired) {
        toastWired = true;
        pages::SetWorkspaceToast([this](std::string message) { Notify(std::move(message)); });
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("##shine-root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float W = display.x;
    const float H = display.y;

    // 项目中心是**整屏**替身（webui Shell.jsx:850 的 screen==='hub'）：
    // 进项目中心时顶栏/导航/侧栏/检查器/底栏/状态栏全部让位，只留它自己。
    if (hubOpen_) {
        DrawProjectHubScreen(RectAt(0, 0, W, H), draw);
        // 每帧（无条件）同步「中心里对话框开着」这个读数。只在开着时写的话，
        // 探针会读到上一帧的残留，把「已经关了」误读成「还开着」。
        hubDialogOpen_ = pages::HubDialogOpen();
        ImGui::End();
        kit::DrawDebugWindows(debug_);
        return;
    }
    // 中心关着时也要清：否则探针在中心关闭后仍会读到「上一个对话框开着」。
    hubDialogOpen_ = false;

    const float dockH = layout_.dockVisible ? static_cast<float>(layout_.dockHeight) : 0.0f;
    const float sideW = layout_.sidePanelVisible ? static_cast<float>(layout_.sidePanelWidth) : 0.0f;
    const float inspW = layout_.inspectorVisible ? static_cast<float>(layout_.inspectorWidth) : 0.0f;
    const float bodyTop = kTopBarHeight;
    const float bodyBottom = H - kStatusBarHeight - dockH;

    // ---- 浮层闸门：先算「chrome 现在接不接受鼠标」，再登记主题菜单的外部关闭 ----
    //
    // 四个开态正好对应 DrawFrame 尾部那四个在**根窗口**里提交 item 的浮层
    // （命令面板 / 设置模态 / 报告模态 / 主题菜单）。开着的时候 chrome 走
    // ChromeHit()、工作区 child 带 NoMouseInputs，于是浮层按钮拿得到
    // HoveredWindow，底下的侧栏 / 顶栏 / 工作区也点不动。
    // 机制（imgui.cpp:6573 / 6607 / 5156 / 5161）写在 ChromeHit 的注释里。
    chromeInteractive_ = !(paletteOpen_ || settingsOpen_ || reportDetail_ >= 0 || themeMenuOpen_);
    themeOutsideHit_ = kit::Hit{};
    if (themeMenuOpen_) {
        // 必须**先于** chrome 注册：同窗口内先注册者独占 HoveredId，晚了就抢不到。
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        themeOutsideHit_ = kit::HitTest(Rect{0.0f, 0.0f, d.x, d.y}, "theme-outside");
    }

    DrawTopBar(RectAt(0, 0, W, kTopBarHeight), draw);
    DrawRail(RectAt(0, bodyTop, kRailWidth, bodyBottom - bodyTop), draw);

    const float centerX = kRailWidth + sideW;
    const float centerW = W - kRailWidth - sideW - inspW;
    if (layout_.sidePanelVisible) {
        DrawSidePanel(RectAt(kRailWidth, bodyTop, sideW, bodyBottom - bodyTop), draw);
    }
    if (layout_.inspectorVisible) {
        DrawInspector(RectAt(W - inspW, bodyTop, inspW, bodyBottom - bodyTop), draw);
    }

    DrawBreadcrumbs(RectAt(centerX, bodyTop, centerW, kCrumbHeight), draw);
    workspace_ = RectAt(centerX, bodyTop + kCrumbHeight, centerW, bodyBottom - bodyTop - kCrumbHeight);
    DrawWorkspace(workspace_, draw);

    if (layout_.dockVisible) {
        DrawDock(RectAt(0, H - kStatusBarHeight - dockH, W, dockH), draw);
    }
    DrawStatusBar(RectAt(0, H - kStatusBarHeight, W, kStatusBarHeight), draw);

    DrawCommandPalette();
    DrawSettingsModal();
    DrawReportModal();
    DrawThemeMenu(themeMenuAnchor_, ImGui::GetForegroundDrawList());
    DrawOverlays(draw);

    ImGui::End();
    kit::DrawDebugWindows(debug_);
}

// ---------------------------------------------------------------- 工作区分发
void Shell::DrawWorkspace(Rect area, ImDrawList* /*draw*/) {
    // ⚠️ 必须用 ImGui::BeginChild 当滚动区：自绘控件只出 draw call，不出裁剪也不出
    //    滚动，内容超出既不会被裁也不会滚，鼠标还会穿透。BeginChild 一次给全。
    // ⚠️ 浮层开着的时候必须让这个 child 退出命中测试，否则浮层按钮全是死的。
    //    机制写在 Scroll.h 的构造注释里（`g.HoveredWindow` 归 child，根窗口 item
    //    一律 hovered=false）。这里四个开态正好对应 DrawFrame 尾部那四个在**根窗口**
    //    里提交 item 的浮层：命令面板 / 设置模态 / 报告模态 / 主题菜单。
    //    项目中心不走这里（整屏替身，浮层在 hub-scroll child 内部提交，本来就能点）。
    const bool overlayBlocksMouse = paletteOpen_ || settingsOpen_ || reportDetail_ >= 0 ||
                                    themeMenuOpen_;
    kit::ScrollRegion region("workspace-scroll", area, /*borders=*/false, /*horizontal=*/false,
                             /*noMouseInputs=*/overlayBlocksMouse);
    if (!region) {
        return;
    }
    const kit::Rect origin = region.content();

    // 页面必须画进 **child 自己的** draw list：BeginChild 的裁剪矩形只作用于它自己的
    // draw list。传父窗口的 draw 进去，内容会一路溢出盖住底栅和状态栏（实测过）。
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // ⚠️ 满幅画布页（出图/出片）不能撑高：它们的画布是「铺满可视区」语义，
    //    撑到 2400 会让 FlowCanvas 的 fit 按 2400 高居中，y 偏移直接顶出视口，
    //    结果一个节点都看不见（实测 canvas=644x2400 → y=1112）。这两页自带
    //    内部滚动，不需要外壳再撑一次。
    const bool fullBleed = layout_.workspace == 4 || layout_.workspace == 5;
    const kit::Rect view{origin.min,
                         ImVec2(origin.max.x, origin.min.y + (fullBleed ? origin.height() : 2400.0f))};

    // 页面要自建视口（侧栏 / 面板 / 列）时必须知道**真正能看见多少**，
    // 而不是下面那个 2400 的布局区高。见 WorkspacePages.h 的说明。
    pages::SetWorkspaceViewportHeight(origin.height());
    // 每帧清一次：上一页面自报的内容高**不能**被这一页继承（否则切工作区后滚动
    // 范围会停在上一页的值上）。没自报的页面这一帧读到 0，退回 2400。
    pages::ResetPageContentHeight();

    switch (layout_.workspace) {
    case 0: DrawOverview(view, draw); break;
    case 1: novel_.Draw(view, draw); break;
    case 2: assets_.Draw(view, draw); break;
    case 3: storyboard_.Draw(view, draw); break;
    case 4: imageflow_.Draw(view, draw); break;
    case 5: videoflow_.Draw(view, draw); break;
    default: gallery_.Draw(view, draw); break;
    }
    // ⚠️ 这行是整个外壳**唯一**让工作区能滚的地方，缺了它滚轮怎么转都停在原地。
    //    页面是纯自绘的，全程没给 ImGui 提交过 item，高度必须显式报上去。
    //
    //    高度优先用页面**自报的真实内容高**（`SetPageContentHeight`），没有自报的
    //    页面退回 2400 的布局区。2400 是「够用」不是「刚好」：页面若按它铺卡片就会
    //    铺出巨型空盒子（实测总控页「账本」卡 1750px 高、只有 6 行），并让工作区
    //    多出上千 px 只能滚到空白的滚动范围。
    const float reported = pages::PageContentHeight();
    region.setContentHeight(reported > 0.0f ? reported : view.height());
}


// ---------------------------------------------------------------- P4.9c 项目中心
void Shell::ToggleProjectHub() {
    hubOpen_ = !hubOpen_;
    themeMenuOpen_ = false;
    settingsOpen_ = false;
    if (hubOpen_) {
        PushLog("info", "进入项目中心");
    }
}

void Shell::DrawProjectHubScreen(Rect area, ImDrawList* draw) {
    // 只留一条顶栏：品牌 + 「返回工作区」+ 当前工程。项目中心自己的页头由它自己画。
    DrawRoundRect(draw, area.min, ImVec2(area.max.x, area.min.y + kTopBarHeight), 0.0f,
                  GlassColor());
    draw->AddLine(ImVec2(area.min.x, area.min.y + kTopBarHeight - 0.5f),
                  ImVec2(area.max.x, area.min.y + kTopBarHeight - 0.5f), ColorLineSubtle(), 1.0f);
    const float cy = 0.5f * kTopBarHeight;
    DrawIcon(draw, "grid", ImVec2(area.min.x + 18.0f, cy - 8.0f), 16.0f, ColorAccent());
    static constexpr char kHubTitle[] = "项目中心";
    draw->AddText(FontBoldAt(13.5f), 13.5f, ImVec2(area.min.x + 40.0f, cy - 6.75f), ColorText(),
                  kHubTitle, kHubTitle + sizeof(kHubTitle) - 1);
    {
        ButtonSpec backSpec;
        backSpec.variant = ButtonVariant::Secondary;
        backSpec.icon = "chevron";
        if (Button(draw, RectAt(area.max.x - 110.0f, cy - 14.0f, 94.0f, 28.0f), "返回工作区", backSpec,
                   "hub-back")) {
            ToggleProjectHub();
        }
    }
    if (Button(draw, RectAt(16.0f, cy - 14.0f, 26.0f, 28.0f), "←", ButtonSpec{},
               "hub-esc")) {
        ToggleProjectHub();
    }

    const Rect view{area.min.x, area.min.y + kTopBarHeight, area.max.x, area.max.y};
    kit::ScrollRegion region("hub-scroll", view);
    if (!region) {
        return;
    }
    // 内容高度由 DrawProjectHub **自报**（`SetPageContentHeight`）—— 只有它知道栅格
    // 排了几行、每行多高。
    //
    // ⚠️ 这里**不能**回退成「上报视口高」。上一版写的正是
    //    `setContentHeight(region.content().height())` —— 上报值恰好等于视口高 ⇒
    //    ScrollMaxY 恒为 0 ⇒ 项目超过一屏就被裁掉且滚不到（1080 高窗口约 6 张，
    //    第 7 张起够不着）。**看着加了、实际等于没加**，是「改了但没生效」最典型的形态：
    //    编译器不报错、像素看不出来，只有真去滚才发现。
    pages::ResetPageContentHeight();
    DrawProjectHub(region.content(), ImGui::GetWindowDrawList());
    const float hubContentH = pages::PageContentHeight();
    if (hubContentH <= 0.0f) {
        // 只报一次。真没上报时这属于覆盖洞（项目卡够不着），静默返回等于把它藏起来。
        static bool warned = false;
        if (!warned) {
            warned = true;
            shine::log::Error("hub-scroll: DrawProjectHub 没上报内容高度 —— "
                              "项目卡超过一屏就滚不到（ScrollMaxY 恒为 0）");
        }
    }
    region.setContentHeight(hubContentH);
}

} // namespace shine::pages
