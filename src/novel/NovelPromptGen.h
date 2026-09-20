#pragma once
// S24（`11` §2.2 的 **V10 `PROMPT_GEN`** / `13` §2.7）：把「九层组装」的结果写成
// **`PromptArtifact` 账**（契约 `02` §2.10）。
//
// 先查后写（这是本步最重要的一条）：**组装本身早就存在** —— `NovelVisual::Assemble`
// 已经按九层（base → stage → scene → action → camera → composition → lighting → style → quality）
// 拼出 `final_prompt` + `negative_prompt`，`prompt_layers` 也在（`SetLayer`/`QueryLayer`）。
// **V10 缺的只是"账"**：`prompt_artifacts` 里没有 V10 的产物 ⇒ K23 没有受检对象、
// V11 的桥（`ToGenShot`）拿不到 `references` ⇒ 影视化链在 V10 断掉。
//
// 照 `13` §2.7 的 PV1–PV6 与不变式 I9：
//   · PV1 每条产物必须带 `input_state_hash`（`04` §2.5，chain=visual / stage=V10）
//   · PV2/I9 哈希不一致 → **不得复用**，重新生成
//   · ⚠️ 表里**没有** `version` / `generation_ref` 两列（PV3–PV5 的载体）→ 版本号记在
//     `model_hint`（`v=N layers=M`）里，**如实记账**，等真需要版本历史时再加列
#include <string>
#include <vector>

#include "db/sqlite/SqliteDb.h"
#include "novel/NovelTypes.h" // RowId

namespace shine::novelcore {

struct PromptGenOutcome {
    bool ok = false;
    std::string error;
    int shots_seen = 0;
    int artifacts_written = 0;
    int reused = 0; // 哈希一致 → 复用（PV2 的另一面：一致才允许跳过）
    int refs_resolved = 0;
    std::vector<std::string> warnings; // 跳过原因 / 一致性 issue（**显式列出，不静默**）
    [[nodiscard]] std::string Describe() const;
};

// 为本章每一镜产出 `PromptArtifact`（`chain=visual` / `stage=V10` / `target_kind=shot`）。
// `project_dir`：用来读该章的**空间层**（`work/ch<NNN>/storyboard.json` 的 `spatial`，`12` §2.5 ——
// V9 只存盘、库里无专列）；**空则跳过空间层**（不阻断，向后兼容）。
[[nodiscard]] PromptGenOutcome GeneratePromptArtifacts(::shine::db::sqlite::Database& db,
                                                       RowId chapter_id,
                                                       std::string_view project_dir = {});

// 离线自检：九层组装 → 账 + 参考图解析 + PV2 复用 + 空 prompt 的处理
[[nodiscard]] bool RunPromptGenSelfCheck();

} // namespace shine::novelcore
