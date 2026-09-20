#pragma once
// V8 `CONTINUITY`（`03` §2.2 的影视化阶段之一 / `12` §2.7 的 C1–C12）——
// **纯机器校验，不调 LLM**：`11` §2.2 早就写明"V9 不做 V8 的判定：判定归 NovelChecks"，
// 这一模块把那条判定真正落地（`12` §3 的 **12-6「无 ContinuityReport」标 S 级**）。
//
// 判定对象：**相邻两镜**的「本镜 `end_state` → 下一镜 `start_state`」（`12` §2.7 的
// 「Ending State = 下一镜 Starting State」）。数据来源分两处：
//   · `shots.start_state_json` / `end_state_json`（库）—— 角色/道具/光/环境/机位
//   · `work/ch<NNN>/storyboard.json`（盘）—— `performance` / `spatial` / `camera` / `transition`
//     （V9 只存盘的"无专列"契约字段）
//
// **三态结论**（与 `06` §2.3 的四态口径一致，**不假装通过**）：
//   · `pass`：有对象且合规
//   · `fail`：有对象且违反（进 `issues`）
//   · `unverified`：**有变化、但缺少核对依据**（例如 C6 要求"服装变化须有事件"，而
//     `ChapterPlan.events` 不在本模块能读到的范围内）—— 进 `notes`，**不算通过也不算失败**
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::novelcore {

struct ContinuityIssue {
    std::string code;     // "C1".."C12"
    std::string severity; // high | medium | low（`12` §2.7 的影响面）
    std::string detail;   // 中文，可直接显示
};

struct ContinuityOutcome {
    bool ok = false;
    std::string error;
    int shots_seen = 0;
    int pairs_checked = 0;  // 参与跨镜比较的镜对数
    int failed = 0;
    int unverified = 0;     // 数据不足（**如实记**，不计入通过）
    std::vector<ContinuityIssue> issues;
    std::vector<std::string> notes; // unverified 的原因（每条一次）
    std::string report_path;        // `work/ch<NNN>/v08_continuity.json`
    [[nodiscard]] std::string Describe() const;
};

// 跑 V8：该章相邻镜的 C1–C12。`project_dir` 用来读 `storyboard.json` 与落 `v08_continuity.json`。
// 没有 `storyboard.json` 时：只读库的那几条仍然判（C6/C7/C10/C11），其余记 unverified。
[[nodiscard]] ContinuityOutcome RunContinuityChecks(::shine::db::sqlite::Database& db,
                                                    RowId chapter_id,
                                                    std::string_view project_dir = {});

// 离线自检：构造违规镜对 → 断言判出对应 C 码；三态口径；报告落盘
[[nodiscard]] bool RunContinuitySelfCheck();

} // namespace shine::novelcore
