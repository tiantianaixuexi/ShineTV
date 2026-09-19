#include "novel/NovelStageLedger.h"

#include "core/Log.h"
#include "novel/NovelChecks.h" // ComputeInputStateHash（`04` §2.5 的哈希，唯一来源）
#include "novel/NovelRunLoop.h"  // ChapterWorkDir（`work/ch<NNN>/`）
#include "novel/NovelVisual.h"   // S20：`prompt_artifacts`（K23 的受检对象）
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Time.h"

#include <yyjson.h>

#include <chrono>
#include <cstddef>

namespace shine::novelcore {
namespace {

// `03` §2.7 的落盘表（顺序即执行顺序；T2–T9 合并执行时不落各自的文件）
constexpr StageFileSpec kStageFiles[] = {
    {"CHAPTER_GOAL", "01_chapter_goal.json"},
    {"OUTLINE", "02_outline.json"},
    {"EVENT_PLAN", "03_event_plan.json"},
    {"CONTEXT_ASSEMBLY", "04_context_pack.json"},
    {"SCENE_PLAN", "05_scene_plan.json"},
    {"CHARACTER_PLAN", "06_character_plan.json"},
    {"ITEM_PLAN", "07_item_plan.json"},
    {"FORESHADOW_PLAN", "08_foreshadow_plan.json"},
    {"SCENE_EVENT_ORDER", "09_chapter_plan.json"},
    {"CHAPTER_REVIEW", "10_review.json"},
    {"CHAPTER_REPAIR", "11_repair_receipt.json"},
    {"STATE_EXTRACT", "12_state_diff.json"},
    {"STATE_VALIDATE", "13_validation.json"},
};

[[nodiscard]] std::string JsonToText(yyjson_mut_doc* doc) {
    const char* text = yyjson_mut_write(doc, 0, nullptr);
    std::string out = text == nullptr ? std::string{"{}"} : std::string{text};
    if (text != nullptr) {
        free(const_cast<char*>(text));
    }
    return out;
}

} // namespace

std::span<const StageFileSpec> StageFileCatalog() noexcept { return kStageFiles; }

std::string_view StageFileName(std::string_view stage) noexcept {
    for (const StageFileSpec& spec : kStageFiles) {
        if (spec.stage == stage) {
            return spec.file;
        }
    }
    return {};
}

std::string WrapStageArtifact(std::string_view stage, std::string_view payload,
                              std::string_view input_state_hash) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return "{}";
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "stage", std::string{stage}.c_str());
    yyjson_mut_obj_add_strcpy(doc, root, "input_state_hash", std::string{input_state_hash}.c_str());
    yyjson_mut_obj_add_int(doc, root, "at",
                           static_cast<std::int64_t>(util::NowMillis() / 1000));
    // payload：能解析成 JSON 就**原样嵌入**（保住契约结构），否则当字符串存
    yyjson_doc* pdoc = yyjson_read(payload.data(), payload.size(), 0);
    if (pdoc != nullptr && yyjson_doc_get_root(pdoc) != nullptr) {
        yyjson_mut_val* copy = yyjson_val_mut_copy(doc, yyjson_doc_get_root(pdoc));
        if (copy != nullptr) {
            yyjson_mut_obj_add_val(doc, root, "payload", copy);
        }
        yyjson_doc_free(pdoc);
    } else {
        yyjson_mut_obj_add_strcpy(doc, root, "payload", std::string{payload}.c_str());
    }
    const std::string out = JsonToText(doc);
    yyjson_mut_doc_free(doc);
    return out;
}

std::optional<StagePayload> ReadStageArtifact(const std::filesystem::path& project_dir, int ord,
                                              std::string_view stage) {
    if (project_dir.empty() || ord <= 0) {
        return std::nullopt;
    }
    const std::string_view name = StageFileName(stage);
    if (name.empty()) {
        return std::nullopt;
    }
    const auto file = ChapterWorkDir(project_dir, ord) / std::string{name};
    const auto bytes = util::ReadFileBytes(file);
    if (!bytes || bytes->empty()) {
        return std::nullopt;
    }
    yyjson_doc* doc = yyjson_read(bytes->data(), bytes->size(), 0);
    if (doc == nullptr) {
        return std::nullopt;
    }
    StagePayload out;
    yyjson_val* root = yyjson_doc_get_root(doc);
    out.input_state_hash = util::json::GetStrCopy(root, "input_state_hash");
    yyjson_val* payload = yyjson_obj_get(root, "payload");
    if (payload == nullptr) {
        // 容忍早期"裸文件"（S12 的 `12_state_diff.json` 就是裸 StateDiff）
        out.payload = *bytes;
    } else if (yyjson_is_str(payload)) {
        out.payload = yyjson_get_str(payload);
    } else {
        const char* text = yyjson_val_write(payload, 0, nullptr);
        if (text != nullptr) {
            out.payload = text;
            free(const_cast<char*>(text));
        }
    }
    yyjson_doc_free(doc);
    return out;
}

