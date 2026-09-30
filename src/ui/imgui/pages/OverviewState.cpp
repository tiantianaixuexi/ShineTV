// 总控页的**状态与运行驱动**（零绘制）
//
// 为什么不叫 Overview_Data.cpp：这一块不是「数据」，它是**线程边界**——
// worker 线程跑 runner_，UI 线程只读 ApplySnapshot 写出来的那份拷贝。
// 把它叫 data 会让下一个人以为「这里可以随便改」，而这里恰恰是
// 「UI 线程永远不碰正在被写的 runner_」这条不变量的看守点。
#include "ui/imgui/pages/Overview.h"

#include "core/Async.h"
#include "core/Settings.h"
#include "pipeline/Checkpoint.h"
#include "ui/imgui/pages/WorkspacePages.h"

#include <algorithm>
#include <cstdio>

namespace shine::pages {
namespace overview {

OverviewRun& State() {
    static OverviewRun state;
    if (state.chain.empty()) {
        for (const auto& def : pipeline::AllStages()) {
            if (def.chain == "text") {
                state.chain.push_back(def);
            }
        }
    }
    return state;
}

void ApplySnapshot(OverviewRun& s, const pipeline::Budget& budget,
                   const std::vector<pipeline::LedgerEntry>& entries) {
    s.budget = budget;
    s.entries = entries;
    s.doneChain.clear();
    for (const auto& def : s.chain) {
        const bool landed =
            std::any_of(entries.begin(), entries.end(), [&def](const pipeline::LedgerEntry& e) {
                return pipeline::StageCode(e.stage) == def.code;
            });
        if (landed) {
            s.doneChain.push_back(def.code);
        }
    }
}

void SyncSnapshot(OverviewRun& s) {
    if (s.running) {
        return;  // worker 正在写 runner_，不并发读
    }
    ApplySnapshot(s, s.runner.Usage(), s.runner.LedgerLog().Entries());
}

int ChainPercent(const OverviewRun& s) {
    if (s.finished) {
        return 100;
    }
    if (s.chain.empty()) {
        return 0;
    }
    return static_cast<int>(s.doneChain.size()) * 100 / static_cast<int>(s.chain.size());
}

bool LlmConfigured() {
    const auto& s = Settings();
    if (s.llmProvider == "openai" || s.llmProvider == "custom") {
        return !s.openaiApiKey.empty();
    }
    if (s.llmProvider == "mimo") {
        return !s.mimoApiKey.empty();
    }
    if (s.llmProvider == "minimax") {
        return !s.minimaxApiKey.empty();
    }
    return false;
}

bool CrossReviewConfigured() {
    const auto& s = Settings();
    return !s.openaiModelCritic.empty() && s.openaiModelCritic != s.openaiModelWriter;
}

pipeline::StopDecision EvaluateStopPolicy(const pipeline::Budget& budget) {
    return pipeline::StopPolicy{}.Evaluate(budget, !Settings().comfyBaseUrl.empty(), LlmConfigured(),
                                           CrossReviewConfigured(), /*committed_scenes=*/false);
}

std::string Money(double value) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "¥%.2f", value);
    return buf;
}

void StartRun(OverviewRun& s, int steps) {
    if (s.running || !s.bound) {
        return;
    }
    // ⚠️ 执行体没接入时**不能调 RunNext**。哪怕让它返回 false 也一样会留下假数据：
    //    Runner::RunNext 先 `budget_.ConsumeLlm(...)`（Runner.cpp:40）**才**调执行体
    //    （:46），所以「执行失败」这条路会留下一条「1 次 LLM / ¥0.01」的真·假账。
    //    不调，就一个数字都不产生。
    if (!OverviewPipelineWired()) {
        s.finished = false;
        s.userStopped = false;
        s.stopRule.clear();
        s.stopReason = StageExecutorMissingReason();
        s.running = false;
        s.runningStage = s.runner.CurrentStage();
        return;
    }
    s.running = true;
    s.finished = false;
    s.userStopped = false;
    s.stopRule.clear();
    s.stopReason.clear();
    s.stopFlag = std::make_shared<std::atomic<bool>>(false);
    s.runningStage = s.runner.CurrentStage();
    const auto flag = s.stopFlag;
    auto* runner = &s.runner;  // State 是函数内 static，生命周期长于 worker
    // ⚠️ 早先这里是 `AllStages().size()` = **28**（T1..T17 + V0..V11），而本页呈现的
    //    chain 只取 chain=="text" 的 17 个。于是「一键全流程」会跑 28 个阶段，进度条的
    //    分母却是 17 —— 数字自己跟自己就对不上。分母得跟界面上画出来的一致。
    //    在 lambda **外面**算：s 是 UI 侧状态，worker 不该碰它。
    const int total = steps < 0 ? static_cast<int>(s.chain.size()) : steps;
    async::RunOnWorker([runner, flag, steps, total] {
        std::string reason;
        pipeline::Budget budget;
        std::vector<pipeline::LedgerEntry> entries;
        for (int i = 0; i < total; ++i) {
            if (flag->load()) {
                break;
            }
            const auto result = runner->RunNext();
            if (!result.ok) {
                reason = result.reason;
                break;
            }
            // 每跑完一个阶段回投一次快照：进度 / KPI / 甘特在运行中是真的在动。
            // 传的是 worker 自己复制的 budget + entries —— UI 线程不去读 runner_。
            budget = runner->Usage();
            entries = runner->LedgerLog().Entries();
            const auto stage = result.stage;
            async::PostToUi([flag, stage, budget, entries] {
                OverviewRun& cur = State();
                if (cur.stopFlag != flag) {
                    return;  // 已经又起过一轮
                }
                ApplySnapshot(cur, budget, entries);
                cur.runningStage = stage;
            });
        }
        async::PostToUi([flag, reason = std::move(reason), budget, entries] {
            OverviewRun& cur = State();
            if (cur.stopFlag != flag) {
                return;  // 已经又起过一轮，丢掉这次收尾
            }
            cur.running = false;
            cur.userStopped = flag->load();
            ApplySnapshot(cur, budget, entries);
            cur.stopReason = std::move(reason);
            // 规则串只在这一轮**真被卡住**时记：StopPolicy 的第 4 个前置（已提交 Scene）
            // 本页查不到、按未满足传，所以它几乎恒返回 stop —— 直接拿来当"卡住的规则"
            // 会把没出事的运行也说成被规则拦下。
            cur.stopRule = cur.stopReason.empty()
                               ? std::string()
                               : EvaluateStopPolicy(cur.budget).rule;
            cur.finished = !cur.userStopped && cur.stopReason.empty() && !cur.chain.empty() &&
                           cur.doneChain.size() == cur.chain.size();
            // worker 已经收工，且 PostToUi 的锁给了 happens-before，这里读 runner_ 是安全的
            cur.runningStage = cur.runner.CurrentStage();
        });
    });
}

} // namespace overview

