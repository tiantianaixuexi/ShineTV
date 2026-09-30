// shine::pages —— 外壳的**底栏**：页签条 + 任务队列 + 日志 + 产物
//
// 四块都是「底部那块里的可滚动列表」，共用同一套 ScrollRegion 视口口径与
// `pages::SetPageContentHeight` 的高度口径。产物的扫描/读取在
// RequestArtifactScan（worker 侧），绘制在这里 —— 所以 P4.6c 从文件中部搬到了
// 末尾附近。
#include "ui/imgui/pages/Shell.h"

#include "ui/imgui/pages/Shell_Layout.h"

#include "comfy/ComfySession.h"
#include "core/Async.h"
#include "core/Log.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "util/Encoding.h"
#include "util/Shell.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::pages {

using namespace shine::kit;

// ---------------------------------------------------------------- P4.6 底栏
void Shell::DrawDock(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x, area.min.y + 0.5f), ImVec2(area.max.x, area.min.y + 0.5f),
                  ColorLineSubtle(), 1.0f);

    const char* tabs[] = {"任务队列", "日志", "产物", "校验报告"};
    const std::vector<SegmentOption> options{{"0", tabs[0]}, {"1", tabs[1]}, {"2", tabs[2]},
                                             {"3", tabs[3]}};
    const std::string value = std::to_string(layout_.dockTab);
    // ⚠️ 宽高一律走 RectAt()。这里原来写 `Rect{x, y, 400.0f, 32.0f}`，而 kit::Rect 的
    //    四参构造是 (minX, minY, maxX, maxY) —— bounds.height() 成了 32 - dockTop(745)
    //    = **-713**。Tabs 内部按 bounds.height() 排每个页签，于是文字位置碰巧还对，
    //    但选中页签那条 2px accent 下划线被画到 y≈31（屏幕顶栏里），点击区域也是负高的。
    //    不报编译错、不崩，只是下划线跑错地方 —— 见 tools\find-rect-wh-misuse.ps1。
    const std::string_view picked = Tabs(draw, RectAt(area.min.x + 12.0f, area.min.y, 400.0f, 32.0f),
                                         options, value, "dock-tabs");
    if (!picked.empty()) {
        layout_.dockTab = std::atoi(std::string(picked).c_str());
    }
    if (IconButton(draw, RectAt(area.max.x - 34.0f, area.min.y + 3.0f, 22.0f, 22.0f), "x", false,
                   false, "dock-close")) {
        ToggleDock();
    }

    const Rect body{area.min.x + 12.0f, area.min.y + 34.0f, area.max.x - 12.0f, area.max.y - 10.0f};
    if (layout_.dockTab == 0) {
        DrawDockQueue(body, draw);
    } else if (layout_.dockTab == 1) {
        DrawDockLogs(body, draw);
    } else if (layout_.dockTab == 2) {
        DrawDockArtifacts(body, draw);
    } else {
        DrawDockReports(body, draw);
    }
}