bool WriteStageArtifact(const std::filesystem::path& project_dir, int ord, std::string_view stage,
                        std::string_view payload, std::string_view input_state_hash) {
    if (project_dir.empty() || ord <= 0 || payload.empty()) {
        return false;
    }
    const std::string_view name = StageFileName(stage);
    if (name.empty()) {
        return false;
    }
    const auto dir = ChapterWorkDir(project_dir, ord);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // `12_state_diff.json` 是**既有契约产物**（K01–K29 直读裸 StateDiff）→ 原样写，不包装。
    const std::string text = stage == "STATE_EXTRACT"
                                 ? std::string{payload}
                                 : WrapStageArtifact(stage, payload, input_state_hash);
    const auto file = dir / std::string{name};
    if (!util::WriteFileBytes(file, text)) {
        log::Warn("阶段产物写入失败（{}）—— 不影响本章生成（`work/` 非权威，可删）",
                  util::PathToUtf8(file));
        return false;
    }
    return true;
}

bool RecordStageArtifact(db::sqlite::Database& db, RowId chapter_id, int ord,
                         const std::filesystem::path& project_dir, std::string_view stage,
                         std::string_view payload, std::string_view input_state_hash) {
    const bool onDisk = WriteStageArtifact(project_dir, ord, stage, payload, input_state_hash);
    if (chapter_id <= 0 || stage.empty()) {
        return onDisk;
    }
    NovelVisual visual(db);
    // 同章同阶段只留一行：先找已有的（`prompt_artifacts` 没有唯一键，`Upsert` 靠 id 判更新）
    RowId existing = 0;
    if (auto list = visual.ListPromptArtifacts(chapter_id); list) {
        for (const PromptArtifactRow& row : *list) {
            if (row.stage == stage && row.chain == "text") {
                existing = row.id;
                break;
            }
        }
    }
    PromptArtifactRow row;
    row.id = existing;
    row.chapter_id = chapter_id;
    row.target_kind = "stage"; // 正文链的粒度是"阶段"（表注释原列 shot|scene|asset|layer）
    row.chain = "text";
    row.stage = std::string{stage};
    row.input_state_hash = std::string{input_state_hash};
    // 库只做"哈希账"：`prompt` 存前 400 字节，总长度记在 `model_hint`
    row.prompt = std::string{payload.substr(0, 400)};
    row.model_hint = fmt::format("bytes={}", payload.size());
    auto r = visual.UpsertPromptArtifact(row);
    if (!r) {
        log::Warn("阶段产物记账失败（{} / 章 #{}）：{} —— 不影响本章生成", stage, chapter_id,
                  r.error().message);
        return onDisk;
    }
    return onDisk;
}

std::optional<StageHashRecord> LoadStageHashRecord(db::sqlite::Database& db, RowId chapter_id) {
    if (chapter_id <= 0) {
        return std::nullopt;
    }
    NovelVisual visual(db);
    auto list = visual.ListPromptArtifacts(chapter_id); // updated DESC, id DESC → 新的在前
    if (!list) {
        return std::nullopt;
    }
    for (const PromptArtifactRow& row : *list) {
        if (row.chain == "text" && !row.stage.empty() && !row.input_state_hash.empty()) {
            return StageHashRecord{row.stage, row.input_state_hash, row.id};
        }
    }
    return std::nullopt;
}

std::size_t FindResumeIndex(db::sqlite::Database& db, RowId chapter_id, int ord,
                            const std::filesystem::path& project_dir,
                            std::span<const std::string_view> stages) {
    for (std::size_t i = 0; i < stages.size(); ++i) {
        const auto art = ReadStageArtifact(project_dir, ord, stages[i]);
        if (!art) {
            return i; // 产物缺失 → 从这里开始
        }
        // P1/P2：拿**当前**状态哈希与该阶段落盘时的哈希比（`04` §2.5，chain=text）
        const std::string now =
            ComputeInputStateHash(db, chapter_id, "text", std::string{stages[i]});
        if (!now.empty() && !art->input_state_hash.empty() && now != art->input_state_hash) {
            return i; // 状态变了 → 该阶段及下游失效
        }
    }
    return stages.size();
}

} // namespace shine::novelcore
