#include "ui/imgui/verify/ReviewScene.h"

#include "core/Log.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/verify/Capture.h"
#include "ui/imgui/verify/ReviewFixture.h"
#include "ui/imgui/verify/ReviewHotspot.h"
#include "ui/imgui/verify/ReviewHoverProbe.h"
#include "ui/imgui/verify/ReviewOverlayProbe.h"
#include "ui/imgui/verify/ReviewShortcutProbe.h"

#include <chrono>
#include <string>
#include <thread>

namespace shine::imguiverify::detail {

// ---- 第一段 + 第二段：静息态覆盖扫描 ----
void RunCoverageSweep(Session& session) {
    imguiapp::Host& host = session.host;
    pages::Shell& shell = session.shell;
    const shine::theme::ThemeId base = session.base;

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

        // 选中第 2 镜再拍一张：证明检查器属性**跟着选中项变**，不是静态占位。
        pages::SelectBookShot(1);
        session.GrabDriven("inspector-shot", kWsStoryboard, base);
        // 换到第 3 章（库里刻意留空，没取过它的镜）→ 侧栏只剩章节点。
        // ⚠️ 等待条件必须**同时**满足 !loading 且 selectedChapter 真的换过去了。
        //    只等 !loading 是不够的：loading 标志是派发瞬间翻的，而视图那时还没落地，
        //    一个字不改就成立 → 一帧都不等 → 拍出来的还是上一章。
        pages::SelectBookChapter(2);
        bookWaited = 0;
        while ((pages::BookSide().loading || pages::BookSide().selectedChapter != 2) &&
               bookWaited < kReportWaitFrameCap) {
            host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ++bookWaited;
        }
        const bool switched = !pages::BookSide().loading && pages::BookSide().selectedChapter == 2;
        WriteManifest(manifest, std::string("chapter-switch=") +
                                    (switched ? "converged" : "TIMEOUT") + " after " +
                                    std::to_string(bookWaited) + " frames shots=" +
                                    std::to_string(pages::BookSide().shots.size()));
        session.bookSnapshotConverged = session.bookSnapshotConverged && switched;
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
        session.GrabDriven("assets-grid-all", kWsAssets, base);
        shell.SetAssetsOverview(false);
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
        shell.SetImageFlowPanel(0);

        // 出片页右侧面板的第二个页签「视频任务」。以前是 4 条写死的 20/40/60/80 进度条、
        // running 恒为第一条 —— 界面上永远显示「第一条在跑、其余到 80%」。
        shell.SetVideoFlowPanel(1);
        host.PumpFrames(1, [&shell](float dt) { shell.DrawFrame(dt); });
        session.GrabDriven("videoflow-panel-tasks", kWsVideoFlow, base);
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
