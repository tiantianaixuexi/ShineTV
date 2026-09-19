#pragma once
// shine::novelcore —— **状态回写与提交**（`07-状态回写与提交.md`，T16 COMMIT）
//
// 本文件修的是本系统最关键的闭环断点：**章节生成完必须把状态变化写回世界**。
//   ① `StateDiff` 契约（`02` §2.5）—— 回写世界的唯一载体（不变式 I10）
//   ② 契约校验的**机器部分**（`07` §2.2 的 ③：D1/D4/D7/D8 + 空 diff 判定）
//   ③ 提交门禁 **G1–G5**（`07` §2.3）
//   ④ **14 块事务**提交 + 快照（§2.4，不变式 I11）+ 幂等（§2.3 的 I1–I3）
//   ⑤ Canon：本 S 只做 `manual` 语义（写 `PROPOSED`）；**`auto` 不在本 S**（`07` §2.5 的 C5 禁止跳门禁）
//
// 分工：LLM 只负责"从正文里看出变化"（`extract` Agent），**基线（Before）永远由代码从库里读**
// （`07` §2.2 的关键设计）。所以本模块不做任何模型调用 —— 它只吃一份 `StateDiff` 与门禁结论。
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace shine::novelcore {

// ———— `StateDiff`（`02` §2.5 逐字段对应；字段名即 JSON 键）————

struct NewEntityDelta {
    std::string temp_id; // 章内临时 id（`ev:` / `en:` 前缀；仅本章有效）
    std::string kind;    // 必须命中 `01` §1.1.2 的 31 种
    std::string name;
    std::string summary;
    std::string status = "active";
    RowId created_chapter = 0;
};

struct CharacterDelta { // → character_status（按章 append 一行）
    RowId entity_id = 0;
    RowId location_id = 0;
    std::string body_state;
    std::string mind_state;
    std::string emotion_json = "{}";
    std::string goal;
    std::string relation_note;
    std::string resource_note;
    std::string secret_note;
    std::string reason; // 状态变化必须有因
};

struct RelationDelta {
    std::string op = "upsert"; // upsert | close
    RowId from_id = 0;
    RowId to_id = 0;
    std::string rel_type;
    int strength = 50;
    std::string reason;
};

struct ItemDelta {
    std::string op = "acquire"; // acquire | lose | move | change_state（与 ScenePlan.items[].op 同集合）
    RowId item_id = 0;
    RowId owner_id = 0;
    RowId location_id = 0;
    std::string how;
    std::string reason;
};

struct EventParticipantDelta {
    RowId entity_id = 0;
    std::string role; // actor | victim | witness | beneficiary | faction_actor
};

struct EventDelta {
    std::string temp_id;
    std::string cause;
    std::vector<EventParticipantDelta> participants;
    RowId location_id = 0;
    std::string time_label;
    std::string action;
    std::string result;
};

struct TempoRef { // 可指向本章 TempId，或已存在的实体 id
    std::string temp_id;
    RowId entity_id = 0;
};

struct CausalDelta {
    TempoRef cause;
    TempoRef effect;
    std::string link_type = "causes"; // causes|enables|prevents|escalates|reveals
    std::string note;
};

struct ForeshadowDelta {
    std::string op = "new"; // new | progress | reinforce | payoff
    RowId foreshadow_id = 0; // 0 = 新建
    std::string title;
    std::string content;
    std::string truth;
    std::string status = "PLANNED";
    RowId setup_ch = 0;
    RowId payoff_ch = 0;
    int importance = 50;
};

struct MysteryBeatDelta {
    std::string beat_type = "hint"; // question|hint|reveal|answer|red_herring
    std::string content;
    RowId target_entity_id = 0;
};

struct MysteryDelta {
    RowId mystery_id = 0; // 0 = 新建
    std::string question;
    std::string answer;
    std::string status = "open";
    MysteryBeatDelta beat;
};

struct PlotBeatDelta {
    std::string beat_type = "setup"; // setup|rising|turning_point|climax|falling|resolution|revelation
    std::string title;
    std::string summary;
};

struct PlotDelta {
    RowId plot_id = 0; // 0 = 新建
    std::string kind = "main";
    std::string title;
    std::string status = "active";
    PlotBeatDelta beat;
};

struct KnowledgeDelta {
    RowId entity_id = 0;
    std::string fact_kind;
    RowId fact_id = 0;
    std::string fact_text;
    bool knows = true;
    RowId chapter_known = 0;
};

struct TimelineDelta {
    RowId event_id = 0;
    std::string time_label;
    RowId location_id = 0;
    std::string cause_note;
    std::string result_note;
};

struct LocationDelta { // ⚠️ 当前无落地表（`02` §3 的 02-3）→ 只记账不写表
    RowId location_id = 0;
    std::string field; // weather|environment|controlling_faction_id|danger|destruction
    std::string value;
    std::string reason;
};

struct StateDiff {
    int contract_version = 1;
    std::string producer = "extractor";
    std::string input_state_hash;
    RowId chapter_id = 0;
    bool no_change_declared = false; // true 时**所有子数组必须为空**（I10）
    std::vector<NewEntityDelta> entities;
    std::vector<CharacterDelta> characters;
    std::vector<RelationDelta> relationships;
    std::vector<ItemDelta> items;
    std::vector<LocationDelta> locations;
    std::vector<EventDelta> events;
    std::vector<CausalDelta> causal;
    std::vector<PlotDelta> plotlines;
    std::vector<ForeshadowDelta> foreshadows;
    std::vector<MysteryDelta> mysteries;
    std::vector<KnowledgeDelta> knowledge;
    std::vector<TimelineDelta> timeline;

    [[nodiscard]] bool HasAnyDelta() const noexcept;
    [[nodiscard]] std::size_t DeltaCount() const noexcept;
    // 稳定摘要（幂等判定用）：FNV-1a over 规范化 JSON → 16 位十六进制
    [[nodiscard]] std::string Hash() const;
    [[nodiscard]] std::string Summary() const; // 人读一行：章=17 实体+1 角色 3 伏笔 1 …
};

[[nodiscard]] std::string StateDiffToJson(const StateDiff& diff);
// 宽容读（缺键取默认）；返回是否解析成功（JSON 非法返回 false）
[[nodiscard]] bool StateDiffFromJson(std::string_view text, StateDiff& out);

// ———— 契约校验的机器部分（`07` §2.2 的 ③）————

struct CommitIssue {
    std::string code;     // contract | causality_break | high | medium | low …（与 `02` §2.6 的 Issue.type 对齐）
    std::string severity; // high | medium | low
    std::string detail;
};

// D1：`characters[].entity_id` 必须是已存在实体或本章 `NewEntity`
// D4：`dead`/`destroyed` 必须同时给出 `entities` 里的 status 变更
// D7：`no_change_declared=true` 时所有子数组必须为空
// D8：`NewEntity.kind` 必须在 31 种内（`01` §1.1.2）
[[nodiscard]] std::vector<CommitIssue> ValidateStateDiff(db::sqlite::Database& db, const StateDiff& diff);

// ———— 门禁 G1–G5（`07` §2.3）————

struct CommitGateReport {
    bool ok = false;
    bool g1_review_pass = false;   // 评审 PASS
    bool g2_checks_pass = false;   // 机器校验无 high
    bool g3_snapshot_ready = false; // 提交前快照已就绪（I11）
    bool g4_diff_valid = false;    // StateDiff 契约通过且非空（或显式声明无变化）
    bool g5_no_high_issue = false; // 无未解决的 high issue
    std::vector<CommitIssue> issues;
    [[nodiscard]] std::string Describe() const; // "G1=1 G2=1 G3=1 G4=1 G5=1"
};

struct CommitContext {
    RowId chapter_id = 0;
    // G1：评审结论（`06` §2.4）。由调用方给 —— 本模块不重跑模型
    bool review_pass = false;
    std::string review_verdict; // 原样记账（PASS/FAIL/…）
    // G2：机器校验结论。⚠️ `06` 的 K01–K29 尚未实现全量 → 本模块以**自己能跑的机器校验**
    // （ValidateStateDiff + 下面的 G4/G5）作为结论；调用方若有更全的 ValidationReport，传它。
    bool machine_checks_pass = true;
    std::vector<CommitIssue> upstream_issues; // 上游（`06`）已发现的问题 → 进 G5 判定与记账
    std::string canon_mode = "manual";        // manual | auto（**auto 不在本 S**：`07` §2.5 C5）
    // 章级快照落盘：`<此目录>/ch<NNN>.json`（不变式 I11；写失败 → 阻断提交）。
    // 空 = 不落盘（**自检以外不要用**，否则 G3 无从谈起）。
    std::string snapshot_dir;
    // 第 11 块（chapters）：正文非空时写 body/words/summary 并把 status 置 done
    std::string chapter_body;
    std::string chapter_summary;
    int chapter_words = 0;
    RowId chapter_pov_entity_id = 0;
};

struct CommitResult {
    bool ok = false;
    bool skipped = false; // 幂等命中（同 diff 已提交过）
    std::string error;
    std::string diff_hash;
    std::vector<std::string> applied;      // 逐块写入摘要（空块也会列出 "跳过"）
    std::vector<RowId> entity_version_ids; // 本章写下的实体快照
    std::string snapshot_path;             // 章级快照路径（UTF-8）
    CommitGateReport gates;
};

// 提交一章的状态回写（14 块事务；任一步失败 → ROLLBACK，库回到提交前）
[[nodiscard]] CommitResult CommitChapterState(db::sqlite::Database& db, const StateDiff& diff,
                                              const CommitContext& ctx);

// 幂等（I1）：同一章 + 同一 diff 哈希是否已提交过
[[nodiscard]] bool ChapterAlreadyCommitted(db::sqlite::Database& db, RowId chapterId,
                                           std::string_view diffHash);

// 离线自检（`SHINE_NOVEL_COMMIT_CHECK` / 并入小说侧自检）：门禁 / 14 块写入 / 幂等 / 快照 / D 规则。
// 返回 fail 条数（0 = 全过）。
[[nodiscard]] int RunCommitSelfCheck();

} // namespace shine::novelcore
