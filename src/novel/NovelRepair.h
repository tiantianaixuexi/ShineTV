#pragma once
// S69：**工程库一致性扫描与修复**（库级，补章级 K 校验够不到的地方）。
//
// 为什么要有它（用户的追问逼出来的）：`06` §2.3 的 K01–K29 是**章级**校验 —— 只在"提交这一章"
// 时看这份 diff + 库现状；它管不到"**库里已经躺着的、跨章的**不一致行"。而这类行有两个来源：
//   1. **历史**：机制修好之前跑出来的脏行（真跑实证：`entity_ownerships` 里有"持有者是 item 自己"
//      "持有者是 prop" "`to_chapter` < `from_chapter`" "owner 指向根本不存在的实体"）；
//   2. **外部 MCP 写入**：外部 Agent 直写 `novel_upsert_*` 时绕过了提交门禁。
// 之前的做法是**我手敲 sqlite 命令**去修 —— 那是一次性的、不可复现、也没法让 AI 自动化，
// 而且**下一次换一本小说/换一个库又要重来**。所以落成工具：
//   · `ScanConsistency`  —— 只读扫描，给"哪里不一致、建议怎么修"（AI 可先看再决定）；
//   · `RepairConsistency` —— 只修**可机械判定**的规则，默认 `dry_run`，每次写 `audit_logs`。
// 对外两个 MCP 工具（`src/novel/NovelMcpTools.cpp`）：
//   `novel_consistency_report`（只读，不需要写开关）/ `novel_consistency_repair`（需写开关）。
//
// ⚠️ **不是所有问题都该机械修**：`fixable=false` 的那些（悬空引用）只报告 + 给修法，
// 因为"删一条引用行"和"重跑那一章"是**语义决策**，机器不该替人做。

#include <string>
#include <string_view>
#include <vector>

#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

// 一致性规则（id 稳定，便于 MCP 调用方按规则挑选 / 白名单）：
//   R1 `ownership_interval`            `to_chapter<>0 && to_chapter<=from_chapter`（空/倒挂区间）
//   R2 `ownership_owner_kind`          **活跃行**（`to_chapter=0`）的持有者不是 `person`
//   R3 `ownership_multi_active`        同一物品有 **多行活跃持有**（I5「唯一持有者」被破坏）
//   R4 `ownership_owner_missing`       持有者指向不存在的实体
//   R5 `ownership_item_missing`        被持有物指向不存在的实体
//   R6 `ownership_item_kind`           被持有物不是物品类（warn，不自动修）
//   R7 `event_participant_missing`     事件参与者/事件本身悬空（不自动修）
//   R8 `event_location_missing`        事件地点悬空（不自动修）
//   R9 `relation_endpoint_missing`     关系边端点悬空（不自动修）
//   R10 `entity_duplicate_name`        **同 kind + 同 `name_norm`** 的重复实体（T3a，warn，
//                                      **永不自动合并** —— "两个李默"可能是两个不同的人，属语义决策）；
//                                      `name_norm` 用 `NormalizeEntityName()`（`NovelNames.h`，与 resolver 同源）
struct ConsistencyIssue {
    std::string rule;     // R1…R9
    std::string severity; // "error" | "warn"
    std::string table;
    RowId row_id = 0;      // 出问题的行 id（0 = 规则以物品/实体为粒度）
    RowId subject_id = 0;  // 相关业务 id（物品 id / 事件 id），便于追查
    std::string detail;    // 给人/模型看的事实
    std::string suggest;   // 修法
    bool fixable = false;  // 能否机械修（false ⇒ 只能报告）
};

struct ConsistencyReport {
    std::vector<ConsistencyIssue> issues;
    int errors = 0;
    int warnings = 0;
    int fixable = 0;

    [[nodiscard]] bool ok() const noexcept { return errors == 0; }
    [[nodiscard]] std::string ToJson() const;
    [[nodiscard]] std::string ToText() const; // 单行摘要 + 逐条（CLI/日志用）
};

// 只读扫描（不写库、不需要写开关）
[[nodiscard]] ConsistencyReport ScanConsistency(db::sqlite::Database& db);

struct RepairOutcome {
    bool dry_run = true;
    int scanned = 0;  // 扫到的可修问题数（scanned = fixed + 失败数）
    int fixed = 0;    // 已修条数（dry_run 时 = **将修**）
    int skipped = 0;  // 不可机械修 / 不在 rules 里的条数
    std::vector<std::string> actions; // 逐条说明：`R2 entity_ownerships#12 → 关闭持有（to_chapter=6）`
    std::string error;                // 非空 = 执行期出错（已尽可能回滚)
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
    [[nodiscard]] std::string ToJson() const;
};

// 修复（`rules` 空 = 全部**可机械修**的规则；只认 R1–R5，其他一律跳过并计入 skipped）。
// `dry_run=true`（默认）只产出计划、**一个字节都不写**。写路径每次追加一条 `audit_logs`。
[[nodiscard]] RepairOutcome RepairConsistency(db::sqlite::Database& db, bool dry_run,
                                              std::string_view rules = {},
                                              std::string_view actor = "consistency-repair");

// 离线自检：内存库造脏 → 扫描抓到 → dry_run 不改库 → apply 修对 → 复扫干净 → 再修 0 条（幂等）
[[nodiscard]] bool RunRepairSelfCheck();

// ★ R11：**软合并 / 重定向**（★ 用户 2026-09-21 定的核心不变量：**永不 DELETE 实体行**）。
// 调用方必须**显式点名 survivor**（`spec` 形如 `"43<108;59<76,134"` = `survivor<loser1,loser2`，多组用 `;`）
// ⇒ **永不自动 merge**（spec 为空 ⇒ 报错返回，一行不动）。
// `dry_run=true`（默认）只出计划、**一个字节都不写**。真写时：
//   ① **压平** `merged_into` 链（**直接指向活实体** —— 硬验收 A：不留 `134→76→59` 这种多跳）；
//   ② 重指**引用/关系/状态**行（`relations` / `event_participants` / `event_details` /
//      `character_status` / `character_knowledge` / `entity_ownerships` / `entity_fields` /
//      **`entity_versions`**）；
//   ③ **逻辑去重**（只删**引用/状态行**；⚠️ **`entity_versions` 是历史表 ⇒ 只重指、绝不去重**）；
//   ④ 写 `audit_logs(action=merge_entity)`（含 survivor / losers / actor / 去重摘要）。
// 兼容面：**旧 id 永远可解析** —— resolver 透明重定向到幸存者（并 warn 留痕，见 `NovelCommit.cpp`）。
[[nodiscard]] RepairOutcome MergeBySpec(db::sqlite::Database& db, std::string_view merge_spec,
                                        bool dry_run, std::string_view actor = "repair");

} // namespace shine::novelcore
