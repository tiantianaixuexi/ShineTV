#include "ui/imgui/verify/ReviewScene.h"

#include "core/Log.h"
#include "gpu/GpuTextureCache.h" // TextureCache().Clear()：淘汰试验（验约束 1 的自愈）
#include "ui/imgui/kit/Image.h"  // PendingImageCount：等真设定图异步落地
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/BookData.h"  // Book()：结果页签两行生成参数的投影读数
#include "ui/imgui/pages/MediaViewer.h"  // 查看器：工程内文件那一支的取证入口
#include "ui/imgui/pages/PageKpi.h"      // KpiGlowRgb()：KPI 柔光的合成色读数
#include "ui/imgui/verify/Capture.h"
#include "ui/imgui/verify/ReviewFixture.h"
#include "ui/imgui/verify/ReviewHotspot.h"
#include "ui/imgui/verify/ReviewHoverProbe.h"
#include "ui/imgui/verify/ReviewOverlayProbe.h"
#include "ui/imgui/verify/ReviewShortcutProbe.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

namespace shine::imguiverify::detail {

// ---- 第一段 + 第二段：静息态覆盖扫描 ----
void RunCoverageSweep(Session& session) {
    imguiapp::Host& host = session.host;
    pages::Shell& shell = session.shell;
    const shine::theme::ThemeId base = session.base;

    // ---- 第零段：**初始页**（项目中心）----
    //
    // 应用启动就落在它上面（`ShellLayout::hubOpen_` 初值为 true，对齐 webui
    // `store.jsx:13` 的 `useState('hub')`），而它是**整屏替身** —— 开着的时候
    // `Shell::DrawFrame` 只画它、工作区一帧都不提交。所以：
    //   * 它必须排在最前（这一张拍的就是「一开机看到的那一屏」）；
    //   * 拍完必须**显式关掉**，否则下面每一张都拍到同一屏，而 manifest 里写的
    //     是 ws-/theme- 的名字 —— 图名与内容不符，且照样记 saved。
    session.GrabHubDriven("hub-initial", base);
    shell.SetHubOpen(false);
    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

    // ---- 第一段：当前主题下全部工作区各一张（含组件画廊）----
    for (int workspace = 0; workspace < pages::kWorkspaceCount; ++workspace) {
        session.Grab(std::string("ws-") + shine::pages::WorkspaceIcon(workspace), workspace, base);
    }

    // ---- 第二段：6 个业务工作区 × 5 套主题（phases.md P6.3 的 30 组）----
    // 每个工作区记下 5 个哈希，末尾检查两两不同：**主题没真正切过去**时 5 张会完全一样。
    for (const shine::theme::ThemeId theme : shine::theme::kAllThemes) {
        for (const int workspace : kThemedWorkspaces) {
            session.themedHashes[workspace].push_back(session.Grab(
                std::string("theme-") + std::string(shine::theme::ThemeIdKey(theme)) + "-" +
                    shine::pages::WorkspaceIcon(workspace),
                workspace, theme));
        }
    }

    // 跑完回到起始主题，别把用户的 theme.json 留在取证用的最后一套上。
    shell.SetTheme(base);
}

// ---- 第三段起：受控状态取证 ----
//
// ⚠️ 这一整段都包在「fixture 造得出来吗」这个 if 里：造不出来时那一族取证跳过，
//    而跳过会体现在 overall 上（各条等待结论保持 false），不是静悄悄放过。
void RunControlledStates(Session& session) {
    imguiapp::Host& host = session.host;
    pages::Shell& shell = session.shell;
    const std::filesystem::path& outputDir = session.outputDir;
    const std::filesystem::path& manifest = session.manifest;
    const shine::theme::ThemeId base = session.base;

    // ⚠️ 这一整段（含三族探针）都是「在工作区里摆一个状态」的取证，而项目中心现在是
    //    **启动默认开着**的那一屏、且整屏顶替工作区。进段先显式关掉它：这里是所有
    //    直接 `PumpFrames` 驱动的探针的共同入口，靠「上一张图刚好把它关了」是不成立的
    //    依赖（`Shoot` 的缺省动作会关，但探针不走 `Shoot`）。
    shell.SetHubOpen(false);
    host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

    // ---- 第三段：底栏「校验报告」页 + 逐项详情模态 ----
    //
    // ⚠️ 这三张的前置动作必须写全：摆一个带**真实格式**报告的工程 → 选第 4 个页签 →
    //    等 worker 那次目录遍历真的回投 → 才推进到可抓图的状态。
    //    少任何一步，拍到的就是「未打开工程」或「尚无校验报告」空态 ——
    //    图名和内容对不上，而 manifest 照样记 saved。
    //
    // 各条等待结论（reportScanConverged / bookSnapshotConverged /
    // assetSnapshotConverged / artifactsConverged）与各族探针的计数都在
    // Session 上：既进 manifest，也进 overall 判据。
    //
    // 工作区序号（kWsOverview / kWsNovel / …）统一在 ReviewSession.h 里，
    // 不在各段就地声明 —— 就地声明过一次，后一段编译直接报「未声明」。
    const std::filesystem::path fixture = outputDir / "_review_reports_project";
    if (!SeedReportFixture(fixture)) {
        shine::log::Error("review: 报告 fixture 写不出来，校验报告三张取证跳过");
    } else {
        // SetProjectRoot 会立刻写 layout.dat —— 先把用户登记的工程根读出来，抓完原样还回去。
        const std::filesystem::path savedRoot = shell.projectRoot();
        const std::string savedName = shell.projectName();
        const int savedTab = shell.dockTab();

        shell.SetProjectRoot(fixture, "取证样例工程");
        shell.SetDockTab(3);
        int waited = 0;
        while (!shell.ReportsReady() && waited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++waited;
        }
        // ⚠️ 等待的结论要写进 manifest。上一轮把一个等待的返回值 (void) 掉，
        //    拍到「扫描中 · 已载入 0 张」那一帧，manifest 依旧 saved。
        session.reportScanConverged = shell.ReportsReady();
        WriteManifest(manifest, std::string("report-scan=") +
                                    (session.reportScanConverged ? "converged" : "TIMEOUT") +
                                    " after " + std::to_string(waited) + " frames");

        session.GrabDriven("dock-reports", kWsOverview, base);

        // 「绑定工程之后」的总控台 —— 与第一段的 `ws-gauge` 是**不同分支**。
        //
        // ⚠️ 第一段那 7 张拍的是**未打开项目**：`s.bound == false`，于是运行状态卡走
        //    「未绑定工程根」分支，两个运行按钮因为 `!s.bound` 而灰掉，而「阶段执行体
        //    未接入 · 两个运行按钮已禁用」那行说明的条件里有 `s.bound` ⇒ **不画**。
        //    也就是说：不单独拍这一张，新增的这条分支在整轮里**一次都跑不到**。
        //    这与「fixture 缺数据造成覆盖洞」是同一族：图拍到了 ≠ 分支跑到了。
        //    （KPI 那半边相反：`wired == false` 是无条件的，所以 ws-gauge 里已经是「未接入」。）
        shell.SetWorkspace(kWsOverview);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("overview-bound", kWsOverview, base);

        // ---- KPI 柔光的**像素**判据（同一页、紧接着拍）----
        //
        // 上一帧刚把总控页画过（工作区就是 overview），所以 `KpiGlowRgb()` 里是**当帧
        // 实际用的那个合成色**。判据要回答的是「这团柔光真到像素了没有」：
        // 这一轮把它从「按 tone 取色的 14 环径向渐变」改成 CSS 的
        // `--accent-dim × opacity .7` + 近实心盘 + `overflow:hidden` 裁剪，
        // 而**裁剪**这一环最容易静默失效 —— `PushClipRect` 写反或写小，
        // 症状是整团柔光消失，而卡片、文字、静息截图全都「正常」。
        //
        // ⚠️ 判据要有**上下界**，只判下界是不够的 —— 这一轮实测踩到：
        //    页面那边的 alpha 链写错（`WithAlpha` 是**调制**不是覆盖），柔光实际没画上去，
        //    报出来的色**退化成卡片底色**（#1A2230）。只判「≥8000」时它读到 350271 像素、
        //    判绿！—— 数的是全屏的卡底色。下界挡「没画/只画一张」，上界挡「色退化成底/面」：
        //    这团柔光是 5 个 90px 圆被裁掉一角，**必然**只有万把像素的量级；
        //    超过 4 万就说明数到的是一块面（卡底 35 万、页面底 更大），不是这团光。
        constexpr int kKpiGlowMinPixels = 8000;   // > 一张卡（实测 13953/5 ≈ 2790）→ 挡「只接了一张」
        constexpr int kKpiGlowMaxPixels = 40000;  // ≈ 2.9 × 实测总量（13953）→ 挡「色退化成面」
        const std::uint32_t kpiGlowRgb = pages::KpiGlowRgb();
        const int kpiGlowPixels = session.GrabCountColor("overview-kpi-glow", kWsOverview, base,
                                                        kpiGlowRgb, /*registerDriven=*/false);
        session.kpiGlow =
            kpiGlowPixels >= kKpiGlowMinPixels && kpiGlowPixels <= kKpiGlowMaxPixels;
        WriteManifest(manifest, std::string("kpi-glow=") + (session.kpiGlow ? "ok" : "BAD-PIXELS") +
                                    " pixels=" + std::to_string(kpiGlowPixels) + " rgb=#" +
                                    /* 统一六位十六进制，便于与 out\_colorcount.ps1 对照 */
                                    [](std::uint32_t rgb) {
                                        char buf[8];
                                        std::snprintf(buf, sizeof(buf), "%06X", rgb);
                                        return std::string(buf);
                                    }(kpiGlowRgb) +
                                    " want=[" + std::to_string(kKpiGlowMinPixels) + "," +
                                    std::to_string(kKpiGlowMaxPixels) + "]");

        // 右栏滚到底：「章节 × V 阶段」矩阵排在最后（停止条件 → 最近产物 → 运行信息 →
        // 矩阵），默认滚动位置只能拍到前两张。
        //
        // ⚠️ 旧门禁只查「图写出来了」，矩阵那张从来没被拍过 —— 与「fixture 缺数据
        //    造成覆盖洞」同族：**页面上有一块内容，但它不在任何一张截图里**。
        //    粘性请求（ScrollRegion::RequestScrollBottom）解决的是「取证驱动在
        //    DrawFrame 外面，拿不到局部 ScrollRegion 对象」这个接线问题。
        //
        // ⚠️ 下面必须**再判一次请求真的生效**，不能只看两张图不一样：
        //    第一版这里只有 `identical-driven-pairs = 0` 这一个判据，而它是绿的 ——
        //    实际滚都没滚（SetScrollHereY 在构造期没有 item 可依）。两张图仍然
        //    不同，只是因为左上角 326 个像素在动。**「图不一样」有二义性**：
        //    「滚到位了」和「别处在动」分不开。于是加 ScrollRegion::LastApplied()
        //    这个正交信号 —— 它读产品自己的 scrollY / maxScrollY，不从像素反推。
        kit::ScrollRegion::RequestScrollBottom("ov-right");
        // 2 帧：第 1 帧消费请求，第 2 帧才读得到生效后的 scrollY（核销就在第 2 帧）。
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        const kit::ScrollRegion::ScrollApplied scrolled = kit::ScrollRegion::LastApplied();
        // ⚠️ 三个分支必须**平级**写。早先写成
        //    `if (id == "ov-right" && maxScrollY > 0 && !ok)`，于是「maxScrollY == 0」
        //    （内容压根没超出区域）和「id 对不上」（请求压根没被核销）都落进 else，
        //    **静默通过** —— 而「没超出」恰恰是最该红的覆盖洞。判据自己漏一支，
        //    表现得和「一切正常」一模一样。
        if (scrolled.id != "ov-right") {
            shine::log::Error(
                "review: ov-right 的滚动请求从没被核销（id=\"{}\"）—— 请求没送到，"
                "或者 ScrollRegion 那一帧没被构造（覆盖洞）",
                scrolled.id);
            session.result.scrollFailed = true;
        } else if (scrolled.maxScrollY <= 0.0f) {
            shine::log::Error(
                "review: ov-right 期望滚动上限=0（自报内容高={:.1f} / 可视高={:.1f}）—— "
                "内容没超出就说明右栏压根没画够，或者 setContentHeight 没被调到（覆盖洞）",
                scrolled.contentHeight, scrolled.viewHeight);
            session.result.scrollFailed = true;
        } else if (!scrolled.ok) {
            shine::log::Error(
                "review: ov-right 滚到底失败：scrollY={:.1f} / 期望上限={:.1f} —— "
                "overview-vstages 这张图拍的仍是顶部，等于矩阵没被取证"
                "（ImGui 侧上限={:.1f}：它比期望值小说明内容高度没报上去，滚轮也滚不动）",
                scrolled.scrollY, scrolled.maxScrollY, scrolled.imGuiMaxScrollY);
            session.result.scrollFailed = true;
        } else if (scrolled.imGuiMaxScrollY < scrolled.maxScrollY - 1.0f) {
            // 粘性请求到位了，但 ImGui 自己不认识这个高度 ⇒ 用户用滚轮依然滚不动。
            // 判据必须报出来，否则「取证能滚」会掩盖「产品滚不动」。
            shine::log::Error(
                "review: ov-right 内容高度没报给 ImGui（期望上限={:.1f} / ImGui 侧={:.1f}）"
                " —— 自绘内容不被 ImGui 计入 ContentSize，滚轮无法滚动",
                scrolled.maxScrollY, scrolled.imGuiMaxScrollY);
            session.result.scrollFailed = true;
        }
        session.GrabDriven("overview-vstages", kWsOverview, base);
        // 复位：滚回顶部，否则后面几张图都在底部状态。
        //
        // ⚠️ 只登记**一次**。同一 id 先后登记「底 / 顶」时 ScrollRegion 按后登记者覆盖，
        //    多写一次不影响结果，但会让读代码的人以为「两次登记是必要的」—— 而实际上
        //    分成两个请求入口（不带 target 参数）时两次登记会互相抵消，正好停在中途，
        //    两张图都拍不到位。顶与底必须共用 RequestScroll 这一个入口。
        kit::ScrollRegion::RequestScrollTop("ov-right");
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 工作区主滚动区（workspace-scroll）也要验 ----
        //
        // ⚠️ 上一轮只验了 `ov-right`，而**工作区才是主滚动区** —— 修好 `setContentHeight`
        //    之后它才第一次真的能滚，却没有任何探针。一个没人验的滚动区等于没有。
        //    这条还顺带把「页面自报内容高度」顶替 2400 这件事验了：期望上限变小 ⇒
        //    滚动范围跟着内容走，而不是那 1700px 的空盒子。
        kit::ScrollRegion::RequestScrollTop("ov-right");
        kit::ScrollRegion::RequestScrollTop("workspace-scroll");
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        kit::ScrollRegion::RequestScrollBottom("workspace-scroll");
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        const kit::ScrollRegion::ScrollApplied ws = kit::ScrollRegion::LastApplied();
        if (ws.id != "workspace-scroll") {
            shine::log::Error(
                "review: workspace-scroll 的滚动请求从没被核销（id=\"{}\"）—— 工作区滚不动，"
                "任何超出视口的内容都够不着", ws.id);
            session.result.scrollFailed = true;
        } else if (ws.maxScrollY <= 0.0f) {
            shine::log::Error(
                "review: workspace-scroll 期望滚动上限=0（自报内容高={:.1f} / 可视高={:.1f}）"
                " —— 页面没上报内容高度（退回 2400 兜底）或内容确实没超出",
                ws.contentHeight, ws.viewHeight);
            session.result.scrollFailed = true;
        } else if (!ws.ok) {
            shine::log::Error(
                "review: workspace-scroll 滚到底失败：scrollY={:.1f} / 期望上限={:.1f}"
                "（ImGui 侧={:.1f}）", ws.scrollY, ws.maxScrollY, ws.imGuiMaxScrollY);
            session.result.scrollFailed = true;
        } else if (ws.imGuiMaxScrollY < ws.maxScrollY - 1.0f) {
            shine::log::Error(
                "review: workspace-scroll 内容高度没报给 ImGui（期望上限={:.1f} / ImGui 侧={:.1f}）"
                " —— 滚轮无法滚动", ws.maxScrollY, ws.imGuiMaxScrollY);
            session.result.scrollFailed = true;
        } else {
            shine::log::Info("review: workspace-scroll 可滚 自报内容高={:.1f} 可视高={:.1f} 上限={:.1f}",
                             ws.contentHeight, ws.viewHeight, ws.maxScrollY);
        }
        session.GrabDriven("overview-scrolled", kWsOverview, base);
        kit::ScrollRegion::RequestScrollTop("workspace-scroll");
        kit::ScrollRegion::RequestScrollTop("ov-right");
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // 两个模态状态拍的是**不同内容**（一份有未过项、一份有未核对项），
        // 于是它们既不该与列表图相同，也不该彼此相同。
        shell.SetReportDetail(1);
        session.GrabDriven("report-modal", kWsOverview, base);
        shell.SetReportDetail(2);
        session.GrabDriven("report-modal-unverified", kWsOverview, base);
        // ⚠️ 必须**先**关掉报告模态再拍下面两个：同一 foreground draw list 上
        //    浮层的 z 序由 DrawFrame 里的调用顺序决定，报告模态画在设置模态**之后**，
        //    两个同时开着时后画的会盖住先画的 —— 拍出来两张图会一模一样。
        shell.SetReportDetail(-1);

        // 另外两个浮层同样要验：它们与页面分别画在不同 draw list 上，z 序错了不崩不报，
        // 只是被工作区内容盖住。上一轮就是靠这两张才发现「模态只剩一条表头带」。
        shell.SetSettingsOpen(true);
        session.GrabDriven("overlay-settings", kWsOverview, base);
        shell.SetSettingsOpen(false);
        shell.SetCommandPaletteOpen(true);
        session.GrabDriven("overlay-palette", kWsOverview, base);
        shell.SetCommandPaletteOpen(false);

        // ---- 第四段：侧栏树 + 检查器 ----
        //
        // ⚠️ 拍这两张必须**真的打开带 novel.db 的工程**并等 worker 读完。侧栏与检查器
        //    读的是 pages::BookSide() 那份只读快照（由 ApplyBook 在 UI 线程重建），
        //    没绑上就只会拍到诚实空态。快照是异步的 —— 显式等到 chapters 非空为止，
        //    等待结论写进 manifest。
        shell.SetWorkspace(kWsStoryboard);
        int bookWaited = 0;
        while ((pages::BookSide().loading || pages::BookSide().chapters.size() < 2) &&
               bookWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++bookWaited;
        }
        const bool bookReady = !pages::BookSide().loading && pages::BookSide().chapters.size() >= 2;
        // ⚠️ 这一行别漏：漏了的话 manifest 头部会写 book-snapshot: TIMEOUT 而正文写
        //    converged，overall 直接 FAIL —— 自己跟自己打架，比缺判据更难查。
        session.bookSnapshotConverged = bookReady;
        WriteManifest(manifest, std::string("book-snapshot=") + (bookReady ? "converged" : "TIMEOUT") +
                                    " chapters=" + std::to_string(pages::BookSide().chapters.size()) +
                                    " shots=" + std::to_string(pages::BookSide().shots.size()) +
                                    " after " + std::to_string(bookWaited) + " frames");
        session.GrabDriven("side-tree", kWsStoryboard, base);
        // ⚠️ 这里**故意不拍**「小说侧栏滚到底」那张，也没有对应判据：
        //    12 章的 fixture 下小说侧栏树是 20 行 = 560px，可视区 ~630px ⇒ **放得下**
        //    （实测读数 560 / 630，滚动上限 0）。给一张「滚动后」的图会与这张**逐字节相同**
        //    ⇒ `identical-driven-pairs` 假红。
        //    真正必然溢出的是**资产侧栏**那棵树（30 行），判据打在那里 —— 见上面的
        //    `asset-tree-scroll`。想给小说侧栏也加判据，得先把 fixture 铺到 22 章以上。

        // 选中第 2 镜再拍一张：证明检查器属性**跟着选中项变**，不是静态占位。
        pages::SelectBookShot(1);
        session.GrabDriven("inspector-shot", kWsStoryboard, base);
        // 换到第 3 章（库里刻意留空，没取过它的镜）→ 侧栏只剩章节点。
        // ⚠️ 等待条件必须**同时**满足 !loading 且 selectedChapter 真的换过去了。
        //    只等 !loading 是不够的：loading 标志是派发瞬间翻的，而视图那时还没落地，
        //    一个字不改就成立 → 一帧都不等 → 拍出来的还是上一章。
        const auto listStorage = [] {
            const pages::BookState& book = pages::Book();
            const pages::BookSideView& side = pages::BookSide();
            return std::array{reinterpret_cast<std::uintptr_t>(book.chapters.data()),
                              reinterpret_cast<std::uintptr_t>(book.assets.data()),
                              reinterpret_cast<std::uintptr_t>(side.chapters.data()),
                              reinterpret_cast<std::uintptr_t>(side.assets.data())};
        };
        const auto beforeSwitch = listStorage();
        const auto chapterCount = pages::BookSide().chapters.size();
        const auto assetCount = pages::BookSide().assets.size();
        const auto shotCount = pages::BookSide().shots.size();
        const auto listsRetained = [&] {
            return listStorage() == beforeSwitch && pages::BookSide().chapters.size() == chapterCount &&
                   pages::BookSide().assets.size() == assetCount;
        };
        pages::SelectBookChapter(2);
        const bool keptWhileLoading = pages::BookSide().loading && listsRetained() &&
                                      chapterCount >= 3 && assetCount > 0 && shotCount > 0 &&
                                      pages::BookSide().shots.size() == shotCount;
        bookWaited = 0;
        while ((pages::BookSide().loading || pages::BookSide().selectedChapter != 2) &&
               bookWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++bookWaited;
        }
        const bool switched = !pages::BookSide().loading && pages::BookSide().selectedChapter == 2;
        const bool reusedLists = keptWhileLoading && listsRetained();
        const bool sliceReady = pages::BookSide().error.empty() && pages::BookSide().shots.empty();
        WriteManifest(manifest, std::string("chapter-switch=") +
                                    (switched ? "converged" : "TIMEOUT") + " after " +
                                    std::to_string(bookWaited) + " frames shots=" +
                                    std::to_string(pages::BookSide().shots.size()) +
                                    " lists=" + (reusedLists ? "reused" : "changed"));
        session.bookSnapshotConverged =
            session.bookSnapshotConverged && switched && reusedLists && sliceReady;
        session.GrabDriven("side-tree-empty-chapter", kWsStoryboard, base);
        pages::SelectBookChapter(0);

        // ---- 第五段：资产 kind 筛选树 + 检查器「关联」段 + 快速跳转 ----
        //
        // 这三样都是本轮新接的，而且**都是只画不联动就会看不出错**的东西：
        //   · kind 树 —— chip 点了主区网格不动、叶子点了详情不换，都只是"看着没反应"
        //   · 关联段 —— 段头以前是 const 局部数组，箭头点了永远不展开
        //   · 快速跳转 —— 按钮画出来不跳 workspace，截图上完全看不出
        // 所以每张的前置动作都显式写全，等待结论也进 manifest。
        shell.SetWorkspace(kWsAssets);
        bookWaited = 0;
        while ((pages::BookSide().loading || pages::BookSide().assets.empty()) &&
               bookWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++bookWaited;
        }
        const bool assetsReady =
            !pages::BookSide().loading && !pages::BookSide().assets.empty();
        WriteManifest(manifest, std::string("asset-snapshot=") +
                                    (assetsReady ? "converged" : "TIMEOUT") + " assets=" +
                                    std::to_string(pages::BookSide().assets.size()) + " after " +
                                    std::to_string(bookWaited) + " frames");
        session.assetSnapshotConverged = assetsReady;
        session.GrabDriven("assets-kind-tree", kWsAssets, base);

        // ---- 总控「最近产物」那几行的真图缩略图（数据源 `generated_images`）----
        //
        // 判据：在这张卡上同时数两个色 ——
        //   · `kRecentAssetColor`（fixture 的 `assets/gen_recent_ref.png`，
        //     `source_kind='asset'`）：证明**非 shot 来源**的出图产物也在卡上。
        //     那张卡读表时**不限 source_kind**（「最近产的东西」，不是「最近的镜图」），
        //     只铺 shot 的 fixture 验不出「查询退化成只认 shot」。
        //   · `kShotColor2`（`assets/shot/s2.png`，`source_kind='shot'`，在卡的第 3 行）：
        //     证明同一张卡上 shot 来源的那几行也在（两族数据都到得了这一卡，
        //     不是只接通了其中一族）。取第 3 行那条而不是最下面那行 —— 见 fixture 的注释
        //     （窗口底边会把卡的最后一行裁掉一半，判据挂在上面会变成脆判据）。
        //
        // ⚠️ 位置不是随手放的：这一卡的数据来自 `BookSide().recentImages`，而 novel.db
        //    快照是**异步**回投的 —— 前面那两张总控图（`overview-bound` / `overview-scrolled`）
        //    拍在快照落地**之前**，卡上只有空态。所以判据挂在这个「快照已收敛」的位置。
        // ⚠️ 拍之前**收起底栏**（Ctrl+J 那条路径：`Shell::ToggleDock`）。理由不是美观：
        //    1600×960 下工作区只能从底栏上方挤出 ~700px，而右栏这一列在页面里排在
        //    运行状态 / KPI 之下（顶端约在 y=403）——「最近产物」卡的正文落在视口之外，
        //    只把右栏滚到顶只能拍到标题，滚到底又把它整张推出视口（它前后还有三张卡）。
        //    收起底栏 ⇒ 工作区高 +190、右栏视口跟着 +190 ⇒ 这一卡整块进视口。
        //    **不改窗口尺寸**：1600×960 是所有取证图（与设计稿 1:1 判据）共同的基准。
        shell.ToggleDock();
        kit::ScrollRegion::RequestScrollTop("workspace-scroll");
        kit::ScrollRegion::RequestScrollTop("ov-right");
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        const auto recentCells = session.GrabCountTwoColors("overview-recent-products", kWsOverview,
                                                            base, kRecentAssetColor, kShotColor2,
                                                            /*registerDriven=*/false);
        session.recentProductsImage = (recentCells.first > 0 && recentCells.second > 0);
        WriteManifest(manifest, std::string("recent-products=") +
                                    (session.recentProductsImage ? "ok" : "NO-IMAGE") +
                                    " asset-src=" + std::to_string(recentCells.first) +
                                    " shot-src=" + std::to_string(recentCells.second) +
                                    " rows=" +
                                    std::to_string(pages::BookSide().recentImages.size()));
        shell.ToggleDock();
        // ⚠️ 必须把**工作区还原**：`Shoot` 会把工作区设成它拍的那一页，而下面紧接着的
        //    那一段（资产侧栏树可滚）依赖「当前就在资产页」—— 它只注册了一个滚动请求，
        //    而**在别的工作区上那一帧不会有那棵树**，请求永远不会被核销，判据于是报
        //    「资产侧栏树的滚动请求从没被核销（id="ov-right"）」。实测过：这条假红看着
        //    像滚动区坏了，根因是上一条探针把页面留在别处。滚动位置一并还原成顶部。
        kit::ScrollRegion::RequestScrollTop("workspace-scroll");
        kit::ScrollRegion::RequestScrollTop("ov-right");
        shell.SetWorkspace(kWsAssets);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 侧栏树可滚（2026-09-30）----
        //
        // ⚠️ 判据打在**资产侧栏那棵树**上，不是小说侧栏：只有它在当前 fixture 下
        //    必然溢出（13 个 kind 组 + 17 个叶子 = 30 行 ≈ 840px，可视区 ~630px）。
        //    小说侧栏那棵 20 行 = 560px，**放得下**（实测读数 560 / 630）——
        //    拿它当判据会报一个「内容没超出」的假红，而那不是缺陷。
        //    两棵树共用同一套 `ScrollRegion` 写法，验这一棵就覆盖了那个机制。
        //
        // 为什么必须有这条：`Tree` 早先把自己的返回值截断成「可见高度」，
        // 于是套上滚动区也**滚不动** —— 后面的 kind 组与叶子在界面上够不着，
        // 而截图上只有「前面几组」，看不出区别。与 ov-right / workspace-scroll 同一条纪律：
        // 读产品自己的滚动读数，不从像素反推。
        kit::ScrollRegion::RequestScrollBottom("asset-tree-scroll");
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        const kit::ScrollRegion::ScrollApplied treeScroll = kit::ScrollRegion::LastApplied();
        if (treeScroll.id != "asset-tree-scroll") {
            shine::log::Error(
                "review: 资产侧栏树的滚动请求从没被核销（id=\"{}\"）—— 侧栏树没有滚动区"
                "（或那一帧没被构造），kind 一多后面的分组与叶子在界面上够不着",
                treeScroll.id);
            session.result.scrollFailed = true;
        } else if (treeScroll.maxScrollY <= 0.0f) {
            shine::log::Error(
                "review: 资产侧栏树期望滚动上限=0（自报内容高={:.1f} / 可视高={:.1f}）—— "
                "要么 setContentHeight 没拿到**完整**内容高度（`Tree` 的返回值被截断了，"
                "这正是修之前的样子），要么这棵树确实没超出可视区",
                treeScroll.contentHeight, treeScroll.viewHeight);
            session.result.scrollFailed = true;
        } else if (!treeScroll.ok) {
            shine::log::Error("review: 资产侧栏树滚到底失败：scrollY={:.1f} / 期望上限={:.1f}"
                              "（ImGui 侧上限={:.1f}）",
                              treeScroll.scrollY, treeScroll.maxScrollY,
                              treeScroll.imGuiMaxScrollY);
            session.result.scrollFailed = true;
        } else {
            shine::log::Info(
                "review: asset-tree-scroll 可滚 自报内容高={:.1f} 可视高={:.1f} 上限={:.1f}",
                treeScroll.contentHeight, treeScroll.viewHeight, treeScroll.maxScrollY);
        }
        session.GrabDriven("assets-kind-tree-scrolled", kWsAssets, base);
        kit::ScrollRegion::RequestScrollTop("asset-tree-scroll");
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // 点第 2 个实体（库里是「周姨」）。必须走 shell.SelectAsset 而不是分别写两个状态 ——
        // 侧栏高亮与主区详情是同一份选中的两个投影，分开写迟早只改得动一边。
        if (assetsReady) {
            shell.SelectAsset(1);
            session.GrabDriven("assets-leaf-selected", kWsAssets, base);
        }

        // 筛到「物品」：fixture 里 person×2 + item×1，所以这一类只剩 1 张卡。
        // 换了筛选若网格张数不变，就是 chip 没接上主区。
        shell.SetKindFilter("item");
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("assets-kind-filtered", kWsAssets, base);
        // ⚠️ 上一张停在**详情**态，证明不了 chip 也筛了**主区网格** —— 详情是按
        //    未筛选全集取的（设计稿 Assets.jsx:131 的 cur 口径），怎么筛都显示周姨。
        //    要证明共享筛选，必须切到总览再拍：筛「物品」后网格应只剩 1 张卡。
        shell.SetAssetsOverview(true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("assets-grid-filtered", kWsAssets, base);
        shell.SetKindFilter({});
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        // 这一帧起，网格里带设定图的那些格子会**第一次**发出图片请求。抓图时顺便数出
        // 「真图在屏上」的像素数 —— 它既是这一族的证据，也是下面淘汰试验的对照。
        const int sheetPixels = session.GrabCountColor("assets-grid-all", kWsAssets, base,
                                                       kSheetColorA, /*registerDriven=*/true);
        // ---- 真设定图链的判据（第三段异步链，前两段是报告扫描与 novel.db）----
        //
        // `GrabDriven` → `Shoot` 内部**已经把异步上屏等落地了**（那是唯一入口，
        // 理由写在 ReviewPixel.cpp：按调用点补等待时漏一处就是一处随机失败的取证）。
        // 所以这里只**记录结论**，不重复等待。
        //
        // ⚠️ 两半缺一不可：`pending==0` 单独看有二义 —— 「全落地了」与「一张都没被请求过」
        //    同形，而后者正是最该报出来的那一支（调用点没跑到 / 筛选后没有带设定图的实体）。
        //    实测踩过：把等待放在**清 kind 筛选之前**时，那一帧的网格被筛成只剩没有设定图的
        //    那一条（fixture 里的「物品」）⇒ 一张图都不会被请求 ⇒ 循环 0 帧就「通过」，
        //    而抓图仍在和异步上屏抢时间 —— 同一二进制连跑两轮，一轮 33848 像素、另一轮 0。
        const int imgRequested = kit::RequestedImageCount();
        const int imgPending = kit::PendingImageCount();
        session.sheetImagesSettled = (imgRequested > 0 && imgPending == 0);
        WriteManifest(manifest, std::string("sheet-images=") +
                                    (session.sheetImagesSettled ? "settled" : "TIMEOUT") +
                                    " requested=" + std::to_string(imgRequested) +
                                    " pending=" + std::to_string(imgPending) +
                                    " pixels=" + std::to_string(sheetPixels));

        // ---- 「纹理被淘汰后会自己回来」这条约束的判据（`kit/Image.h` 约束 1）----
        //
        // 头文件第 1 条要求**每帧无条件 `Request`**，理由就写在约束里：「被 LRU 淘汰
        // 或设备丢失后会重新上屏。只调一次的话，纹理一旦被淘汰这张图就**永远不再出现**
        // （状态还是 Ready，但 `Texture()` 返回空句柄）」。
        //
        // 这条必须自己有判据 —— 它一旦被写坏，症状是**「图闪过一次就没了」**：
        // 不报错、不崩、静息截图上也只在淘汰之后才看得出来。
        //
        // 这里**直接在 GPU 侧清空纹理缓存**（等价于内存压力下的 LRU 淘汰），
        // 而**不动服务侧的条目状态** —— 那正是「`State == Ready` 而 `Texture()` 空句柄」
        // 这个形态。（服务侧的 `OnDeviceLost` 会把 `Ready` 改回 `NotLoaded`，那条路
        // 靠 `State` 判断也能自愈，所以**不能**拿它来测这条约束。）
        // 下一帧 `RequestImage` 无条件 `Request` ⇒ 从 CPU 缩略图缓存重传 ⇒ 图必须回来。
        //
        // 判据取「淘汰前后**相等**」而不是含糊的「大于 0」：同一轮、同一个页面状态，
        // 差一个像素都说明这条链没有完整恢复。
        gpu::TextureCache().Clear();
        const int recoveredPixels = session.GrabCountColor(
            "assets-grid-after-evict", kWsAssets, base, kSheetColorA, /*registerDriven=*/false);
        session.sheetImagesRecovered = (sheetPixels > 0 && recoveredPixels == sheetPixels);
        WriteManifest(manifest, std::string("sheet-recover=") +
                                    (session.sheetImagesRecovered ? "ok" : "LOST") + " pixels=" +
                                    std::to_string(recoveredPixels) + " before=" +
                                    std::to_string(sheetPixels));
        shell.SetAssetsOverview(false);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 第九段：查看器（工程内文件那一支：派生链 / 详情页 / 导出整版）----
        //
        // 这一族要挡的是**同一类缺陷的第三个实例**：真图是异步上屏的，而查看器用的是
        // **另一个档位**（`kit::RequestImageFull`，2048）—— 它只在查看器开着的那一帧
        // 才被请求。缺了下面这两条判据，「大图档压根没请求过」与「请求了但没落地」
        // 在截图上完全一样（都画成 `Art` 占位），而 overall 照样 PASS。
        //
        // 判据用**像素计数**（截图跨轮不可复现，不能比图）：派生链 front 那一层在
        // fixture 里是纯色 `#22C55E`（`ReviewFixture.h` 的 kChainColorFront），
        // 而它同时出现在两个位点上 —— 页面里的 `.dthumb`（缩略图档）与查看器舞台
        // （大图档）。遮罩把下层压暗，所以那部分像素**不再等于**这个色值，
        // 数到的就只会是查看器里那张。
        //
        // ⚠️ 第 1 页 = 派生链「正脸」层（`AssetViewerRequest` 的页序：0 = 基线整版）。
        //    **落在哪一页由 `OpenMediaViewer` 的第二个实参决定**，不是请求构造器 ——
        //    写错这一处时两种写法都编译得过、截图也都有内容（拍到的只是别的一页），
        //    而这一族的判据是「数某个纯色」，数的正是那一页的颜色。
        pages::OpenMediaViewer(pages::AssetViewerRequest(0), /*initialIndex=*/1);
        const int viewerPixels = session.GrabCountColor("viewer-image", kWsAssets, base,
                                                        kChainColorFront, /*registerDriven=*/true);
        const int viewerRequested = kit::RequestedImageFullCount();

        // 「导出整版」的联系表：另一屏（2 列网格 + 逐层标签），也必须有一张图。
        // 数基线设定图的纯色 `#FF2D55` —— 联系表第一格就是它。它与大图那一屏用在
        // **同一条判据**里：两屏都是「查看器里真图上屏了没有」，分开写两个 bool
        // 只会让 overall 那一行更长，判的却是同一件事。
        pages::OpenMediaViewer(pages::AssetSheetViewerRequest(0));
        const int gridPixels = session.GrabCountColor("viewer-sheet", kWsAssets, base, kSheetColorA,
                                                      /*registerDriven=*/true);
        // ⚠️ 这里用**大图档自己的** pending，不是总的 `PendingImageCount()`：
        //    注册表是进程级的、只增不减，网格那批缩略图在上面的淘汰试验
        //    （`gpu::TextureCache().Clear()`）之后、从总览切回详情之后就不再被绘制，
        //    于是永远停在「Ready 但没有纹理」= pending。拿总的 pending 当判据，
        //    这条在**任何**后续位置都等不到归零 —— 报出来的是「大图链断了」，
        //    而实际上坏的是另一族图（没人再画它们）。实测就是这样红的第一版。
        const int viewerPending = kit::PendingImageFullCount();
        // 四半缺一不可（与 `sheet-images` 同一条纪律）：只看 pending==0 时，
        // 「大图档一张都没请求过」与「全落地了」同形；只看 requested>0 时，
        // 「请求了但还在飞」会被当成落地。再加上两屏各自的**纯色像素 > 0**
        // 才算真的上了屏（大图那一层 + 联系表基线那一格）。
        session.viewerFullSettled =
            (viewerRequested > 0 && viewerPending == 0 && viewerPixels > 0 && gridPixels > 0);
        WriteManifest(manifest, std::string("viewer-full=") +
                                    (session.viewerFullSettled ? "settled" : "TIMEOUT") +
                                    " requested=" + std::to_string(viewerRequested) +
                                    " pending=" + std::to_string(viewerPending) +
                                    " pixels=" + std::to_string(viewerPixels) +
                                    " sheet-pixels=" + std::to_string(gridPixels));
        pages::CloseMediaViewer();
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // 检查器「关联」段：默认收起（设计稿 Shell.jsx:146 的 c:false），必须显式展开。
        // ⚠️ 这正是本轮修掉的死段头 —— 展开态以前是每帧新建的 const 局部数组。
        pages::SelectBookShot(0);
        shell.SetInspectorSection(2, true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        const pages::BookRelationView relation = pages::BookSide().relation;
        WriteManifest(manifest,
                      std::string("relation=") + (relation.foreshadows.empty() ? "no-foreshadow" : "ok") +
                          " tags=" + std::to_string(1 + (relation.foreshadows.empty() ? 0 : 1) +
                                                   (relation.sceneOrd > 0 ? 1 : 0) +
                                                   (relation.shotCode.empty() ? 0 : 1)));
        session.GrabDriven("inspector-relations", kWsStoryboard, base);
        shell.SetInspectorSection(2, false);

        // 小说侧栏的「快速跳转」按钮组：默认全展开，点一下真的会切工作区。
        shell.SetWorkspace(kWsNovel);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("side-jump-buttons", kWsNovel, base);

        // 小说页的另外两个**有内容**的模式：设定（实体网格）与流水线（阶段表）。
        // ⚠️ 这两张以前不存在。`mode_` 只能靠点标签切换，取证到不了，于是这两个模式
        //    里的东西从来没被看过一眼 ——「设定」那张网格的卡片是**反向矩形**（整张
        //    不画也不可点）就是这么活下来的：不是没人修，是没人拍到过。
        //    「覆盖 7 个工作区」不等于「覆盖每个工作区的每个视图」，多态视图要逐个点名。
        shell.SetNovelMode(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("novel-mode-world", kWsNovel, base);
        shell.SetNovelMode(3);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("novel-mode-pipeline", kWsNovel, base);
        shell.SetNovelMode(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 第七段：把「取证进不去」的视图逐个点名补齐 ----
        //
        // 本轮的第一条教训：**覆盖要按「视图」点名，不是按「页面」点名**。
        // 「7 个工作区全拍了」听起来够了，但一个工作区里往往还有 4 个 dock 页签、
        // 3 个检查器段、2 个右侧面板页签 —— 它们只能靠点 UI 切换，没有 public 入口，
        // 取证就永远进不去，里面有什么缺陷也就没人看得见。已经这样漏掉过一个
        // 整张不画的卡片网格。所以这一段专门补：能进但没拍的（dock 0/1/2、检查器
        // 「预览」段），以及刚加了入口的（出图/出片的第二个面板页签、画布折叠态）。
        //
        // 每张都必须显式把状态设成它承诺的样子，跑完再复位 —— 否则就是「图名和内容
        // 对不上，而 manifest 照样记 saved」。工作区常量在 ReviewSession.h 里统一声明过。

        // 底栏四个页签：以前只拍了 3（校验报告），0/1/2 三个页签**从来没被拍过**。
        // 这三个都是大面积视图（任务队列 / 日志 / 产物），最该有证据图。
        //
        // ⚠️ 产物页（2）走 worker 扫 `<root>/output`，**必须等它回投**再拍。
        //    不等的话拍到的是「目录是空的」—— 而那个空态本身是**正确**的输出，
        //    manifest 同样记 saved，图名与内容对不上却抓不到。这就是「等待结果要写进
        //    manifest」那条纪律的又一个实例：结论进 manifest，也进 overall 判据。
        shell.SetDockTab(2);
        int artifactWaited = 0;
        while (shell.ArtifactRowCount() == 0 && artifactWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++artifactWaited;
        }
        const int artifactRows = shell.ArtifactRowCount();
        // ⚠️ 这里是**赋值**给外层的那个变量。早先写成 `const bool artifactsConverged = …`
        // 在块内又声明了一个同名局部量，把外层的遮住 —— manifest 那行写的是
        // `converged`（局部值），而 `overall` 行读的是外层的 false，于是同一件事
        // 在两处得到相反结论。判据变量一律**赋值**，不在块内重新声明。
        session.artifactsConverged = artifactRows > 0;
        WriteManifest(manifest, std::string("artifact-snapshot=") +
                                    (session.artifactsConverged ? "converged" : "TIMEOUT") +
                                    " rows=" + std::to_string(artifactRows) + " after " +
                                    std::to_string(artifactWaited) + " frames");
        session.GrabDriven("dock-artifacts", kWsOverview, base);
        for (int tab : {0, 1}) {
            shell.SetDockTab(tab);
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            session.GrabDriven(std::string("dock-") + DockTabSlug(tab), kWsOverview, base);
        }
        shell.SetDockTab(3);

        // 检查器第 1 段「预览」：以前只拍过段 0（属性）与段 2（关联）。
        shell.SetWorkspace(kWsNovel);
        shell.SetInspectorSection(0, false);
        shell.SetInspectorSection(1, true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("inspector-preview", kWsNovel, base);
        shell.SetInspectorSection(1, false);
        shell.SetInspectorSection(0, true);

        // 出图页右侧面板的第二个页签 + 画布折叠态。
        shell.SetImageFlowPanel(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("imageflow-panel-batch", kWsImageFlow, base);
        // 第三个页签「图评审」：以前画 5 行假复选框（返回值丢弃 ⇒ 点不动、勾选态写死
        // i < 3、五行标签全是同一个字符串「构图稳定」）。静息态截图看着还挺像个评审页，
        // 所以必须有证据图盯着这一页。
        shell.SetImageFlowPanel(2);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("imageflow-panel-review", kWsImageFlow, base);
        shell.SetImageFlowFolded(true);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("imageflow-canvas-folded", kWsImageFlow, base);
        shell.SetImageFlowFolded(false);
        // 第四个页签「结果」：**当前选中镜的成图**（这一格早先画的是 `Art` 占位画）。
        // 前置动作写全：明写选第 1 镜（`#FB7185` 那张）—— 不靠上一条探针留下的选中态，
        // 那正是「拍到的其实是别的状态」那一类假绿。
        shell.SetImageFlowPanel(3);
        pages::SelectBookShot(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        const int resultPixels = session.GrabCountColor("imageflow-result-shot", kWsImageFlow, base,
                                                        kShotColor1, /*registerDriven=*/true);
        session.imageFlowResultImage = (resultPixels > 0);
        WriteManifest(manifest, std::string("imageflow-result=") +
                                    (session.imageFlowResultImage ? "ok" : "NO-IMAGE") +
                                    " pixels=" + std::to_string(resultPixels));
        // ---- 同一格上那两行生成参数（`Prompt` / `生成参数`）的**投影** ----
        //
        // 判的是「页面手里那份数据」对不对，**不是像素**：这两行在同一帧里从
        // `BookShot` 读出来、当帧画完，没有图片那种「worker 还没解码完」的异步窗口
        // ⇒ 没有「链断了」与「拍早了」同形的那种二义，数色值在这里没有对象可数。
        // 它挡的是：① `BookQuery.cpp` 没把那几列 SELECT 出来（行永远空）；
        // ② 列序读串（`steps` 当 `width`）；③ 行被「全空」判掉 ⇒ 界面上那两行
        // 凭空少掉，而其余判据全绿。期望值来自 `ReviewFixture.h` 的**独立契约**，
        // 不是「再查一遍库」——那等于拿同一条查询对照自己。
        {
            const pages::BookState& book = pages::Book();
            const pages::BookShot* shot = book.shot();
            const std::string prompt = shot != nullptr ? shot->imagePrompt : std::string();
            const std::string model = shot != nullptr ? shot->imageModel : std::string();
            const int steps = shot != nullptr ? shot->imageSteps : 0;
            const int width = shot != nullptr ? shot->imageWidth : 0;
            const int height = shot != nullptr ? shot->imageHeight : 0;
            session.imageFlowParams = prompt == kShot1Prompt && model == kShot1Model &&
                                      steps == kShot1Steps && width == kShot1Width &&
                                      height == kShot1Height;
            WriteManifest(manifest,
                          std::string("imageflow-params=") +
                              (session.imageFlowParams ? "ok" : "MISMATCH") + " prompt=" +
                              (shot != nullptr && !prompt.empty() ? prompt : "<empty>") +
                              " model=" + (model.empty() ? "<empty>" : model) + " steps=" +
                              std::to_string(steps) + " size=" + std::to_string(width) + "x" +
                              std::to_string(height) + "  期望=" + kShot1Prompt + " / " +
                              kShot1Model + " / " + std::to_string(kShot1Steps) + " 步 / " +
                              std::to_string(kShot1Width) + "x" + std::to_string(kShot1Height));
        }
        shell.SetImageFlowPanel(0);

        // 出片页右侧面板的第二个页签「视频任务」。以前是 4 条写死的 20/40/60/80 进度条、
        // running 恒为第一条 —— 界面上永远显示「第一条在跑、其余到 80%」。
        shell.SetVideoFlowPanel(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("videoflow-panel-tasks", kWsVideoFlow, base);
        shell.SetVideoFlowPanel(0);

        // ---- 成片条那几格的真图（逐镜成图，与分镜 `.tthumb` 同一条链）----
        //
        // 判据用**数纯色**：fixture 里第 1 镜的成片图是 `#FB7185`、第 2 镜是 `#34D399`
        // （`ReviewFixture.cpp` 的 `assets/shot/s1.png` / `s2.png`，两者都刻意选在五套
        // 主题调色板之外 ⇒ 数到它就只可能来自那两张图）。一个色 > 0 只能说「有一张真图
        // 上了屏」，两张都数才排得掉「只接了第一格 / 只画了一张」这种半接线。
        //
        // ⚠️ 抓的是**第 3 个页签（成片）**，不是上面那张「视频任务」：成片条画在面板
        //    之外（每帧都画），但抓图前必须先让这一帧与上一张**不是同一个状态**，
        //    否则两张图逐字节相同 ⇒ `identical-driven-pairs` 假红。
        // ⚠️ 数不到时**先看 fixture 的镜图有没有铺满**：`source_kind='shot'` 那三行
        //    只挂在第 1/2 镜上（第 4 镜刻意登记了不存在的文件、第 3/5 镜没登记）
        //    ⇒ 这一条同时覆盖三条分支里的①。②③ 在同一张图里（「示意」与「待出片」）。
        shell.SetVideoFlowPanel(2);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        const auto shotCells = session.GrabCountTwoColors("videoflow-film-strip", kWsVideoFlow,
                                                          base, kShotColor1, kShotColor2,
                                                          /*registerDriven=*/true);
        session.filmStripImages = (shotCells.first > 0 && shotCells.second > 0);
        WriteManifest(manifest, std::string("film-strip=") +
                                    (session.filmStripImages ? "ok" : "NO-IMAGE") +
                                    " shot1=" + std::to_string(shotCells.first) +
                                    " shot2=" + std::to_string(shotCells.second));
        shell.SetVideoFlowPanel(0);

        // 分镜页选中的镜。默认 0 = 第一张，**不显式换一张就证明不了选中态会跟着动**
        // —— 与「页面恰好停在你想要的状态」是同一类陷阱。
        //
        // ⚠️ 两张**必须成对**：只有「换到第 2 张」那一张时，它与任何别的图都不重样，
        //    判据「受控图不许重样」也就抓不到「这个入口是死的」。配一张默认态，
        //    入口一旦接错字段（真发生过：写进了零读点的 `selectedShot_`）两张就逐字节
        //    相同，判据当场变红。**单独一张「证明性」的截图，证明不了任何东西。**
        shell.SelectStoryboardShot(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("storyboard-shot-1", kWsStoryboard, base);
        shell.SelectStoryboardShot(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("storyboard-shot-2", kWsStoryboard, base);
        shell.SelectStoryboardShot(0);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });

        // ---- 第六段：悬停态 + toast ----
        //
        // 前 52 张全是**静息态**。悬停是整条链路最容易断的状态（后端接线 / HitTest
        // 占位 / hover 样式），而它在静息态截图上完全看不出来，所以单独一族判据，
        // 见 ReviewHoverProbe.cpp；跑完把 `hover-probes=` 那一行写进 manifest。
        //
        // ⚠️ SHINE_SCAN 的热区扫描**必须**排在探针之后：探针之前扫到的是「fixture
        //    还没就绪」的那一帧，据此去改探针坐标会一路改错（原因见
        //    ReviewHotspot.cpp）。
        RunHoverProbes(session);
        RunHotspotScanEnv(host, shell, base);

        // ---- 第八段：动作判据（快捷键按了到底有没有发生）----
        //
        // 注入按键 → 比对该动作真正会改的那个状态前后变没变，跑完按语义复位。
        // 一族的理由与四类状态读数见 ReviewShortcutProbe.cpp。
        RunShortcutProbes(session);

        // ---- 浮层按钮点击探针 / 项目中心 / 命令面板 ----
        //
        // 这几族读的是产品自己的开态，效果不在像素上，所以必须与 hover / shortcuts
        // 同级进 overall：漏进去就只剩一行日志，overall 照样 PASS —— 那是假绿。
        RunOverlayProbes(session);

        // toast：设计稿到处在用的 notify(...)。它只活 3.2s，所以必须**同一轮里**触发
        // 紧跟着抓 —— 跨轮再拍早就过期了，拍到的会是「没有 toast」，而图名还叫 toast。
        shell.Notify("分镜 · 上下文：第 1 章「雨夜里的第七封来信」", shine::theme::Tone::Ok);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("toast-ok", kWsOverview, base);
        shell.Notify("ComfyUI 未连接 · 取证期间不连真实服务", shine::theme::Tone::Warn);
        host.PumpFrames(2, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("toast-warn", kWsOverview, base);

        shell.SetProjectRoot(savedRoot, savedName);
        shell.SetDockTab(savedTab);
    }
}

} // namespace shine::imguiverify::detail
