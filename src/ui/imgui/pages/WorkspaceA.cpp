#include "ui/imgui/pages/WorkspacePages.h"

#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "pipeline/Budget.h"
#include "pipeline/Checkpoint.h"
#include "pipeline/Ledger.h"
#include "pipeline/Runner.h"
#include "pipeline/StageMachine.h"
#include "pipeline/StopPolicy.h"
#include "novel/NovelRunLoop.h"
#include "ui/imgui/kit/Scroll.h"
#include "util/Encoding.h"
#include "util/Shell.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace shine::pages {
namespace {

using namespace shine::kit;

constexpr float kGap = 16.0f;

// KPI 卡：右上角 90px accent 圆模糊 2px 溢出；数值 24px/800 等宽数字
void KpiCard(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view value,
             std::string_view unit, std::string_view footnote, int tone) {
    DrawShadowed(draw, bounds.min, bounds.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
    // 右上角 90px accent 柔光。设计稿是 filter:blur(2px) 的模糊圆，ImGui draw list
    // 没有模糊可用 —— 用同心圆叠出径向衰减。
    //
    // 关键是**每层 alpha 相同**而不是每层颜色相同：圆环面积等分，中心累计到峰值、
    // 最外圈只有峰值的 1/layers，边界那一跳就只有 1/layers，肉眼看不出硬边。
    // 均匀叠加 4 层实心圆会读成「贴了张色斑」而不是「有光」。
    const float blobR = 45.0f; // 90px 直径
    const ImVec2 blob(bounds.max.x - 26.0f, bounds.min.y + 26.0f);
    const ImU32 toneColor = ToneColor(static_cast<theme::Tone>(tone));
    constexpr int kLayers = 14;
    constexpr float kPeak = 0.16f;
    for (int i = 0; i < kLayers; ++i) {
        const float r = blobR * static_cast<float>(i + 1) / static_cast<float>(kLayers);
        draw->AddCircleFilled(blob, r, WithAlpha(toneColor, kPeak / kLayers), 24);
    }
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(bounds.min.x + 16.0f, bounds.min.y + 14.0f),
                  ColorTextSecondary(), label.data(), label.data() + label.size());
    ImFont* valueFont = FontBoldAt(24.0f);
    draw->AddText(valueFont, 24.0f, ImVec2(bounds.min.x + 16.0f, bounds.min.y + 30.0f), ColorText(),
                  value.data(), value.data() + value.size());
    const float valueW =
        valueFont->CalcTextSizeA(24.0f, 1e9f, 0.0f, value.data(), value.data() + value.size()).x;
    if (!unit.empty()) {
        draw->AddText(FontAt(12.5f), 12.5f,
                      ImVec2(bounds.min.x + 16.0f + valueW + 4.0f, bounds.min.y + 48.0f),
                      ColorTextMuted(), unit.data(), unit.data() + unit.size());
    }
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(bounds.min.x + 16.0f, bounds.max.y - 24.0f),
                  ColorTextMuted(), footnote.data(), footnote.data() + footnote.size());
}

float LabelWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, 1e9f, 0.0f, text, text + std::strlen(text)).x;
}

// ================================================================ P5.1 总控的真实数据源
//
// 旧版这一页的每个数字都是字面量（KPI "12480"/"3/17"/"128"、账本哈希
// `stage.code + "-a91f2c"`、8 条同名的 "SN 预算上限"、恒 0% 的进度）。
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

OverviewRun& OverviewState() {
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

// 把一份**拷贝**写进 UI 快照。worker 回投时传的就是它自己复制的那份，
// 所以 UI 线程永远不碰正在被写的 runner_。
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

[[nodiscard]] int ChainPercent(const OverviewRun& s) {
    if (s.finished) {
        return 100;
    }
    if (s.chain.empty()) {
        return 0;
    }
    return static_cast<int>(s.doneChain.size()) * 100 / static_cast<int>(s.chain.size());
}

// StopPolicy::Evaluate 的四个前置里，三个能从 AppSettings 真的读出来；
// 第四个（已提交 Scene）要查 novel.db —— ImGui 侧还没接那条路，所以按"未满足"传，
// 并在卡片脚注里写明这是假设，不把它当成真判定。
[[nodiscard]] bool LlmConfigured() {
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

[[nodiscard]] bool CrossReviewConfigured() {
    const auto& s = Settings();
    return !s.openaiModelCritic.empty() && s.openaiModelCritic != s.openaiModelWriter;
}

[[nodiscard]] pipeline::StopDecision EvaluateStopPolicy(const pipeline::Budget& budget) {
    return pipeline::StopPolicy{}.Evaluate(budget, !Settings().comfyBaseUrl.empty(), LlmConfigured(),
                                           CrossReviewConfigured(), /*committed_scenes=*/false);
}

[[nodiscard]] std::string Money(double value) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "¥%.2f", value);
    return buf;
}

} // namespace

// 外壳把"当前工程根"交给本页的唯一入口（对应 Qt 版 PipelineWorkspace::SetContext）。
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

// 调用方是 Shell.cpp —— 必须在匿名命名空间**之外**，否则外部链接不到。
// 没绑定时本页就是空态，不编数。
void BindOverviewProject(std::filesystem::path root) {
    OverviewRun& s = OverviewState();
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
    SyncSnapshot(s);
}

namespace {

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
    auto* runner = &s.runner;  // OverviewState 是函数内 static，生命周期长于 worker
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
                OverviewRun& cur = OverviewState();
                if (cur.stopFlag != flag) {
                    return;  // 已经又起过一轮
                }
                ApplySnapshot(cur, budget, entries);
                cur.runningStage = stage;
            });
        }
        async::PostToUi([flag, reason = std::move(reason), budget, entries] {
            OverviewRun& cur = OverviewState();
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

} // namespace

Rect ViewHeader(Rect area, ImDrawList* draw, const char* icon, const char* title,
                const char* subtitle, Rect* rightOut) {
    DrawIcon(draw, icon, ImVec2(area.min.x, area.min.y), 20.0f, ColorAccent());
    draw->AddText(FontBoldAt(18.0f), 18.0f, ImVec2(area.min.x + 28.0f, area.min.y - 2.0f),
                  ColorText(), title, title + std::strlen(title));
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(area.min.x + 28.0f, area.min.y + 22.0f),
                  ColorTextMuted(), subtitle, subtitle + std::strlen(subtitle));
    if (rightOut != nullptr) {
        *rightOut = Rect{area.max.x - 320.0f, area.min.y, area.max.x, area.min.y + 30.0f};
    }
    return Rect{area.min.x, area.min.y + 48.0f, area.max.x, area.max.y};
}

