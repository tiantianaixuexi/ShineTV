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
    // S33：**V1 骨架一致性**（把"软约束"变成"**可检测的**软约束"）。
    // `skeleton_total == 0` = 没跑过 `--novel-stages`（没骨架可下、无从比对）。
    // ⚠️ 偏离**只记账、不 fail** —— LLM 合并/拆镜有时是合理的（`03` §2.2 本就允许合并），
    //    但必须**看得见**（`11` §2.7 W2：不确定/偏离也要可见）。
    int skeleton_total = 0;             // 下发的骨架镜数
    int skeleton_missing = 0;           // 骨架里有、V9 输出里没有
    int skeleton_extra = 0;             // V9 输出里有、骨架里没有
    int skeleton_duration_mismatch = 0; // 同一 (scene_ord, ord) 的 duration 差 > 0.2s
    // S34：V3–V7 的**关键字段未被采纳**的处数（V4 前景 / V5 景别 / V3 表情 / V7 环境音）。
    // 比骨架更细一层：骨架验"镜数与时长"，这里验"**上游的设计有没有真的进分镜**"。
    // ⚠️ 只挑每阶段一个有代表性的字段（全字段比对会被 LLM 的措辞差异淹没）；V2 仍无承载字段。
    int stage_mismatch = 0;
    std::vector<std::string> warnings; // 落不了的字段 / 丢掉的镜 / 骨架偏离（**显式列出，不静默**）
    [[nodiscard]] std::string Describe() const;
};

// 产出并落库。`call` 由调用方注入（与 `NovelDirector` 同款纪律：本层不做 HTTP）。
[[nodiscard]] std::expected<StoryboardOutcome, AgentError>
GenerateStoryboard(::shine::db::sqlite::Database& db, const LlmCallFn& call,
                   const StoryboardRequest& req);

// 离线自检：mock LLM 产 `NarrativeShot[]` → 断言落库字段/归属/幂等/落盘
[[nodiscard]] bool RunStoryboardSelfCheck();

} // namespace shine::agent
