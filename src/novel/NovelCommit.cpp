#include "novel/NovelCommit.h"

#include "core/Log.h"
#include "novel/NovelGraph.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Reflect.h"
#include "util/Strings.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace shine::novelcore {
namespace {

[[nodiscard]] std::int64_t NowSec() noexcept { return util::NowMillis() / 1000; }

// `01` §1.1.2 的 31 种 kind（D8 用）——**唯一一份**（别在别处再列一遍）
[[nodiscard]] const std::set<std::string>& KnownKinds() {
    static const std::set<std::string> kinds = {
        "universe",  "world_rule", "history",      "culture",       "language",
        "religion",  "economy",    "tech",         "society",       "calendar",
        "person",    "creature",   "clothing",     "prop",          "treasure",
        "item",      "location",   "faction",      "power_system",  "ability",
        "event",     "plot",       "arc",          "conflict",      "theme",
        "motif",     "ending",     "secret",       "foreshadowing", "mystery",
        "resource"};
    return kinds;
}

[[nodiscard]] bool Contains(std::string_view hay, std::string_view needle) noexcept {
    return hay.find(needle) != std::string_view::npos;
}

// 章级快照落盘（不变式 I11）：`<dir>/ch<NNN>.json`
[[nodiscard]] std::expected<std::filesystem::path, std::string>
WriteChapterSnapshot(std::string_view dir, const StateDiff& diff, std::string_view diffHash,
                     const std::vector<RowId>& versionIds) {
    if (dir.empty()) {
        return std::unexpected(std::string{"未提供章级快照目录（不变式 I11 要求提交前有快照）"});
    }
    const std::filesystem::path root = util::PathFromUtf8(dir);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        return std::unexpected(fmt::format("快照目录建不出来：{}", util::PathToUtf8(root)));
    }
    const std::filesystem::path file = root / fmt::format("ch{:03}.json", diff.chapter_id);

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return std::unexpected(std::string{"快照 JSON 分配失败"});
    }
    yyjson_mut_val* rootVal = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, rootVal);
    yyjson_mut_obj_add_int(doc, rootVal, "chapter_id", diff.chapter_id);
    yyjson_mut_obj_add_int(doc, rootVal, "committed_at", NowSec());
    yyjson_mut_obj_add_strncpy(doc, rootVal, "diff_hash", diffHash.data(), diffHash.size());
    yyjson_mut_obj_add_strncpy(doc, rootVal, "input_state_hash", diff.input_state_hash.data(),
                               diff.input_state_hash.size());
    // 涉及的实体级快照 id（Before 值在那几张行里，回滚按它们走）
    yyjson_mut_val* versionArr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, rootVal, "entity_version_ids", versionArr);
    for (const RowId id : versionIds) {
        yyjson_mut_arr_add_int(doc, versionArr, id);
    }
    // 原始 diff（作为**内嵌 JSON**放进快照，回滚时按它反推）
    const std::string diffJson = StateDiffToJson(diff);
    yyjson_mut_obj_add_val(doc, rootVal, "diff", yyjson_mut_raw(doc, diffJson.c_str()));

    std::size_t len = 0;
    char* text = yyjson_mut_val_write(rootVal, YYJSON_WRITE_PRETTY, &len);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) {
        return std::unexpected(std::string{"快照序列化失败"});
    }
    const bool written = util::WriteFileBytes(file, std::string_view{text, len});
    std::free(text);
    if (!written) {
        return std::unexpected(fmt::format("快照写盘失败：{}", util::PathToUtf8(file)));
    }
    return file;
}

// 解析 TempoRef（TempId → 真实 id）
[[nodiscard]] RowId ResolveTempo(const TempoRef& ref, const std::map<std::string, RowId>& tempIds) {
    if (!ref.temp_id.empty()) {
        const auto it = tempIds.find(ref.temp_id);
        return it == tempIds.end() ? 0 : it->second;
    }
    return ref.entity_id;
}

} // namespace

// ———— StateDiff 自身 ————

bool StateDiff::HasAnyDelta() const noexcept { return DeltaCount() > 0; }

std::size_t StateDiff::DeltaCount() const noexcept {
    return entities.size() + characters.size() + relationships.size() + items.size() +
           locations.size() + events.size() + causal.size() + plotlines.size() + foreshadows.size() +
           mysteries.size() + knowledge.size() + timeline.size();
}

std::string StateDiff::Hash() const {
    // 规范化 JSON（字段顺序由反射固定）→ FNV-1a → 16 位十六进制
    const std::string json = StateDiffToJson(*this);
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char ch : json) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 1099511628211ULL;
    }
    return fmt::format("{:016x}", hash);
}

std::string StateDiff::Summary() const {
    return fmt::format(
        "章={} 实体+{} 角色{} 关系{} 道具{} 事件{} 因果{} 情节线{} 伏笔{} 谜团{} 知识{} 时间轴{} 地点{}"
        "{}",
        chapter_id, entities.size(), characters.size(), relationships.size(), items.size(),
        events.size(), causal.size(), plotlines.size(), foreshadows.size(), mysteries.size(),
        knowledge.size(), timeline.size(), locations.size(),
        no_change_declared ? "（显式声明无变化）" : "");
}

