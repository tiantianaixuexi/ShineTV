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
    const Rect left{content.min.x, gridTop, content.max.x - rightW - kGap, content.max.y};
    const Rect rightCol{left.max.x + kGap, gridTop, content.max.x, content.max.y};

    // ---- 甘特：行 = T 链阶段，列 = 链上 8 等分桶，表头是那一桶的**真实阶段码** ----
    // 旧版是 `(i*3)%8` 的假跨度 + 只有"在/不在跨度"两态。现在三态都来自真实状态：
    // 账本里有 → done(ok)，正在跑 → run(accent)，被规则卡住 → stop(danger)，其余 todo。
    constexpr int kGanttCols = 8;
    // 卡高 = 卡头 44 + 列头 20 + 行 22×N + 脚注 16 + 卡脚 16。窄窗口先砍行数。
    const float ledgerMinH = 130.0f;
    int ganttRows = std::min(totalStages, kGanttCols);
    while (ganttRows > 1 &&
           96.0f + 22.0f * static_cast<float>(ganttRows) > left.height() - kGap - ledgerMinH) {
        --ganttRows;
    }
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
    const Rect ledger{left.min.x, gantt.max.y + kGap, left.max.x, left.max.y};
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
        const int maxRows =
            std::max(1, static_cast<int>((ledgerBody.height() - 34.0f) / rowH));
        const int shown = std::min(static_cast<int>(s.entries.size()), maxRows);
        const int first = static_cast<int>(s.entries.size()) - shown;
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
        if (static_cast<int>(s.entries.size()) > shown) {
            DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                            ImVec2(ledgerBody.min.x, ledgerBody.max.y - 24.0f), ledgerBody.width(),
                            ColorTextMuted(),
                            "共 " + std::to_string(static_cast<int>(s.entries.size())) + " 条，显示最后 " +
                                std::to_string(shown) + " 条");
        }
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(ledgerBody.min.x, ledgerBody.max.y - 8.0f), ledgerBody.width(),
                        ColorTextMuted(),
                        "阶段产物 " + std::to_string(static_cast<int>(s.entries.size())) +
                            " · LLM 调用 " + std::to_string(s.budget.llm_calls) + " · 高档 " +
                            std::to_string(s.budget.high_quality_calls) + " · 镜头 " +
                            std::to_string(s.budget.shots) + " · 估算成本 " + Money(s.budget.cost));
    }

    // ---- 右列：停止条件 / 最近产物 / 运行信息 ----
    // 三张卡按可用高度分配，窄窗口时先砍「最近产物」的行数，不让它们压出内容区。
    const float infoH = 120.0f;
    const float stopMin = 220.0f;  // 判定 20 + 原因两行 30 + S1-S4 80 + 脚注两行 28 + 卡头卡脚
    const float artMin = 78.0f;
    int artRows = static_cast<int>(std::min<std::size_t>(s.entries.size(), 4));
    const float artRoom = rightCol.height() - infoH - stopMin - 2.0f * kGap;
    while (artRows > 0 && artMin + 34.0f * static_cast<float>(artRows) > artRoom) {
        --artRows;
    }
    const float artH = s.entries.empty() ? artMin : artMin + 34.0f * static_cast<float>(artRows);
    const float stopH = std::max(stopMin, rightCol.height() - infoH - artH - 2.0f * kGap);

    // 停止条件：真实判定 + S1–S4 的真实阈值。
    // ⚠️ 设计稿的「S1–S12」清单来自 mock.js；src/pipeline/StopPolicy 只判
    // 「S1–S4 预算」/S5/S6/S7/S8 五组，且**没有可枚举的规则表接口**
    // （与 Qt 版 src/ui/pages/pipeline/StopReportView.h 同一个结论）。
    // 所以这里不硬凑 12 行：画真实判定 + 真实阈值 + 一行前置状态。
    const Rect stop{rightCol.min.x, rightCol.min.y, rightCol.max.x, rightCol.min.y + stopH};
    Rect stopBody = Card(draw, stop, "停止条件 · S1–S8", "alert", false, false);
    const pipeline::StopDecision decision = EvaluateStopPolicy(s.budget);
    float sy = stopBody.min.y;
    StatusDot(draw, ImVec2(stopBody.min.x + 4.0f, sy + 6.0f),
              decision.stop ? theme::Tone::Warn : theme::Tone::Ok, false);
    const std::string verdict = decision.stop ? decision.rule : std::string("未触发任何停止条件");
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(stopBody.min.x + 16.0f, sy),
                  decision.stop ? ColorOf(theme::Current().statusWarn) : ColorOf(theme::Current().statusOk),
                  verdict.data(), verdict.data() + verdict.size());
    sy += 20.0f;
    if (decision.stop && !decision.reason.empty()) {
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(stopBody.min.x, sy), stopBody.width(),
                        ColorTextMuted(), decision.reason, true);
        sy += 30.0f;
    }
    // S1–S4 就是 Budget 的四个上限，顺序与 StopReportView 一致
    const std::vector<std::pair<std::string, std::string>> thresholds = {
        {"S1", "LLM 调用 " + std::to_string(s.budget.llm_calls) + " / " +
                   std::to_string(s.budget.max_llm_calls)},
        {"S2", "高档 " + std::to_string(s.budget.high_quality_calls) + " / " +
                   std::to_string(s.budget.max_high_quality_calls)},
        {"S3", "镜头 " + std::to_string(s.budget.shots) + " / " + std::to_string(s.budget.max_shots)},
        {"S4", "成本 " + Money(s.budget.cost) + " / " + Money(s.budget.max_cost)},
    };
    for (const auto& [code, name] : thresholds) {
        StatusDot(draw, ImVec2(stopBody.min.x + 4.0f, sy + 6.0f),
                  overBudget ? theme::Tone::Warn : theme::Tone::Idle, false);
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(stopBody.min.x + 16.0f, sy + 1.0f), ColorTextMuted(),
                      code.data(), code.data() + code.size());
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, ImVec2(stopBody.min.x + 48.0f, sy),
                        stopBody.width() - 48.0f, ColorTextSecondary(), name);
        sy += 20.0f;
    }
    DrawTextClipped(
        draw, FontAt(11.0f), 11.0f, ImVec2(stopBody.min.x, sy + 4.0f), stopBody.width(),
        ColorTextMuted(),
        std::string("S5 LLM ") + (LlmConfigured() ? "已配置" : "未配置") + " · S6 ComfyUI " +
            (Settings().comfyBaseUrl.empty() ? "未配置" : "已配置") + " · S7 复核 " +
            (CrossReviewConfigured() ? "已配置" : "未配置") + " · S8 Scene 未查询 novel.db",
        true);

    // 最近产物（webui Overview.jsx:132-157 整块，之前 ImGui 侧缺失）
    const Rect art{rightCol.min.x, stop.max.y + kGap, rightCol.max.x, stop.max.y + kGap + artH};
    Rect artBody = Card(draw, art, "最近产物", "folder", false, false);
    ButtonSpec viewSpec;
    viewSpec.variant = ButtonVariant::Ghost;
    viewSpec.size = ButtonSize::Small;
    viewSpec.disabled = s.entries.empty();
    if (Button(draw, RectAt(art.max.x - 60.0f, art.min.y + 10.0f, 44.0f, 24.0f), "查看", viewSpec,
               "ov-view")) {
        const std::string err = util::ShellOpen(util::PathFromUtf8(s.entries.back().output_path));
        if (!err.empty()) {
            log::Error("总控：打开产物失败 {}", err);
        }
    }
    if (s.entries.empty()) {
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(artBody.min.x, artBody.min.y),
                        artBody.width(), ColorTextMuted(),
                        s.bound ? "还没有落盘产物。" : "未绑定工程根。", true);
    } else {
        float ay = artBody.min.y;
        for (std::size_t i = s.entries.size() - static_cast<std::size_t>(artRows);
             i < s.entries.size(); ++i) {
            const auto& entry = s.entries[i];
            const std::filesystem::path path = util::PathFromUtf8(entry.output_path);
            const Rect row{artBody.min.x - 4.0f, ay, artBody.max.x, ay + 32.0f};
            // ⚠️ 一次 HitTest 拿 hovered + clicked：先 Hovered(idA) 再 Clicked(idB)
            // 会在同一矩形上叠两个 InvisibleButton，ImGui 只让先注册的那个拿到
            // HoveredId，第二个永远 clicked=false —— 这一行点不开。
            const Hit hit = HitTest(row, "ov-art-" + std::to_string(i));
            if (hit.hovered) {
                DrawRoundRect(draw, row.min, row.max, 6.0f, ColorFillHover());
            }
            const std::string name = util::PathToUtf8(path.filename());
            DrawTextClipped(draw, FontBoldAt(12.0f), 12.0f, ImVec2(row.min.x + 6.0f, ay + 3.0f),
                            row.width() - 26.0f, ColorText(), name);
            std::string sub = pipeline::StageCode(entry.stage);
            if (!entry.degradation.empty()) {
                sub += " · 降级 " + entry.degradation;
            }
            DrawTextClipped(draw, FontAt(11.0f), 11.0f, ImVec2(row.min.x + 6.0f, ay + 18.0f),
                            row.width() - 26.0f, ColorTextMuted(), sub);
            DrawIcon(draw, "chevron", ImVec2(row.max.x - 16.0f, ay + 10.0f), 12.0f, ColorTextMuted());
            if (hit.clicked) {
                const std::string err = util::ShellOpen(path);
                if (!err.empty()) {
                    log::Error("总控：打开产物失败 {}", err);
                }
            }
            ay += 34.0f;
        }
    }

    // 运行信息：Overview.jsx:160-167 的四行（当前项目 / 数据目录 / 检查点 / 预算余量）
    const Rect info{rightCol.min.x, rightCol.max.y - infoH, rightCol.max.x, rightCol.max.y};
    Rect infoBody = Card(draw, info, "运行信息", "info", false, false);
    char chapterText[16];
    std::snprintf(chapterText, sizeof(chapterText), "ch%03d", s.chapter < 0 ? 0 : s.chapter);
    KeyValues(draw, infoBody,
              {{"当前项目", s.root.empty() ? std::string("未打开项目")
                                          : util::PathToUtf8(s.root.filename())},
               {"数据目录", s.root.empty() ? std::string("—") : util::PathToUtf8(s.root)},
               {"检查点", s.chapter < 0 ? std::string("无") : std::string(chapterText)},
               {"预算余量", Money(s.budget.max_cost - s.budget.cost) + " / " +
                                Money(s.budget.max_cost)}});
}

// ================================================================ P5.2 小说 / P5.3 资产
//
// 这两页的实现已**搬到 WorkspaceB.cpp**：它们要读 novel.db，而开库 / 查询 /
// worker 回投那一整套（BookState）都在 B 的匿名命名空间里。两页的真数据实现见
// WorkspaceB.cpp 的 NovelPage::Draw / AssetsPage::Draw —— 旧版这里的实现每个数字
// 都是写死的（"第 3 章 · 雨夜"、字数 2180、差异 0.2418、12 张假资产卡）。

} // namespace shine::pages
