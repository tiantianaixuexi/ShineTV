#pragma once
// shine::novelcore —— **`06` §2.3 的 K01–K29 机器校验**（`auto` 的唯一阻塞项）
//
// 为什么单独一个模块：`06` §2.1 的裁决规则是「**凡是能用代码判定的，禁止交给 LLM**」。
// 这 29 条就是「能判定」的全集；它们此前**只有文档定义、没有代码实现**
// （`NovelCommit` 的 G2 只能用它自己那几条机器校验兜底，`NovelRunLoop` 的
// `verifiers_complete` 因此恒 false → `auto` 一律被拒启动）。本模块把它补齐。
//
// 三种「不通过」要分清（本模块的核心语义，别混）：
//   - `Fail`          有受检对象，判出不通过          → 是否阻断由 `severity` 决定
//   - `Missing`       有受检对象，但**数据源缺失**    → 等同 Fail（按该条的 severity）
//   - `NotApplicable` **没有受检对象**（空真）        → 放行 + 记账，不假装通过
// `NotApplicable` 与 `Pass` 分开记账，是为了让「校验器没跑」和「跑了且通过」可区分
// （`06` §2.7 M1：`checks_run` 必须列出实际执行的 check_id）。
//
// 数据源分三类（`CheckSpec::availability`），决定「谁传对象进来」：
//   - `Library`      直接查库（K02–K08 / K10–K11 / K14–K18 / K22–K28 / K29 …）
//   - `Artifact`     读工程落盘产物（K12 `work/ch*/12_state_diff.json`、K13 `snapshots/ch*.json`）
//   - `ContractInput` 结论**产生在别处**、库里没有承载，只能由调用方传
//                    （K19–K21：生成侧 `ApiGraphValidator` / `Sanitize` 的结论）
//
// **v9（S13）起**：K09 / K22 / K23 / K24 的受检对象已落地进库（`shots.start_state_json` /
// `end_state_json` / `timeline_json` + `prompt_artifacts`），于是它们从 `contract-input` 升为
// `library` —— 调用方显式传值仍然优先（桥 / 生成侧可直接给），不传才查库。
// 传空且库里也没有 ⇒ `NotApplicable`（空真，**不伪造通过**）。
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/sqlite/SqliteDb.h"
#include "novel/NovelCommit.h"
#include "novel/NovelDb.h"
#include "novel/NovelTypes.h"

namespace shine::novelcore {

// ———— 单条结果 ————

enum class CheckOutcome {
    Pass,          // 有受检对象且通过
    Fail,          // 有受检对象且不通过
    Missing,       // 有受检对象但数据源缺失（等同不通过）
    NotApplicable, // 无受检对象（空真，放行 + 记账）
};
[[nodiscard]] std::string_view CheckOutcomeName(CheckOutcome outcome) noexcept;

struct CheckResult {
    std::string check_id; // "K01"
    std::string name;     // "contract.schema"
    CheckOutcome outcome = CheckOutcome::NotApplicable;
    std::string severity; // high | medium | low（`06` §2.3 的「失败即 …」列）
    std::string detail;   // 中文，可直接显示 / 进 `ValidationReport`
};

// 是否放行（`07` §2.3 G2：「low 级可记但放行」）
[[nodiscard]] bool CheckPassed(const CheckResult& result) noexcept;

// ———— 目录（29 条的元信息；`availability` = 谁提供受检对象）————

enum class CheckAvailability {
    Library,       // 查库 / 查工程产物
    Artifact,      // 读工程落盘产物（`work/`、`snapshots/`）
    ContractInput, // 上游契约对象只能由调用方传入
};
[[nodiscard]] std::string_view CheckAvailabilityName(CheckAvailability availability) noexcept;

struct CheckSpec {
    std::string_view check_id;
    std::string_view name;        // `06` §2.3 的 check_id 列
    std::string_view severity;    // 失败级别
    CheckAvailability availability;
    std::string_view scope;       // chapter | scene | shot | continuity | generation
    std::string_view data_source; // 表名或产物名（人读）
    std::string_view note;        // 口径 / 缺口
};

// K01–K29 全量目录（**唯一一份**；`06` §2.3 的表即此）
[[nodiscard]] std::span<const CheckSpec> CheckCatalog() noexcept;
// 全部 check_id（"K01"…"K29"）
[[nodiscard]] std::vector<std::string_view> AllCheckIds();
// **`09` §2.1 auto 前置②的判据**：29 条校验器是否全部已实现（= 目录满 29 且无占位）
[[nodiscard]] bool VerifiersComplete() noexcept;
// 「K01–K29 全部 pass」判定（`07` §2.3 G2）：`low` 可记但放行
[[nodiscard]] bool AllChecksPass(std::span<const CheckResult> results) noexcept;

// ———— ContractInput 三组只读对象 ————

// K09（`12` §2.7）：相邻镜的 `end_state` 与 `start_state` **逐字段比对**。
// v9（S13）起 `shots` 有 `start_state_json`/`end_state_json` 两列 → 不传也会查库。
struct ShotStateSnapshot {
    RowId shot_id = 0;
    int ord = 0;
    std::string start_state_json = "{}";
    std::string end_state_json = "{}";
    std::vector<RowId> character_ids; // K22：这面镜里出场的人物（用来解析视觉阶段）
};

// K24（`02` §2.9）：一镜的 `Beat[]` 时间轴（`begin_s`/`end_s`，单位秒）
struct BeatSpan {
    double begin_s = 0.0;
    double end_s = 0.0;
};

// K19–K21（`13` §2.6）：生成侧结论。由 `video` 侧（`ApiGraphValidator` /
// `VideoProject::Sanitize` / `NovelShotBridge`）跑完后回填；不重复实现那三套规则。
struct GenerationCheckInput {
    bool has_graph = false;      // 是否提交了 API 图（K19 的受检对象）
    bool graph_ok = false;       // `ValidateApiGraph` 的结论
    std::string graph_detail;
    bool has_sanitize = false;   // 是否跑过 `Sanitize`（K20/K21 的受检对象）
    int size_corrections = 0;    // 宽高/帧数被纠正的条数（K20 → low）
    int ref_truncations = 0;     // 参考图被截断的条数（K21 → high）
    std::string sanitize_detail;
    // K28：降级账是否已落盘可见（`degradations.jsonl` / `cost_report.json`）
    bool degradations_recorded = false;
};

// ———— 一次章节校验的输入 ————

struct CheckInputs {
    RowId chapter_id = 0;
    int chapter_ord = 0;               // 0 = 由 chapter_id 反查
    std::filesystem::path project_dir; // K12 `work/`、K28 降级账（空 = K12 靠内存 diff / K28 不看盘）
    // K13 的章级快照目录。**单独给一列**是因为 `NovelCommit` 只知道 `snapshot_dir`
    // （它不一定等于 `<工程根>/snapshots`）；空 = 退回 `project_dir/snapshots`。
    std::filesystem::path snapshot_dir;
    int word_target = 3000; // K25（`02` §2.1 默认值）
    // K01/K11/K17：内存里的 StateDiff。空 = 尝试读 `work/ch<NNN>/12_state_diff.json`，
    // 读不到则这三条 NotApplicable（「本章没有 StateDiff 产物」由 K12 负责判）。
    const StateDiff* diff = nullptr;
    std::span<const ShotStateSnapshot> shots; // K09 / K22
    std::span<const BeatSpan> beats;          // K24
    double scene_duration_s = 0.0;            // K24 的 `[0, duration]`
    // K23：产物记录的 `input_state_hash`（`PromptArtifact`，`02` §2.10）。**当前无表存储**
    // → 空 = NotApplicable（不伪造通过）。
    std::string prompt_state_hash;
    std::string prompt_chain = "visual"; // text | visual
    std::string prompt_stage;            // 阶段名（参与哈希）
    GenerationCheckInput gen;                 // K19–K21 / K28
    std::int64_t orphan_stale_seconds = 1800; // K27（<=0 = 无条件视为孤儿）
};

// ———— 一次校验的报告（`07` §2.3 G2 的输入）————

struct ValidationReport {
    RowId chapter_id = 0;
    std::vector<CheckResult> checks; // 恒为 29 条（含 NotApplicable）