// ---------------------------------------------------------------- P4.6a 任务队列
// 队列行来自 comfy::QueueModel 的真实快照（Shell.jsx:222-232 的行结构）。
// 早先这里是一条写死的 Progress(42%, run=true) —— 界面上永远在"跑"，与真实运行态无关。
void Shell::DrawDockQueue(Rect body, ImDrawList* draw) {
    const std::vector<comfy::QueueModel::Row> rows =
        comfy::ComfySession::Instance().Queue().Snapshot();
    // ⚠️ 画**全部**条目，超出由 ScrollRegion 滚 —— 原来这里是
    //    `if (y + 30.0f > body.max.y) break;`，队列有 50 个任务时只画前 6 个，
    //    剩下的**无声消失**，界面上看不出还有 44 个。列表被截断且不提示 = 界面在骗人。
    //    `tools\find-silent-truncation.ps1` 扫的就是这一族写法（全树 10 处）。
    //
    // 队列顺序是 Comfy 那边定的（运行中在前），所以从**顶部**开始画、不做尾部窗口。
    constexpr float kQueueRowH = 32.0f;
    constexpr float kQueueFootH = 18.0f;
    kit::ScrollRegion list("dock-queue-list", body);
    if (list) {
        // 内容必须画在 child **自己的** draw list 上：BeginChild 的裁剪矩形只写进
        // 它自己那条 list（见 Scroll.h 的说明）。
        ImDrawList* ldraw = list.drawList();
        const Rect inner = list.content();
        float y = inner.min.y;
        for (const comfy::QueueModel::Row& row : rows) {
            const bool running = row.state == comfy::TaskState::Running;
            const bool failed = row.state == comfy::TaskState::Failed;
            // 队列行走 kit::ListRow：底色、命中、文字 Y 全在里面。原来这一段
            // 自己算 `y + 3.0f` / `y + 4.0f`，与同一行里居中的 Tag 并排看时
            // 字比 Tag 高出 6px —— 「很多按钮的字不在中间」最扎眼的一处。
            //
            // ⚠️ 队列行**不可点**（这里只要 hover 底），所以 id 传空串：
            // ListRow 会跳过命中测试。传了 id 就等于把一次永远没人读的
            // clicked 提交给 ImGui 的 ID 栈，白占一个 item。
            //
            // suppressed 走 chromeInteractive_ 闸门：浮层开着时底下的 dock
            // 行不该出 hover 高亮（原来靠 ChromeHit 返回空 Hit 达成这件事，
            // 换成 kit 组件后闸门必须显式传进来，否则浮层会「漏高亮」）。
            kit::ListRowSpec spec;
            spec.id = {}; // 不可点
            spec.suppressed = !chromeInteractive_;
            spec.title = row.label.empty() ? row.promptId : row.label;
            spec.titleSize = 12.0f;
            spec.paddingX = 16.0f; // 给左边的 StatusDot 让位
            spec.chevron = "none";
            const Rect line{inner.min.x, y, inner.max.x, y + 30.0f};
            kit::ListRow(ldraw, line, spec);
            // 状态点画在 ListRow 之外：它是 7px 圆点居中，不是 16px 图标。
            StatusDot(ldraw, ImVec2(line.min.x + 8.0f, line.center().y),
                      failed ? theme::Tone::Danger : (running ? theme::Tone::Busy : theme::Tone::Idle),
                      running);
            // 细进度 + 百分比：走 QueueModel 的真实 progress，不是写死的 42。
            const float pct = row.progress * 100.0f;
            Progress(ldraw, Rect{line.min.x + 248.0f, y + 12.0f, line.min.x + 408.0f, y + 16.0f}, pct,
                     running, true);
            if (row.progressMax > 0) {
                const std::string p = std::to_string(row.progressValue) + "/" +
                                      std::to_string(row.progressMax) + " · " +
                                      std::to_string(static_cast<int>(pct)) + "%";
                DrawTextClipped(ldraw, MonoAt(10.5f), 10.5f,
                                ImVec2(line.min.x + 416.0f,
                                       kit::CenterTextY(MonoAt(10.5f), 10.5f, line.center().y)),
                                180.0f, ColorTextMuted(), p);
            }
            const Rect tagBox = RectAt(line.max.x - TagWidth("", true, false) - 6.0f, y + 6.0f,
                                       TagWidth("", true, false), TagHeight(true));
            Tag(ldraw, tagBox, failed ? "失败" : (running ? "运行中" : "排队"),
                failed ? theme::Tone::Danger : (running ? theme::Tone::Busy : theme::Tone::Idle), true);
            y += kQueueRowH;
        }
        ImFont* f = FontAt(10.5f);
        const std::string foot =
            rows.empty()
                ? "队列为空 · Comfy 未提交任务（或未连接）"
                : "共 " + std::to_string(rows.size()) + " 个任务 · 队列由出图 / 出片 / 小说生成共享";
        DrawTextClipped(ldraw, f, 10.5f, ImVec2(inner.min.x, y + 2.0f), inner.width(),
                        ColorTextMuted(), foot);
        list.setContentHeight(y + kQueueFootH - inner.min.y);
    }
}

