#pragma once
// S21（`10-初始化入口.md`）：初始化链的**门禁**（§2.3 N1–N14）与**阶段表**（§2.2 I1–I16）。
//
// 收的是差距 10-1 / 10-2 / 10-3（都标 A 级）：
//   10-1 `CreateProject` 只建空库、无任何世界内容 → 第 1 章之前无路可走
//   10-2 无初始化流程与阶段
//   10-3 **无「可开写」门禁** → 空世界也能点「生成本章」，生成无意义内容
//
// 照规格的纪律：
//   · 门禁失败**不允许警告后放行**（`10` §2.3）：输出 `InitReport{passed, failures[{n_id,detail,fix_hint}]}`
//   · 初始化产物一律经 `INIT_COMMIT` 事务写入（`R1`），不逐条散写
//   · 路径 D（模板）骨架写 `CANON`、内容留空写 `PROPOSED`（`R5`）；路径 C（AI）一律 `PROPOSED`（`R3`/`G7`）
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/sqlite/SqliteDb.h"

namespace shine::novelcore {

// ———— 门禁（`10` §2.3）————
struct InitFailure {
    std::string n_id;     // "N1" … "N14"
    std::string detail;   // 中文：实际差什么（带上实测值，便于判断）
    std::string fix_hint; // 怎么补
};

struct InitReport {
    bool passed = false;
    std::vector<InitFailure> failures;
    [[nodiscard]] std::string Describe() const; // 一行摘要（可直接显示）
    [[nodiscard]] std::string Json() const;     // 落 `init/I15_gate.json` / 给 CLI 输出
};

// 逐条判定 N1–N14；全部满足才 `passed=true`（`10` §2.3 的"全部满足才允许进入 CHAPTER_INIT"）
[[nodiscard]] InitReport CheckInitGate(db::sqlite::Database& db);

// ———— 阶段表（`10` §2.2）————
struct InitStageSpec {
    std::string_view code;     // "INIT_CONCEPT" … "INIT_COMMIT"
    std::string_view artifact; // `init/` 下的产物文件名（空 = 该阶段不落盘）
    bool needs_llm = false;    // 路径 C 的"域生成"阶段（`G2`：一次一个域）
};
[[nodiscard]] std::span<const InitStageSpec> InitStageCatalog() noexcept;
[[nodiscard]] std::filesystem::path InitDir(const std::filesystem::path& project_dir);

// ———— 路径 B/D 的结构骨架（不调用 LLM）————
// 建：`world_meta.book_title`、`writing_style` 一行、1 条 `author_rules(severity=error)`、
// 1 卷、1 条 `main` plot、1 条 mystery、1 条 `scope=world` secret；另补 `field_defs` 种子（N12）
// 与 15 个内置 Agent（N13）。
// ⚠️ **内容类**条件（主角/地点/伏笔…）**不伪造** —— 骨架只给结构，门禁如实报缺（`R5` 的本意）。
struct InitSkeletonResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> created; // "world_meta.book_title" / "volumes#1" …
};
[[nodiscard]] InitSkeletonResult RunInitSkeleton(db::sqlite::Database& db,
                                                 const std::filesystem::path& project_dir,
                                                 std::string_view book_title,
                                                 int target_chapters = 100);

// 离线自检（N1–N14 逐条构造触发 + 骨架后的报告）
[[nodiscard]] bool RunInitSelfCheck();

// `10` §2.3 的 **N12 / N13 种子**：`field_defs`（11 条系统定义）+ 15 个内置 Agent。
// 幂等、best-effort（失败只告警）—— 它就是"建库/挂库"这一步该做的事，单独拎出来是因为
// ⚠️ **门禁的修法提示写着"打开工程时会自动跑"，实际上 `NovelDb::Open` → `Migrate()` 并不跑**：
// 于是 GUI「新建工程」（只 Open）与 `--mcp-stdio` 挂一个新库这两条路，`--novel-init --gate-only`
// 会报 N12/N13 不过（2026-09-20 实测：MCP 线上灌完设定后门禁只剩这两条）。
// ⇒ 凡"从零建/挂一个工程库"的入口，都要调它一次。
void EnsureProjectSeeds(db::sqlite::Database& db);

} // namespace shine::novelcore