// ================================================================ 公开入口（外壳调）

// 阶段执行体是否已接入。
//
// 未接入时全流程**真的跑不了**，所以顶栏的「运行」和本页的「一键全流程」
// 都靠这一个读数决定要不要给用户按。两边各写一份判断的话，迟早只改得动一边。
bool OverviewPipelineWired() { return false; }

// 「没接入」这句话**只有这一份**。页面上、toast 里、按钮 tooltip 里说的必须是同一句，
// 措辞不一致会让人以为是两回事。
const char* StageExecutorMissingReason() {
    return "阶段执行体未接入 · 业务层没有「替前端跑一个 T 阶段」的服务接口（"
           "真执行体在 novel/ 且签名与 StageExecutor 不同形状）";
}

// 调用方是 Shell.cpp —— 必须在 overview 命名空间**之外**（见下面的注释）。
// 没绑定时本页就是空态，不编数。
void BindOverviewProject(std::filesystem::path root) {
    overview::OverviewRun& s = overview::State();
    if (s.running || root.empty()) {
        return;
    }
    s.root = std::move(root);
    // 阶段执行体：**没有接入**，所以传 nullptr 而不是编一个。
    //
    // ⚠️ 早先这里注入的是 `[](StageId, const std::string&, std::string&) { return true; }`
    //    —— 一个什么都不做、直接报成功的空 lambda。后果是整页看起来**完全正常**：
    //    Runner 认定 17 个阶段全部成功，账本里落下 17 条 work/T*.json 占位、预算扣成
    //    「17 次 LLM / ¥0.17」、进度条走到 100%、状态写「全流程完成：产物与账本已落盘」。
    //    **一个阶段都没真跑过。**
    //    这比旧版的字面量假数据（12480 / 3/17）更坏：字面量一眼看得出是假的，
    //    而这一套是真数据通路算出来的假结果 —— 验不出来，只能不产生它。
    //
    // 传 nullptr 也不是没成本：Runner::RunNext 在**调用执行体之前**就先扣预算
    // （Runner.cpp:40 的 ConsumeLlm 在 :46 的 executor_ 调用之前），所以「让执行体
    // 返回 false」这条路仍会留下一条「1 次 LLM / ¥0.01」的假账。正解是
    // StartRun 里压根不调 RunNext —— 见那里。
    //
    // 真正能跑 T 链的执行体在 novel/（NovelDirector::GenerateChapter、
    // novelcore::GenerateOneChapter、NovelContinuity），但它们的签名是
    // expected<...> / ChapterGenOutcome，与 StageExecutor = bool(StageId, string&, string&)
    // **不是同一形状，也没有适配层**。补那个适配层是跨模块的事，不在这里顺手编一个假的。
    s.runner.Configure(s.root, pipeline::RunMode::Auto, nullptr, nullptr);
    s.bound = true;
    s.finished = false;
    s.userStopped = false;
    s.stopRule.clear();
    s.stopReason = StageExecutorMissingReason();
    s.runningStage = s.runner.CurrentStage();
    std::error_code ec;
    const auto checkpointFile = s.root / "work" / "checkpoint.json";
    if (std::filesystem::exists(checkpointFile, ec)) {
        s.chapter = pipeline::Checkpoint::Load(checkpointFile).chapter;
    } else {
        s.chapter = -1;  // Load() 对缺文件返回默认值 0，0 与"真的第 0 章"分不开
    }
    overview::SyncSnapshot(s);
}

} // namespace shine::pages