// ---------------------------------------------------------------- P4.6b 日志
void Shell::DrawDockLogs(Rect body, ImDrawList* draw) {
    // 日志来自运行期真实事件：PushLog 写入 / 流水线收尾 / 产物打开失败等。
    // 早先这里是四条写死的 "T3 生成图 · …" —— 跟程序实际发生的事毫无关系。
    if (logLines_.empty()) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 420.0f, body.min.y + 90.0f},
              "terminal", "暂无日志", "启动流水线或打开产物后，这里会实时滚动");
        return;
    }
    // 最新的在下面（webui 勾到底），行高 18，按行数裁。
    //
    // ⚠️ 这里**保留**尾部窗口（不改成滚动）：日志是「跟随最新」的流，每来一行就
    //    把用户拽到底部会和「往上翻看历史」打架（真做跟随还要判断用户是否已在底部，
    //    那是另一件事）。但**必须写明裁了多少** —— 原来只有一句注释，界面上看不出
    //    更早的行存在过，那和队列那边一样属于「静默截断」。
    // scan:allow-silent-truncation 日志流按设计只保留最近 N 行，界面上已写明总行数
    constexpr float kLogRowH = 18.0f;
    const int maxRows = std::max(1, static_cast<int>((body.height() - 22.0f) / kLogRowH));
    const int total = static_cast<int>(logLines_.size());
    const int first = std::max(0, total - maxRows);
    float y = body.min.y;
    for (int i = first; i < total; ++i) {
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(body.min.x, y), ColorTextSecondary(),
                      logLines_[static_cast<std::size_t>(i)].c_str(), nullptr);
        y += kLogRowH;
    }
    if (runActive_) {
        // 运行中在末尾留一个闪烁光标块（Shell.jsx:236 的 log-caret）。
        const float blink = Pulse(1.0f) > 0.5f ? 1.0f : 0.0f;
        if (blink > 0.0f) {
            DrawRoundRect(draw, ImVec2(body.min.x, y + 3.0f), ImVec2(body.min.x + 7.0f, y + 15.0f),
                          1.0f, ColorAccent());
        }
    }
    if (total > maxRows) {
        DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(body.min.x, body.max.y - 14.0f),
                        body.width(), ColorTextMuted(),
                        "共 " + std::to_string(total) + " 行 · 这里只显示最近 " +
                            std::to_string(maxRows) + " 行");
    }
}


// ---------------------------------------------------------------- P4.6c 产物
// 真实扫 <projectRoot>/output。IO 走 worker：UI 线程只读缓存 artifacts_，
// 换工程 / 点刷新 / 超过 3 秒才重扫，不在每帧碰 std::filesystem。
void Shell::RequestArtifactScan() {
    if (artifactScanning_ || artifactScanRoot_ == layout_.projectRoot) {
        return;
    }
    const std::filesystem::path root = layout_.projectRoot / "output";
    artifactScanning_ = true;
    artifactScanRoot_ = layout_.projectRoot;
    async::RunOnWorker([this, root] {
        std::vector<ArtifactRow> found;
        std::error_code ec;
        if (std::filesystem::is_directory(root, ec)) {
            // 只收文件，深度 2：output/ 下面通常还有一层按阶段的子目录。
            for (auto it = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) {
                    break;
                }
                if (!it->is_regular_file(ec)) {
                    continue;
                }
                ArtifactRow row;
                row.path = it->path();
                row.name = util::PathToUtf8(it->path().filename());
                row.bytes = it->file_size(ec);
                row.kind = util::PathToUtf8(it->path().extension());
                if (!row.kind.empty() && row.kind.front() == '.') {
                    row.kind.erase(row.kind.begin());
                }
                found.push_back(std::move(row));
                if (found.size() >= 200) {
                    break;  // 一次最多 200 条，够看也不至于把内存吃穿
                }
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const ArtifactRow& a, const ArtifactRow& b) {
            return a.name < b.name;
        });
        async::PostToUi([this, root, rows = std::move(found)] {
            artifactScanning_ = false;
            artifactRoot_ = root;
            artifacts_ = std::move(rows);
            // 排下一次重扫。与报告页同一处修正：这里原来也是 `= 0.0f`，
            // 于是 aged 恒假、除了换工程再没有触发点。
            artifactRefreshAt_ = ImGui::GetTime() + 2.0f;
        });
    });
}

