#include "ui/verify/review/P09Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/pipeline/GanttView.h"
#include "ui/pages/pipeline/LedgerView.h"
#include "ui/pages/pipeline/PipelineWorkspace.h"
#include "ui/pages/pipeline/StopReportView.h"
#include "core/Async.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QPixmap>
#include <QTimer>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::app {
namespace {
namespace fs = std::filesystem;
struct State {
    fs::path dir;
    // 与本轮 5 个 review::Grab 调用点**一一对应**（按拍摄顺序）：
    // 空态总控台 → 有数据总控台 → 甘特 → 账本 → 停止报告。
    // 收尾按这份清单逐张查存在 + 字节数下限 + 逐字节重复，缺一张即 overall=FAIL。
    std::vector<std::string> expected{"pipeline-empty", "pipeline-normal", "gantt-full",
                                      "ledger-full", "stop-report"};
    std::vector<std::string> manifest;
};

void Finish(State* state) {
    std::vector<std::string> files;
    files.reserve(state->expected.size());
    for (const std::string& stem : state->expected) {
        files.push_back(stem + ".png");
    }
    const review::FinishOptions opt{
        .dir = state->dir,
        .header = "P09-S10 shots",
        .expected = files,
        .manifest = &state->manifest,
        .min_bytes = 100,
        // 本轮 5 张承诺的状态两两不同：两个不同的总控台窗口（空 / 有数据）加上
        // 三个不同控件的子视图。逐字节相同 = 有一张没拍到它该拍的状态，判 FAIL。
        .fail_on_duplicate = true,
    };
    const review::FinishResult result = review::EvaluateShots(opt);
    std::printf("[p09-review]\n%s", result.report.c_str());
    review::WriteAndExit(opt);
}

void Run(State* state) {
    auto* empty = new PipelineWorkspace();
    empty->resize(1280, 820);
    empty->show();
    review::Grab(empty, state->dir, "pipeline-empty", state->manifest);
    auto* workspace = new PipelineWorkspace();
    workspace->resize(1280, 820);
    workspace->show();
    workspace->LoadMock();
    review::Pump();
    review::Grab(workspace, state->dir, "pipeline-normal", state->manifest);
    // 甘特 / 账本 / 停止报告是**同一个两列网格里的三块**（PipelineWorkspace::BuildUi：
    // gantt_ / ledger_ 在左列，stop_ 在右列，装在同一个 QScrollArea 里），
    // 三者同时可见，所以**不需要切页**就能各自拍到。
    // 原来这里有三行 `if (auto* tabs = workspace->findChild<QTabWidget*>())`：
    // PipelineWorkspace 全文件没有 QTabWidget（该页面不是页签布局），
    // 那三行恒为假、什么都不做，看着像断言其实只是空动作 —— 已删，别再加回来。
    review::Pump();
    review::Grab(workspace->Gantt(), state->dir, "gantt-full", state->manifest);
    review::Pump();
    review::Grab(workspace->Ledger(), state->dir, "ledger-full", state->manifest);
    review::Pump();
    review::Grab(workspace->StopReport(), state->dir, "stop-report", state->manifest);
    Finish(state);
}
} // namespace
void SaveP09Review(const std::filesystem::path& dir) {
    auto* state = new State;
    state->dir = dir;
    std::error_code ec;
shine::util::EnsureDir(dir);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
