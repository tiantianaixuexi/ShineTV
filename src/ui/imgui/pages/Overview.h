#pragma once
// shine::pages —— 总控页（P5.1）的**私有契约**
//
// 这个文件原先不存在：状态结构（OverviewRun）、快照同步、配置探针、运行驱动，
// 以及 DrawOverview 里那七个子区域，全挤在 WorkspaceA.cpp 一个匿名命名空间里。
// 匿名命名空间能防链接冲突，但**不能跨文件**——所以拆分的第一个动作不是「切代码行」，
// 是把「谁需要谁」写成声明。
//
// ⚠️ 公开入口（BindOverviewProject / OverviewPipelineWired / StageExecutorMissingReason /
//    DrawOverview / ViewHeader）**不在这里**，它们是外壳要调的，声明在 WorkspacePages.h。
//    这里只放「总控页这一页内部共享」的东西 —— 换一页就不成立的量一律不留。
#include "pipeline/Budget.h"
#include "pipeline/Ledger.h"
#include "pipeline/Runner.h"
#include "pipeline/StageMachine.h"
#include "pipeline/StopPolicy.h"
#include "ui/imgui/kit/Views.h"
#include "ui/imgui/kit/Widgets.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace shine::pages {
namespace overview {

// ================================================================ 总控的真实数据源
//
// 旧版这一页的每个数字都是字面量（KPI "12480"/"3/17"/"128"、账本哈希
// `stage.code + "-91f2c"`、8 条同名的 "SN 预算上限"、恒 0% 的进度）。
// 现在全部改成读业务层：pipeline::Runner（预算 / 账本 / 当前阶段）、
// pipeline::Checkpoint（章节）、pipeline::StopPolicy（停止判定）、
// AppSettings（LLM / ComfyUI / 复核模型是否真的配了）。取不到就是取不到 ——
// 页面上显示空态，不补数字。
//
// 线程模型照搬 Qt 版同页 src/ui/pages/pipeline/PipelineWorkspace：
//   * worker 线程跑 runner_，UI 线程只读下面那份**快照**（budget/entries/doneChain）；
//   * 停止按阶段粒度 —— stopFlag 只在 RunNext 返回后检查，不打断阶段内部。
struct OverviewRun {
    pipeline::Runner runner;
    std::vector<pipeline::StageDefinition> chain;  // chain == "text" 的 T1–T17
    std::filesystem::path root;
    bool bound = false;     // 外壳有没有把工程根传进来（BindOverviewProject）
    bool running = false;
    bool finished = false;  // T 链每个阶段都在账本里落过盘
    bool userStopped = false;
    pipeline::StageId runningStage = pipeline::StageId::T1;
    std::string stopRule;    // StopPolicy 返回的真实规则串
    std::string stopReason;  // Runner::RunResult / StopPolicy 的中文原因
    std::shared_ptr<std::atomic<bool>> stopFlag;
    int chapter = -1;  // Checkpoint 的 chapter；-1 = 没有检查点文件

    // UI 快照：worker 写 runner_，UI 每帧只读这份拷贝
    pipeline::Budget budget;
    std::vector<pipeline::LedgerEntry> entries;
    std::vector<std::string> doneChain;  // T 链里已落盘账本的阶段码（按链序）
};

// 函数内 static（生命周期长于 worker 线程，所以 worker 捕获指针是安全的）。
OverviewRun& State();

// 把一份**拷贝**写进 UI 快照。worker 回投时传的就是它自己复制的那份，
// 所以 UI 线程永远不碰正在被写的 runner_。
void ApplySnapshot(OverviewRun& s, const pipeline::Budget& budget,
                   const std::vector<pipeline::LedgerEntry>& entries);

void SyncSnapshot(OverviewRun& s);

[[nodiscard]] int ChainPercent(const OverviewRun& s);

// StopPolicy::Evaluate 的四个前置里，三个能从 AppSettings 真的读出来；
// 第四个（已提交 Scene）要查 novel.db —— ImGui 侧还没接那条路，所以按"未满足"传，
// 并在卡片脚注里写明这是假设，不把它当成真判定。
[[nodiscard]] bool LlmConfigured();
[[nodiscard]] bool CrossReviewConfigured();
[[nodiscard]] pipeline::StopDecision EvaluateStopPolicy(const pipeline::Budget& budget);

[[nodiscard]] std::string Money(double value);

// 跑 steps 个阶段（steps < 0 = 整条链）。已在跑 / 没绑工程 / 执行体未接入时什么都不做。
void StartRun(OverviewRun& s, int steps);

// ================================================================ 子区域
//
// 七块各自成函数，是因为它们**各自是一张卡**：任何一张卡的内部改动（换列数、
// 改行高、加一行脚注）都不该要求读者同时读另外六张。返回值只回「我画到哪了」，
// 下一块据此接排 —— 外壳那层 `content` / `left` / `rightCol` 的算法留在
// OverviewPage.cpp 一处，**不**由各块自己算。
//
// 每块的绘制顺序**就是**原来 DrawOverview 里的先后顺序；改顺序会改叠压关系。

// 页头右侧操作条：运行下一阶段 + 一键全流程 / 停止。
void DrawHeaderActions(ImDrawList* draw, kit::Rect right, OverviewRun& s);

// 「运行状态」卡：三态文案 + 百分比 + 进度条 + StageFlow。
void DrawFlowCard(ImDrawList* draw, kit::Rect card, const OverviewRun& s);

// KPI 行。返回**实际行数** —— gridTop 必须按行数推，不能当只有一行。
int DrawKpiRow(ImDrawList* draw, kit::Rect content, float kpiTop, const OverviewRun& s);

// 全书甘特卡。返回卡底。
kit::Rect DrawGantt(ImDrawList* draw, kit::Rect left, const OverviewRun& s);

// 账本卡。返回卡底。
kit::Rect DrawLedger(ImDrawList* draw, kit::Rect left, float ledgerTop, const OverviewRun& s);

// 停止条件 · S1–S12（`09` §2.2 的真实规则表），**左列整宽**。返回卡底。
kit::Rect DrawStopRuleTable(ImDrawList* draw, kit::Rect content, float s12Top);

// 右栏（ScrollRegion 内的四张卡）。返回**绝对屏幕 y** 的内容底，供自报页面高度。
float DrawRightRail(kit::Rect rightCol, const OverviewRun& s);

} // namespace overview
} // namespace shine::pages
