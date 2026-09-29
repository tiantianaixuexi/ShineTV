#include "ui/verify/review/P08Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/videoflow/ChainView.h"
#include "ui/pages/videoflow/FinalCutView.h"
#include "ui/pages/videoflow/VideoTaskView.h"
#include "flow/VideoChain.h"
#include "ui/pages/videoflow/VideoFlowWorkspace.h"
#include "core/Async.h"
#include "util/Encoding.h"
#include "util/File.h"
#include <QTabWidget>

#include <QApplication>
#include <QPixmap>
#include <QTimer>

#include <cstdio>
#include <filesystem>
#include <vector>
#include <string>
#include <system_error>
#include <utility>

namespace shine::app {
namespace {
namespace fs = std::filesystem;
struct State {
    fs::path dir;
    // 与本轮 6 个 review::Grab 调用点**一一对应**（按拍摄顺序）：
    // Canvas 空态 → Canvas 有链 → Chain 连通 → Chain 断开 → 任务运行中 → 成片页。
    // 收尾按这份清单逐张查存在 + 字节数下限 + 逐字节重复：缺一张、或有一张拍成了
    // 另一张的像素，都是 overall=FAIL。清单必须跟着 grab 改，否则就是「悄悄少一张」。
    std::vector<std::string> expected{"video-flow-empty", "video-flow-normal", "chain-connected",
                                      "chain-broken", "video-tasks-running", "final-videos"};
    std::vector<std::string> manifest;
    // 附加判据：既进报告也进退出码（FinishOptions::extra）。
    // 面板内容栈查不到时那两次 setCurrentIndex 是空动作，后两张会拍成上一个页签
    // 却照样记 saved —— 这里让它显式判失败。
    std::vector<std::pair<std::string, bool>> extra;
};

void Finish(State* state) {
    std::vector<std::string> files;
    files.reserve(state->expected.size());
    for (const std::string& stem : state->expected) {
        files.push_back(stem + ".png");
    }
    const review::FinishOptions opt{
        .dir = state->dir,
        .header = "P08-S8 shots",
        .expected = files,
        .manifest = &state->manifest,
        .min_bytes = 100,
        // 本轮 6 张承诺的状态两两不同（4 个不同控件，其中 Canvas / Chain 各拍两张但
        // 状态不同），所以逐字节相同就等于有一张没拍到它该拍的状态，判 FAIL。
        .fail_on_duplicate = true,
        .extra = state->extra,
    };
    const review::FinishResult result = review::EvaluateShots(opt);
    std::printf("[p08-review]\n%s", result.report.c_str());
    review::WriteAndExit(opt);
}

void Run(State* state) {
    auto* workspace = new VideoFlowWorkspace();
    workspace->resize(1280, 820);
    workspace->show();
    review::Grab(workspace->Canvas(), state->dir, "video-flow-empty", state->manifest);
    workspace->LoadMock();
    review::Pump();
    review::Grab(workspace->Canvas(), state->dir, "video-flow-normal", state->manifest);
    review::Grab(workspace->Chain(), state->dir, "chain-connected", state->manifest);
    auto broken = shine::flow::BuildVideoChain({{1, "a", "b", ""}, {2, "x", "c", ""}});
    workspace->Chain()->SetChain(std::move(broken));
    review::Grab(workspace->Chain(), state->dir, "chain-broken", state->manifest);
    workspace->LoadMock();
    // 面板内容栈是页签（VideoFlowWorkspace 刻意保留 QTabWidget 就是给这里反查的，
    // 见该文件 BuildUi 里的取舍注释）。查不到就记 FAIL，而不是拍一张错页签还标 saved。
    bool tab_ok = true;
    if (auto* tabs = workspace->findChild<QTabWidget*>()) {
        tabs->setCurrentIndex(1);
    } else {
        tab_ok = false;
    }
    review::Pump();
    workspace->Tasks()->ShowRunning();
    review::Grab(workspace->Tasks(), state->dir, "video-tasks-running", state->manifest);
    if (auto* tabs = workspace->findChild<QTabWidget*>()) {
        tabs->setCurrentIndex(2);
    } else {
        tab_ok = false;
    }
    review::Pump();
    review::Grab(workspace->Final(), state->dir, "final-videos", state->manifest);
    state->extra.push_back({"videoflow-tab-switch", tab_ok});
    Finish(state);
}
} // namespace
void SaveP08Review(const std::filesystem::path& dir) {
    auto* state = new State;
    state->dir = dir;
    std::error_code ec;
shine::util::EnsureDir(dir);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