std::string StateDiffToJson(const StateDiff& diff) {
    return util::reflect::ToJsonString(diff, false); // 紧凑：用于哈希与 `audit_logs.detail`
}

bool StateDiffFromJson(std::string_view text, StateDiff& out) {
    if (util::Trim(text).empty()) {
        return false;
    }
    yyjson_doc* probe = yyjson_read(text.data(), text.size(), 0);
    if (probe == nullptr) {
        return false;
    }
    const bool isObject = yyjson_is_obj(yyjson_doc_get_root(probe));
    yyjson_doc_free(probe);
    if (!isObject) {
        return false;
    }
    return util::reflect::FromJsonString(text, out) > 0;
}

// ———— 契约校验（`07` §2.2 的 ③，机器部分）————

std::vector<CommitIssue> ValidateStateDiff(db::sqlite::Database& db, const StateDiff& diff) {
    std::vector<CommitIssue> out;
    NovelGraph graph(db);

    if (diff.chapter_id <= 0) {
        out.push_back({"contract", "high", "StateDiff 缺 chapter_id"});
    }
    // D7：显式声明无变化时子数组必须为空
    if (diff.no_change_declared && diff.HasAnyDelta()) {
        out.push_back({"contract", "high",
                       fmt::format("D7：no_change_declared=true，但子数组非空（共 {} 条 delta）",
                                   diff.DeltaCount())});
    }

    std::set<std::string> declaredDeadNames;
    for (std::size_t i = 0; i < diff.entities.size(); ++i) {
        const NewEntityDelta& e = diff.entities[i];
        if (e.temp_id.empty()) {
            out.push_back({"contract", "high", fmt::format("entities[{}] 缺 temp_id", i)});
        }
        if (e.name.empty()) {
            out.push_back({"contract", "high", fmt::format("entities[{}]（temp_id={}）缺 name", i, e.temp_id)});
        }
        // D8：kind 必须在 31 种内
        if (e.kind.empty() || KnownKinds().find(e.kind) == KnownKinds().end()) {
            out.push_back({"contract", "high",
                           fmt::format("D8：NewEntity「{}」的 kind「{}」不在 31 种元类别内",
                                       e.name, e.kind)});
        }
        if (e.status == "dead" || e.status == "destroyed") {
            declaredDeadNames.insert(e.name);
        }
    }

    // 新实体还没写库 → character 只能引用**已存在**实体（D1）
    for (const CharacterDelta& c : diff.characters) {
        if (c.entity_id <= 0) {
            out.push_back({"contract", "high", "characters[] 缺 entity_id"});
            continue;
        }
        if (!graph.GetEntity(c.entity_id)) {
            out.push_back({"contract", "high",
                           fmt::format("D1：character.entity_id={} 既不在 entities 也没在本章 NewEntity 声明",
                                       c.entity_id)});
            continue;
        }
        if (c.reason.empty()) {
            out.push_back({"evidence_missing", "low",
                           fmt::format("character {} 没写 reason（状态变化必须有因）", c.entity_id)});
        }
        // D4：出现 dead/destroyed 必须显式声明
        const bool saysDead = Contains(c.body_state, "dead") || Contains(c.body_state, "destroyed") ||
                              Contains(c.mind_state, "dead") || Contains(c.mind_state, "destroyed");
        if (saysDead) {
            const auto entity = graph.GetEntity(c.entity_id);
            const std::string name = entity ? entity->name : std::string{};
            const bool declared = !name.empty() && declaredDeadNames.count(name) > 0;
            const bool alreadyDead = entity && (entity->status == "dead" || entity->status == "destroyed");
            if (!declared && !alreadyDead) {
                out.push_back({"high", "high",
                               fmt::format("D4：character {} 出现 dead/destroyed，但没有 entities[] 里的 "
                                           "status 变更声明（`02` §2.5 的 entities 只有 NewEntity —— "
                                           "既有实体的状态变更需由调用方另行声明）",
                                           c.entity_id)});
            }
        }
    }

    // 事件参与者的 entity_id 必须存在
    for (const EventDelta& ev : diff.events) {
        for (const EventParticipantDelta& p : ev.participants) {
            if (p.entity_id > 0 && !graph.GetEntity(p.entity_id)) {
                out.push_back({"contract", "high",
                               fmt::format("D1：事件「{}」的参与者 entity_id={} 不存在", ev.temp_id, p.entity_id)});
            }
        }
    }
    return out;
}

std::string CommitGateReport::Describe() const {
    return fmt::format("G1={} G2={} G3={} G4={} G5={}", g1_review_pass ? 1 : 0, g2_checks_pass ? 1 : 0,
                       g3_snapshot_ready ? 1 : 0, g4_diff_valid ? 1 : 0, g5_no_high_issue ? 1 : 0);
}

// ———— 提交 ————

