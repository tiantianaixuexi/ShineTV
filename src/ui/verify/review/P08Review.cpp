#include "ui/verify/review/P08Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "core/Async.h"
#include "ui/pages/videoflow/QmlVideoFlowPage.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "flow/VideoChain.h"

#include <QApplication>
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
    // 与本文件 6 个抓图调用点**一一对应**（按拍摄顺序）：
    //   画布空态 → 画布有图 → 链全通 → 链断链 → 任务运行中 → 成片页。
    // 收尾按这份清单逐张查存在 + 字节数下限 + 逐字节重复：缺一张、或有一张拍成了
    // 另一张的像素，都是 overall=FAIL。清单必须跟着抓图改，否则就是「悄悄少一张」。
    std::vector<std::string> expected{"video-flow-empty", "video-flow-normal", "chain-connected",
                                      "chain-broken", "video-tasks-running", "final-videos"};
    std::vector<std::string> manifest;
    // 附加判据：既进报告也进退出码（FinishOptions::extra）。
    // 迁移前这里是两处 `findChild<QTabWidget*>()` —— 那个反查在 QWidget 版就已经
    // **恒不成立**（P09 的三处同类断言是同一批历史残留），页面却照样记 saved。
    // 现在页签状态上了桥（Page.tab），改成**读回真值**：写完 ActiveTab() 必须
    // 等于期望的页签键，否则判 FAIL。这条能抓住「宿主写了没人读的死属性」那一类缺陷
    // （2026-09-29 在资产页抓到过：写 viewIndex 而全页没有一处读它）。
    std::vector<std::pair<std::string, bool>> extra;
    // 迁移后无等价实现的取证项：显式记账，不进 expected，也就不会拉低 overall。
    std::vector<std::string> notCovered;
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
        .not_covered = &state->notCovered,
        .min_bytes = 100,
        // 本轮 6 张承诺的状态两两不同（前两张是画布区、后四张是面板区，
        // 面板那四张又分别落在三个页签上），所以逐字节相同就等于有一张
        // 没拍到它该拍的状态，判 FAIL。
        .fail_on_duplicate = true,
        .extra = state->extra,
    };
    const review::FinishResult result = review::EvaluateShots(opt);
    std::printf("[p08-review]\n%s", result.report.c_str());
    review::WriteAndExit(opt);
}

void Run(State* state) {
    // 取证视口 = **真实分辨率** 1920×1080。QML 页面整页是一个 QQuickWidget，
    // 拿不到「面板/任务/成片」这些子控件，所以按 QML 报出来的几何裁剪：
    // 像素仍来自同一次真实抓帧（QuickHost::GrabBlocking），不是把宿主拉高取全页。
    //
    // ⚠️ 页面用 new 且**不 delete**：QuickHost 不随页面析构（QmlVideoFlowPage.cpp
    //    的 HostPool 注释），页面先死会让仍存活的引擎持有悬垂的 `Page`。
    auto* page = new QmlVideoFlowPage();
    page->resize(1920, 1080);
    page->show();
    review::Pump();

    // ① 画布空态：还没导入任何视频工作流（产品的初始状态）。
    //    这一张与 ② 的区别是**结构性的**：空态没有节点/连线 Repeater 项，
    //    且 fit() 显式复位了 zoom/viewX/viewY（否则会停在上一次的取景上）。
    review::GrabImage(page->GrabRegion(QStringLiteral("Canvas")), state->dir, "video-flow-empty",
                      state->manifest);

    // ② 画布有图：4 节点 3 连线，v2 选中（复刻迁移前 LoadMock 的 SelectNode("2")）。
    page->LoadMock();
    review::Pump();
    review::GrabImage(page->GrabRegion(QStringLiteral("Canvas")), state->dir, "video-flow-normal",
                      state->manifest);

    // ③ 首尾帧链全通（演示数据三个镜头首尾相接 → 两条链都是通的）。
    page->SetTab(QStringLiteral("chain"));
    const bool chain_tab = page->ActiveTab() == QStringLiteral("chain");
    review::Pump();
    review::GrabImage(page->GrabRegion(QStringLiteral("Panel")), state->dir, "chain-connected",
                      state->manifest);

    // ④ 断链：换一条真断的链（末帧 ≠ 下一镜首帧），面板要出现 ⚠ 断链 那几行。
    page->SetChain(shine::flow::BuildVideoChain({{1, "a", "b", ""}, {2, "x", "c", ""}}));
    const bool broken = page->ChainProbe().contains(QString::fromUtf8("断链 1"));
    state->manifest.push_back("chain-broken-probe " + page->ChainProbe().toStdString());
    review::Pump();
    review::GrabImage(page->GrabRegion(QStringLiteral("Panel")), state->dir, "chain-broken",
                      state->manifest);

    // ⑤ 视频任务：回到演示数据并推进一个任务到运行中。
    page->LoadMock();
    page->SetTab(QStringLiteral("task"));
    const bool task_tab = page->ActiveTab() == QStringLiteral("task");
    page->ShowRunning();
    state->manifest.push_back("video-tasks-probe " + page->TaskProbe().toStdString());
    review::Pump();
    review::GrabImage(page->GrabRegion(QStringLiteral("Panel")), state->dir, "video-tasks-running",
                      state->manifest);

    // ⑥ 成片页。
    page->SetTab(QStringLiteral("cut"));
    const bool cut_tab = page->ActiveTab() == QStringLiteral("cut");
    review::Pump();
    review::GrabImage(page->GrabRegion(QStringLiteral("Panel")), state->dir, "final-videos",
                      state->manifest);

    state->extra.push_back({"tab-switch-chain", chain_tab});
    state->extra.push_back({"tab-switch-task", task_tab});
    state->extra.push_back({"tab-switch-cut", cut_tab});
    state->extra.push_back({"chain-broken-probe", broken});

    // —— 迁移后没有等价实现的取证项：显式记账，不进 expected ——
    // （写进 notCovered 而不是 manifest：它们本来就不该被 Finish() 当成
    //  「应该有却没有」的图片来判失败。缺了这一栏，「没这一行」会被读成「验过了」。）
    state->notCovered.push_back(
        "chain-row-tooltip  链段行的 detail（flow::VideoChainLink::detail）迁移前挂在 "
        "QLabel 的 tooltip 上；设计稿的 [data-tip]::after 与 QML 都没有对应实现"
        "（与 IconBtn.tip 同一个已记录缺口），本仓也不 import QtQuick.Controls。断链的"
        "处置建议已并入行内 policy 文本");
    state->notCovered.push_back(
        "film-thumb  胶片格的首帧缩略图：迁移前 FilmStrip::Cell.thumb 从未被填过，"
        "设计稿用的是 <Art seed> 程序化占位图（假缩略），两版都显示显式空态。要接真缩略"
        "需 worker 解码后由宿主注入 file:// URL，本轮未做");
    state->notCovered.push_back(
        "task-stop-button  设计稿 TaskList 的「停止」：Widgets 版那一格就是「演示运行」，"
        "两版都没有停止实现。QML 侧只给两个真有实现的按钮，不做没接线的死按钮");

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
