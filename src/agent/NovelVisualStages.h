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