bool ChapterAlreadyCommitted(db::sqlite::Database& db, RowId chapterId, std::string_view diffHash) {
    if (chapterId <= 0 || diffHash.empty()) {
        return false;
    }
    auto st = db.Prepare(
        "SELECT COUNT(*) FROM audit_logs WHERE action='commit_chapter' AND target_kind='chapter' "
        "AND target_id=?1 AND detail LIKE ?2");
    if (!st) {
        return false;
    }
    (void)st->BindInt(1, chapterId);
    (void)st->BindText(2, fmt::format("%diff_hash={}%", diffHash));
    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
        return st->ColumnInt(0) > 0;
    }
    return false;
}

CommitResult CommitChapterState(db::sqlite::Database& db, const StateDiff& diff,
                                const CommitContext& ctx) {
    CommitResult out;
    out.diff_hash = diff.Hash();
    out.gates.issues = ValidateStateDiff(db, diff);
    for (const CommitIssue& issue : ctx.upstream_issues) {
        out.gates.issues.push_back(issue);
    }

    if (diff.chapter_id <= 0) {
        out.error = "StateDiff 缺 chapter_id";
        return out;
    }
    // 模式校验放在幂等判定**之前**：模式本身非法是"参数错"，不该被"已提交过"盖掉
    if (ctx.canon_mode == "auto") {
        // `07` §2.5 C5：禁止在 auto 下跳门禁；本 S 不做 auto（留 S9 的运行模式）
        out.error = "canon_mode=auto 尚未实现（07 §2.5 的无人值守 Canon 属 S9 的运行模式）；请用 manual";
        return out;
    }
    // I1：同章 + 同 diff 哈希 → 跳过（幂等）
    if (ChapterAlreadyCommitted(db, diff.chapter_id, out.diff_hash)) {
        out.ok = true;
        out.skipped = true;
        out.gates.ok = true;
        log::Info("章节 {} 的这份 StateDiff 已提交过（diff_hash={}），按幂等跳过", diff.chapter_id,
                  out.diff_hash);
        return out;
    }

    const auto hasHigh = [&out]() {
        for (const CommitIssue& issue : out.gates.issues) {
            if (issue.severity == "high") {
                return true;
            }
        }
        return false;
    };

    // G4：契约校验通过且非空（或显式声明无变化）
    out.gates.g4_diff_valid = !hasHigh() && (diff.HasAnyDelta() || diff.no_change_declared);
    // G1 / G2 / G5
    out.gates.g1_review_pass = ctx.review_pass;
    out.gates.g2_checks_pass = ctx.machine_checks_pass && !hasHigh();
    out.gates.g5_no_high_issue = !hasHigh();

    // 涉及实体（用于提交前快照）
    std::set<RowId> touched;
    for (const CharacterDelta& c : diff.characters) {
        touched.insert(c.entity_id);
    }
    for (const RelationDelta& r : diff.relationships) {
        touched.insert(r.from_id);
        touched.insert(r.to_id);
    }
    for (const ItemDelta& it : diff.items) {
        touched.insert(it.owner_id);
        touched.insert(it.item_id);
    }
    for (const KnowledgeDelta& k : diff.knowledge) {
        touched.insert(k.entity_id);
    }
    for (const EventDelta& ev : diff.events) {
        for (const EventParticipantDelta& p : ev.participants) {
            touched.insert(p.entity_id);
        }
    }
    touched.erase(0);

    // G3 / 不变式 I11：**提交前**必须已有快照。实体级快照在事务里采（Before 值），
    // 章级快照文件在事务外先落盘；写失败即阻断。
    const auto snapshot = WriteChapterSnapshot(ctx.snapshot_dir, diff, out.diff_hash,
                                               out.entity_version_ids);
    if (!snapshot) {
        out.gates.g3_snapshot_ready = false;
        out.error = snapshot.error();
    } else {
        out.gates.g3_snapshot_ready = true;
        out.snapshot_path = util::PathToUtf8(*snapshot);
    }

    out.gates.ok = out.gates.g1_review_pass && out.gates.g2_checks_pass &&
                   out.gates.g3_snapshot_ready && out.gates.g4_diff_valid && out.gates.g5_no_high_issue;
    if (!out.gates.ok) {
        std::string reason;
        for (const CommitIssue& issue : out.gates.issues) {
            if (issue.severity == "high") {
                reason = fmt::format("；{}：{}", issue.code, issue.detail);
                break;
            }
        }
        out.error = fmt::format("提交门禁未通过（{}）{}", out.gates.Describe(),
                                out.error.empty() ? reason : ("；" + out.error));
        return out;
    }

    // ———— 14 块事务 ————
    NovelGraph graph(db);
    if (auto begin = db.Exec("BEGIN IMMEDIATE"); !begin) {
        out.error = fmt::format("BEGIN 失败：{}", begin.error().message);
        return out;
    }
    const auto fail = [&](std::string message) {
        (void)db.Exec("ROLLBACK");
        out.ok = false;
        out.error = std::move(message);
        return out;
    };

    std::map<std::string, RowId> tempIds;

    // 块 0（写在其它块之前）：被改实体的 **Before 快照**（`07` §2.4「章级快照内容 = diff + Before 值」）
    for (const RowId id : touched) {
        if (auto saved = graph.SnapshotEntity(id, fmt::format("ch{} commit (before)", diff.chapter_id));
            saved) {
            out.entity_version_ids.push_back(*saved);
        }
    }
    out.applied.push_back(fmt::format("快照: 实体 {} 个", out.entity_version_ids.size()));

    // 块 1：entities（先插入，回填 TempId → 真实 id）
    for (const NewEntityDelta& e : diff.entities) {
        EntityRow row;
        row.kind = e.kind;
        row.name = e.name;
        row.summary = e.summary;
        row.status = e.status.empty() ? "active" : e.status;
        row.created_chapter = e.created_chapter > 0 ? e.created_chapter : diff.chapter_id;
        auto id = graph.UpsertEntity(row);
        if (!id) {
            return fail(fmt::format("块 1 entities 失败：{}", id.error().message));
        }
        if (!e.temp_id.empty()) {
            tempIds[e.temp_id] = *id;
        }
        out.applied.push_back(fmt::format("entities: +1（{} → #{}）", e.name, *id));
    }

    // 块 2：character_status（I2：先删同 (entity_id, chapter_id) 再插，重复提交不叠加）
    for (const CharacterDelta& c : diff.characters) {
        if (auto del = db.Prepare("DELETE FROM character_status WHERE entity_id=?1 AND chapter_id=?2");
            del) {
            (void)del->BindInt(1, c.entity_id);
            (void)del->BindInt(2, diff.chapter_id);
            if (auto s = del->Step(); !s) {
                return fail(fmt::format("块 2 清理旧状态失败：{}", s.error().message));
            }
        }
        CharacterStatusRow row;
        row.entity_id = c.entity_id;
        row.chapter_id = diff.chapter_id;
        row.location_id = c.location_id;
        row.body_state = c.body_state;
        row.mind_state = c.mind_state;
        row.emotion_json = c.emotion_json.empty() ? "{}" : c.emotion_json;
        row.goal = c.goal;
        row.relation_note = c.relation_note;
        row.resource_note = c.resource_note;
        row.secret_note = c.secret_note;
        if (auto id = graph.UpsertCharacterStatus(row); !id) {
            return fail(fmt::format("块 2 character_status 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("character_status: 实体 #{} @ch{}", c.entity_id,
                                          diff.chapter_id));
    }

    // 块 3：relations（upsert / close）
    for (const RelationDelta& r : diff.relationships) {
        if (r.op == "close") {
            auto st = db.Prepare(
                "UPDATE relations SET to_chapter=?1 WHERE from_id=?2 AND to_id=?3 AND rel_type=?4 "
                "AND (to_chapter=0 OR to_chapter>?1)");
            if (!st) {
                return fail(fmt::format("块 3 close 准备失败：{}", st.error().message));
            }
            (void)st->BindInt(1, diff.chapter_id);
            (void)st->BindInt(2, r.from_id);
            (void)st->BindInt(3, r.to_id);
            (void)st->BindText(4, r.rel_type);
            if (auto s = st->Step(); !s) {
                return fail(fmt::format("块 3 close 失败：{}", s.error().message));
            }
            out.applied.push_back(fmt::format("relations: close #{}→#{}", r.from_id, r.to_id));
            continue;
        }
        RelationRow row;
        row.from_id = r.from_id;
        row.to_id = r.to_id;
        row.rel_type = r.rel_type;
        row.strength = r.strength;
        row.from_chapter = diff.chapter_id;
        row.reason = r.reason;
        if (auto id = graph.UpsertRelation(row); !id) {
            return fail(fmt::format("块 3 relations 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("relations: upsert #{}→#{}（{}）", r.from_id, r.to_id, r.rel_type));
    }

    // 块 4：entity_ownerships（acquire / lose）
    for (const ItemDelta& it : diff.items) {
        if (it.op == "lose") {
            auto st = db.Prepare(
                "UPDATE entity_ownerships SET to_chapter=?1 WHERE owner_id=?2 AND item_id=?3 AND "
                "to_chapter=0");
            if (!st) {
                return fail(fmt::format("块 4 lose 准备失败：{}", st.error().message));
            }
            (void)st->BindInt(1, diff.chapter_id);
            (void)st->BindInt(2, it.owner_id);
            (void)st->BindInt(3, it.item_id);
            if (auto s = st->Step(); !s) {
                return fail(fmt::format("块 4 lose 失败：{}", s.error().message));
            }
            out.applied.push_back(fmt::format("ownerships: lose #{}↛#{}", it.owner_id, it.item_id));
            continue;
        }
        if (it.op != "acquire") {
            out.applied.push_back(fmt::format("ownerships: {} 不改表（move/change_state 无落地表）", it.op));
            continue;
        }
        OwnershipRow row;
        row.owner_id = it.owner_id;
        row.item_id = it.item_id;
        row.from_chapter = diff.chapter_id;
        row.how = it.how;
        row.note = it.reason;
        if (auto id = graph.UpsertOwnership(row); !id) {
            return fail(fmt::format("块 4 ownerships 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("ownerships: acquire #{}←#{}", it.item_id, it.owner_id));
    }

    // 块 5：event_details + event_participants
    for (const EventDelta& ev : diff.events) {
        EntityRow entity;
        entity.kind = "event";
        entity.name = ev.action.empty() ? fmt::format("ch{} 事件", diff.chapter_id) : ev.action;
        entity.summary = ev.result;
        entity.created_chapter = diff.chapter_id;
        EventDetailRow detail;
        detail.time_label = ev.time_label;
        detail.location_id = ev.location_id;
        detail.cause_note = ev.cause;
        detail.result_note = ev.result;
        auto id = graph.UpsertEvent(entity, detail);
        if (!id) {
            return fail(fmt::format("块 5 事件失败：{}", id.error().message));
        }
        if (!ev.temp_id.empty()) {
            tempIds[ev.temp_id] = *id;
        }
        for (const EventParticipantDelta& p : ev.participants) {
            if (auto added = graph.UpsertEventParticipant({.event_id = *id, .entity_id = p.entity_id,
                                                          .role = p.role});
                !added) {
                return fail(fmt::format("块 5 参与者失败：{}", added.error().message));
            }
        }
        out.applied.push_back(fmt::format("events: +1（{} → #{}，{} 名参与者）", ev.temp_id, *id,
                                          ev.participants.size()));
    }

    // 块 6：causal_links（TempId → 真实 id）
    for (const CausalDelta& cd : diff.causal) {
        const RowId cause = ResolveTempo(cd.cause, tempIds);
        const RowId effect = ResolveTempo(cd.effect, tempIds);
        if (cause <= 0 || effect <= 0) {
            return fail(fmt::format("块 6 因果链的端点解析不到真实 id（cause={} effect={}）", cause, effect));
        }
        if (auto id = graph.UpsertCausalLink({.cause_event_id = cause,
                                             .effect_event_id = effect,
                                             .link_type = cd.link_type,
                                             .note = cd.note});
            !id) {
            return fail(fmt::format("块 6 因果失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("causal: #{}→#{}（{}）", cause, effect, cd.link_type));
    }

    // 块 7：foreshadowings
    for (const ForeshadowDelta& f : diff.foreshadows) {
        ForeshadowRow row;
        row.id = f.foreshadow_id;
        row.title = f.title;
        row.content = f.content;
        row.status = f.status;
        row.setup_ch = f.setup_ch;
        row.payoff_ch = f.payoff_ch;
        row.importance = f.importance;
        row.truth = f.truth;
        if (auto id = graph.UpsertForeshadow(row); !id) {
            return fail(fmt::format("块 7 伏笔失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("foreshadows: {} #{} → {}", f.op, f.foreshadow_id, f.status));
    }

    // 块 8：mysteries + mystery_beats
    for (const MysteryDelta& m : diff.mysteries) {
        MysteryRow row;
        row.id = m.mystery_id;
        row.question = m.question;
        row.answer = m.answer;
        row.status = m.status;
        row.ask_ch = diff.chapter_id;
        auto id = graph.UpsertMystery(row);
        if (!id) {
            return fail(fmt::format("块 8 谜团失败：{}", id.error().message));
        }
        if (!m.beat.content.empty() || !m.beat.beat_type.empty()) {
            if (auto beat = graph.UpsertMysteryBeat({.mystery_id = *id,
                                                    .beat_type = m.beat.beat_type,
                                                    .chapter_id = diff.chapter_id,
                                                    .content = m.beat.content,
                                                    .target_entity_id = m.beat.target_entity_id});
                !beat) {
                return fail(fmt::format("块 8 谜团节拍失败：{}", beat.error().message));
            }
        }
        out.applied.push_back(fmt::format("mysteries: #{}（{}）", *id, m.status));
    }

    // 块 9：plots + plot_beats
    for (const PlotDelta& pd : diff.plotlines) {
        PlotRow row;
        row.id = pd.plot_id;
        row.kind = pd.kind;
        row.title = pd.title;
        row.status = pd.status;
        row.intro_ch = diff.chapter_id;
        auto id = graph.UpsertPlot(row);
        if (!id) {
            return fail(fmt::format("块 9 剧情线失败：{}", id.error().message));
        }
        if (!pd.beat.title.empty() || !pd.beat.summary.empty()) {
            if (auto beat = graph.UpsertPlotBeat({.plot_id = *id,
                                                 .chapter_id = diff.chapter_id,
                                                 .ord = 0,
                                                 .beat_type = pd.beat.beat_type,
                                                 .title = pd.beat.title,
                                                 .summary = pd.beat.summary});
                !beat) {
                return fail(fmt::format("块 9 剧情线节拍失败：{}", beat.error().message));
            }
        }
        out.applied.push_back(fmt::format("plots: #{}（{}）", *id, pd.status));
    }

    // 块 10：character_knowledge
    for (const KnowledgeDelta& k : diff.knowledge) {
        if (auto id = graph.UpsertKnowledge({.entity_id = k.entity_id,
                                            .fact_kind = k.fact_kind,
                                            .fact_id = k.fact_id,
                                            .fact_text = k.fact_text,
                                            .knows = k.knows ? 1 : 0,
                                            .chapter_known = k.chapter_known > 0 ? k.chapter_known
                                                                                 : diff.chapter_id});
            !id) {
            return fail(fmt::format("块 10 知情失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("knowledge: 实体 #{} {}={}", k.entity_id, k.fact_kind,
                                          k.knows ? "知道" : "不知道"));
    }

    // 块 11：chapters（**本章已完成**的标志 → 放最后一批写）
    if (!ctx.chapter_body.empty()) {
        ChapterRow row;
        if (auto cur = graph.GetChapter(diff.chapter_id); cur) {
            row = *cur;
        }
        row.id = diff.chapter_id;
        row.body = ctx.chapter_body;
        row.words = ctx.chapter_words > 0 ? ctx.chapter_words
                                          : static_cast<int>(ctx.chapter_body.size());
        row.summary = ctx.chapter_summary;
        row.status = "done";
        if (auto id = graph.UpsertChapter(row); !id) {
            return fail(fmt::format("块 11 章节落盘失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("chapters: 正文 {} 字 + status=done", row.words));
    } else {
        out.applied.push_back("chapters: 跳过（本次没带正文）");
    }

    // 块 12：entity_versions —— **已在块 0 采过 Before 值**（见上），这里只补记账
    out.applied.push_back(fmt::format("entity_versions: {} 条（提交前采集）",
                                      out.entity_version_ids.size()));

    // 块 13：canon_logs（manual → PROPOSED；**不做自动 Canon**）
    if (auto canon = graph.SetCanon("chapter", diff.chapter_id, "PROPOSED",
                                    fmt::format("diff_hash={} verdict={}", out.diff_hash,
                                                ctx.review_verdict));
        !canon) {
        return fail(fmt::format("块 13 canon_logs 失败：{}", canon.error().message));
    }
    out.applied.push_back("canon_logs: chapter → PROPOSED（manual）");

    // 块 14：audit_logs（汇总一条；`detail` 内含 diff_hash 供幂等判定）
    if (auto audit = graph.LogAudit(
            "agent", "commit_chapter", "chapter", diff.chapter_id,
            fmt::format("diff_hash={} {}", out.diff_hash, diff.Summary()));
        !audit) {
        return fail(fmt::format("块 14 audit_logs 失败：{}", audit.error().message));
    }
    out.applied.push_back("audit_logs: commit_chapter +1");

    if (auto commit = db.Exec("COMMIT"); !commit) {
        (void)db.Exec("ROLLBACK");
        out.ok = false;
        out.error = fmt::format("COMMIT 失败：{}", commit.error().message);
        return out;
    }
    out.ok = true;
    log::Info("章节 {} 状态回写提交成功：{}（diff_hash={}）", diff.chapter_id, diff.Summary(),
              out.diff_hash);
    return out;
}

// ———— 自检 ————

int RunCommitSelfCheck() {
    int fail = 0;
    const auto expect = [&](bool cond, std::string_view name) {
        if (cond) {
            log::Info("S8 commit PASS {}", name);
        } else {
            ++fail;
            log::Error("S8 commit FAIL {}", name);
        }
    };

    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("S8 commit FAIL 内存库打开失败");
        return fail + 1;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("S8 commit FAIL 建表失败 {}", r.error().message);
        return fail + 1;
    }

    const std::filesystem::path snapRoot =
        std::filesystem::temp_directory_path() / "shine_s8_commit_check";
    std::error_code ec;
    std::filesystem::remove_all(snapRoot, ec);

    NovelGraph g(mem);
    auto lin = g.UpsertEntity({.kind = std::string{kind::person}, .name = "林默"});
    auto foe = g.UpsertEntity({.kind = std::string{kind::person}, .name = "裴照"});
    auto loc = g.UpsertEntity({.kind = std::string{kind::location}, .name = "黑森林"});
    auto ring = g.UpsertEntity({.kind = std::string{kind::item}, .name = "黑戒指"});
    auto chapter = g.UpsertChapter({.ord = 1, .title = "雨夜", .pov_entity_id = lin.value_or(0)});
    if (!lin || !foe || !loc || !ring || !chapter) {
        log::Error("S8 commit FAIL 前置数据失败");
        return fail + 1;
    }

    // 一份「有实质变化」的 StateDiff
    StateDiff diff;
    diff.chapter_id = *chapter;
    diff.input_state_hash = "sha1:s8selfcheck";
    diff.entities.push_back({.temp_id = "en:1",
                             .kind = "person",
                             .name = "北境密使",
                             .summary = "入城联络的密探",
                             .status = "active",
                             .created_chapter = *chapter});
    diff.characters.push_back({.entity_id = *lin,
                               .location_id = *loc,
                               .body_state = "右手旧伤复发",
                               .mind_state = "高度警觉",
                               .emotion_json = R"({"fear":70,"trust":20})",
                               .goal = "查明密使身份",
                               .relation_note = "对裴照保持戒心",
                               .resource_note = "持有笔迹抄本",
                               .secret_note = "卧底身份未暴露",
                               .reason = "第 1 章夜间潜入军械库"});
    diff.relationships.push_back({.op = "upsert",
                                  .from_id = *lin,
                                  .to_id = *foe,
                                  .rel_type = "rival",
                                  .strength = 30,
                                  .reason = "目击与被目击"});
    diff.items.push_back({.op = "acquire",
                          .item_id = *ring,
                          .owner_id = *lin,
                          .how = "抄录",
                          .reason = "取证"});
    diff.events.push_back({.temp_id = "ev:1",
                           .cause = "第三笔无主支出",
                           .participants = {{.entity_id = *lin, .role = "actor"}},
                           .location_id = *loc,
                           .time_label = "昭元三年·冬·第三夜",
                           .action = "认定笔迹归属",
                           .result = "取得抄本"});
    diff.causal.push_back({.cause = {.temp_id = "ev:1"},
                           .effect = {.entity_id = *lin},
                           .link_type = "reveals",
                           .note = "自检：TempId 解析用"});
    diff.foreshadows.push_back({.op = "progress",
                                .foreshadow_id = 0,
                                .title = "无主支出",
                                .content = "第三笔支出经手人",
                                .truth = "北境密使",
                                .status = "DEVELOPING",
                                .setup_ch = 1,
                                .payoff_ch = 40,
                                .importance = 80});
    diff.mysteries.push_back({.mystery_id = 0,
                              .question = "谁在挪用军费",
                              .answer = "北境密使",
                              .status = "hinted",
                              .beat = {.beat_type = "hint", .content = "账目第三笔对不上"}});
    diff.plotlines.push_back({.plot_id = 0,
                              .kind = "main",
                              .title = "追查密使",
                              .status = "active",
                              .beat = {.beat_type = "setup", .title = "发现异常", .summary = "账目缺口"}});
    diff.knowledge.push_back({.entity_id = *lin,
                              .fact_kind = "secret",
                              .fact_id = 5,
                              .fact_text = "北境密使已入城",
                              .knows = true,
                              .chapter_known = *chapter});

    CommitContext ctx;
    ctx.chapter_id = *chapter;
    ctx.review_verdict = "PASS";
    ctx.snapshot_dir = util::PathToUtf8(snapRoot);
    ctx.chapter_body = "雪原上只剩下风声。";
    ctx.chapter_summary = "林默在雨夜潜入军械库。";

    // ① 门禁：G1 不满足 → **拒绝提交**，且库里一行都不该有
    {
        CommitContext bad = ctx;
        bad.review_pass = false;
        const CommitResult r = CommitChapterState(mem, diff, bad);
        auto statusCount = mem.Prepare("SELECT COUNT(*) FROM character_status");
        int rows = -1;
        if (statusCount && statusCount->Step()) {
            rows = static_cast<int>(statusCount->ColumnInt(0));
        }
        expect(!r.ok && !r.gates.g1_review_pass && rows == 0,
               "G1 未过 → 拒绝提交且不写库（character_status 仍为 0 行）");
    }
    // ② 门禁：没给 snapshot_dir → G3 不过（不变式 I11）
    {
        CommitContext noSnap = ctx;
        noSnap.review_pass = true;
        noSnap.snapshot_dir.clear();
        const CommitResult r = CommitChapterState(mem, diff, noSnap);
        expect(!r.ok && !r.gates.g3_snapshot_ready, "缺快照 → 拒绝提交（G3）");
    }
    // ③ 正常提交：14 块都写到
    CommitResult first;
    {
        CommitContext ok = ctx;
        ok.review_pass = true;
        first = CommitChapterState(mem, diff, ok);
        expect(first.ok && !first.skipped, "门禁齐备 → 提交成功");
        expect(first.gates.g1_review_pass && first.gates.g2_checks_pass &&
                   first.gates.g3_snapshot_ready && first.gates.g4_diff_valid &&
                   first.gates.g5_no_high_issue,
               "G1–G5 逐条为真");
        expect(!first.entity_version_ids.empty(), "提交前写了实体级快照（Before 值）");
    }
    // ④ 世界状态**确有变化**（这是本 S 的核心判据）
    {
        auto status = g.GetLatestCharacterStatus(*lin, *chapter);
        auto fs = g.ListOpenForeshadows();
        auto rels = g.GetRelations(*lin);
        auto owns = g.GetOwnerships(*lin);
        auto know = g.CharacterKnows(*lin, "secret", 5, *chapter);
        bool hasEvent = false;
        if (auto st = mem.Prepare("SELECT COUNT(*) FROM event_participants WHERE entity_id=?1");
            st) {
            (void)st->BindInt(1, *lin);
            if (st->Step()) {
                hasEvent = st->ColumnInt(0) > 0;
            }
        }
        bool hasCanon = false;
        if (auto st = mem.Prepare(
                "SELECT COUNT(*) FROM canon_logs WHERE target_kind='chapter' AND target_id=?1 "
                "AND status='PROPOSED'");
            st) {
            (void)st->BindInt(1, *chapter);
            if (st->Step()) {
                hasCanon = st->ColumnInt(0) > 0;
            }
        }
        expect(status && status->body_state == "右手旧伤复发" && status->location_id == *loc,
               "character_status 有本章新行");
        expect(fs && fs->size() == 1 && (*fs)[0].status == "DEVELOPING", "foreshadowings 新增且状态前进");
        expect(rels && !rels->empty(), "relations 写入");
        expect(owns && !owns->empty(), "entity_ownerships 写入");
        expect(know && *know, "character_knowledge 写入且按章可判");
        expect(hasEvent, "event_participants 写入");
        expect(hasCanon, "canon_logs 写 PROPOSED（manual，不自动升 CANON）");
        auto chRow = g.GetChapter(*chapter);
        expect(chRow && chRow->status == "done" && !chRow->body.empty(), "chapters 落正文 + status=done");
    }
    // ⑤ 章级快照文件落盘（不变式 I11）
    {
        const auto path = std::filesystem::path{util::PathFromUtf8(first.snapshot_path)};
        const auto bytes = util::ReadFileBytes(path);
        const std::string text = bytes ? *bytes : std::string{};
        expect(!first.snapshot_path.empty() && std::filesystem::exists(path) &&
                   text.find(first.diff_hash) != std::string::npos &&
                   text.find("\"diff\"") != std::string::npos &&
                   text.find("entity_version_ids") != std::string::npos,
               "章级快照 ch001.json 落盘且含 diff_hash / diff / 快照 id");
    }
    // ⑥ 幂等：同一份 diff 再提交 → 跳过，且**行数不变**
    {
        const auto countOf = [&mem](std::string_view table) {
            auto st = mem.Prepare(fmt::format("SELECT COUNT(*) FROM {}", table));
            if (st && st->Step()) {
                return st->ColumnInt(0);
            }
            return static_cast<RowId>(-1);
        };
        const RowId statusBefore = countOf("character_status");
        const RowId auditBefore = countOf("audit_logs");
        const RowId versionsBefore = countOf("entity_versions");
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult second = CommitChapterState(mem, diff, ok);
        expect(second.ok && second.skipped, "重复提交 → 幂等跳过");
        expect(countOf("character_status") == statusBefore && countOf("audit_logs") == auditBefore &&
                   countOf("entity_versions") == versionsBefore,
               "幂等：character_status / audit_logs / entity_versions 都不增加");
    }
    // ⑦ D 规则：no_change_declared 与死人不声明 → 拒绝
    {
        StateDiff contradictory;
        contradictory.chapter_id = *chapter;
        contradictory.no_change_declared = true;
        contradictory.characters.push_back({.entity_id = *lin, .reason = "x"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, contradictory, ok);
        expect(!r.ok && !r.gates.g4_diff_valid, "D7：声明无变化却带 delta → 拒绝");
    }
    {
        StateDiff death;
        death.chapter_id = *chapter;
        death.characters.push_back({.entity_id = *foe, .body_state = "dead", .reason = "被杀"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, death, ok);
        expect(!r.ok, "D4：死人没在 entities 里声明 → 拒绝");
    }
    {
        StateDiff badKind;
        badKind.chapter_id = *chapter;
        badKind.entities.push_back({.temp_id = "en:9", .kind = "魂环", .name = "千年魂环"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, badKind, ok);
        expect(!r.ok, "D8：kind 不在 31 种内 → 拒绝");
    }
    // ⑧ auto 模式明确不做
    {
        CommitContext autoMode = ctx;
        autoMode.review_pass = true;
        autoMode.canon_mode = "auto";
        const CommitResult r = CommitChapterState(mem, diff, autoMode);
        expect(!r.ok && r.error.find("auto") != std::string::npos, "auto 模式未实现 → 明确拒绝（不静默降级）");
    }
    // ⑨ diff 存读往返（`audit_logs.detail` / 快照都用它）
    {
        StateDiff back;
        const std::string json = StateDiffToJson(diff);
        expect(StateDiffFromJson(json, back) && back.Hash() == diff.Hash() &&
                   back.characters.size() == diff.characters.size() &&
                   back.causal.size() == 1 && back.causal[0].cause.temp_id == "ev:1",
               "StateDiff JSON 往返一致（含嵌套 TempoRef）");
        StateDiff broken;
        expect(!StateDiffFromJson("{ not json", broken), "非法 JSON → 解析失败");
    }

    std::filesystem::remove_all(snapRoot, ec);
    if (fail == 0) {
        log::Info("S8 提交自检通过（门禁 G1–G5 / 14 块写入 / 幂等 / 快照 / D 规则 / auto 拒绝）");
    }
    return fail;
}

} // namespace shine::novelcore
