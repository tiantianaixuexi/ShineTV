#pragma once
// 影视化链的**阶段化**实现（`03` §2.2 的 V1–V7）。
//
// 现状：V9 `STORYBOARD` 把 V1–V8 **一次 LLM 推演**（`03` §2.2 明确允许合并）。本模块把阶段
// **逐个独立**：每阶段一个 prompt、一份产物（`work/ch<NNN>/vNN_*.json`）、一个
// `input_state_hash`（阶段级续跑，`03` §2.7 的 P1/P2）。
//
// 顺序（`03` §2.2），做完一个接一个：
//   **V1 `SCENE_BREAKDOWN` ✅（本文件）** → V2 `DIRECTOR_INTENT` → V3 `PERFORMANCE` →
//   V4 `SPATIAL` → V5 `CAMERA` → V6 `TIMELINE` → V7 `AUDIO`
//   （V8 `CONTINUITY` 已独立：`novel/NovelContinuity.*`，**纯机器校验**）
//
// ⚠️ **V1 的产物立刻被消费**（否则就是没人用的中间文件）：`NovelStoryboard`（V9）会读它的
// **镜骨架**（每场几镜 / 每镜时长与一句话概要）并下发给自己的 prompt ⇒ "**镜的切分**"这一步
// 从 V9 里**提前到 V1**。
#include "agent/NovelDirector.h" // LlmCallFn / LlmRole / AgentError
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelTypes.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::agent {

struct SceneBreakdownRequest {
    novelcore::RowId chapter_id = 0;
    std::string project_dir; // 产物落 `work/ch<NNN>/v01_scene_breakdown.json`
    std::string extra_hint;
};

struct SceneBreakdownOutcome {
    bool ok = false;
    std::string error;
    bool reused = false;   // `input_state_hash` 一致 → 复用产物（**不重调 LLM**）
    int scenes = 0;
    int shots_planned = 0; // 骨架里的镜数合计
    int llm_calls = 0;
    std::string artifact_path;
    std::vector<std::string> warnings; // 七要素缺失 / 冲突为空等（**显式列出，不静默**）
    [[nodiscard]] std::string Describe() const;
};

// V1 `SCENE_BREAKDOWN`：`12` §2.2 的**七要素**（SceneGoal / CharacterGoal / Conflict / Emotion /
// Information / Environment / ImportantProps）+ **镜骨架**（该场切成几镜、每镜时长与概要）。
[[nodiscard]] std::expected<SceneBreakdownOutcome, AgentError>
RunSceneBreakdown(::shine::db::sqlite::Database& db, const LlmCallFn& call,
                  const SceneBreakdownRequest& req);

// ———— V2–V7：**通用阶段执行器**（`03` §2.2 的其余六个阶段）————
// 这六个阶段是**同构**的：读上一阶段的产物 → 一次 LLM（`Planner`，中档）→ 解析 `items[]` →
// 落 `work/ch<NNN>/vNN_<name>.json`。所以用**一张规格表 + 一个执行器**，而不是抄六遍。
enum class VisualStageId {
    V1SceneBreakdown,
    V2DirectorIntent,
    V3Performance,
    V4Spatial,
    V5Camera,
    V6Timeline,
    V7Audio,
};
[[nodiscard]] std::string_view VisualStageCode(VisualStageId stage) noexcept; // "V1"…"V7"
[[nodiscard]] std::string_view VisualStageName(VisualStageId stage) noexcept; // "SCENE_BREAKDOWN"…

struct StageRequest {
    novelcore::RowId chapter_id = 0;
    std::string project_dir;
    VisualStageId stage = VisualStageId::V2DirectorIntent;
    std::string extra_hint;
    // S41：**走 Agent 工具循环**（多轮 + MCP 工具）而不是"单轮把上下文塞满"。
    // 目前只在 **V4 `SPATIAL`** 上试点（`kSpecs` 里的 `v4_spatial`，白名单只给只读查询工具）。
    bool use_agent_tools = false;
};

struct StageOutcome {
    bool ok = false;
    std::string error;
    bool reused = false; // 链式哈希一致 → 复用产物（**不重调 LLM**）
    int items = 0;       // 产出的条目数（每镜/每场一条）
    int llm_calls = 0;
    std::string artifact_path;
    std::vector<std::string> warnings;
    [[nodiscard]] std::string Describe() const;
};

// 跑**一个**阶段（V2–V7）。上游产物不存在 → 明确报错（提示先跑上一阶段）。
// ⚠️ **链式哈希**：本阶段的 `input_state_hash` = 本阶段库状态哈希 ⊕ **上游产物的哈希** ——
// 于是"上游变了"必然让下游失效重算（否则改了 V4，V5 还拿着旧结果复用）。
[[nodiscard]] std::expected<StageOutcome, AgentError>
RunVisualStage(::shine::db::sqlite::Database& db, const LlmCallFn& call,
               const StageRequest& req);

struct StagesOutcome {
    bool ok = false;
    std::string error;
    std::string detail; // 每阶段一行
    int stages_run = 0;
    int stages_reused = 0;
    int llm_calls = 0;
    [[nodiscard]] std::string Describe() const;
};

// 一次跑 **V1 → V7**（各自哈希复用会自动跳过已跑过的）。`--novel-stages` 用的就是它。
// `up_to`（S35）：**只跑到某阶段**就停 —— 调试用。比如只想验 V4 的 `layers` / 只想让 V10 有
// 空间层可读，就没必要烧 V5–V7 的 LLM 调用（`--novel-stages 1 --up-to V4`）。默认 V7 = 跑全程。
[[nodiscard]] std::expected<StagesOutcome, AgentError>
RunAllVisualStages(::shine::db::sqlite::Database& db, const LlmCallFn& call,
                   novelcore::RowId chapter_id, std::string_view project_dir,
                   std::string_view extra_hint = {},
                   VisualStageId up_to = VisualStageId::V7Audio, bool use_agent_tools = false);

// 该阶段的 **system 提示词**（S42：**唯一来源**）。
// 为什么要有它：V4 走 Agent（`AgentKit::DefaultPromptFor("v4_spatial")`）时，原先**又抄了一份**
// 提示词 ⇒ 两处必须同步，迟早分叉。现在 `AgentKit` 直接取这里，改提示词只改一处。
[[nodiscard]] std::string_view StageSystemPrompt(VisualStageId stage) noexcept;

// `--up-to` 的解析（S35）：接受 `V1`…`V7` / `1`…`7`（大小写不敏感）。非法 → `nullopt`（调用方报错）。
[[nodiscard]] std::optional<VisualStageId> ParseVisualStage(std::string_view text) noexcept;

// V1 产物里的**镜骨架**（`(scene_ord, ord) → {duration, beat}`）。读不到 → 空。
// `NovelStoryboard`（V9）用它把"镜的切分"接过来；也用来在 `storyboard.json` 缺 `duration` 时兜底。
struct ShotSkeleton {
    int scene_ord = 0;
    int ord = 0;
    double duration = 0.0;
    std::string beat;
};
[[nodiscard]] std::vector<ShotSkeleton> LoadShotSkeleton(std::string_view project_dir,
                                                         int chapter_ord);

// 离线自检（mock LLM，不发网络）：V1 产物落盘 → 骨架可读回 → 同哈希复用 → 缺 conflict 有告警
[[nodiscard]] bool RunStagesSelfCheck();

} // namespace shine::agent