void Shell::DrawDockArtifacts(Rect body, ImDrawList* draw) {
    // 换工程 / 2 秒后重扫。空工程根不扫，直接给诚实空态。
    const bool stale = artifactScanRoot_ != layout_.projectRoot;
    const bool aged = artifactRefreshAt_ > 0.0f && ImGui::GetTime() >= artifactRefreshAt_;
    if (stale || aged) {
        artifactScanning_ = false;  // 上一次的结果作废
        RequestArtifactScan();
    }

    if (layout_.projectRoot.empty()) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 460.0f, body.min.y + 90.0f},
              "folder", "未打开工程", "产物浏览器指向项目的 output/ 目录");
        return;
    }
    if (artifacts_.empty() && !artifactScanning_) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 460.0f, body.min.y + 90.0f},
              "folder", "output/ 目录是空的", "跑完出图或出片后，产物会出现在这里");
        return;
    }

    // ⚠️ 这里原来有一处**静默截断**，而且是扫描器长期漏掉的形态：
    //        if (y + 24.0f > body.max.y - 16.0f) { break; }
    //    `tools/find-silent-truncation.ps1` 的正则当年写成 `\.max\.y\s*\)`，只吃
    //    「给下边界留页脚」之外的朴素写法，于是这个 `- 16.0f` 让它在**有真缺陷的树上
    //    报 0** —— 扫描器报 0 和「没有截断」长得一模一样。产物一多，第 N+1 个之后的
    //    文件**无声消失**，界面上看不出 output/ 里还有东西。
    //    改成画**全部**条目，列表自己滚；页脚那一句留在滚动区**外面**。
    constexpr float kArtRowH = 26.0f;
    constexpr float kArtFootH = 18.0f;
    const Rect listArea{body.min.x, body.min.y, body.max.x, body.max.y - kArtFootH};
    kit::ScrollRegion list("dock-artifact-list", listArea);
    if (list) {
        // 内容必须画在 child 自己的 draw list 上，否则裁剪无效（见 Scroll.h）。
        ImDrawList* ldraw = list.drawList();
        const Rect inner = list.content();
        float y = inner.min.y;
        for (const ArtifactRow& row : artifacts_) {
            // 产物行走 kit::ListRow：文件名 / 种类 / chevron 三段全在里面，
            // 原来自己算 `y + 5.0f`（行高 24）时文件名偏上 1.25px、种类偏上
            // 1.75px —— 同一行里两段字各偏各的，种类比文件名更歪。
            const Rect line{inner.min.x, y, inner.min.x + 520.0f, y + 24.0f};
            kit::ListRowSpec spec;
            spec.id = chromeInteractive_ ? "dock-art-" + row.name : std::string_view{};
            spec.icon = "folder";
            spec.iconSize = 13.0f;
            spec.iconGap = 6.0f;
            spec.iconColor = ColorAccentHover();
            spec.title = row.name;
            spec.titleSize = 11.5f;
            spec.titleMono = true;
            spec.titleColor = ColorText(); // 可点开的实体名：primary，不是 secondary
            spec.trailing = row.kind;
            spec.trailingSize = 10.5f;
            spec.paddingX = 3.0f;
            // 命中走 ChromeHit 的闸门语义：浮层开着时 id 传空 ⇒ 不注册 item。
            spec.suppressed = !chromeInteractive_;
            const kit::Hit hit = kit::ListRow(ldraw, line, spec);
            if (hit.clicked) {
                const std::string err = util::ShellOpen(row.path);
                PushLog(err.empty() ? "info" : "err",
                        std::string("打开产物 ") + row.name + (err.empty() ? "" : " 失败：" + err));
            }
            y += kArtRowH;
        }
        list.setContentHeight(y - inner.min.y);
    }
    const std::string foot =
        "共 " + std::to_string(artifacts_.size()) + " 个产物 · 指向 " +
        util::PathToUtf8(artifactRoot_) + " · 点击条目用系统默认程序打开";
    draw->AddText(FontAt(10.5f), 10.5f, ImVec2(body.min.x, body.max.y - 14.0f), ColorTextMuted(),
                  foot.data(), foot.data() + foot.size());
}


} // namespace shine::pages
