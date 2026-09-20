#pragma once
// V11 `GENERATION`（`03` §2.2：**可选下游动作**，不属于影视化阶段表）——
// 把 V10 产出的 `PromptArtifact` 交给**出图队列**，并把"产物 ↔ 提示词"的关联**双向**记下来。
//
// 三条纪律（`03` §2.2 / `09` §2.4）：
//   1. **不调 LLM**（纯提交出图；`09` §2.4 的模型分层表里 V11 一栏就是"—"）。
//   2. **不做生成评审、无重生成循环**（`02` §2.6 的 `target_type=generation` 已移除）；
//      失败只有"单镜隔离"。
//   3. **单镜失败不阻断其余镜**（`09` §2.2 的隔离），失败的镜留在 `out.shots` 里带 error。
//
// 与**桥**的分工（`11` §2.5 / `video/NovelShotBridge.h`）：提示词 → `VideoProject` 的转换、
// K20/K21 的纠正记账由 `video::ToGenShot` 做（那座桥**不自动出图**）；本模块负责
// 「读账 → 组桥的输入 → 提交出图队列 → 写回关联（`prompt_artifact_id` / `generation_ref`）」。
//
// 落点（PV5「双向可查」）：
//   `prompt_artifacts.generation_ref` = `"va:<visual_artifacts.id>"`  →  `visual_artifacts.prompt_artifact_id`
//   反向 → 该镜的 `prompt_artifacts.id`；再经 `visual_artifacts.job_id` 可查 `generated_images`。
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::novelcore {

struct GenerationShotOutcome {
    RowId shot_id = 0;
    RowId prompt_artifact_id = 0;
    int prompt_version = 0;       // 用的是哪一版 Prompt（PV3 后可能 > 1）
    RowId visual_artifact_id = 0; // 新写的 `visual_artifacts` 行（shot 层；0 = 没写，见 note）
    RowId generated_image_id = 0;
    std::string job_id;
    std::string rel_path;
    bool ok = false;
    std::string error; // ok=false 的中文原因（该镜隔离失败，不影响其余镜）
};

struct GenerationOutcome {
    bool ok = false;
    std::string error; // 全局失败（如"该章没有 PromptArtifact"）
    int shots_seen = 0;
    int images_submitted = 0;
    int skipped_existing = 0; // 该镜已有产物（`generation_ref` 非空）→ 不重复出图
    int failed = 0;
    std::vector<GenerationShotOutcome> shots;
    std::vector<std::string> warnings; // K20/K21 记账 + 跳过原因（**显式列出，不静默**）
    std::string checks_path;           // `work/ch<NNN>/generation_checks.json`（K19–K21 的盘上账）
    [[nodiscard]] std::string Describe() const;
};

// 跑 V11：该章每个**最新版** `PromptArtifact` → 出图一条。
// `project_dir`：用来落 `work/ch<NNN>/generation_checks.json`（K19–K21 的数据源）。
// `force=false`：该镜已有产物（`generation_ref` 非空）就跳过（幂等，不重复烧出图）。
[[nodiscard]] GenerationOutcome RunChapterGeneration(::shine::db::sqlite::Database& db,
                                                     RowId chapter_id,
                                                     std::string_view project_dir = {},
                                                     bool force = false);

// 离线自检：端到端（PromptArtifact → 出图 → `visual_artifacts.prompt_artifact_id` /
// `prompt_artifacts.generation_ref` **双向可查** + 幂等 + 单镜失败不阻断）
[[nodiscard]] bool RunGenerationSelfCheck();

} // namespace shine::novelcore
