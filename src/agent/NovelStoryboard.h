#pragma once
// S23（`11` §2.2 的 **V9 `STORYBOARD`** / `12` 卷）：**叙事分镜的产出** ——
// `NarrativeShot[]`（契约 `02` §2.7）→ `shots` 表（`01` §2.6）。
//
// 边界（照规格，不越界）：
//   · V9 落的是"这一镜该怎么拍"；**不产生成 Prompt** —— 那是 V10 `PROMPT_GEN`（`02` §2.10）
//   · V8 `CONTINUITY` 是**纯机器校验**（`12` §2.7 / `06` K09）：本模块**不复刻**判定，
//     只把 `start_state`/`end_state`/`timeline` 按契约落到对应列，让 K09/K22/K24 有受检对象
//   · V1–V8（SCENE_BREAKDOWN → DIRECTOR_INTENT → … → AUDIO）在本实现里**合并为一次推演**
//     （与 `03` §2.2 允许 T2–T9 合并同理）；逐步展开留后续
//   · **无专列的契约字段**（`transition`/`performance`/`spatial`/`camera`/`audio`）**只存盘**
//     （`work/ch<NNN>/storyboard.json` 存完整契约），不硬塞进语义不符的列（在 `warnings` 里说明）
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "agent/NovelDirector.h" // LlmCallFn / AgentError / LlmRole
#include "db/sqlite/SqliteDb.h"

namespace shine::agent {

struct StoryboardRequest {
    std::int64_t chapter_id = 0;
    std::filesystem::path project_dir; // 完整契约落 `work/ch<NNN>/storyboard.json`
    std::string extra_hint;            // 可选：追加给模型的导向（如"多用手持镜头"）
};

struct StoryboardOutcome {
    bool ok = false;
    std::string error;
    int shots_written = 0;
    int scenes_covered = 0;
    int llm_calls = 0;
    std::vector<std::string> warnings; // 落不了的字段 / 丢掉的镜（**显式列出，不静默**）
    [[nodiscard]] std::string Describe() const;
};

// 产出并落库。`call` 由调用方注入（与 `NovelDirector` 同款纪律：本层不做 HTTP）。
[[nodiscard]] std::expected<StoryboardOutcome, AgentError>
GenerateStoryboard(::shine::db::sqlite::Database& db, const LlmCallFn& call,
                   const StoryboardRequest& req);

// 离线自检：mock LLM 产 `NarrativeShot[]` → 断言落库字段/归属/幂等/落盘
[[nodiscard]] bool RunStoryboardSelfCheck();

} // namespace shine::agent