// ================================================================ P5.1 总控
// 纯数据页：先做它验证数据通路（pipeline::{StageMachine, Budget, StopPolicy, Ledger}）。
// 阶段表直接来自 pipeline::AllStages()，不是前端硬编码的假数据。
void DrawOverview(Rect area, ImDrawList* draw) {
    OverviewRun& s = OverviewState();
    SyncSnapshot(s);

    Rect right;
    Rect content = ViewHeader(area, draw, "gauge", "全流程总控台",
                              "一句话到成片 · 阶段机 / 预算 / 账本 / 检查点", &right);

    // 头右侧：运行下一阶段(secondary) + 一键全流程(primary)；运行中换成 danger 停止
    // （Overview.jsx:31-35 同款切换）。没绑工程时两个按钮都是 disabled —— 不假装能跑。
    const char* nextLabel = "运行下一阶段";
    const float nextW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, nextLabel));
    const char* runLabel = s.running ? "停止" : "一键全流程";
    const float runW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, runLabel));
    ButtonSpec secondary;
    secondary.variant = ButtonVariant::Secondary;
    secondary.icon = "zap";
    secondary.disabled = s.running || !s.bound || !OverviewPipelineWired();
    if (Button(draw, RectAt(right.max.x - runW - nextW - 12.0f, right.min.y, nextW, 30.0f), nextLabel,
               secondary, "ov-next")) {
        StartRun(s, 1);
    }
    ButtonSpec primary;
    primary.variant = s.running ? ButtonVariant::Danger : ButtonVariant::Primary;
    primary.icon = s.running ? "stop" : "play";
    // 「一键全流程」在执行体未接入时也 disabled —— 它不是"点了会失败"，是**真的跑不了**。
    // 早先它可点，点完跑出一整套看起来正常的假数字（见 BindOverviewProject 注释）。
    primary.disabled = !s.running && (!s.bound || !OverviewPipelineWired());
    if (Button(draw, RectAt(right.max.x - runW, right.min.y, runW, 30.0f), runLabel, primary,
               s.running ? "ov-stop" : "ov-all")) {
        if (s.running) {
            s.stopFlag->store(true);  // 阶段粒度：当前阶段收尾后停
        } else {
            StartRun(s, -1);
        }
    }
    // 禁用原因写在按钮**下面**而不是 tooltip：取证截图抓不到 tooltip，
    // 而「为什么按不动」必须在那张图上看得见。
    //
    // ⚠️ AddText 的长度参数是**字节数**。这里跟着字面量走（sizeof），不手数 ——
    //    「阶段执行体未接入 · 两个运行按钮已禁用」手算成 33 会少画两个字且不报错。
    if (!s.running && !OverviewPipelineWired() && s.bound) {
        static constexpr char kDisabledWhy[] = "阶段执行体未接入 · 两个运行按钮已禁用";
        draw->AddText(FontAt(11.5f), 11.5f,
                      ImVec2(right.max.x - runW - nextW - 12.0f, right.min.y + 34.0f),
                      ColorTextMuted(), kDisabledWhy, kDisabledWhy + sizeof(kDisabledWhy) - 1);
    }

    // 1) 运行状态卡 + StageFlow
    const Rect flowCard{content.min.x, content.min.y, content.max.x, content.min.y + 122.0f};
    Rect flowBody = Card(draw, flowCard, "运行状态", "zap", false, s.running);
    // Overview.jsx:42-46 的三态文案，判据全部来自 runner_ / 账本，不写死
    std::string status;
    ImU32 statusColor = ColorTextMuted();
    if (s.running) {
        status = "当前阶段 " + pipeline::StageCode(s.runningStage) + " · 运行中";
        statusColor = ColorAccent();
    } else if (s.finished) {
        status = "全流程完成：产物与账本已落盘";
        statusColor = ColorOf(theme::Current().statusOk);
    } else if (s.userStopped) {
        status = "已手动停止 · 停在 " + pipeline::StageCode(s.runningStage);
        statusColor = ColorOf(theme::Current().statusWarn);
    } else if (!s.bound) {
        status = "未绑定工程根 · 打开一本小说后这里才有真实数据";
    } else if (!OverviewPipelineWired()) {
        // ⚠️ 早先这个状态走的是下面的「等待运行 · 从 T1 开始」分支。执行体没接入时
        //    「等待运行」是在**许诺一件做不到的事** —— 用户会一直等下去。
        // 直接把「跑不了」和原因写在这一行，而不是藏进某个脚注。
        status = std::string("未接入阶段执行体 · 本页不产生任何运行数据") ;
        statusColor = ColorOf(theme::Current().statusWarn);
    } else if (s.entries.empty()) {
        status = "等待运行 · 从 " + (s.chain.empty() ? std::string("T1") : s.chain.front().code) + " 开始";
    } else {
        status = "已停在 " + pipeline::StageCode(s.runningStage) + " · 未跑完";
        if (!s.stopRule.empty()) {
            status += " · " + s.stopRule;
        }
        if (!s.stopReason.empty()) {
            status += "：" + s.stopReason;
        }
    }
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(flowBody.min.x, flowBody.min.y), statusColor, status.data(),
                  status.data() + status.size());
    const int percent = ChainPercent(s);
    const std::string pct = std::to_string(percent) + "%";
    draw->AddText(FontBoldAt(18.0f), 18.0f, ImVec2(flowBody.max.x - 40.0f, flowBody.min.y - 2.0f),
                  ColorText(), pct.data(), pct.data() + pct.size());
    Progress(draw, Rect{flowBody.min.x, flowBody.min.y + 22.0f, flowBody.max.x, flowBody.min.y + 28.0f},
             static_cast<float>(percent) / 100.0f, s.running, false);

    std::vector<StageNode> nodes;
    nodes.reserve(s.chain.size());
    for (const auto& def : s.chain) {
        const bool landed =
            std::find(s.doneChain.begin(), s.doneChain.end(), def.code) != s.doneChain.end();
        const StageState state =
            landed ? StageState::Done : (s.running && def.id == s.runningStage ? StageState::Running
                                                                               : StageState::Todo);
        nodes.push_back(StageNode{def.code, def.name, state});
    }
    if (!nodes.empty()) {
        StageFlow(draw,
                  Rect{flowBody.min.x, flowBody.max.y - 34.0f, flowBody.min.x + 1400.0f, flowBody.max.y},
                  nodes);
    }

    // 2) KPI 行：repeat(auto-fit, minmax(210px,1fr)) gap 14
    const float kpiTop = flowCard.max.y + kGap;
    const int kpiCols = AutoGridCols(content.width(), 210.0f, 14.0f);
    const float kpiW =
        (content.width() - 14.0f * static_cast<float>(kpiCols - 1)) / static_cast<float>(kpiCols);
    // 五张卡的数全部来自 Budget / 账本。脚注写的是**口径**（上限、档位、条数），
    // 不是 mock 里那种没有业务字段可对应的"较上轮 +12"。
    //
    // ⚠️ 执行体未接入时，账本/预算恒为全 0。于是这五行会显示「0/17 · LLM 0 次 ·
    //    估算成本 ¥0.00 · 镜头 0 个 · 阶段产物 0 条」—— 看着像**测出来的零**，
    //    其实是**从来没测**。这两个 0 在界面上必须长得不一样，否则就是谎报。
    //    统一改成「未接入」，口径信息（上限/档位）保留。
    const bool wired = OverviewPipelineWired();
    const int totalStages = static_cast<int>(s.chain.size());
    const bool overBudget = s.budget.Exceeded();
    const std::string kNoData = "未接入";
    struct Kpi {
        std::string label;
        std::string value;
        std::string unit;
        std::string footnote;
        int tone;
    };
    const Kpi kpis[] = {
        {"阶段进度",
         wired ? std::to_string(s.doneChain.size()) + "/" + std::to_string(totalStages) : kNoData,
         "",
         !wired ? std::string("执行体未接入 · 进度无从产生")
                : (s.doneChain.empty()
                       ? std::string("账本里还没有阶段产物")
                       : ("已落盘 " + s.doneChain.front() +
                          (s.doneChain.size() > 1 ? " – " + s.doneChain.back() : std::string()))),
         0},
        {"LLM 调用", wired ? std::to_string(s.budget.llm_calls) : kNoData, "次",
         "高档 " + std::to_string(s.budget.high_quality_calls) + " · 上限 " +
             std::to_string(s.budget.max_llm_calls),
         1},
        {"估算成本", wired ? Money(s.budget.cost) : kNoData, "",
         "上限 " + Money(s.budget.max_cost), 2},
        {"镜头", wired ? std::to_string(s.budget.shots) : kNoData, "个",
         "上限 " + std::to_string(s.budget.max_shots), 3},
        {"阶段产物", wired ? std::to_string(static_cast<int>(s.entries.size())) : kNoData, "条",
         overBudget ? ("预算已停：" + (s.budget.Reason().empty() ? std::string("超限") : s.budget.Reason()))
                    : std::string("work/ 下每阶段一文件"),
         4},
    };
    for (std::size_t i = 0; i < std::size(kpis); ++i) {
        const int column = static_cast<int>(i) % kpiCols;
        const int row = static_cast<int>(i) / kpiCols;
        KpiCard(draw,
                RectAt(content.min.x + (kpiW + 14.0f) * static_cast<float>(column),
                       kpiTop + (96.0f + 14.0f) * static_cast<float>(row), kpiW, 96.0f),
                kpis[i].label, kpis[i].value, kpis[i].unit, kpis[i].footnote, kpis[i].tone);
    }

    // 3) .grid-3-1（1fr / 300px gap16）
    // ⚠️ gridTop 必须按 KPI 的**实际行数**推，不能当只有一行：auto-fit 排 4 列时
    // 5 个 KPI 会换到第二行，按一行算会让下面的甘特卡盖住第二行卡片（文字直接压在一起）。
    const int kpiRows =
        (static_cast<int>(std::size(kpis)) + kpiCols - 1) / std::max(1, kpiCols);
    const float gridTop = kpiTop + (96.0f + 14.0f) * static_cast<float>(kpiRows) + kGap;
    const float rightW = 300.0f;
    // 右栏按**可视高度**收口，不跟着 2400 的布局区高跑。
    // ⚠️ 跟着布局区高跑的后果是实测出来的：右栏 child 高 1978px，于是它成了
    //    一个**布局容器**而不是视口 —— 卡片在里面从头排到尾、裁切交给外层，
    //    「右栏自己滚」这件事压根不存在（构造期读到的可视高 1978 > 内容高 654，
    //    期望滚动上限 0）。正文流（左列甘特 + 账本）仍然按布局区高排。
    const float viewportH = pages::WorkspaceViewportHeight();
    const float rightBottom =
        viewportH > 0.0f ? std::min(content.max.y, area.min.y + viewportH) : content.max.y;
    const Rect left{content.min.x, gridTop, content.max.x - rightW - kGap, content.max.y};
    const Rect rightCol{left.max.x + kGap, gridTop, content.max.x, rightBottom};

    // ---- 甘特：行 = T 链阶段，列 = 链上 8 等分桶，表头是那一桶的**真实阶段码** ----
    // 旧版是 `(i*3)%8` 的假跨度 + 只有"在/不在跨度"两态。现在三态都来自真实状态：
    // 账本里有 → done(ok)，正在跑 → run(accent)，被规则卡住 → stop(danger)，其余 todo。
    constexpr int kGanttCols = 8;
    // 行数只受**列数**限制（8 等分桶是设计稿定的），不再有「窄窗口先砍行数」：
    // 那个循环拿 `left.height() - kGap - ledgerMinH` 当预算，而 `left` 的下边界
    // 来自 Shell 的 2400 布局区，预算恒大于卡高 ⇒ 循环**永远不触发**，是死代码；
    // 它记的还是「甘特与账本挤在同一列固定高度里」那个已经被拆掉的布局。
    // 现在账本卡按内容定高、内容超出由工作区滚动（真能滚了），行数无需为它让位。
    const int ganttRows = std::min(totalStages, kGanttCols);
    const Rect gantt{left.min.x, left.min.y, left.max.x,
                     left.min.y + 96.0f + 22.0f * static_cast<float>(ganttRows)};
    Rect ganttBody = Card(draw, gantt, "全书甘特 · 阶段链", "grid", false, false);
    const float rowLabelW = 88.0f;
    const float cellW =
        std::max(24.0f, (ganttBody.width() - rowLabelW) / static_cast<float>(kGanttCols));
    for (int c = 0; c < kGanttCols; ++c) {
        const int first = totalStages == 0 ? 0 : c * totalStages / kGanttCols;
        const std::string label = totalStages == 0 ? "—" : s.chain[static_cast<std::size_t>(first)].code;
        draw->AddText(FontBoldAt(10.5f), 10.5f,
                      ImVec2(ganttBody.min.x + rowLabelW + cellW * c + 8.0f, ganttBody.min.y),
                      ColorTextMuted(), label.data(), label.data() + label.size());
    }
    float gy = ganttBody.min.y + 20.0f;
    for (int i = 0; i < ganttRows; ++i) {
        const auto& def = s.chain[static_cast<std::size_t>(i)];
        draw->AddText(MonoAt(10.5f), 10.5f, ImVec2(ganttBody.min.x, gy + 5.0f), ColorTextSecondary(),
                      def.code.data(), def.code.data() + def.code.size());
        const bool done = std::find(s.doneChain.begin(), s.doneChain.end(), def.code) != s.doneChain.end();
        const bool running = s.running && def.id == s.runningStage;
        // 停在这一格 = 它就是当前阶段，而这轮真的带着停止原因结束了
        const bool stopped = !running && !s.stopReason.empty() && def.id == s.runningStage;
        const int column = totalStages == 0 ? 0 : i * kGanttCols / totalStages;
        for (int c = 0; c < kGanttCols; ++c) {
            ImU32 fill = ColorFillMuted();
            if (c == column) {
                if (done) {
                    fill = ColorOf(theme::CurrentDerived().ganttCell[static_cast<int>(theme::Tone::Ok)]);
                } else if (running) {
                    fill = ColorOf(theme::CurrentDerived().accentDim);
                } else if (stopped) {
                    fill = ColorOf(theme::CurrentDerived().ganttCell[static_cast<int>(theme::Tone::Danger)]);
                }
            }
            DrawRoundRect(draw, ImVec2(ganttBody.min.x + rowLabelW + cellW * c + 4.0f, gy),
                          ImVec2(ganttBody.min.x + rowLabelW + cellW * (c + 1) - 4.0f, gy + 22.0f),
                          4.0f, fill);
            if (c == column && (done || running || stopped)) {
                DrawIconCentered(draw, done ? "check" : (stopped ? "x" : "play"),
                                 ImVec2(ganttBody.min.x + rowLabelW + cellW * c + cellW * 0.5f, gy + 11.0f),
                                 11.0f,
                                 done    ? ColorOf(theme::Current().statusOk)
                                 : stopped ? ColorOf(theme::Current().statusDanger)
                                           : ColorAccent());
            }
        }
        gy += 22.0f;
    }
    if (totalStages > ganttRows) {
        DrawTextClipped(draw, FontAt(11.0f), 11.0f, ImVec2(ganttBody.min.x, ganttBody.max.y - 12.0f),
                        ganttBody.width(), ColorTextMuted(),
                        "T 链共 " + std::to_string(totalStages) + " 个阶段，这里显示前 " +
                            std::to_string(ganttRows) + " 个");
    }

    // ---- 账本：input_hash 直接用 Ledger 记下来的值（不再拼 "T1-a91f2c"）----
    //
    // ⚠️ 卡高按**内容**算，不再拉到 `left.max.y`（= Shell 给的 2400 布局区底）。
    //    拉满的后果实测是一张 **1750px 高、只有 6 行**的卡：6 行数据浮在顶上，
    //    下面 1450px 全是空背景，顺带让工作区多出约 1450px 的无效滚动 —— 滚到底
    //    只能看到一片空盒子。**卡高跟着内容走，需要滚动交给工作区**（现在它真能滚了）。
    const int ledgerTotal = static_cast<int>(s.entries.size());
    constexpr int kLedgerMaxRows = 14;
    const int ledgerShown = std::min(ledgerTotal, kLedgerMaxRows);
    // 卡高 = 头 44 + 列头 18 + 行 22×N + 脚注 14 + 汇总行 16 + 卡脚 16
    const float ledgerBodyH =
        s.entries.empty()
            ? 24.0f
            : 18.0f + 22.0f * static_cast<float>(ledgerShown) +
                  (ledgerTotal > ledgerShown ? 14.0f : 0.0f) + 16.0f;
    const float ledgerH = 44.0f + ledgerBodyH + 16.0f;
    const float ledgerTop = gantt.max.y + kGap;
    const Rect ledger{left.min.x, ledgerTop, left.max.x, ledgerTop + ledgerH};
    Rect ledgerBody = Card(draw, ledger, "账本 · 成本 / 调用 / 降级", "list", false, false);
    if (s.entries.empty()) {
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, ImVec2(ledgerBody.min.x, ledgerBody.min.y),
                        ledgerBody.width(), ColorTextMuted(),
                        s.bound ? "还没有落盘产物：跑完一个阶段后这里会出现真实记录。"
                                : "未绑定工程根。",
                        true);
    } else {
        const float colStage = ledgerBody.min.x;
        const float colHash = colStage + 70.0f;
        const float colOut = colHash + 150.0f;
        const float colDeg = colOut + 280.0f;
        const std::pair<const char*, float> heads[] = {{"阶段", colStage}, {"输入哈希", colHash},
                                                      {"产物", colOut},     {"降级", colDeg}};
        for (const auto& [text, x] : heads) {
            draw->AddText(FontBoldAt(10.5f), 10.5f, ImVec2(x, ledgerBody.min.y), ColorTextMuted(), text,
                          text + std::strlen(text));
        }
        const float rowH = 22.0f;
        // 「显示最后 N 条」是按**行数上限**定的，不再按「可用高度」反推 ——
        // 可用高度依赖那张被撑到 2400 的卡，是循环论证。
        const int shown = ledgerShown;
        const int first = ledgerTotal - shown;
        float ly = ledgerBody.min.y + 18.0f;
        for (int i = first; i < static_cast<int>(s.entries.size()); ++i) {
            const auto& entry = s.entries[static_cast<std::size_t>(i)];
            const std::string code = pipeline::StageCode(entry.stage);
            draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(colStage, ly), ColorTextSecondary(), code.data(),
                          code.data() + code.size());
            DrawTextClipped(draw, MonoAt(11.5f), 11.5f, ImVec2(colHash, ly), 140.0f, ColorTextMuted(),
                            entry.input_hash.empty() ? "—" : entry.input_hash);
            DrawTextClipped(draw, MonoAt(11.5f), 11.5f, ImVec2(colOut, ly),
                            colDeg - colOut - 12.0f, ColorText(),
                            util::PathToUtf8(util::PathFromUtf8(entry.output_path).filename()));
            if (entry.degradation.empty()) {
                draw->AddText(FontAt(11.5f), 11.5f, ImVec2(colDeg, ly), ColorTextMuted(), "—", "—");
            } else {
                const float tagW = TagWidth(entry.degradation, true, false);
                Tag(draw, RectAt(colDeg, ly - 2.0f, tagW, 17.0f), entry.degradation, theme::Tone::Warn,
                    true);
            }
            ly += rowH;
        }
        if (ledgerTotal > shown) {
            // 脚注位置跟着行数走（脚注那 14px 已经算进卡高了），不再用 max.y - 24。
            DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                            ImVec2(ledgerBody.min.x, ly + 2.0f), ledgerBody.width(),
                            ColorTextMuted(),
                            "共 " + std::to_string(ledgerTotal) + " 条，显示最后 " +
                                std::to_string(shown) + " 条");
            ly += 14.0f;
        }
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(ledgerBody.min.x, ly + 2.0f), ledgerBody.width(),
                        ColorTextMuted(),
                        "阶段产物 " + std::to_string(ledgerTotal) +
                            " · LLM 调用 " + std::to_string(s.budget.llm_calls) + " · 高档 " +
                            std::to_string(s.budget.high_quality_calls) + " · 镜头 " +
                            std::to_string(s.budget.shots) + " · 估算成本 " + Money(s.budget.cost));
    }

    // ---- 停止条件 · S1–S12（`09` §2.2 的真实规则表），**左列整宽** ----
    //
    // ⚠️ 早先这里写着一句「设计稿的 S1–S12 来自 mock.js，`StopPolicy` 只判 5 组、
    //    没有可枚举的规则表接口，所以不硬凑 12 行」。**那个结论是错的**，而且错得
    //    有害：真实的 S 编号就在 `shine::novelcore` 里，是三个公开的自由函数
    //    （`src/novel/NovelRunLoop.h:43-48`）：
    //      StopCode（S1…S12）/ StopCodeName / StopCodeCondition / StopCodeHint
    //    实现在 `NovelRunLoop.cpp:102-138` 逐条落地，阈值（>=2 次、>2×、>=5 次、
    //    >=500 条…）都写在那十二条中文判据里。**零 shine_core 改动。**
    //
    //    错因是**编号撞名**：`src/pipeline/StopPolicy.cpp:7-11` 的字面量也叫
    //    「S1–S4 预算」/ S5 / S6 / S7 / S8，那是另一套 5 组判定。旧实现照着它把
    //    「LLM 调用 0/1000」标成 S1 —— 于是同一个 S1 在领域文档里是「机器校验连续
    //    失败 >= 2 次」，在界面上却是「LLM 调用预算」。**这不是少了个功能，是误导。**
    //
    //    放**左列整宽**而不是右栏：这些判据句子长（「Comfy 不可用且本章需要出图：
    //    探活失败 >= 3 次（间隔 5s）」），塞进 300px 的右栏会被裁掉后半句 ——
    //    规则表被截断等于没写。右栏留给「流水线停止规则」（实时预算状态）。
    constexpr int kStopRuleCount = 12;
    constexpr float kStopRuleRowH = 20.0f;
    const float s12H = 44.0f + 16.0f + kStopRuleRowH * kStopRuleCount + 16.0f;
    const float s12Top = ledger.max.y + kGap;
    const Rect s12{content.min.x, s12Top, content.max.x, s12Top + s12H};
    Rect s12Body = Card(draw, s12, "停止条件 · S1–S12", "alert", false, false);
    float sry = s12Body.min.y;
    for (int i = 1; i <= kStopRuleCount; ++i) {
        const novelcore::StopCode code = static_cast<novelcore::StopCode>(i);
        const std::string codeText(novelcore::StopCodeName(code));
        const std::string_view cond = novelcore::StopCodeCondition(code);
        const std::string_view hint = novelcore::StopCodeHint(code);
        draw->AddText(MonoAt(11.0f), 11.0f, ImVec2(s12Body.min.x, sry + 1.0f), ColorTextMuted(),
                      codeText.data(), codeText.data() + codeText.size());
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(s12Body.min.x + 34.0f, sry),
                        s12Body.width() * 0.56f, ColorTextSecondary(), cond, true);
        // 右半列给「停下后该做什么」（`StopCodeHint`）—— 判据说明**什么时候停**，
        // 提示说明**停下之后怎么办**，两者缺一这条规则就没法用。
        DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                        ImVec2(s12Body.min.x + s12Body.width() * 0.58f, sry),
                        s12Body.width() * 0.42f, ColorTextMuted(), hint, true);
        sry += kStopRuleRowH;
    }
    // ⚠️ 诚实标注：这是**规则表**，不是实时状态。真正的逐条判定发生在
    //    `novelcore::EvaluateStop`，它要一整轮跑出来的现场数字（各 check_id 失败
    //    次数、评审 FAIL 累计、引用缺失计数…），而本页没有跑起来的章，没有那些数。
    //    画成「已触发 / 未触发」就是编状态 —— 那正是本轮清掉的那类假数据。
    DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(s12Body.min.x, sry + 2.0f),
                    s12Body.width(), ColorTextMuted(),
                    "规则表（`09` §2.2，novelcore::StopCodeCondition / StopCodeHint）。"
                    "逐条判定发生在跑完一章之后，本页没有现场数字，故不标触发状态。");

    // ---- 右列：流水线停止规则 / 最近产物 / 运行信息 / 章节 × V 阶段 ----
    //
    // ⚠️ 这里原来用「每张卡各自 `max(下限, 剩余)`」分高度，右栏是**重叠**的：
    //    右栏可用高约 247px，而三张卡的下限之和是
    //      停止 220 + 最近产物 78 + 运行信息 120 + 2×16 = **450**
    //    算出来的实际 rect：
    //      停止条件  [500, 720]      ← 唯一正常
    //      最近产物  [736, 814]      ← **完全在可视区外**（747），被 clip 掉
    //      运行信息  [627, 747]      ← 与停止条件**重叠 93px**
    //    两张卡的背景色相同，叠在一起看不出来 —— 像素扫描 `overview-bound.png`
    //    的右列 y=506..745 是**一整块连续背景、中间只有一条分隔线**，证实了这一点。
    //    也就是说「最近产物」从来没被看到过，「运行信息」和「停止条件」的文字
    //    一直画在同一块背景上。
    //
    // 改成：每张卡按**内容真实高度**算，y 依次累加，溢出交给 ScrollRegion 滚动。
    // 自绘 draw call 既不裁剪也不接管命中，Scroll.h 的 BeginChild 一次给全
    // （裁剪矩形 + 滚动偏移 + 输入归属）—— 这正是它存在的理由。
    //
    // 「章节 × V 阶段」矩阵（数据层 `BookSideView::chapterVStages` 已接好）之所以
    // 放不下，也是同一个原因：右栏在这个窗口高度下塞不下第四张卡。它留在数据层，
    // 等布局重排（压 KPI 行高 / 加高窗口）后直接画，见 refactor/PROGRESS.md。
    //
    // ⚠️ 上面那段注释的结论已经过期：矩阵现已渲染在下面。它当时记的「放不下」建立
    //    在一个**从未生效**的机制上（右栏的 ScrollRegion 当时根本不能滚），所以
    //    「放不下」是必然的而不是设计约束。留在这里提醒别再拿它当结论。
    float rightBottomUsed = gridTop;
    ScrollRegion rightScroll("ov-right", rightCol);
    if (rightScroll) {
        // ⚠️ 右栏整块必须画在 **child 自己的** draw list 上。
        //    Shell.cpp 传进来的是**外层**（工作区 child）的 list，BeginChild 的裁剪
        //    矩形只写进 child 自己那条 list 的 CmdBuffer —— 画到外层 list 上，右栏照样
        //    Begin/End，但裁剪对那些 draw call **完全无效**。症状不是「卡片消失」，
        //    而是右栏滚上去之后盖住 KPI 行和页头按钮（实测：停止条件卡浮到 y=93，
        //    压在「镜头 / 未接入 ↑」和页头「运行」按钮上面），一眼看去像布局算错。
        ImDrawList* cdraw = rightScroll.drawList();
        const Rect rc = rightScroll.content();
        const float cardW = rc.width();
        float ry = rc.min.y;
        const pipeline::StopDecision decision = EvaluateStopPolicy(s.budget);
        const bool hasReason = decision.stop && !decision.reason.empty();
        const std::string verdict =
            decision.stop ? decision.rule : std::string("未触发任何停止条件");

        // ---- 卡 1：流水线停止规则（预算 + 前置条件，**不用 S 编号**）----
        //
        // 与左栏那张「停止条件 · S1–S12」**不是同一套编号**：`pipeline::StopPolicy`
        // 判 5 组（预算 / LLM 前置 / Comfy 前置 / 交叉复核 / 场景门禁），与 `09` §2.2
        // 的 S1–S12 是两回事。旧实现两边都用 S 开头，于是「S1」在这张卡里指 LLM 调用
        // 预算、在左栏那张卡里指机器校验连续失败 —— 同一个代号两个意思。
        const float budgetH = 44.0f + 20.0f + (hasReason ? 30.0f : 0.0f) + 4.0f * 20.0f + 28.0f;
        const Rect budget{rc.min.x, ry, rc.max.x, ry + budgetH};
        Rect budgetBody = Card(cdraw, budget, "流水线停止规则", "list", false, false);
        ry = budget.max.y + kGap;
        float by = budgetBody.min.y;
        StatusDot(cdraw, ImVec2(budgetBody.min.x + 4.0f, by + 6.0f),
                  decision.stop ? theme::Tone::Warn : theme::Tone::Ok, false);
        cdraw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(budgetBody.min.x + 16.0f, by),
                       decision.stop ? ColorOf(theme::Current().statusWarn)
                                     : ColorOf(theme::Current().statusOk),
                       verdict.data(), verdict.data() + verdict.size());
        by += 20.0f;
        if (hasReason) {
            DrawTextClipped(cdraw, FontAt(11.5f), 11.5f, ImVec2(budgetBody.min.x, by),
                            budgetBody.width(), ColorTextMuted(), decision.reason, true);
            by += 30.0f;
        }
        // 四个上限就是 Budget 的四个字段（`src/pipeline/Budget.h`），顺序与旧
        // StopReportView 一致。刻意不编 S 编号 —— 上面那张卡已经把 S1–S12 占给真规则了。
        const std::vector<std::pair<std::string, std::string>> thresholds = {
            {"LLM 调用", std::to_string(s.budget.llm_calls) + " / " +
                             std::to_string(s.budget.max_llm_calls)},
            {"高档调用", std::to_string(s.budget.high_quality_calls) + " / " +
                              std::to_string(s.budget.max_high_quality_calls)},
            {"镜头", std::to_string(s.budget.shots) + " / " + std::to_string(s.budget.max_shots)},
            {"成本", Money(s.budget.cost) + " / " + Money(s.budget.max_cost)},
        };
        for (const auto& [code, name] : thresholds) {
            StatusDot(cdraw, ImVec2(budgetBody.min.x + 4.0f, by + 6.0f),
                      overBudget ? theme::Tone::Warn : theme::Tone::Idle, false);
            DrawTextClipped(cdraw, FontAt(12.5f), 12.5f, ImVec2(budgetBody.min.x + 16.0f, by),
                            budgetBody.width() - 16.0f, ColorTextSecondary(), code);
            DrawTextClipped(cdraw, FontAt(12.0f), 12.0f, ImVec2(budgetBody.min.x + 90.0f, by),
                            budgetBody.width() - 90.0f, ColorText(), name);
            by += 20.0f;
        }
        DrawTextClipped(
            cdraw, FontAt(11.0f), 11.0f, ImVec2(budgetBody.min.x, by + 4.0f), budgetBody.width(),
            ColorTextMuted(),
            std::string("前置 · LLM ") + (LlmConfigured() ? "已配置" : "未配置") + " · ComfyUI " +
                (Settings().comfyBaseUrl.empty() ? "未配置" : "已配置") + " · 交叉复核 " +
                (CrossReviewConfigured() ? "已配置" : "未配置") + " · Scene 未查询 novel.db",
            true);

        // 最近产物（webui Overview.jsx:132-157 整块）
        //
        // ⚠️ 画**全部**条目，不再按「可用高度」砍行 —— 砍行的逻辑本身依赖一个
        //    算错了的空间（见上面的重叠分析），而且砍掉之后用户完全看不出
        //    「还有更多」。现在内容超出由 ScrollRegion 滚。
        const int artRows = static_cast<int>(s.entries.size());
        const float artH =
            44.0f + 16.0f + (artRows > 0 ? 32.0f * static_cast<float>(artRows) + 4.0f : 24.0f);
        const Rect art{rc.min.x, ry, rc.max.x, ry + artH};
        Rect artBody = Card(cdraw, art, "最近产物", "folder", false, false);
        ry = art.max.y + kGap;
        ButtonSpec viewSpec;
        viewSpec.variant = ButtonVariant::Ghost;
        viewSpec.size = ButtonSize::Small;
        viewSpec.disabled = s.entries.empty();
        if (Button(cdraw, RectAt(art.max.x - 60.0f, art.min.y + 10.0f, 44.0f, 24.0f), "查看", viewSpec,
                   "ov-view")) {
            const std::string err = util::ShellOpen(util::PathFromUtf8(s.entries.back().output_path));
            if (!err.empty()) {
                log::Error("总控：打开产物失败 {}", err);
            }
        }
        if (artRows == 0) {
            DrawTextClipped(cdraw, FontAt(12.0f), 12.0f, ImVec2(artBody.min.x, artBody.min.y),
                            artBody.width(), ColorTextMuted(),
                            s.bound ? "还没有落盘产物。" : "未绑定工程根。", true);
        } else {
            float ay = artBody.min.y;
            for (std::size_t i = 0; i < s.entries.size(); ++i) {
                const auto& entry = s.entries[i];
                const std::filesystem::path path = util::PathFromUtf8(entry.output_path);
                const Rect row{artBody.min.x - 4.0f, ay, artBody.max.x, ay + 32.0f};
                // ⚠️ 一次 HitTest 拿 hovered + clicked：先 Hovered(idA) 再 Clicked(idB)
                // 会在同一矩形上叠两个 InvisibleButton，ImGui 只让先注册的那个拿到
                // HoveredId，第二个永远 clicked=false —— 这一行点不开。
                const Hit hit = HitTest(row, "ov-art-" + std::to_string(i));
                if (hit.hovered) {
                    DrawRoundRect(cdraw, row.min, row.max, 6.0f, ColorFillHover());
                }
                const std::string name = util::PathToUtf8(path.filename());
                DrawTextClipped(cdraw, FontBoldAt(12.0f), 12.0f, ImVec2(row.min.x + 6.0f, ay + 3.0f),
                                row.width() - 26.0f, ColorText(), name);
                std::string sub = pipeline::StageCode(entry.stage);
                if (!entry.degradation.empty()) {
                    sub += " · 降级 " + entry.degradation;
                }
                DrawTextClipped(cdraw, FontAt(11.0f), 11.0f, ImVec2(row.min.x + 6.0f, ay + 18.0f),
                                row.width() - 26.0f, ColorTextMuted(), sub);
                DrawIcon(cdraw, "chevron", ImVec2(row.max.x - 16.0f, ay + 10.0f), 12.0f,
                         ColorTextMuted());
                if (hit.clicked) {
                    const std::string err = util::ShellOpen(path);
                    if (!err.empty()) {
                        log::Error("总控：打开产物失败 {}", err);
                    }
                }
                ay += 36.0f;
            }
        }

        // 运行信息：Overview.jsx:160-167 的四行（当前项目 / 数据目录 / 检查点 / 预算余量）
        const float infoH = 44.0f + 4.0f * 20.0f + 16.0f;
        const Rect info{rc.min.x, ry, rc.max.x, ry + infoH};
        Rect infoBody = Card(cdraw, info, "运行信息", "info", false, false);
        ry = info.max.y + kGap;
        char chapterText[16];
        std::snprintf(chapterText, sizeof(chapterText), "ch%03d", s.chapter < 0 ? 0 : s.chapter);
        KeyValues(cdraw, infoBody,
                  {{"当前项目", s.root.empty() ? std::string("未打开项目")
                                              : util::PathToUtf8(s.root.filename())},
                   {"数据目录", s.root.empty() ? std::string("—") : util::PathToUtf8(s.root)},
                   {"检查点", s.chapter < 0 ? std::string("无") : std::string(chapterText)},
                   {"预算余量", Money(s.budget.max_cost - s.budget.cost) + " / " +
                                    Money(s.budget.max_cost)}});

        // ---- 章节 × V 阶段矩阵：行 = 章，列 = V1–V7 ----
        //
        // ⚠️ 这个维度**在数据层一直就有**：表 `stage_artifacts` 带 `chapter_id` + `stage`
        //    + `scene_ord/shot_ord` + `created`，只读接口 `ListStageArtifacts(chapterId,
        //    stage)` 也在（`src/novel/NovelVisual.h:242-243`）。早先把「章节 × 阶段」
        //    整个记成缺口（「章节维度在数据层根本不存在」）是**只看了 T 链**得出的结论
        //    —— T 链的 `LedgerEntry` / `work/T1.json` 确实不带章节，但**V 链带**。
        //    所以左边那张 T 链甘特没有章节轴是事实，这里这张有。
        //
        // 上一轮这张卡渲染过又删掉了：当时右栏是「各自 max(下限, 剩余)」的重叠布局，
        // 塞不进去。改成 ScrollRegion + 内容真实高度之后**容量不再是硬约束**，
        // 高度按行数直接算，超出由滚动承载。
        const pages::BookSideView& book = pages::BookSide();
        if (book.bound && !book.chapters.empty()) {
            const int rows = static_cast<int>(book.chapters.size());
            // 高度按内容**逐段**算，不写死一个数：
            //   Card 带标题时 body.min.y = min.y+44、body.max.y = max.y-16
            //   （Widgets.cpp:416），所以卡高 = 44(头) + bodyH + 16(底 pad)；
            //   bodyH = 16(列头留白) + 22*行数 + 6(行后间隙) + 16(脚注)。
            //
            // ⚠️ 早先这里写的是 `44 + 16 + 22*rows + 16` —— 脚注没有自己的高度，
            //    于是 `matBody.max.y - 12` 落在**最后一行里面**（行底 = +16+22*rows，
            //    脚注顶 = +22*rows+4），脚注压在最后一行上。**表面上矩阵是对的**，
            //    只有截图放大看才发现最后一行被文字糊住。
            constexpr float kMatBodyTopPad = 16.0f;
            constexpr float kMatRowH = 22.0f;
            constexpr float kMatFootGap = 6.0f;
            constexpr float kMatFootH = 16.0f;
            constexpr float kMatBottomPad = 16.0f;
            const float matBodyH = kMatBodyTopPad + kMatRowH * static_cast<float>(rows) +
                                   kMatFootGap + kMatFootH;
            const float matH = 44.0f + matBodyH + kMatBottomPad;
            const Rect mat{rc.min.x, ry, rc.max.x, ry + matH};
            Rect matBody = Card(cdraw, mat, "章节 × V 阶段", "grid", false, false);
            const float labelW = 40.0f;
            const float cell = (matBody.width() - labelW) / 7.0f;
            for (int v = 0; v < 7; ++v) {
                const std::string head = "V" + std::to_string(v + 1);
                cdraw->AddText(FontBoldAt(9.5f), 9.5f,
                              ImVec2(matBody.min.x + labelW + cell * v + 2.0f, matBody.min.y),
                              ColorTextMuted(), head.data(), head.data() + head.size());
            }
            float my = matBody.min.y + kMatBodyTopPad;
            for (int i = 0; i < rows; ++i) {
                const pages::BookChapterView& ch = book.chapters[static_cast<std::size_t>(i)];
                const std::string ord = "第" + std::to_string(ch.ord);
                cdraw->AddText(MonoAt(9.5f), 9.5f, ImVec2(matBody.min.x, my + 4.0f),
                              ColorTextSecondary(), ord.data(), ord.data() + ord.size());
                for (int v = 0; v < 7; ++v) {
                    // 下标与 chapters 对齐；长度不足（快照还没回来）时按「没跑过」处理。
                    const int count =
                        i < static_cast<int>(book.chapterVStages.size())
                            ? book.chapterVStages[static_cast<std::size_t>(i)]
                                  [static_cast<std::size_t>(v)]
                            : 0;
                    const Rect cell0{matBody.min.x + labelW + cell * v + 1.5f, my,
                                     matBody.min.x + labelW + cell * (v + 1) - 1.5f, my + 17.0f};
                    DrawRoundRect(cdraw, cell0.min, cell0.max, 3.0f,
                                  count > 0 ? ColorOf(theme::CurrentDerived()
                                                          .ganttCell[static_cast<int>(theme::Tone::Ok)])
                                            : ColorFillMuted());
                    if (count > 0) {
                        // 居中用实测宽度，不写死字符数偏移 —— 数字会从 1 涨到两位数，
                        // 固定偏移会让两位数整体偏右。
                        const std::string n = std::to_string(count);
                        ImFont* nf = MonoAt(9.0f);
                        const float nw = nf->CalcTextSizeA(9.0f, 1e9f, 0.0f, n.data(),
                                                           n.data() + n.size())
                                             .x;
                        cdraw->AddText(nf, 9.0f,
                                      ImVec2(cell0.min.x + (cell0.max.x - cell0.min.x - nw) * 0.5f,
                                             cell0.min.y + 4.0f),
                                      ColorText(), n.data(), n.data() + n.size());
                    }
                }
                my += kMatRowH;
            }
            // 脚注位置由同一组常量推出来，不是 `max.y - 12` 之类的手写锚点。
            const float footY = my + kMatFootGap; // my 此时已是最后一行的底
            DrawTextClipped(cdraw, FontAt(10.5f), 10.5f,
                            ImVec2(matBody.min.x, footY), matBody.width(), ColorTextMuted(),
                            "格内数字 = 该章在该阶段的产物条数（ListStageArtifacts）。");
            // 推进 ry —— ScrollRegion 的可滚高度靠最后一次上报的内容底算出来，
            // 忘了推就等于这张卡「不在内容里」，滚到底也拍不到（与「有内容但没被拍到」同族）。
            ry = mat.max.y + kGap;
        }
        // 把累加出来的内容高度报给 ScrollRegion —— 没有这一行，右栏就**滚不动**
        // （ScrollMaxY 恒为 0），四张卡里视口以下的三张永远够不着。上面「溢出交给
        // ScrollRegion 滚动」这句话指的就是这一行。
        rightScroll.setContentHeight(ry - rc.min.y);
        rightBottomUsed = ry; // 供下面自报页面内容高度用（绝对屏幕 y）
    }

    // ---- 自报本页真实内容高度 ----
    //
    // Shell 给的布局区是写死的 2400px（本页在 2400 里排版），但 2400 不是本页的
    // 真实高度。真实高度 = 左右两列**实际画到的最底**，多给一个 kGap 让最后一张卡
    // 离页面底有点余量。Shell 拿它当工作区滚动区高度，滚动范围从此跟着内容走。
    //
    // ⚠️ 取 `max(左, 右)` 而不是右列的 `ry`：左列的账本卡通常比右栏**更深**，
    //    只报右列会让工作区把左列的账本卡底裁掉（外层 child 按上报的高度裁）。
    pages::SetPageContentHeight(std::max(std::max(ledger.max.y, s12.max.y), rightBottomUsed) -
                                area.min.y + kGap);
}

// ================================================================ P5.2 小说 / P5.3 资产
//
// 这两页的实现已**搬到 WorkspaceB.cpp**：它们要读 novel.db，而开库 / 查询 /
// worker 回投那一整套（BookState）都在 B 的匿名命名空间里。两页的真数据实现见
// WorkspaceB.cpp 的 NovelPage::Draw / AssetsPage::Draw —— 旧版这里的实现每个数字
// 都是写死的（"第 3 章 · 雨夜"、字数 2180、差异 0.2418、12 张假资产卡）。

} // namespace shine::pages