    [[nodiscard]] bool Ok() const noexcept; // = AllChecksPass(checks)
    [[nodiscard]] std::vector<std::string> FailedIds() const;  // Fail|Missing 的 check_id
    [[nodiscard]] std::vector<std::string> RanIds() const;     // `06` §2.7 M1 的 checks_run
    [[nodiscard]] std::vector<std::string> SkippedIds() const; // NotApplicable（空真）
    [[nodiscard]] const CheckResult* Find(std::string_view checkId) const noexcept;
    [[nodiscard]] std::string Describe() const; // "K01=pass K02=fail(high) …"（只列非 pass）
    [[nodiscard]] std::string Json() const;     // 落盘 / 记账用
};

// 跑全量 K01–K29（29 条都会出现在报告里）
[[nodiscard]] ValidationReport RunChapterChecks(db::sqlite::Database& db, const CheckInputs& in);

// ———— 单条入口（离线可断言；video 侧 / 桥 / `NovelCommit` 可直接复用）————

[[nodiscard]] CheckResult CheckShotContinuity(std::span<const ShotStateSnapshot> shots);
[[nodiscard]] CheckResult CheckBeatTimeline(std::span<const BeatSpan> beats, double duration_s);
// K19 / K20 / K21 三条（生成侧；`has_*=false` 时各自 NotApplicable）
[[nodiscard]] std::vector<CheckResult> CheckGeneration(const GenerationCheckInput& gen);

// `04` §2.5 的 `input_state_hash`：sha1(规范化状态摘要)（K23 / H1–H5 的唯一算法）。
// chain = "text" | "visual"；stage = 阶段名。输入清单是封闭列表，见 `04` §2.5。
[[nodiscard]] std::string ComputeInputStateHash(db::sqlite::Database& db, RowId chapter_id,
                                               std::string_view chain, std::string_view stage);
// sha1（`04` §2.5 指定的算法）；hex 小写 40 字符
[[nodiscard]] std::string Sha1Hex(std::string_view data);

// K23：产物记录的 hash 与当前状态是否一致（不一致即不得复用，不变式 I9）
[[nodiscard]] CheckResult CheckStateHashMatch(std::string_view stored_hash,
                                             std::string_view current_hash);

// ———— 自检（并入小说侧 `SHINE_NOVEL_GRAPH_CHECK`，汇总项 `checks`）————
// 覆盖：29 条目录完整性 / K04·K06·K07·K08·K14·K15·K16·K26·K27 的构造触发 /
// K09·K24 的 ContractInput 判定 / K20·K21 的降级记账 / K23 的 sha1 向量与失效判定 /
// `NotApplicable` 与 `Fail` 不混淆。返回 fail 条数（0 = 全过）。
[[nodiscard]] int RunChecksSelfCheck();

} // namespace shine::novelcore
