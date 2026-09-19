#pragma once
// S19（`03` §2.7「断点续跑、幂等与中间产物落盘」）：正文链的**阶段产物落盘 + 断点判定**。
//
// 落地的是 P1/P2/P5 三条幂等规则：
//   P1 每个阶段产物文件包含 `input_state_hash`；与当前状态一致 → **允许跳过该阶段**
//   P2 不一致 → 该阶段及下游全部失效，从该阶段重跑
//   P5 崩溃恢复：读盘找最后一个「产物完整且哈希一致」的阶段 → 从下一个阶段继续
//
// ⚠️ 两处**如实记账**的偏差：
//   ① `12_state_diff.json` 保持**裸 StateDiff**（这是 S12 交付的既有契约产物，K01–K29 的
//      `RunChapterChecks` 直读它）；它的 `input_state_hash` 记在 `_manifest.json` 里。
//   ② `03` §2.7 的文件名表里**没有正文草稿**（P4：`NOVEL_WRITE` 落盘前不写 `chapters.body`），
//      所以 T11 的产物就是 `chapters.body` 本身，不落 `work/`。
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "novel/NovelRunLoop.h" // ChapterWorkDir（`work/ch<NNN>/`）与 `RowId`

namespace shine::novelcore {

// `03` §2.7 的落盘文件名 —— **照规格，不自己编**（`stage` = §2.2 的代码列）
struct StageFileSpec {
    std::string_view stage;
    std::string_view file;
};
[[nodiscard]] std::span<const StageFileSpec> StageFileCatalog() noexcept;

// 某阶段的产物文件名；不在目录里 → 空
[[nodiscard]] std::string_view StageFileName(std::string_view stage) noexcept;

// 包装格式（P1）：`{"stage":…,"input_state_hash":…,"at":<epoch秒>,"payload":<原样 JSON>}`
[[nodiscard]] std::string WrapStageArtifact(std::string_view stage, std::string_view payload,
                                            std::string_view input_state_hash);

// ⚠️ 名字避开 `NovelRunLoop.h` 的 `StageArtifact`（那是 `_manifest.json` 的"产物条目"：
// 路径 + 字节数 + 内容指纹）；这里是"产物内容 + 落盘时的输入状态哈希"。
struct StagePayload {
    std::string payload;          // 原样 JSON（已解包）
    std::string input_state_hash; // 落盘时的输入状态哈希
};

// 读某章某阶段的产物；文件不存在 / 格式不对 → `nullopt`
[[nodiscard]] std::optional<StagePayload>
ReadStageArtifact(const std::filesystem::path& project_dir, int ord, std::string_view stage);

// 写产物（自动建目录）。**失败不阻断生成**（`work/` 是"非权威，可删"），只返回 false 让调用方 Warn。
[[nodiscard]] bool WriteStageArtifact(const std::filesystem::path& project_dir, int ord,
                                      std::string_view stage, std::string_view payload,
                                      std::string_view input_state_hash);

// ———— S20（`02` §2.10 / 差距 `03-11`）：正文链的阶段产物**同时记账到库** ————
// 原先 `prompt_artifacts` 只有建表与读写 API、**没有写入方** ⇒ K23（`prompt.state_hash_match` /
// 不变式 I9）恒 `n/a`，P1 的"复用判定"也只在 `work/`（盘被删就没了）。
// 本函数 = ① 落盘（P1 的哈希写进产物文件）+ ② 落库（`prompt_artifacts`：chapter/chain=text/
// stage/input_state_hash；同章同阶段**只留一行**，重复写是覆盖）。
// ⚠️ `prompt` 列存 payload 的**前 400 字节**（库只做"哈希账"，完整产物在 `work/`）；
// payload 总字节数记在 `model_hint`（`bytes=NNNN`）。
[[nodiscard]] bool RecordStageArtifact(db::sqlite::Database& db, RowId chapter_id, int ord,
                                       const std::filesystem::path& project_dir,
                                       std::string_view stage, std::string_view payload,
                                       std::string_view input_state_hash);

// 库里的阶段账（K23 的受检对象）：该章最后写入的那条 `chain=text` 产物（没有 → `nullopt`）
struct StageHashRecord {
    std::string stage;
    std::string input_state_hash;
    RowId artifact_id = 0;
};
[[nodiscard]] std::optional<StageHashRecord> LoadStageHashRecord(db::sqlite::Database& db,
                                                                 RowId chapter_id);

// —— P1/P2/P5：断点判定 ——
// `stages` 是**实际执行顺序**的阶段代号（`03` §2.2 的代码列，只需含"会落盘"的那些）。
// 逐阶段用 `ComputeInputStateHash(db, chapter_id, "text", stage)` 与盘上哈希比对：
//   产物缺失 → 停在此阶段；哈希不一致 → 停在此阶段（P2：下游全失效）；
//   全部一致 → 返回 `stages.size()`（无活可干）。
[[nodiscard]] std::size_t FindResumeIndex(db::sqlite::Database& db, RowId chapter_id, int ord,
                                         const std::filesystem::path& project_dir,
                                         std::span<const std::string_view> stages);

} // namespace shine::novelcore
