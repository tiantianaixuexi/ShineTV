#include "novel/NovelCommit.h"

#include "core/Log.h"
#include "novel/NovelChecks.h"
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

// ★ S65：引用字段的**统一解析** —— `*_id` 优先（已存在的库 id），否则查本章 TempId 映射
//（`entities[]` 在块 1 先落库并回填 `tempIds`，所以块 2–5 解析时映射已就绪）。
[[nodiscard]] RowId ResolveRef(RowId id, const std::string& tempId,
                               const std::map<std::string, RowId>& tempIds) {
    if (id > 0) {
        return id;
    }
    if (tempId.empty()) {
        return 0;
    }
    const auto it = tempIds.find(tempId);
    return it == tempIds.end() ? 0 : it->second;
}

// 本章声明过的 TempId 集合（`entities[]` + `events[]`）—— 门禁用它判"temp 引用是否合法"。
[[nodiscard]] std::set<std::string> DeclaredTempIds(const StateDiff& diff) {
    std::set<std::string> out;
    for (const NewEntityDelta& e : diff.entities) {
        if (!e.temp_id.empty()) {
            out.insert(e.temp_id);
        }
    }
    for (const EventDelta& e : diff.events) {
        if (!e.temp_id.empty()) {
            out.insert(e.temp_id);
        }
    }
    return out;
}

// 校验一个引用字段：`*_id` 与 `*_temp_id` **二选一**。返回错误文案（空 = 通过）。
// `required`：必填字段在"两个都没填"时必须报错（如 `participants[].entity_id`）。
[[nodiscard]] std::string CheckRef(const char* where, RowId id, const std::string& tempId,
                                   const std::set<std::string>& declared, bool required) {
    if (id > 0) {
        return {}; // 已存在的库 id：存在性与 kind 由 K02/K03 判（不在这里重复）
    }
    if (!tempId.empty()) {
        return declared.count(tempId) > 0
                   ? std::string{}
                   : fmt::format("{} 引用的 temp_id「{}」没在本章 entities[] / events[] 里声明",
                                 where, tempId);
    }
    return required ? fmt::format("{} 既没填已有实体 id，也没填本章 temp_id（二者必有一）", where)
                    : std::string{};
}

// ★ S65：**宽容读** —— `*_id` 字段写成**字符串 TempId**（如 `"entity_id":"en:7"`）也认。
// 为什么必须容忍两种写法：`util::reflect` 是**机械映射**（标量键 ↔ 标量字段），字符串读不进
// `RowId` ⇒ 会**静默留 0**（引用凭空消失）。而模型很自然地会这么写 —— 它产出的
// `entities[].temp_id` 本就是字符串 `"en:7"`，而我们又要求它**别把序号 7 当 id** ⇒ 两条路它都会试。
// 于是：数字 → `*_id`（反射负责）；字符串 TempId → 这里回填到 `*_temp_id`。
[[nodiscard]] bool IsTempIdLike(std::string_view s) {
    const auto colon = s.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= s.size()) {
        return false;
    }
    for (std::size_t i = 0; i < colon; ++i) {
        const char ch = s[i];
        if (!(ch >= 'a' && ch <= 'z') && ch != '_') {
            return false;
        }
    }
    for (std::size_t i = colon + 1; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

// 取对象里某个键的**字符串 TempId**（不是字符串 / 不像 TempId → 空）
[[nodiscard]] std::string TempIdStr(yyjson_val* obj, const char* key) {
    yyjson_val* v = obj == nullptr ? nullptr : yyjson_obj_get(obj, key);
    if (v == nullptr || !yyjson_is_str(v)) {
        return {};
    }
    const std::string_view sv{yyjson_get_str(v), yyjson_get_len(v)};
    return IsTempIdLike(sv) ? std::string{sv} : std::string{};
}

[[nodiscard]] yyjson_val* ArrOf(yyjson_val* root, const char* key) {
    yyjson_val* v = yyjson_obj_get(root, key);
    return (v != nullptr && yyjson_is_arr(v)) ? v : nullptr;
}

// ⚠️ **必须在 `util::reflect::FromJsonString` 之后调用** —— 它按**反射后的数组下标**对齐。
void FillStringRefs(yyjson_val* root, StateDiff& out) {
    if (root == nullptr || !yyjson_is_obj(root)) {
        return;
    }
    const auto each = [](yyjson_val* arr, const auto& fn) {
        if (arr == nullptr) {
            return;
        }
        yyjson_arr_iter it = yyjson_arr_iter_with(arr);
        std::size_t i = 0;
        while (yyjson_val* o = yyjson_arr_iter_next(&it)) {
            fn(o, i++);
        }
    };
    each(ArrOf(root, "characters"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.characters.size()) {
            return;
        }
        if (out.characters[i].entity_temp_id.empty()) {
            out.characters[i].entity_temp_id = TempIdStr(o, "entity_id");
        }
        if (out.characters[i].location_temp_id.empty()) {
            out.characters[i].location_temp_id = TempIdStr(o, "location_id");
        }
    });
    each(ArrOf(root, "relationships"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.relationships.size()) {
            return;
        }
        if (out.relationships[i].from_temp_id.empty()) {
            out.relationships[i].from_temp_id = TempIdStr(o, "from_id");
        }
        if (out.relationships[i].to_temp_id.empty()) {
            out.relationships[i].to_temp_id = TempIdStr(o, "to_id");
        }
    });
    each(ArrOf(root, "items"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.items.size()) {
            return;
        }
        if (out.items[i].item_temp_id.empty()) {
            out.items[i].item_temp_id = TempIdStr(o, "item_id");
        }
        if (out.items[i].owner_temp_id.empty()) {
            out.items[i].owner_temp_id = TempIdStr(o, "owner_id");
        }
        if (out.items[i].location_temp_id.empty()) {
            out.items[i].location_temp_id = TempIdStr(o, "location_id");
        }
    });
    each(ArrOf(root, "knowledge"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.knowledge.size()) {
            return;
        }
        if (out.knowledge[i].entity_temp_id.empty()) {
            out.knowledge[i].entity_temp_id = TempIdStr(o, "entity_id");
        }
    });
    each(ArrOf(root, "events"), [&](yyjson_val* o, std::size_t i) {
        if (i >= out.events.size()) {
            return;
        }
        if (out.events[i].location_temp_id.empty()) {
            out.events[i].location_temp_id = TempIdStr(o, "location_id");
        }
        each(ArrOf(o, "participants"), [&](yyjson_val* p, std::size_t j) {
            if (j >= out.events[i].participants.size()) {
                return;
            }
            if (out.events[i].participants[j].entity_temp_id.empty()) {
                out.events[i].participants[j].entity_temp_id = TempIdStr(p, "entity_id");
            }
        });
    });
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
        // S55：**区分"JSON 语法就过不了"和"语法过了但字段映射不上"** —— 原先三者都返回
        // false、调用方只能笼统地说"不是合法的 StateDiff JSON"，实测时把我自己也误导了
        //（先误判成"markdown 围栏"，其实围栏早修好了）。有区分才知道该改提示词还是改映射。
        log::Warn("StateDiffFromJson：JSON 语法解析失败（{} 字）—— 检查是否被截断/含裸引号",
                  text.size());
        return false;
    }
    const bool isObject = yyjson_is_obj(yyjson_doc_get_root(probe));
    // ★ S61：**契约外的顶层键必须可见**。宽容读取（缺键走默认）是刻意的，但它会把
    // "键名根本不属于这套契约"也一并吞掉：实测 Extractor 的提示词写的是
    // `new_entities` / `foreshadow_updates`（契约里是 `entities` / `foreshadows`）⇒ 模型报的
    // 新人物/新伏笔被**静默丢弃**，只有同名的 `events` 落了库（四章 diff 全是 events=6、
    // 其余 0，库里 person 恒 1）。⇒ 契约外的键一律告警，键名一错第一次跑就看得见。
    // `summary` 是 `NovelDirector` 自己的字段（不在 StateDiff 契约内，单独读取）→ 白名单。
    if (isObject) {
        constexpr auto kKnown = util::reflect::FieldNames<StateDiff>();
        std::string unknown;
        std::size_t nUnknown = 0;
        yyjson_obj_iter it = yyjson_obj_iter_with(yyjson_doc_get_root(probe));
        for (yyjson_val* k = yyjson_obj_iter_next(&it); k != nullptr; k = yyjson_obj_iter_next(&it)) {
            const std::string_view key{yyjson_get_str(k), yyjson_get_len(k)};
            if (key == "summary") continue; // 见上：本模块自用字段
            bool known = false;
            for (const std::string_view n : kKnown) {
                if (n == key) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                ++nUnknown;
                if (nUnknown <= 6) {
                    unknown += (unknown.empty() ? "" : ", ") + std::string{key};
                }
            }
        }
        if (nUnknown > 0) {
            log::Warn("StateDiffFromJson：{} 个**契约外的顶层键**会被忽略（{}）—— 键名须与 "
                      "`02` §2.5 一致（entities/characters/relationships/…），否则模型报的 "
                      "delta 会静默丢失；请核对**提示词里的形状**与契约是否同一套",
                      nUnknown, unknown);
        }
    }
    if (!isObject) {
        log::Warn("StateDiffFromJson：JSON 能解析但**根不是对象**（StateDiff 必须是 `{{...}}`）");
        yyjson_doc_free(probe);
        return false;
    }
    const int filled = util::reflect::FromJsonString(text, out);
    // ★ S65：反射**之后**再补"字符串 TempId"那一路引用（要按反射后的数组下标对齐）。
    // ⚠️ 顺序不能反：放在反射前的话，数组还是空的，什么都补不上。
    FillStringRefs(yyjson_doc_get_root(probe), out);
    yyjson_doc_free(probe);
    if (filled <= 0) {
        log::Warn("StateDiffFromJson：JSON 合法但**字段一个都没映射上**（{} 字）"
                  "—— 多半是键名/结构不符合 `02` §2.5 的契约（不是格式问题）",
                  text.size());
    }
    return filled > 0;
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
    // ★ S65：本章声明过的 TempId（`entities[]` + `events[]`）—— 下面所有引用字段共用
    const std::set<std::string> declaredTemp = DeclaredTempIds(diff);
    for (const CharacterDelta& c : diff.characters) {
        if (const std::string err = CheckRef("characters[].entity_id", c.entity_id, c.entity_temp_id,
                                             declaredTemp, /*required=*/true);
            !err.empty()) {
            out.push_back({"contract", "high", err});
            continue;
        }
        if (const std::string err = CheckRef("characters[].location_id", c.location_id,
                                             c.location_temp_id, declaredTemp, /*required=*/false);
            !err.empty()) {
            out.push_back({"contract", "high", err});
        }
        // 后半段（存在性 / reason / D4）只对**已有实体**有意义：新建实体没有"上一版状态"可比
        if (c.entity_id <= 0) {
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

    // 事件参与者的 entity_id 必须存在（S65：或引用本章新建实体的 temp_id）
    for (const EventDelta& ev : diff.events) {
        if (const std::string err = CheckRef("events[].location_id", ev.location_id,
                                             ev.location_temp_id, declaredTemp, /*required=*/false);
            !err.empty()) {
            out.push_back({"contract", "high", fmt::format("事件「{}」：{}", ev.temp_id, err)});
        }
        for (const EventParticipantDelta& p : ev.participants) {
            // 🔴 S64：**门禁必须与落库同口径** —— 原先只校 `p.entity_id > 0 && 不存在`，
            // 于是 `entity_id = 0` **溜过门禁**，却在**块 5** 落库时炸
            // （`块 5 参与者失败：事件参与必须指定 event_id 与 entity_id`）⇒ 那一轮 LLM 白跑。
            // S65：改成"`entity_id` 与 `entity_temp_id` **二选一**" —— 后者用于**本章新建**的角色
            //（原先没有这个写法，模型才会把 `en:7` 的序号 7 当 id 填）。
            if (const std::string err =
                    CheckRef("events[].participants[].entity_id", p.entity_id, p.entity_temp_id,
                             declaredTemp, /*required=*/true);
                !err.empty()) {
                out.push_back({"contract", "high", fmt::format("事件「{}」：{}", ev.temp_id, err)});
                continue;
            }
            if (p.entity_id > 0 && !graph.GetEntity(p.entity_id)) {
                out.push_back({"contract", "high",
                               fmt::format("D1：事件「{}」的参与者 entity_id={} 不存在", ev.temp_id, p.entity_id)});
            }
        }
    }

    // S65：这几类引用**此前门禁完全没校**（只靠 K02/K03 的存在性/kind）——
    // 补上"temp_id 是否在本章声明"，否则一个写错的 temp_id 会静默解析成 0（引用丢失）。
    for (const RelationDelta& r : diff.relationships) {
        const std::pair<const char*, std::pair<RowId, const std::string*>> fields[] = {
            {"relationships[].from_id", {r.from_id, &r.from_temp_id}},
            {"relationships[].to_id", {r.to_id, &r.to_temp_id}},
        };
        for (const auto& [where, v] : fields) {
            if (const std::string err = CheckRef(where, v.first, *v.second, declaredTemp, true);
                !err.empty()) {
                out.push_back({"contract", "high", err});
            }
        }
    }
    for (const ItemDelta& it : diff.items) {
        const std::pair<const char*, std::pair<RowId, const std::string*>> fields[] = {
            {"items[].item_id", {it.item_id, &it.item_temp_id}},
            {"items[].owner_id", {it.owner_id, &it.owner_temp_id}},
            {"items[].location_id", {it.location_id, &it.location_temp_id}},
        };
        for (const auto& [where, v] : fields) {
            const bool required = std::string_view{where} == "items[].item_id";
            if (const std::string err = CheckRef(where, v.first, *v.second, declaredTemp, required);
                !err.empty()) {
                out.push_back({"contract", "high", err});
            }
        }
    }
    for (const KnowledgeDelta& k : diff.knowledge) {
        if (const std::string err = CheckRef("knowledge[].entity_id", k.entity_id, k.entity_temp_id,
                                             declaredTemp, /*required=*/true);
            !err.empty()) {
            out.push_back({"contract", "high", err});
        }
    }
    return out;
}

// ★ S65b：见头文件注释。判别力 = "按字面解释必然错"才改写。
int NormalizeNumericTempRefs(db::sqlite::Database& db, StateDiff& diff) {
    // 本章声明的 `en:N` / `ev:N` 按**序号**建索引（模型常把 N 当 id 填）
    std::map<RowId, std::string> byOrd;
    const auto addTemp = [&byOrd](const std::string& tid) {
        const auto colon = tid.rfind(':');
        if (colon == std::string::npos || colon + 1 >= tid.size()) {
            return;
        }
        RowId n = 0;
        for (std::size_t i = colon + 1; i < tid.size(); ++i) {
            if (tid[i] < '0' || tid[i] > '9') {
                return;
            }
            n = n * 10 + static_cast<RowId>(tid[i] - '0');
            if (n > 100000000) {
                return;
            }
        }
        if (n > 0 && byOrd.find(n) == byOrd.end()) {
            byOrd[n] = tid;
        }
    };
    for (const NewEntityDelta& e : diff.entities) {
        addTemp(e.temp_id);
    }
    for (const EventDelta& e : diff.events) {
        addTemp(e.temp_id);
    }
    if (byOrd.empty()) {
        return 0;
    }
    // 库里该 id 的 kind（不存在 → 空）。`item` 期望放宽到同类 kind —— **与 NovelChecks 的
    // `IsItemKind` 同口径**（那处在 NovelChecks.cpp 的匿名命名空间里，取不到；两张表必须一致）。
    const auto kindOf = [&db](RowId id) -> std::string {
        auto st = db.Prepare("SELECT kind FROM entities WHERE id=?1");
        if (!st) {
            return {};
        }
        (void)st->BindInt(1, id);
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            return st->ColumnText(0);
        }
        return {};
    };
    const auto kindOk = [](std::string_view actual, std::string_view expected) {
        if (expected == kind::item) {
            return actual == kind::item || actual == kind::treasure || actual == kind::prop ||
                   actual == kind::clothing || actual == kind::resource;
        }
        return actual == expected;
    };
    int n = 0;
    // 逐字段处理：`*_id` 字面解释必错、且序号有对应 TempId ⇒ 改写
    const auto fix = [&](const char* where, RowId& id, std::string& tempId,
                         std::string_view expected) {
        if (id <= 0 || !tempId.empty()) {
            return;
        }
        const std::string k = kindOf(id);
        if (kindOk(k, expected)) {
            return; // 字面解释是对的 ⇒ 一个字都不动（不改语义）
        }
        const auto it = byOrd.find(id);
        if (it == byOrd.end()) {
            return;
        }
        log::Warn("StateDiff 归一化：{} 的 {} 字面解释不成立（库 #{} 的 kind='{}'，期望 {}）"
                  "而本章声明了「{}」⇒ 按 TempId 解释（与 `02` §2.5 的 `*_temp_id` 等价）",
                  where, id, id, k.empty() ? "不存在" : k, expected, it->second);
        tempId = it->second;
        id = 0;
        ++n;
    };
    for (CharacterDelta& c : diff.characters) {
        fix("characters[].entity_id", c.entity_id, c.entity_temp_id, kind::person);
        fix("characters[].location_id", c.location_id, c.location_temp_id, kind::location);
    }
    for (RelationDelta& r : diff.relationships) {
        fix("relationships[].from_id", r.from_id, r.from_temp_id, kind::person);
        fix("relationships[].to_id", r.to_id, r.to_temp_id, kind::person);
    }
    for (ItemDelta& it : diff.items) {
        fix("items[].item_id", it.item_id, it.item_temp_id, kind::item);
        fix("items[].owner_id", it.owner_id, it.owner_temp_id, kind::person);
        fix("items[].location_id", it.location_id, it.location_temp_id, kind::location);
    }
    for (EventDelta& ev : diff.events) {
        fix("events[].location_id", ev.location_id, ev.location_temp_id, kind::location);
        for (EventParticipantDelta& p : ev.participants) {
            fix("events[].participants[].entity_id", p.entity_id, p.entity_temp_id, kind::person);
        }
    }
    for (KnowledgeDelta& k : diff.knowledge) {
        fix("knowledge[].entity_id", k.entity_id, k.entity_temp_id, kind::person);
    }
    return n;
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
    // S9（`07` §2.5）：manual → PROPOSED；auto → CANON。**auto 不跳门禁**（C5）：
    // 下面的 G1–G5 判定对两种模式完全一致，`auto` 只把块 13 的状态从 PROPOSED 换成 CANON。
    const std::string canonMode = ctx.canon_mode.empty() ? "manual" : ctx.canon_mode;
    const bool autoCanon = canonMode == "auto";
    if (!autoCanon && canonMode != "manual") {
        out.error = fmt::format("canon_mode='{}' 非法（只能 manual / auto，07 §2.5）", canonMode);
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
    //
    // ⚠️ 顺序：快照必须排在 G2 **之前** —— K13（`snapshot.before_commit`）的受检对象就是
    // 这个文件，先写它这条才有对象可判（S11 把 K 校验接进提交路径时踩实的）。
    const auto snapshot = WriteChapterSnapshot(ctx.snapshot_dir, diff, out.diff_hash,
                                               out.entity_version_ids);
    if (!snapshot) {
        out.gates.g3_snapshot_ready = false;
        out.error = snapshot.error();
    } else {
        out.gates.g3_snapshot_ready = true;
        out.snapshot_path = util::PathToUtf8(*snapshot);
    }

    // `07` §2.1 / 不变式 I10：`work/ch<NNN>/12_state_diff.json` 必须存在。原先**没有任何写入点**
    // （`03` 的阶段产物未落地）→ K12 只能拿内存对象当受检对象。这里补上：本章的 StateDiff 与
    // 正文/快照一起留档，断点续跑与事后审计都有据可查。写失败**只告警**（审计产物，不阻断提交）。
    if (!ctx.project_dir.empty() && diff.chapter_id > 0) {
        RowId ord = 0;
        if (auto st = db.Prepare("SELECT ord FROM chapters WHERE id=?1"); st) {
            (void)st->BindInt(1, diff.chapter_id);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                ord = st->ColumnInt(0);
            }
        }
        if (ord > 0) {
            const std::filesystem::path dir =
                util::PathFromUtf8(ctx.project_dir) / "work" / fmt::format("ch{:03}", ord);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!util::WriteFileBytes(dir / "12_state_diff.json", StateDiffToJson(diff))) {
                log::Warn("章节 {} 的 12_state_diff.json 落盘失败（不影响提交）", diff.chapter_id);
            }
        }
    }

    // ———— G2：`06` §2.3 的 K01–K29 全量报告（S11 起是**真门禁**）————
    // 调用方给了报告就用它（`g2_source="caller"`）；没给则在本函数内跑一遍（`"inline"`）——
    // 这样「谁能提交」在任何入口都是同一套判据，不给调用方留后门。
    // 不通过的条目以 `{check_id, severity, detail}` 并入 issue 账 → G5 与拒绝原因都能指名道姓。
    bool g2Pass = true;
    if (ctx.validation != nullptr) {
        out.gates.g2_source = "caller";
        g2Pass = ctx.validation->Ok();
        out.checks_describe = ctx.validation->Describe();
        out.failed_check_ids = ctx.validation->FailedIds();
        for (const CheckResult& cr : ctx.validation->checks) {
            if (CheckPassed(cr)) {
                continue;
            }
            out.gates.issues.push_back({fmt::format("{} {}", cr.check_id, cr.name), cr.severity,
                                        cr.detail});
        }
    } else {
        out.gates.g2_source = "inline";
        CheckInputs cin;
        cin.chapter_id = diff.chapter_id;
        cin.project_dir = util::PathFromUtf8(ctx.project_dir);
        cin.snapshot_dir = util::PathFromUtf8(ctx.snapshot_dir);
        cin.diff = &diff; // 内存里的 StateDiff 就是 K12 的受检对象（`03` 的产物落盘尚未落地）
        const ValidationReport report = RunChapterChecks(db, cin);
        g2Pass = report.Ok();
        out.checks_describe = report.Describe();
        out.failed_check_ids = report.FailedIds();
        for (const CheckResult& cr : report.checks) {
            if (CheckPassed(cr)) {
                continue;
            }
            out.gates.issues.push_back({fmt::format("{} {}", cr.check_id, cr.name), cr.severity,
                                        cr.detail});
        }
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
    out.gates.g2_checks_pass = g2Pass && !hasHigh();
    out.gates.g5_no_high_issue = !hasHigh();

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
        // S65：引用先解析（`*_temp_id` → 块 1 刚落库的真实 id）
        const RowId cEid = ResolveRef(c.entity_id, c.entity_temp_id, tempIds);
        const RowId cLid = ResolveRef(c.location_id, c.location_temp_id, tempIds);
        if (cEid <= 0) {
            return fail(fmt::format("块 2 角色引用解析不到真实 id（entity_id={} temp_id={}）", c.entity_id,
                                    c.entity_temp_id));
        }
        if (auto del = db.Prepare("DELETE FROM character_status WHERE entity_id=?1 AND chapter_id=?2");
            del) {
            (void)del->BindInt(1, cEid);
            (void)del->BindInt(2, diff.chapter_id);
            if (auto s = del->Step(); !s) {
                return fail(fmt::format("块 2 清理旧状态失败：{}", s.error().message));
            }
        }
        CharacterStatusRow row;
        row.entity_id = cEid;
        row.chapter_id = diff.chapter_id;
        row.location_id = cLid;
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
        out.applied.push_back(fmt::format("character_status: 实体 #{} @ch{}", cEid,
                                          diff.chapter_id));
    }

    // 块 3：relations（upsert / close）
    for (const RelationDelta& r : diff.relationships) {
        // S65：引用解析（`*_temp_id` → 本章新建实体的真实 id）
        const RowId rFrom = ResolveRef(r.from_id, r.from_temp_id, tempIds);
        const RowId rTo = ResolveRef(r.to_id, r.to_temp_id, tempIds);
        if (rFrom <= 0 || rTo <= 0) {
            return fail(fmt::format("块 3 关系端点解析不到真实 id（from={}/{} to={}/{}）", r.from_id,
                                    r.from_temp_id, r.to_id, r.to_temp_id));
        }
        if (r.op == "close") {
            auto st = db.Prepare(
                "UPDATE relations SET to_chapter=?1 WHERE from_id=?2 AND to_id=?3 AND rel_type=?4 "
                "AND (to_chapter=0 OR to_chapter>?1)");
            if (!st) {
                return fail(fmt::format("块 3 close 准备失败：{}", st.error().message));
            }
            (void)st->BindInt(1, diff.chapter_id);
            (void)st->BindInt(2, rFrom);
            (void)st->BindInt(3, rTo);
            (void)st->BindText(4, r.rel_type);
            if (auto s = st->Step(); !s) {
                return fail(fmt::format("块 3 close 失败：{}", s.error().message));
            }
            out.applied.push_back(fmt::format("relations: close #{}→#{}", rFrom, rTo));
            continue;
        }
        RelationRow row;
        row.from_id = rFrom;
        row.to_id = rTo;
        row.rel_type = r.rel_type;
        row.strength = r.strength;
        row.from_chapter = diff.chapter_id;
        row.reason = r.reason;
        if (auto id = graph.UpsertRelation(row); !id) {
            return fail(fmt::format("块 3 relations 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("relations: upsert #{}→#{}（{}）", rFrom, rTo, r.rel_type));
    }

    // 块 4：entity_ownerships（acquire / lose）
    for (const ItemDelta& it : diff.items) {
        // S65：引用解析（物品/持有者都可能是本章新建的）
        const RowId iItem = ResolveRef(it.item_id, it.item_temp_id, tempIds);
        const RowId iOwner = ResolveRef(it.owner_id, it.owner_temp_id, tempIds);
        if (iItem <= 0) {
            return fail(fmt::format("块 4 物品引用解析不到真实 id（item_id={} temp_id={}）", it.item_id,
                                    it.item_temp_id));
        }
        if (it.op == "lose") {
            auto st = db.Prepare(
                "UPDATE entity_ownerships SET to_chapter=?1 WHERE owner_id=?2 AND item_id=?3 AND "
                "to_chapter=0");
            if (!st) {
                return fail(fmt::format("块 4 lose 准备失败：{}", st.error().message));
            }
            (void)st->BindInt(1, diff.chapter_id);
            (void)st->BindInt(2, iOwner);
            (void)st->BindInt(3, iItem);
            if (auto s = st->Step(); !s) {
                return fail(fmt::format("块 4 lose 失败：{}", s.error().message));
            }
            out.applied.push_back(fmt::format("ownerships: lose #{}↛#{}", iOwner, iItem));
            continue;
        }
        if (it.op != "acquire") {
            out.applied.push_back(fmt::format("ownerships: {} 不改表（move/change_state 无落地表）", it.op));
            continue;
        }
        OwnershipRow row;
        row.owner_id = iOwner;
        row.item_id = iItem;
        row.from_chapter = diff.chapter_id;
        row.how = it.how;
        row.note = it.reason;
        if (auto id = graph.UpsertOwnership(row); !id) {
            return fail(fmt::format("块 4 ownerships 失败：{}", id.error().message));
        }
        out.applied.push_back(fmt::format("ownerships: acquire #{}←#{}", iItem, iOwner));
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
        // S65：地点可以是本章新建的
        detail.location_id = ResolveRef(ev.location_id, ev.location_temp_id, tempIds);
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
            // S65：参与者可以是本章新建的角色
            const RowId pEid = ResolveRef(p.entity_id, p.entity_temp_id, tempIds);
            if (pEid <= 0) {
                return fail(fmt::format("块 5 参与者引用解析不到真实 id（entity_id={} temp_id={}）",
                                        p.entity_id, p.entity_temp_id));
            }
            if (auto added = graph.UpsertEventParticipant({.event_id = *id, .entity_id = pEid,
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
            // 同块 9：节拍序在被父下编号（原先写死 0，属同一处缺陷）
            RowId nextOrd = 1;
            if (auto st = db.Prepare("SELECT COALESCE(MAX(ord),0)+1 FROM mystery_beats "
                                     "WHERE mystery_id=?1");
                st) {
                (void)st->BindInt(1, *id);
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    nextOrd = st->ColumnInt(0);
                }
            }
            if (auto beat = graph.UpsertMysteryBeat({.mystery_id = *id,
                                                    .beat_type = m.beat.beat_type,
                                                    .chapter_id = diff.chapter_id,
                                                    .content = m.beat.content,
                                                    .target_entity_id = m.beat.target_entity_id,
                                                    .ord = static_cast<int>(nextOrd)});
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
            // `ord` 必须**父下严格递增**（`06` §2.3 K08）：原先写死 0，同一剧情线的多个节拍
            // 会全部挤在 ord=0 → K08 判不严格递增。节拍序由本函数编号（`PlotBeatDelta` 本身没有 ord）。
            RowId nextOrd = 1;
            if (auto st = db.Prepare("SELECT COALESCE(MAX(ord),0)+1 FROM plot_beats WHERE plot_id=?1");
                st) {
                (void)st->BindInt(1, *id);
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    nextOrd = st->ColumnInt(0);
                }
            }
            if (auto beat = graph.UpsertPlotBeat({.plot_id = *id,
                                                 .chapter_id = diff.chapter_id,
                                                 .ord = static_cast<int>(nextOrd),
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
        // S65：知情者可以是本章新建的角色
        const RowId kEid = ResolveRef(k.entity_id, k.entity_temp_id, tempIds);
        if (kEid <= 0) {
            return fail(fmt::format("块 10 知情者引用解析不到真实 id（entity_id={} temp_id={}）",
                                    k.entity_id, k.entity_temp_id));
        }
        if (auto id = graph.UpsertKnowledge({.entity_id = kEid,
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

    // 块 13：canon_logs（manual → PROPOSED；auto → CANON，门禁已在上面判定，S9）
    const std::string_view canonStatus = autoCanon ? std::string_view{"CANON"}
                                                   : std::string_view{"PROPOSED"};
    if (auto canon = graph.SetCanon("chapter", diff.chapter_id, canonStatus,
                                    fmt::format("diff_hash={} verdict={}", out.diff_hash,
                                                ctx.review_verdict));
        !canon) {
        return fail(fmt::format("块 13 canon_logs 失败：{}", canon.error().message));
    }
    out.applied.push_back(fmt::format("canon_logs: chapter → {}（{}）", canonStatus, canonMode));

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
        ok.project_dir = util::PathToUtf8(snapRoot); // S12：让 StateDiff 产物有地方落
        first = CommitChapterState(mem, diff, ok);
        expect(first.ok && !first.skipped, "门禁齐备 → 提交成功");
        expect(first.gates.g1_review_pass && first.gates.g2_checks_pass &&
                   first.gates.g3_snapshot_ready && first.gates.g4_diff_valid &&
                   first.gates.g5_no_high_issue,
               "G1–G5 逐条为真");
        expect(!first.entity_version_ids.empty(), "提交前写了实体级快照（Before 值）");
        // S12（`07` §2.1 / 不变式 I10）：本章 StateDiff 必须落成 work/ch<NNN>/12_state_diff.json
        const auto diffPath = snapRoot / "work" / "ch001" / "12_state_diff.json";
        const auto bytes = util::ReadFileBytes(diffPath);
        StateDiff reread;
        expect(bytes && StateDiffFromJson(*bytes, reread) && reread.Hash() == diff.Hash(),
               "S12：work/ch001/12_state_diff.json 落盘且读回一致（不变式 I10）");
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
    // ⑧ `auto`（S9，`07` §2.5）：门禁全满足 → 提交并写 CANON；门禁不满足 → 拒绝（C5 不跳门禁）
    {
        auto autoChapter = g.UpsertChapter({.ord = 99, .title = "第九九章"});
        expect(autoChapter.has_value(), "auto：建章");
        StateDiff autoDiff;
        autoDiff.chapter_id = autoChapter.value_or(0);
        autoDiff.input_state_hash = "sha1:auto-check";
        autoDiff.characters.push_back(
            {.entity_id = *lin, .mind_state = "决意", .reason = "auto 自检"});
        CommitContext autoMode = ctx;
        autoMode.chapter_id = autoChapter.value_or(0);
        autoMode.review_pass = true;
        autoMode.canon_mode = "auto";
        const CommitResult r = CommitChapterState(mem, autoDiff, autoMode);
        // 失败时把原因带上（诊断用；`checks_describe` 会指名道姓是哪条 K）
        expect(r.ok && !r.skipped,
               fmt::format("auto：G1–G5 全满足 → 提交成功（{}｜{}）", r.error, r.checks_describe));
        int canon = 0;
        if (auto st = mem.Prepare("SELECT COUNT(*) FROM canon_logs WHERE target_kind='chapter' "
                                  "AND target_id=?1 AND status='CANON'");
            st) {
            (void)st->BindInt(1, autoChapter.value_or(0));
            if (st->Step()) {
                canon = st->ColumnInt(0);
            }
        }
        expect(canon == 1, "auto：canon_logs 写 CANON（不是 PROPOSED）");
        // 门禁不满足（G1 评审 FAIL）→ 拒绝，不静默降级为 manual
        auto gateChapter = g.UpsertChapter({.ord = 100, .title = "第一百章"});
        expect(gateChapter.has_value(), "auto：建第二张章");
        StateDiff d2;
        d2.chapter_id = gateChapter.value_or(0);
        d2.characters.push_back({.entity_id = *lin, .mind_state = "犹豫", .reason = "gate 自检"});
        CommitContext bad = ctx;
        bad.chapter_id = gateChapter.value_or(0);
        bad.review_pass = false;
        bad.canon_mode = "auto";
        const CommitResult r2 = CommitChapterState(mem, d2, bad);
        expect(!r2.ok && !r2.gates.g1_review_pass, "auto：G1 不满足 → 拒绝（不静默降级）");
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
    // ⑩ S11：G2 真的是 `06` §2.3 的 K01–K29 报告（`inline`），且失败项指名道姓
    {
        expect(first.gates.g2_source == "inline", "G2 口径 = 提交路径内跑 K01–K29（inline）");
        expect(first.checks_describe.find("K01") != std::string::npos &&
                   first.failed_check_ids.empty(),
               "K 报告摘要可读且干净提交无失败项");

        StateDiff dangling;
        dangling.chapter_id = *chapter;
        dangling.characters.push_back(
            {.entity_id = 999999, .body_state = "不该存在", .reason = "S11 自检"});
        CommitContext ok = ctx;
        ok.review_pass = true;
        const CommitResult r = CommitChapterState(mem, dangling, ok);
        bool hasK02 = false;
        for (const std::string& id : r.failed_check_ids) {
            if (id == "K02") {
                hasK02 = true;
            }
        }
        expect(!r.ok && hasK02, "K02（引用不存在实体）→ 拒绝提交并把 K02 报给调用方");
        expect(!r.checks_describe.empty() && r.checks_describe.find("K02") != std::string::npos,
               "被拒时报告里能看到 K02");
    }

    std::filesystem::remove_all(snapRoot, ec);
    if (fail == 0) {
        log::Info("S8 提交自检通过（门禁 G1–G5 / 14 块写入 / 幂等 / 快照 / D 规则 / "
                  "manual→PROPOSED · auto→CANON）");
    }
    return fail;
}

} // namespace shine::novelcore
