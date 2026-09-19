// shine::novelcore —— `06` §2.3 的 K01–K29 机器校验（实现；设计说明见 `NovelChecks.h`）
#include "novel/NovelChecks.h"

#include "core/Log.h"
#include "novel/NovelFields.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace shine::novelcore {
namespace {

// ———— K01–K29 目录（`06` §2.3 的**唯一一份**实现清单）————
// `availability`：Library 查库 / Artifact 读工程产物 / ContractInput 只能由调用方传对象。
constexpr CheckSpec kCatalog[] = {
    {"K01", "contract.schema", "high", CheckAvailability::Artifact, "chapter", "StateDiff + 02",
     "复用 NovelCommit::ValidateStateDiff（D1/D4/D7/D8）"},
    {"K02", "entity.exists", "high", CheckAvailability::Library, "entities",
     "本章 scenes/scene_cast/shots + StateDiff 的全部引用"},
    {"K03", "entity.kind_match", "high", CheckAvailability::Library, "entities", "location/person/item/conflict"},
    {"K04", "invariant.dead_not_acting", "high", CheckAvailability::Library, "entities + event_participants",
     "不变式 I3"},
    {"K05", "invariant.knowledge_boundary", "high", CheckAvailability::Library,
     "character_knowledge / secret_knowledge", "不变式 I2；正文侧不可机器判定，只判库侧时间序"},
    {"K06", "invariant.causal_acyclic", "high", CheckAvailability::Library, "causal_links", "不变式 I4"},
    {"K07", "invariant.location_unique", "high", CheckAvailability::Library,
     "character_status / entity_ownerships", "不变式 I5"},
    {"K08", "invariant.order_monotonic", "high", CheckAvailability::Library,
     "chapters / scenes / plot_beats", "不变式 I6"},
    {"K09", "invariant.shot_continuity", "high", CheckAvailability::Library, "shots", "不变式 I7"},
    {"K10", "foreshadow.overdue", "high", CheckAvailability::Library, "foreshadowings", "不变式 I8；超期告警记 low"},
    {"K11", "foreshadow.no_regress", "high", CheckAvailability::Artifact, "foreshadowings + StateDiff",
     "状态反向迁移即 high"},
    {"K12", "chapter.has_state_diff", "high", CheckAvailability::Artifact, "work/ch<NNN>/12_state_diff.json",
     "不变式 I10；内存 StateDiff 等价（阶段产物未落地）"},
    {"K13", "snapshot.before_commit", "high", CheckAvailability::Artifact, "snapshots/ch<NNN>.json",
     "不变式 I11；文件名按 chapter_id（NovelCommit 口径）"},
    {"K14", "field.registered", "high", CheckAvailability::Library, "field_defs", "不变式 I12"},
    {"K15", "field.type_valid", "high", CheckAvailability::Library, "field_defs", "按 value_type/enum_json 校值"},
    {"K16", "pov.in_cast", "medium", CheckAvailability::Library, "scenes / scene_cast", "POV 必须在场"},
    {"K17", "item.ownership_consistent", "high", CheckAvailability::Artifact, "entity_ownerships + StateDiff",
     "acquire 前不应已持有"},
    {"K18", "travel.time_sane", "medium", CheckAvailability::Library, "location_distances",
     "time_label 是自由文本 → 只做相等比较；无距离记录跳过"},
    {"K19", "gen.graph_valid", "high", CheckAvailability::ContractInput, "object_info",
     "沿用 ApiGraphValidator，不重复实现"},
    {"K20", "gen.size_aligned", "low", CheckAvailability::ContractInput, "VideoProject.Sanitize",
     "自动纠正 + 记 low（放行）"},
    {"K21", "gen.ref_limit", "high", CheckAvailability::ContractInput, "video::Shot.referenceImages", "上限 9"},
    {"K22", "visual.state_resolvable", "medium", CheckAvailability::Library, "visual_states",
     "受检对象 = 分镜的角色（`04` A4：不阻塞正文生成）"},
    {"K23", "prompt.state_hash_match", "high", CheckAvailability::Library,
     "prompt_artifacts + 04 §2.5", "不变式 I9"},
    {"K24", "beat.timeline_covered", "high", CheckAvailability::Library, "shots.timeline_json",
     "首尾覆盖 [0,duration] 且互不重叠"},
    {"K25", "word.count_in_range", "low", CheckAvailability::Library, "chapters.words",
     "word_target ±30%；越界记 low（不阻断）"},
    {"K26", "entity.kind_registered", "high", CheckAvailability::Library, "entities", "不变式 I13"},
    {"K27", "job.no_orphan", "high", CheckAvailability::Library, "generated_images",
     "`11` §2.6.5 的回收兜底"},
    {"K28", "degrade.visible", "medium", CheckAvailability::Library,
     "visual_artifacts + degradations.jsonl", "`04` §2.4「降级必须可见」"},
    {"K29", "asset.status_gate", "high", CheckAvailability::Library, "visual_assets + visual_artifacts",
     "`11` §2.6.1 S1"},
};

// —— 31 种 kind（`01` §1.1.2）：**从 NovelTypes.h 的 kind:: 常量列**，不另抄字面量表 ——
constexpr std::string_view kKinds[] = {
    kind::universe,     kind::world_rule, kind::history,     kind::culture,  kind::language,
    kind::religion,     kind::economy,    kind::tech,        kind::society,  kind::calendar,
    kind::person,       kind::creature,   kind::clothing,    kind::prop,     kind::treasure,
    kind::item,         kind::location,   kind::faction,     kind::power_system, kind::ability,
    kind::event,        kind::plot,       kind::arc,         kind::conflict, kind::theme,
    kind::motif,        kind::ending,     kind::secret,      kind::foreshadowing, kind::mystery,
    kind::resource,
};

[[nodiscard]] bool IsRegisteredKind(std::string_view k) {
    for (const std::string_view known : kKinds) {
        if (known == k) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsItemKind(std::string_view k) {
    return k == kind::item || k == kind::treasure || k == kind::prop || k == kind::clothing ||
           k == kind::resource;
}

// ———— 小工具 ————

[[nodiscard]] const CheckSpec* FindSpec(std::string_view id) noexcept {
    for (const CheckSpec& spec : kCatalog) {
        if (spec.check_id == id) {
            return &spec;
        }
    }
    return nullptr;
}

[[nodiscard]] CheckResult Mk(std::string_view id, CheckOutcome outcome, std::string detail = {}) {
    CheckResult r;
    r.check_id.assign(id);
    if (const CheckSpec* spec = FindSpec(id)) {
        r.name.assign(spec->name);
        r.severity.assign(spec->severity);
    } else {
        r.severity = "high";
    }
    r.outcome = outcome;
    r.detail = std::move(detail);
    return r;
}

// 告警类（`K10` 的超期告警 / `K20` 的自动纠正）：记 `low` → 放行但可见
[[nodiscard]] CheckResult MkLow(CheckResult r, std::string detail) {
    r.outcome = CheckOutcome::Fail;
    r.severity = "low";
    r.detail += detail;
    return r;
}

[[nodiscard]] int CountSql(db::sqlite::Database& db, std::string_view sql) {
    auto st = db.Prepare(sql);
    if (!st) {
        return 0;
    }
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) {
        return 0;
    }
    return st->ColumnInt(0);
}

[[nodiscard]] int CountSql1(db::sqlite::Database& db, std::string_view sql, RowId a) {
    auto st = db.Prepare(sql);
    if (!st) {
        return 0;
    }
    (void)st->BindInt(1, a);
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) {
        return 0;
    }
    return st->ColumnInt(0);
}

[[nodiscard]] int CountSql2(db::sqlite::Database& db, std::string_view sql, RowId a, RowId b) {
    auto st = db.Prepare(sql);
    if (!st) {
        return 0;
    }
    (void)st->BindInt(1, a);
    (void)st->BindInt(2, b);
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) {
        return 0;
    }
    return st->ColumnInt(0);
}

[[nodiscard]] RowId IntSql1(db::sqlite::Database& db, std::string_view sql, RowId a, RowId fallback = 0) {
    auto st = db.Prepare(sql);
    if (!st) {
        return fallback;
    }
    (void)st->BindInt(1, a);
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) {
        return fallback;
    }
    return st->ColumnInt(0);
}

// 解析 `[1,2,3]` 形态的 id 数组（宽容：非法 JSON / 非数组 / 非整数 → 忽略该项）
[[nodiscard]] std::vector<RowId> ParseIdArray(std::string_view text) {
    std::vector<RowId> out;
    if (text.empty()) {
        return out;
    }
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (doc == nullptr) {
        return out;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_arr(root)) {
        std::size_t idx = 0;
        std::size_t max = 0;
        yyjson_val* item = nullptr;
        yyjson_arr_foreach(root, idx, max, item) {
            if (yyjson_is_int(item)) {
                out.push_back(static_cast<RowId>(yyjson_get_sint(item)));
            } else if (yyjson_is_str(item)) {
                RowId id = 0;
                const char* s = yyjson_get_str(item);
                const auto* end = s + std::strlen(s);
                if (std::from_chars(s, end, id).ec == std::errc{} && id > 0) {
                    out.push_back(id);
                }
            }
        }
    }
    yyjson_doc_free(doc);
    return out;
}

struct EntityInfo {
    bool exists = false;
    std::string kind;
    std::string status;
};

[[nodiscard]] EntityInfo LookupEntity(db::sqlite::Database& db, RowId id) {
    EntityInfo info;
    if (id <= 0) {
        return info;
    }
    auto st = db.Prepare("SELECT kind,status FROM entities WHERE id=?1");
    if (!st) {
        return info;
    }
    (void)st->BindInt(1, id);
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) {
        return info;
    }
    info.exists = true;
    info.kind = st->ColumnText(0);
    info.status = st->ColumnText(1);
    return info;
}

struct ReadCtx {
    db::sqlite::Database* db = nullptr;
    RowId chapter = 0;
    int ord = 0;
    const std::filesystem::path* projectDir = nullptr;
    const std::filesystem::path* snapshotDir = nullptr; // K13
    int wordTarget = 3000;
    const StateDiff* diff = nullptr;
    std::span<const ShotStateSnapshot> shots;
    std::span<const BeatSpan> beats;
    double duration = 0.0;
    std::string_view promptHash;
    std::string_view chain;
    std::string_view stage;
    const GenerationCheckInput* gen = nullptr;
    std::int64_t orphanStaleSeconds = 1800;
};

// 引用（K02/K03 共用）：`expected` 见 KindMatches
struct Ref {
    RowId id = 0;
    std::string_view expected;
    std::string where;
};

[[nodiscard]] std::vector<Ref> CollectRefs(const ReadCtx& c) {
    std::vector<Ref> refs;
    const auto add = [&refs](RowId id, std::string_view expected, std::string where) {
        if (id > 0) {
            refs.push_back(Ref{id, expected, std::move(where)});
        }
    };
    if (c.diff != nullptr) {
        const StateDiff& d = *c.diff;
        for (const CharacterDelta& x : d.characters) {
            add(x.entity_id, kind::person, "characters[].entity_id");
            add(x.location_id, kind::location, "characters[].location_id");
        }
        for (const RelationDelta& x : d.relationships) {
            add(x.from_id, kind::person, "relationships[].from_id");
            add(x.to_id, kind::person, "relationships[].to_id");
        }
        for (const ItemDelta& x : d.items) {
            add(x.item_id, kind::item, "items[].item_id");
            add(x.owner_id, kind::person, "items[].owner_id");
            add(x.location_id, kind::location, "items[].location_id");
        }
        for (const EventDelta& x : d.events) {
            add(x.location_id, kind::location, "events[].location_id");
            for (const EventParticipantDelta& p : x.participants) {
                add(p.entity_id, kind::person, "events[].participants[].entity_id");
            }
        }
        for (const KnowledgeDelta& x : d.knowledge) {
            add(x.entity_id, kind::person, "knowledge[].entity_id");
        }
    }
    if (c.chapter > 0 && c.db != nullptr) {
        std::vector<RowId> sceneIds;
        if (auto st = c.db->Prepare("SELECT id,pov_entity_id,location_id,conflict_id FROM scenes "
                                    "WHERE chapter_id=?1");
            st) {
            (void)st->BindInt(1, c.chapter);
            while (true) {
                auto s = st->Step();
                if (!s || *s != db::sqlite::StepResult::Row) {
                    break;
                }
                sceneIds.push_back(st->ColumnInt(0));
                add(st->ColumnInt(1), kind::person, "scenes.pov_entity_id");
                add(st->ColumnInt(2), kind::location, "scenes.location_id");
                add(st->ColumnInt(3), kind::conflict, "scenes.conflict_id");
            }
        }
        for (const RowId sceneId : sceneIds) {
            if (auto st = c.db->Prepare("SELECT entity_id FROM scene_cast WHERE scene_id=?1"); st) {
                (void)st->BindInt(1, sceneId);
                while (true) {
                    auto s = st->Step();
                    if (!s || *s != db::sqlite::StepResult::Row) {
                        break;
                    }
                    add(st->ColumnInt(0), kind::person, "scene_cast.entity_id");
                }
            }
            if (auto st = c.db->Prepare("SELECT character_ids_json,prop_ids_json FROM shots "
                                        "WHERE scene_id=?1");
                st) {
                (void)st->BindInt(1, sceneId);
                while (true) {
                    auto s = st->Step();
                    if (!s || *s != db::sqlite::StepResult::Row) {
                        break;
                    }
                    for (const RowId id : ParseIdArray(st->ColumnText(0))) {
                        add(id, kind::person, "shots.character_ids_json");
                    }
                    for (const RowId id : ParseIdArray(st->ColumnText(1))) {
                        add(id, kind::item, "shots.prop_ids_json");
                    }
                }
            }
        }
    }
    return refs;
}

// ———— K01 ————
[[nodiscard]] CheckResult CheckK01(const ReadCtx& c) {
    if (c.diff == nullptr) {
        return Mk("K01", CheckOutcome::NotApplicable,
                  "无 StateDiff（内存未传，且 work/ch<NNN>/12_state_diff.json 不存在）");
    }
    const std::vector<CommitIssue> issues = ValidateStateDiff(*c.db, *c.diff);
    std::string bad;
    int n = 0;
    for (const CommitIssue& issue : issues) {
        if (issue.code != "contract") {
            continue;
        }
        ++n;
        if (n <= 3) {
            bad += fmt::format("{}{}", bad.empty() ? "" : "；", issue.detail);
        }
    }
    if (n > 0) {
        return Mk("K01", CheckOutcome::Fail, fmt::format("契约不合规 {} 处：{}", n, bad));
    }
    return Mk("K01", CheckOutcome::Pass,
              fmt::format("StateDiff 契约校验通过（{} 条 delta）", c.diff->DeltaCount()));
}

// ———— K02 ————
[[nodiscard]] CheckResult CheckK02(const ReadCtx& c) {
    const std::vector<Ref> refs = CollectRefs(c);
    if (refs.empty()) {
        return Mk("K02", CheckOutcome::NotApplicable, "本章没有任何实体引用");
    }
    std::string bad;
    int n = 0;
    for (const Ref& r : refs) {
        if (!LookupEntity(*c.db, r.id).exists) {
            ++n;
            if (n <= 5) {
                bad += fmt::format("{}{} #{}", bad.empty() ? "" : "；", r.where, r.id);
            }
        }
    }
    if (n > 0) {
        return Mk("K02", CheckOutcome::Fail, fmt::format("引用不存在的实体 {} 处：{}", n, bad));
    }
    return Mk("K02", CheckOutcome::Pass, fmt::format("{} 处实体引用全部存在", refs.size()));
}

// ———— K03 ————
[[nodiscard]] bool KindMatches(std::string_view actual, std::string_view expected) {
    if (expected == kind::item) {
        return IsItemKind(actual);
    }
    return actual == expected;
}

[[nodiscard]] CheckResult CheckK03(const ReadCtx& c) {
    const std::vector<Ref> refs = CollectRefs(c);
    if (refs.empty()) {
        return Mk("K03", CheckOutcome::NotApplicable, "本章没有任何实体引用");
    }
    std::string bad;
    int n = 0;
    for (const Ref& r : refs) {
        const EntityInfo info = LookupEntity(*c.db, r.id);
        if (!info.exists) {
            continue; // 不存在由 K02 报，这里只管「存在但 kind 不符」
        }
        if (!KindMatches(info.kind, r.expected)) {
            ++n;
            if (n <= 5) {
                bad += fmt::format("{}{} #{} 是 {}（期望 {}）", bad.empty() ? "" : "；", r.where, r.id,
                                   info.kind.empty() ? "?" : info.kind, r.expected);
            }
        }
    }
    if (n > 0) {
        return Mk("K03", CheckOutcome::Fail, fmt::format("实体 kind 与用途不匹配 {} 处：{}", n, bad));
    }
    return Mk("K03", CheckOutcome::Pass, "引用的实体 kind 与用途一致");
}

// ———— K04 ————
[[nodiscard]] CheckResult CheckK04(const ReadCtx& c) {
    auto st = c.db->Prepare("SELECT e.id,e.name,e.status,p.role FROM event_participants p "
                            "JOIN entities e ON e.id=p.entity_id "
                            "WHERE p.role='actor' AND e.status IN ('dead','destroyed') LIMIT 6");
    if (!st) {
        return Mk("K04", CheckOutcome::Missing, "查询 event_participants 失败");
    }
    std::string bad;
    int n = 0;
    while (true) {
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            break;
        }
        ++n;
        if (n <= 3) {
            bad += fmt::format("{}{}#{}({})", bad.empty() ? "" : "；", st->ColumnText(1),
                               st->ColumnInt(0), st->ColumnText(2));
        }
    }
    if (n > 0) {
        return Mk("K04", CheckOutcome::Fail,
                  fmt::format("不变式 I3：{} 个已死亡/已毁实体作为行动者：{}", n, bad));
    }
    return Mk("K04", CheckOutcome::Pass, "无已死亡/已毁实体充当行动者");
}

// ———— K05 ————
[[nodiscard]] CheckResult CheckK05(const ReadCtx& c) {
    if (c.chapter <= 0) {
        return Mk("K05", CheckOutcome::NotApplicable, "未给 chapter_id");
    }
    const int know = CountSql1(*c.db, "SELECT COUNT(*) FROM character_knowledge WHERE knows=1 "
                                      "AND chapter_known > ?1", c.chapter);
    const int secret = CountSql1(*c.db, "SELECT COUNT(*) FROM secret_knowledge WHERE knows=1 "
                                        "AND chapter_known > ?1", c.chapter);
    int conflict = 0;
    if (c.diff != nullptr) {
        NovelGraph graph(*c.db);
        for (const KnowledgeDelta& k : c.diff->knowledge) {
            if (k.knows || k.entity_id <= 0 || k.fact_kind.empty() || k.fact_id <= 0) {
                continue;
            }
            // 声明「不知道」，但库里在该章时点已知道 → 冲突
            if (auto has = graph.CharacterKnows(k.entity_id, k.fact_kind, k.fact_id, c.chapter);
                has && *has) {
                ++conflict;
            }
        }
    }
    if (know + secret + conflict > 0) {
        return Mk("K05", CheckOutcome::Fail,
                  fmt::format("不变式 I2 时间序越界：character_knowledge {} 条 / secret_knowledge {} 条"
                              "的 chapter_known 晚于本章；StateDiff 与库冲突 {} 处"
                              "（正文侧 POV 实际用到的 fact 不可机器判定，需语义评审）",
                              know, secret, conflict));
    }
    return Mk("K05", CheckOutcome::Pass,
              fmt::format("第 {} 章的知情时间序自洽（正文侧不可判部分交语义评审）", c.chapter));
}

// ———— K06 ————
[[nodiscard]] CheckResult CheckK06(const ReadCtx& c) {
    std::unordered_map<RowId, std::vector<RowId>> adj;
    int edges = 0;
    if (auto st = c.db->Prepare("SELECT cause_event_id,effect_event_id FROM causal_links"); st) {
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            adj[st->ColumnInt(0)].push_back(st->ColumnInt(1));
            ++edges;
        }
    }
    if (edges == 0) {
        return Mk("K06", CheckOutcome::Pass, "无因果边（平凡无环）");
    }
    enum : int { White = 0, Gray = 1, Black = 2 };
    std::unordered_map<RowId, int> color;
    RowId hit = 0;
    const auto dfs = [&](auto&& self, RowId u) -> bool {
        color[u] = Gray;
        for (const RowId v : adj[u]) {
            const int cv = color[v];
            if (cv == Gray) {
                hit = v;
                return true;
            }
            if (cv == White && self(self, v)) {
                return true;
            }
        }
        color[u] = Black;
        return false;
    };
    for (const auto& [u, _] : adj) {
        if (color[u] == White && dfs(dfs, u)) {
            return Mk("K06", CheckOutcome::Fail,
                      fmt::format("不变式 I4：causal_links 有环（节点 #{}，共 {} 条边）", hit, edges));
        }
    }
    return Mk("K06", CheckOutcome::Pass, fmt::format("{} 条因果边无环", edges));
}

// ———— K07 ————
[[nodiscard]] CheckResult CheckK07(const ReadCtx& c) {
    const int locDup = CountSql(*c.db,
                                "SELECT COUNT(*) FROM (SELECT 1 FROM character_status WHERE location_id>0 "
                                "GROUP BY entity_id,chapter_id HAVING COUNT(DISTINCT location_id)>1)");
    const int ownDup = CountSql(*c.db,
                                "SELECT COUNT(*) FROM (SELECT 1 FROM entity_ownerships WHERE to_chapter=0 "
                                "GROUP BY item_id HAVING COUNT(*)>1)");
    const int rows = CountSql(*c.db, "SELECT COUNT(*) FROM character_status") +
                     CountSql(*c.db, "SELECT COUNT(*) FROM entity_ownerships");
    if (locDup + ownDup > 0) {
        return Mk("K07", CheckOutcome::Fail,
                  fmt::format("不变式 I5：同一实体同章有多个地点 {} 组；同一物品同时多个持有者 {} 件",
                              locDup, ownDup));
    }
    if (rows == 0) {
        return Mk("K07", CheckOutcome::NotApplicable, "character_status / entity_ownerships 均无数据");
    }
    return Mk("K07", CheckOutcome::Pass, "同章地点唯一、物品持有者唯一");
}

// ———— K08 ————
[[nodiscard]] std::string CheckOrderMonotonic(db::sqlite::Database& db, std::string_view sql,
                                              std::string_view what) {
    // 判据 = `06` §2.3 K08 原文：**在其父下严格递增**（`ord` 相等即违规）。
    // 刻意不额外要求「首行 ord > 0」—— `ord=0` 在库里表示"未编号"，那不是我该判的东西
    // （编号责任在写入方；`NovelCommit` 已给节拍编号，见块 8/块 9）。
    std::string bad;
    RowId parent = -1;
    int prev = -1;
    bool first = true;
    if (auto st = db.Prepare(sql); st) {
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            const RowId p = st->ColumnInt(0);
            const int ord = static_cast<int>(st->ColumnInt(1));
            if (p != parent) {
                parent = p;
                prev = -1;
                first = true;
            }
            if (!first && ord <= prev && bad.size() < 96) {
                bad += fmt::format("{}父 {} 的 ord {} 未严格递增（前一个 {}）", bad.empty() ? "" : "；",
                                   p, ord, prev);
            }
            prev = ord;
            first = false;
        }
    }
    if (!bad.empty()) {
        return fmt::format("{}：{}", what, bad);
    }
    return {};
}

[[nodiscard]] CheckResult CheckK08(const ReadCtx& c) {
    std::string bad;
    if (std::string s = CheckOrderMonotonic(*c.db,
                                            "SELECT 0,ord FROM chapters ORDER BY ord", "chapters");
        !s.empty()) {
        bad += s;
    }
    if (std::string s = CheckOrderMonotonic(
            *c.db, "SELECT chapter_id,ord FROM scenes ORDER BY chapter_id,ord", "scenes");
        !s.empty()) {
        bad += (bad.empty() ? "" : "；") + s;
    }
    if (std::string s = CheckOrderMonotonic(
            *c.db, "SELECT plot_id,ord FROM plot_beats ORDER BY plot_id,ord", "plot_beats");
        !s.empty()) {
        bad += (bad.empty() ? "" : "；") + s;
    }
    if (!bad.empty()) {
        return Mk("K08", CheckOutcome::Fail, fmt::format("不变式 I6：{}", bad));
    }
    return Mk("K08", CheckOutcome::Pass, "chapters/scenes/plot_beats 的 ord 在其父下严格递增");
}

// ———— K09（ContractInput；`12` §2.7）————
// 逐字段比对的粒度 = `state_snapshot` 的**顶层字段组**
// （characters / props / lighting / environment / camera_position，见 `12` §2.7.1 C1–C12）。
// 归一化只做空白裁剪：两边都是 LLM 产出的 JSON，缩进/空格不应算变化。
[[nodiscard]] std::string NormalizeExtras(std::string_view json) {
    std::string out;
    out.reserve(json.size());
    bool inString = false;
    bool escaped = false;
    for (const char ch : json) {
        if (inString) {
            out.push_back(ch);
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
            out.push_back(ch);
        } else if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
            out.push_back(ch);
        }
    }
    return out;
}

// 返回两组顶层字段名中不同的那些（用于人读 detail）
[[nodiscard]] std::vector<std::string> DiffTopLevelKeys(std::string_view a, std::string_view b) {
    std::vector<std::string> out;
    yyjson_doc* da = yyjson_read(a.data(), a.size(), 0);
    yyjson_doc* db = yyjson_read(b.data(), b.size(), 0);
    const auto keysOf = [](yyjson_doc* d) {
        std::map<std::string, std::string> m;
        if (d == nullptr) {
            return m;
        }
        yyjson_val* root = yyjson_doc_get_root(d);
        if (!yyjson_is_obj(root)) {
            return m;
        }
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(root, &iter);
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            yyjson_val* val = yyjson_obj_iter_get_val(key);
            const char* k = yyjson_get_str(key);
            std::size_t len = 0;
            const char* raw = yyjson_val_write(val, 0, &len);
            m.emplace(k == nullptr ? std::string{} : std::string{k}, raw == nullptr ? std::string{}
                                                                                  : std::string{raw, len});
            if (raw != nullptr) {
                free(const_cast<char*>(raw));
            }
        }
        return m;
    };
    const std::map<std::string, std::string> ma = keysOf(da);
    const std::map<std::string, std::string> mb = keysOf(db);
    for (const auto& [k, v] : ma) {
        const auto it = mb.find(k);
        if (it == mb.end() || it->second != v) {
            out.push_back(k);
        }
    }
    for (const auto& [k, v] : mb) {
        if (!ma.contains(k)) {
            out.push_back(k);
        }
    }
    if (da != nullptr) {
        yyjson_doc_free(da);
    }
    if (db != nullptr) {
        yyjson_doc_free(db);
    }
    return out;
}

[[nodiscard]] CheckResult ImplShotContinuity(std::span<const ShotStateSnapshot> shots) {
    if (shots.empty()) {
        return Mk("K09", CheckOutcome::NotApplicable,
                  "无叙事分镜起止状态数据（`12` §1.4：shots 表无 start/end 列，V9 STORYBOARD 未落地）");
    }
    std::string bad;
    int n = 0;
    for (std::size_t i = 0; i + 1 < shots.size(); ++i) {
        const std::string endPrev = NormalizeExtras(shots[i].end_state_json);
        const std::string startNext = NormalizeExtras(shots[i + 1].start_state_json);
        if (endPrev == startNext) {
            continue;
        }
        ++n;
        if (n <= 3) {
            const std::vector<std::string> keys = DiffTopLevelKeys(shots[i].end_state_json,
                                                                  shots[i + 1].start_state_json);
            std::string keyText;
            for (std::size_t k = 0; k < keys.size() && k < 4; ++k) {
                keyText += (keyText.empty() ? "" : ",") + keys[k];
            }
            bad += fmt::format("{}镜 #{} → #{} 不接（字段：{}）", bad.empty() ? "" : "；",
                               shots[i].shot_id, shots[i + 1].shot_id,
                               keyText.empty() ? std::string{"<整段>"} : keyText);
        }
    }
    if (n > 0) {
        return Mk("K09", CheckOutcome::Fail,
                  fmt::format("不变式 I7：{} 处相邻镜起止状态不接：{}", n, bad));
    }
    return Mk("K09", CheckOutcome::Pass,
              fmt::format("{} 面镜的 end_state → 下一镜 start_state 逐字段一致", shots.size()));
}

// ———— K10 ————
[[nodiscard]] CheckResult CheckK10(const ReadCtx& c) {
    const int totalOrd = static_cast<int>(IntSql1(*c.db, "SELECT COALESCE(MAX(ord),0) FROM chapters", 0));
    struct Fs {
        RowId id = 0;
        std::string title;
        int setup = 0;
        int payoff = 0;
        int importance = 50;
    };
    std::vector<Fs> open;
    if (auto st = c.db->Prepare("SELECT id,title,setup_ch,payoff_ch,importance FROM foreshadowings "
                                "WHERE status<>'RESOLVED' AND setup_ch>0");
        st) {
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            open.push_back(Fs{st->ColumnInt(0), st->ColumnText(1), static_cast<int>(st->ColumnInt(2)),
                              static_cast<int>(st->ColumnInt(3)), static_cast<int>(st->ColumnInt(4))});
        }
    }
    if (open.empty()) {
        return Mk("K10", CheckOutcome::Pass, "无未回收伏笔（不变式 I8 平凡成立）");
    }
    std::string blocked;
    std::string warned;
    int blockCount = 0;
    int warnCount = 0;
    const int cursor = c.ord > 0 ? c.ord : totalOrd;
    for (const Fs& f : open) {
        const int ref = f.payoff > 0 ? f.payoff : cursor;
        const int span = ref - f.setup;
        const double allowed = f.importance >= 80 ? 0.6 * static_cast<double>(totalOrd)
                                                  : (f.importance >= 40 ? 30.0 : 15.0);
        const double warnAt = f.importance >= 80 ? 1.2 * allowed : (f.importance >= 40 ? 40.0 : 20.0);
        const double blockAt = f.importance >= 80 ? 2.0 * allowed : (f.importance >= 40 ? 60.0 : 30.0);
        if (static_cast<double>(span) > blockAt) {
            ++blockCount;
            if (blockCount <= 3) {
                blocked += fmt::format("{}{}(#{}) 跨度 {} > 阻断线 {:.0f}", blocked.empty() ? "" : "；",
                                       f.title, f.id, span, blockAt);
            }
        } else if (static_cast<double>(span) > warnAt) {
            ++warnCount;
            if (warnCount <= 3) {
                warned += fmt::format("{}{}(#{}) 跨度 {} > 告警线 {:.0f}", warned.empty() ? "" : "；",
                                      f.title, f.id, span, warnAt);
            }
        }
    }
    if (blockCount > 0) {
        return Mk("K10", CheckOutcome::Fail,
                  fmt::format("不变式 I8：{} 条伏笔超期阻断：{}", blockCount, blocked));
    }
    if (warnCount > 0) {
        return MkLow(Mk("K10", CheckOutcome::Fail,
                        fmt::format("{} 条伏笔超期告警（不阻断）：{}", warnCount, warned)),
                     "");
    }
    return Mk("K10", CheckOutcome::Pass, fmt::format("{} 条未回收伏笔均未超期", open.size()));
}

// ———— K11 ————
[[nodiscard]] int ForeshadowRank(std::string_view status) {
    if (status == "PLANNED") return 0;
    if (status == "PLANTED") return 1;
    if (status == "DEVELOPING") return 2;
    if (status == "REVEALED") return 3;
    if (status == "RESOLVED") return 4;
    return -1;
}

[[nodiscard]] CheckResult CheckK11(const ReadCtx& c) {
    if (c.diff == nullptr || c.diff->foreshadows.empty()) {
        return Mk("K11", CheckOutcome::NotApplicable, "无 StateDiff 伏笔 delta");
    }
    std::string bad;
    int n = 0;
    for (const ForeshadowDelta& f : c.diff->foreshadows) {
        if (f.foreshadow_id <= 0) {
            continue; // 新建，无回退可言
        }
        auto st = c.db->Prepare("SELECT status FROM foreshadowings WHERE id=?1");
        if (!st) {
            continue;
        }
        (void)st->BindInt(1, f.foreshadow_id);
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            continue;
        }
        const int cur = ForeshadowRank(st->ColumnText(0));
        if (cur < 0) {
            continue;
        }
        int target = -1;
        if (!f.status.empty()) {
            target = ForeshadowRank(f.status);
        }
        bool regress = target >= 0 && target < cur;
        if (!regress && f.op == "progress") {
            regress = cur >= 3; // 已 REVEALED/RESOLVED 的「推进」= 回退方向
        }
        if (!regress && (f.op == "reinforce" || f.op == "payoff")) {
            regress = cur >= 4; // 已回收再回收
        }
        if (regress) {
            ++n;
            if (n <= 3) {
                bad += fmt::format("{}伏笔 #{} 现为 {}，delta(op={},status={})", bad.empty() ? "" : "；",
                                   f.foreshadow_id, st->ColumnText(0), f.op, f.status);
            }
        }
    }
    if (n > 0) {
        return Mk("K11", CheckOutcome::Fail,
                  fmt::format("伏笔状态反向迁移 {} 处（`01` §2.3.3 的顺序 PLANNED→PLANTED→DEVELOPING"
                              "→REVEALED→RESOLVED）：{}",
                              n, bad));
    }
    return Mk("K11", CheckOutcome::Pass, "伏笔状态迁移方向合法");
}

// ———— K12 ————
[[nodiscard]] CheckResult CheckK12(const ReadCtx& c) {
    if (c.diff != nullptr) {
        return Mk("K12", CheckOutcome::Pass,
                  "本章有 StateDiff（内存对象即受检对象；提交路径会把它落成 "
                  "`work/ch<NNN>/12_state_diff.json`，S12）");
    }
    if (c.projectDir == nullptr || c.projectDir->empty()) {
        return Mk("K12", CheckOutcome::Missing, "未给工程根 → 无法查 work/ch<NNN>/12_state_diff.json");
    }
    if (c.ord <= 0) {
        return Mk("K12", CheckOutcome::Missing, "章序未知，无法定位 work/ch<NNN>/");
    }
    std::error_code ec;
    const std::filesystem::path file =
        *c.projectDir / "work" / fmt::format("ch{:03}", c.ord) / "12_state_diff.json";
    if (std::filesystem::exists(file, ec)) {
        return Mk("K12", CheckOutcome::Pass, fmt::format("{} 存在", util::PathToUtf8(file)));
    }
    return Mk("K12", CheckOutcome::Fail,
              fmt::format("不变式 I10：缺 {}（每章必须产出一份 StateDiff 或声明 no_change）",
                          util::PathToUtf8(file)));
}

// ———— K13 ————
[[nodiscard]] CheckResult CheckK13(const ReadCtx& c) {
    if (c.snapshotDir == nullptr || c.snapshotDir->empty()) {
        return Mk("K13", CheckOutcome::Missing, "未给快照目录 → 无法查 ch<NNN>.json");
    }
    if (c.chapter <= 0) {
        return Mk("K13", CheckOutcome::Missing, "chapter_id 未知");
    }
    std::error_code ec;
    const std::filesystem::path file = *c.snapshotDir / fmt::format("ch{:03}.json", c.chapter);
    if (std::filesystem::exists(file, ec)) {
        return Mk("K13", CheckOutcome::Pass, fmt::format("{} 存在", util::PathToUtf8(file)));
    }
    return Mk("K13", CheckOutcome::Fail,
              fmt::format("不变式 I11：COMMIT 前无章级快照 {}", util::PathToUtf8(file)));
}

// ———— K14 ————
[[nodiscard]] CheckResult CheckK14(const ReadCtx& c) {
    const int total = CountSql(*c.db, "SELECT COUNT(*) FROM entity_fields");
    if (total == 0) {
        return Mk("K14", CheckOutcome::Pass, "无实体字段值（平凡成立）");
    }
    const int unregistered = CountSql(
        *c.db, "SELECT COUNT(*) FROM entity_fields f LEFT JOIN field_defs d "
               "ON d.field_key=f.field_key WHERE d.id IS NULL");
    if (unregistered > 0) {
        std::string keys;
        if (auto st = c.db->Prepare("SELECT DISTINCT f.field_key FROM entity_fields f "
                                    "LEFT JOIN field_defs d ON d.field_key=f.field_key "
                                    "WHERE d.id IS NULL LIMIT 5");
            st) {
            while (true) {
                auto s = st->Step();
                if (!s || *s != db::sqlite::StepResult::Row) {
                    break;
                }
                keys += (keys.empty() ? "" : ",") + st->ColumnText(0);
            }
        }
        return Mk("K14", CheckOutcome::Fail,
                  fmt::format("不变式 I12：{} 个字段值的 field_key 未登记（{}）", unregistered, keys));
    }
    const int badType = CountSql(*c.db,
                                 "SELECT COUNT(*) FROM field_defs WHERE value_type NOT IN "
                                 "('text','number','json','enum')");
    if (badType > 0) {
        return Mk("K14", CheckOutcome::Fail,
                  fmt::format("{} 条 field_defs 的 value_type 非法（只允许 text|number|json|enum）",
                              badType));
    }
    return Mk("K14", CheckOutcome::Pass, fmt::format("{} 个字段值全部有登记", total));
}

// ———— K15 ————
[[nodiscard]] bool ValidateFieldValue(std::string_view valueType, std::string_view enumJson,
                                     std::string_view valueText, std::string_view valueJson,
                                     std::string& why) {
    if (valueType == "text") {
        if (valueText.size() > 4096) {
            why = "text 超过 4096 字节";
            return false;
        }
        for (const char ch : valueText) {
            if (static_cast<unsigned char>(ch) < 0x20 && ch != '\t' && ch != '\n' && ch != '\r') {
                why = "text 含控制字符";
                return false;
            }
        }
        return true;
    }
    if (valueType == "number") {
        double v = 0.0;
        const auto* begin = valueText.data();
        const auto* end = begin + valueText.size();
        const auto r = std::from_chars(begin, end, v);
        if (valueText.empty() || r.ec != std::errc{} || r.ptr != end) {
            why = fmt::format("number 解析失败（\"{}\"）", valueText);
            return false;
        }
        return true;
    }
    if (valueType == "json") {
        yyjson_doc* doc = valueJson.empty() ? nullptr : yyjson_read(valueJson.data(), valueJson.size(), 0);
        if (doc == nullptr) {
            why = "json 不是合法 JSON";
            return false;
        }
        yyjson_val* root = yyjson_doc_get_root(doc);
        const bool objOrArr = yyjson_is_obj(root) || yyjson_is_arr(root);
        yyjson_doc_free(doc);
        if (!objOrArr) {
            why = "json 的根必须是对象或数组";
            return false;
        }
        return true;
    }
    if (valueType == "enum") {
        yyjson_doc* doc = enumJson.empty() ? nullptr : yyjson_read(enumJson.data(), enumJson.size(), 0);
        if (doc == nullptr) {
            why = "enum_json 不是合法 JSON";
            return false;
        }
        bool hit = false;
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (yyjson_is_arr(root)) {
            std::size_t idx = 0;
            std::size_t max = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(root, idx, max, item) {
                if (yyjson_is_str(item)) {
                    const char* s = yyjson_get_str(item);
                    if (s != nullptr && valueText == s) {
                        hit = true;
                    }
                }
            }
        }
        yyjson_doc_free(doc);
        if (!hit) {
            why = fmt::format("enum 值 \"{}\" 不在 enum_json 内", valueText);
            return false;
        }
        return true;
    }
    why = fmt::format("未知 value_type \"{}\"", valueType);
    return false;
}

[[nodiscard]] CheckResult CheckK15(const ReadCtx& c) {
    const int total = CountSql(*c.db, "SELECT COUNT(*) FROM entity_fields f JOIN field_defs d "
                                      "ON d.field_key=f.field_key");
    if (total == 0) {
        return Mk("K15", CheckOutcome::NotApplicable, "无已登记字段的值可校");
    }
    std::string bad;
    int n = 0;
    if (auto st = c.db->Prepare("SELECT f.field_key,d.value_type,d.enum_json,f.value_text,f.value_json "
                                "FROM entity_fields f JOIN field_defs d ON d.field_key=f.field_key "
                                "LIMIT 500");
        st) {
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            std::string why;
            if (!ValidateFieldValue(st->ColumnText(1), st->ColumnText(2), st->ColumnText(3),
                                    st->ColumnText(4), why)) {
                ++n;
                if (n <= 3) {
                    bad += fmt::format("{}{}：{}", bad.empty() ? "" : "；", st->ColumnText(0), why);
                }
            }
        }
    }
    if (n > 0) {
        return Mk("K15", CheckOutcome::Fail, fmt::format("字段值不合声明类型 {} 处：{}", n, bad));
    }
    return Mk("K15", CheckOutcome::Pass, fmt::format("{} 个字段值全部符合声明的类型/枚举", total));
}

// ———— K16 ————
[[nodiscard]] CheckResult CheckK16(const ReadCtx& c) {
    if (c.chapter <= 0) {
        return Mk("K16", CheckOutcome::NotApplicable, "未给 chapter_id");
    }
    std::string bad;
    int n = 0;
    int total = 0;
    if (auto st = c.db->Prepare("SELECT id,pov_entity_id FROM scenes WHERE chapter_id=?1 "
                                "AND pov_entity_id>0");
        st) {
        (void)st->BindInt(1, c.chapter);
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            ++total;
            const RowId sceneId = st->ColumnInt(0);
            const RowId pov = st->ColumnInt(1);
            if (CountSql2(*c.db, "SELECT COUNT(*) FROM scene_cast WHERE scene_id=?1 AND entity_id=?2",
                          sceneId, pov) == 0) {
                ++n;
                if (n <= 3) {
                    bad += fmt::format("{}scene #{} 的 pov #{}", bad.empty() ? "" : "；", sceneId, pov);
                }
            }
        }
    }
    if (total == 0) {
        return Mk("K16", CheckOutcome::NotApplicable, "本章没有声明 POV 的场景");
    }
    if (n > 0) {
        return Mk("K16", CheckOutcome::Fail, fmt::format("POV 不在 scene_cast 中 {} 处：{}", n, bad));
    }
    return Mk("K16", CheckOutcome::Pass, fmt::format("{} 个场景的 POV 都在场", total));
}

// ———— K17 ————
[[nodiscard]] CheckResult CheckK17(const ReadCtx& c) {
    if (c.diff == nullptr || c.diff->items.empty()) {
        return Mk("K17", CheckOutcome::NotApplicable, "无 StateDiff 物品 delta");
    }
    std::string bad;
    int n = 0;
    for (const ItemDelta& it : c.diff->items) {
        if (it.item_id <= 0 || it.owner_id <= 0) {
            continue;
        }
        if (it.op != "acquire" && it.op != "lose") {
            continue; // move / change_state 不涉持有关系
        }
        const int held = CountSql2(*c.db,
                                   "SELECT COUNT(*) FROM entity_ownerships WHERE item_id=?1 "
                                   "AND owner_id=?2 AND to_chapter=0",
                                   it.item_id, it.owner_id);
        if (it.op == "acquire" && held > 0) {
            ++n;
            if (n <= 3) {
                bad += fmt::format("{}物品 #{} 已被 #{} 持有却 acquire", bad.empty() ? "" : "；",
                                   it.item_id, it.owner_id);
            }
        } else if (it.op == "lose" && held == 0) {
            ++n;
            if (n <= 3) {
                bad += fmt::format("{}物品 #{} 不在 #{} 手上却 lose", bad.empty() ? "" : "；",
                                   it.item_id, it.owner_id);
            }
        }
    }
    if (n > 0) {
        return Mk("K17", CheckOutcome::Fail, fmt::format("物品持有关系不自洽 {} 处：{}", n, bad));
    }
    return Mk("K17", CheckOutcome::Pass, "物品 acquire/lose 与当前持有关系自洽");
}

// ———— K18 ————
[[nodiscard]] CheckResult CheckK18(const ReadCtx& c) {
    if (c.chapter <= 0) {
        return Mk("K18", CheckOutcome::NotApplicable, "未给 chapter_id");
    }
    struct SceneLeg {
        RowId location = 0;
        std::string timeLabel;
    };
    std::vector<SceneLeg> legs;
    if (auto st = c.db->Prepare("SELECT location_id,time_label FROM scenes WHERE chapter_id=?1 "
                                "ORDER BY ord,id");
        st) {
        (void)st->BindInt(1, c.chapter);
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            legs.push_back(SceneLeg{st->ColumnInt(0), st->ColumnText(1)});
        }
    }
    if (legs.size() < 2) {
        return Mk("K18", CheckOutcome::NotApplicable, "本章不足两场，无从比对行程");
    }
    NovelGraph graph(*c.db);
    std::string bad;
    int n = 0;
    int unknown = 0;
    for (std::size_t i = 0; i + 1 < legs.size(); ++i) {
        const SceneLeg& a = legs[i];
        const SceneLeg& b = legs[i + 1];
        if (a.location <= 0 || b.location <= 0 || a.location == b.location) {
            continue;
        }
        auto days = graph.LocationDistanceDays(a.location, b.location);
        if (!days || *days < 0.0) {
            ++unknown; // `06` §2.3：见未知跳过
            continue;
        }
        if (*days >= 1.0 && !a.timeLabel.empty() && a.timeLabel == b.timeLabel) {
            ++n;
            if (n <= 3) {
                bad += fmt::format("{}场 {}→{} 地点 #{}→#{} 需 {:.1f} 天但 time_label 均为「{}」",
                                   bad.empty() ? "" : "；", i + 1, i + 2, a.location, b.location, *days,
                                   a.timeLabel);
            }
        }
    }
    if (n > 0) {
        return Mk("K18", CheckOutcome::Fail, fmt::format("行程时间不可信 {} 处：{}", n, bad));
    }
    return Mk("K18", CheckOutcome::Pass,
              fmt::format("相邻场地点变化可解释（{} 组无距离记录已跳过；time_label 是自由文本，"
                          "只做相等比较）",
                          unknown));
}

// 前向声明：下面的库来源聚合要用它（定义在 K24 段）
[[nodiscard]] CheckResult ImplBeatTimeline(std::span<const BeatSpan> beats, double duration_s);

// ———— v9（S13）：K09 / K22 / K24 的**库来源**（`shots` 的起止状态与 Beat 时间轴）————
// 受检对象优先用调用方显式传入的（桥/生成侧可以直接给），没给就从库里读 —— 于是这三条从
// `contract-input` 升为 `library`（数据来源落地后 `CheckSpec::availability` 也随之改）。
[[nodiscard]] std::vector<ShotStateSnapshot> LoadShotSnapshots(db::sqlite::Database& db,
                                                             RowId chapterId) {
    std::vector<ShotStateSnapshot> out;
    if (chapterId <= 0) {
        return out;
    }
    auto st = db.Prepare(
        "SELECT s.id,s.ord,s.start_state_json,s.end_state_json,s.character_ids_json "
        "FROM shots s JOIN scenes sc ON sc.id=s.scene_id WHERE sc.chapter_id=?1 "
        "ORDER BY sc.ord,s.ord,s.id");
    if (!st) {
        return out;
    }
    (void)st->BindInt(1, chapterId);
    while (true) {
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            break;
        }
        ShotStateSnapshot snap;
        snap.shot_id = st->ColumnInt(0);
        snap.ord = static_cast<int>(st->ColumnInt(1));
        snap.start_state_json = st->ColumnText(2);
        snap.end_state_json = st->ColumnText(3);
        snap.character_ids = ParseIdArray(st->ColumnText(4));
        out.push_back(std::move(snap));
    }
    return out;
}

// K24 的库来源：`shots.timeline_json` = `{"duration_s":N,"beats":[{begin_s,end_s}]}`（`02` §2.9）。
// 逐镜跑一遍覆盖/重叠判定并汇总（任一一镜不合规即 fail）。
[[nodiscard]] CheckResult CheckTimelineFromDb(db::sqlite::Database& db, RowId chapterId) {
    if (chapterId <= 0) {
        return Mk("K24", CheckOutcome::NotApplicable, "未给 chapter_id");
    }
    auto st = db.Prepare("SELECT s.id,s.timeline_json FROM shots s JOIN scenes sc "
                         "ON sc.id=s.scene_id WHERE sc.chapter_id=?1 ORDER BY sc.ord,s.ord,s.id");
    if (!st) {
        return Mk("K24", CheckOutcome::Missing, "查询 shots 失败");
    }
    (void)st->BindInt(1, chapterId);
    int withTimeline = 0;
    std::string bad;
    int badCount = 0;
    while (true) {
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            break;
        }
        const RowId shotId = st->ColumnInt(0);
        const std::string text = st->ColumnText(1);
        if (text.empty() || text == "{}") {
            continue; // 该镜没产出时间轴
        }
        ++withTimeline;
        double duration = 0.0;
        std::vector<BeatSpan> beats;
        yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
        if (doc == nullptr) {
            ++badCount;
            bad += fmt::format("{}镜 #{} 的 timeline_json 不是合法 JSON",
                               bad.empty() ? "" : "；", shotId);
            continue;
        }
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (yyjson_is_obj(root)) {
            if (yyjson_val* d = yyjson_obj_get(root, "duration_s"); yyjson_is_num(d)) {
                duration = yyjson_get_real(d);
            }
            if (yyjson_val* arr = yyjson_obj_get(root, "beats"); yyjson_is_arr(arr)) {
                std::size_t idx = 0;
                std::size_t max = 0;
                yyjson_val* item = nullptr;
                yyjson_arr_foreach(arr, idx, max, item) {
                    BeatSpan span;
                    if (yyjson_val* b = yyjson_obj_get(item, "begin_s"); yyjson_is_num(b)) {
                        span.begin_s = yyjson_get_real(b);
                    }
                    if (yyjson_val* e = yyjson_obj_get(item, "end_s"); yyjson_is_num(e)) {
                        span.end_s = yyjson_get_real(e);
                    }
                    beats.push_back(span);
                }
            }
        }
        yyjson_doc_free(doc);
        const CheckResult one = ImplBeatTimeline(beats, duration);
        if (one.outcome == CheckOutcome::Fail || one.outcome == CheckOutcome::Missing) {
            ++badCount;
            if (badCount <= 3) {
                bad += fmt::format("{}镜 #{}：{}", bad.empty() ? "" : "；", shotId, one.detail);
            }
        }
    }
    if (withTimeline == 0) {
        return Mk("K24", CheckOutcome::NotApplicable,
                  "本章没有镜产出 Beat 时间轴（`shots.timeline_json` 全空）");
    }
    if (badCount > 0) {
        return Mk("K24", CheckOutcome::Fail,
                  fmt::format("{} 面镜的 Beat 时间轴不合规：{}", badCount, bad));
    }
    return Mk("K24", CheckOutcome::Pass, fmt::format("{} 面镜的 Beat 时间轴覆盖合规", withTimeline));
}

// K23 的库来源：本章最新的 PromptArtifact（`02` §2.10）。没有则返回空（= NotApplicable）。
struct PromptHashSource {
    std::string hash;
    std::string chain = "visual";
    std::string stage;
    RowId artifactId = 0;
};

[[nodiscard]] PromptHashSource LoadPromptHash(db::sqlite::Database& db, RowId chapterId) {
    PromptHashSource out;
    if (chapterId <= 0) {
        return out;
    }
    NovelVisual visual(db);
    auto list = visual.ListPromptArtifacts(chapterId);
    if (!list || list->empty()) {
        return out;
    }
    // `ListPromptArtifacts` 已按 updated DESC,id DESC → 第一条即最新
    out.hash = (*list)[0].input_state_hash;
    out.chain = (*list)[0].chain.empty() ? std::string{"visual"} : (*list)[0].chain;
    out.stage = (*list)[0].stage;
    out.artifactId = (*list)[0].id;
    return out;
}

// ———— K19–K21（ContractInput：生成侧结论由 video 侧回填，规则不在这里重复实现）————
[[nodiscard]] std::vector<CheckResult> ImplGeneration(const GenerationCheckInput& gen) {
    std::vector<CheckResult> out;
    if (!gen.has_graph) {
        out.push_back(Mk("K19", CheckOutcome::NotApplicable, "本次没有提交 API 图（无受检对象）"));
    } else if (gen.graph_ok) {
        out.push_back(Mk("K19", CheckOutcome::Pass,
                         gen.graph_detail.empty() ? "API 图对 /object_info 校验通过" : gen.graph_detail));
    } else {
        out.push_back(Mk("K19", CheckOutcome::Fail,
                         gen.graph_detail.empty() ? "API 图校验不通过（`13` §2.6 U4 的闸门）"
                                                  : gen.graph_detail));
    }
    if (!gen.has_sanitize) {
        out.push_back(Mk("K20", CheckOutcome::NotApplicable, "未跑 VideoProject.Sanitize"));
    } else if (gen.size_corrections <= 0) {
        out.push_back(Mk("K20", CheckOutcome::Pass, "宽高 / 帧数无需纠正"));
    } else {
        CheckResult r = Mk("K20", CheckOutcome::Fail,
                           fmt::format("{} 处宽高/帧数被 Sanitize 自动纠正（low：放行但必须可见）{}",
                                       gen.size_corrections,
                                       gen.sanitize_detail.empty() ? "" : "：" + gen.sanitize_detail));
        r.severity = "low";
        out.push_back(r);
    }
    if (!gen.has_sanitize) {
        out.push_back(Mk("K21", CheckOutcome::NotApplicable, "未跑 VideoProject.Sanitize"));
    } else if (gen.ref_truncations <= 0) {
        out.push_back(Mk("K21", CheckOutcome::Pass, "参考图未超上限（≤9）"));
    } else {
        out.push_back(Mk("K21", CheckOutcome::Fail,
                         fmt::format("{} 面镜的参考图被截断（上限 9，`11` §2.5）", gen.ref_truncations)));
    }
    return out;
}

// ———— K22 ————
[[nodiscard]] CheckResult CheckK22(const ReadCtx& c) {
    if (c.shots.empty()) {
        return Mk("K22", CheckOutcome::NotApplicable, "无叙事分镜（NarrativeShot）→ 无受检角色");
    }
    if (c.chapter <= 0) {
        return Mk("K22", CheckOutcome::Missing, "有叙事分镜但缺 chapter_id，无法解析视觉阶段");
    }
    NovelVisual visual(*c.db);
    std::string bad;
    int noAsset = 0;
    int noState = 0;
    int total = 0;
    for (const ShotStateSnapshot& shot : c.shots) {
        for (const RowId entityId : shot.character_ids) {
            if (entityId <= 0) {
                continue;
            }
            ++total;
            auto asset = visual.FindAssetByEntity(entityId);
            if (!asset) {
                ++noAsset;
                if (noAsset <= 3) {
                    bad += fmt::format("{}角色 #{} 无 visual_assets", bad.empty() ? "" : "；", entityId);
                }
                continue;
            }
            if (auto state = visual.ResolveVisualState(asset->id, c.chapter); !state) {
                ++noState;
                if (noState <= 3) {
                    bad += fmt::format("{}角色 #{} 在第 {} 章解析不出视觉阶段",
                                       bad.empty() ? "" : "；", entityId, c.chapter);
                }
            }
        }
    }
    if (total == 0) {
        return Mk("K22", CheckOutcome::NotApplicable, "分镜里没有角色");
    }
    if (noAsset > 0) {
        return Mk("K22", CheckOutcome::Missing,
                  fmt::format("{} 个出场角色没有视觉资产、{} 个解析不出阶段：{}", noAsset, noState, bad));
    }
    if (noState > 0) {
        return Mk("K22", CheckOutcome::Fail,
                  fmt::format("{} 个出场角色在本章解析不出 visual_states：{}", noState, bad));
    }
    return Mk("K22", CheckOutcome::Pass, fmt::format("{} 个出场角色都能解析出阶段外观", total));
}

// ———— K23 ————
[[nodiscard]] CheckResult CheckK23(const ReadCtx& c) {
    if (c.promptHash.empty()) {
        return Mk("K23", CheckOutcome::NotApplicable,
                  "无 PromptArtifact.input_state_hash（`02` §2.10 无表存储，阶段产物未落地）");
    }
    if (c.chapter <= 0) {
        return Mk("K23", CheckOutcome::Missing, "有产物哈希但缺 chapter_id，无法重算当前状态哈希");
    }
    return CheckStateHashMatch(c.promptHash,
                               ComputeInputStateHash(*c.db, c.chapter, c.chain, c.stage));
}

// ———— K24（ContractInput）————
[[nodiscard]] CheckResult ImplBeatTimeline(std::span<const BeatSpan> beats, double duration_s) {
    if (beats.empty()) {
        return Mk("K24", CheckOutcome::NotApplicable, "无 Beat[]（音频 / 对白节拍尚未产出）");
    }
    if (!(duration_s > 0.0)) {
        return Mk("K24", CheckOutcome::Missing, "有 Beat[] 但没给 duration，无法判定覆盖");
    }
    std::vector<BeatSpan> sorted(beats.begin(), beats.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const BeatSpan& a, const BeatSpan& b) { return a.begin_s < b.begin_s; });
    std::string bad;
    for (const BeatSpan& b : sorted) {
        if (b.end_s < b.begin_s) {
            bad += fmt::format("{}[{:.2f},{:.2f}] 区间反了", bad.empty() ? "" : "；", b.begin_s, b.end_s);
        }
    }
    if (std::fabs(sorted.front().begin_s) > 1e-6) {
        bad += fmt::format("{}首拍从 {:.2f}s 开始（未覆盖 0）", bad.empty() ? "" : "；",
                           sorted.front().begin_s);
    }
    for (std::size_t i = 0; i + 1 < sorted.size(); ++i) {
        if (sorted[i].end_s > sorted[i + 1].begin_s + 1e-6) {
            bad += fmt::format("{}第 {}、{} 拍重叠（{:.2f} > {:.2f}）", bad.empty() ? "" : "；", i + 1,
                               i + 2, sorted[i].end_s, sorted[i + 1].begin_s);
            break;
        }
    }
    if (std::fabs(sorted.back().end_s - duration_s) > 1e-6) {
        bad += fmt::format("{}末拍到 {:.2f}s，未覆盖 {:.2f}s", bad.empty() ? "" : "；",
                           sorted.back().end_s, duration_s);
    }
    if (!bad.empty()) {
        return Mk("K24", CheckOutcome::Fail, fmt::format("Beat 时间轴不合规：{}", bad));
    }
    return Mk("K24", CheckOutcome::Pass,
              fmt::format("{} 个 beat 覆盖 [0,{:.2f}] 且互不重叠", beats.size(), duration_s));
}

// ———— K25 ————
[[nodiscard]] CheckResult CheckK25(const ReadCtx& c) {
    if (c.chapter <= 0) {
        return Mk("K25", CheckOutcome::NotApplicable, "未给 chapter_id");
    }
    const RowId words = IntSql1(*c.db, "SELECT words FROM chapters WHERE id=?1", c.chapter, -1);
    if (words < 0) {
        return Mk("K25", CheckOutcome::Missing, fmt::format("章节 #{} 不存在", c.chapter));
    }
    if (words == 0) {
        return Mk("K25", CheckOutcome::NotApplicable, "正文未写（words=0）");
    }
    const double target = static_cast<double>(c.wordTarget);
    const double lo = target * 0.7;
    const double hi = target * 1.3;
    if (static_cast<double>(words) < lo || static_cast<double>(words) > hi) {
        CheckResult r = Mk("K25", CheckOutcome::Fail,
                           fmt::format("字数 {} 超出 word_target {} 的 ±30%（[{:.0f},{:.0f}]，low："
                                       "记录但不阻断）",
                                       words, c.wordTarget, lo, hi));
        r.severity = "low";
        return r;
    }
    return Mk("K25", CheckOutcome::Pass,
              fmt::format("字数 {} 在 [ {:.0f}, {:.0f} ] 内", words, lo, hi));
}

// ———— K26 ————
[[nodiscard]] CheckResult CheckK26(const ReadCtx& c) {
    std::string bad;
    int n = 0;
    if (auto st = c.db->Prepare("SELECT DISTINCT kind FROM entities"); st) {
        while (true) {
            auto s = st->Step();
            if (!s || *s != db::sqlite::StepResult::Row) {
                break;
            }
            const std::string k = st->ColumnText(0);
            if (!IsRegisteredKind(k)) {
                ++n;
                if (n <= 5) {
                    bad += fmt::format("{}{}", bad.empty() ? "" : ",", k);
                }
            }
        }
    }
    if (n > 0) {
        return Mk("K26", CheckOutcome::Fail,
                  fmt::format("不变式 I13：{} 种未登记的 entities.kind（{}）—— 只允许 `01` §1.1.2 的 31 种",
                              n, bad));
    }
    return Mk("K26", CheckOutcome::Pass, "entities.kind 全部命中 31 种元类别");
}

// ———— K27 ————
[[nodiscard]] CheckResult CheckK27(const ReadCtx& c) {
    const std::int64_t nowSec = static_cast<std::int64_t>(util::NowMillis() / 1000);
    const std::int64_t cutoff = c.orphanStaleSeconds <= 0
                                    ? (std::int64_t{1} << 62)
                                    : nowSec - c.orphanStaleSeconds;
    const int n = CountSql1(*c.db,
                            "SELECT COUNT(*) FROM generated_images WHERE status IN "
                            "('RUNNING','QUEUED') AND updated < ?1",
                            cutoff);
    if (n > 0) {
        std::string ids;
        if (auto st = c.db->Prepare("SELECT job_id FROM generated_images WHERE status IN "
                                    "('RUNNING','QUEUED') AND updated < ?1 LIMIT 3");
            st) {
            (void)st->BindInt(1, cutoff);
            while (true) {
                auto s = st->Step();
                if (!s || *s != db::sqlite::StepResult::Row) {
                    break;
                }
                ids += (ids.empty() ? "" : ",") + st->ColumnText(0);
            }
        }
        return Mk("K27", CheckOutcome::Fail,
                  fmt::format("{} 条出图任务停在 RUNNING/QUEUED 超过 {}s（job: {}）—— 启动回收兜底"
                              "（`11` §2.6.5）",
                              n, c.orphanStaleSeconds, ids));
    }
    return Mk("K27", CheckOutcome::Pass, "无超期的 RUNNING/QUEUED 出图任务");
}

// ———— K28 ————
[[nodiscard]] CheckResult CheckK28(const ReadCtx& c) {
    int degraded = 0;
    if (c.ord > 0) {
        degraded = CountSql1(*c.db,
                             "SELECT COUNT(*) FROM visual_artifacts WHERE degraded=1 "
                             "AND (chapter_scope=0 OR chapter_scope<=?1) "
                             "AND (chapter_to=0 OR chapter_to>=?1)",
                             c.ord);
    } else {
        degraded = CountSql(*c.db, "SELECT COUNT(*) FROM visual_artifacts WHERE degraded=1");
    }
    const int genCorrections =
        (c.gen != nullptr && c.gen->has_sanitize)
            ? c.gen->size_corrections + c.gen->ref_truncations
            : 0;
    bool onDisk = false;
    if (c.projectDir != nullptr && !c.projectDir->empty()) {
        std::error_code ec;
        onDisk = std::filesystem::exists(*c.projectDir / "degradations.jsonl", ec) ||
                 std::filesystem::exists(*c.projectDir / "cost_report.json", ec);
    }
    const bool recorded = (c.gen != nullptr && c.gen->degradations_recorded) || onDisk;
    if ((degraded > 0 || genCorrections > 0) && !recorded) {
        return Mk("K28", CheckOutcome::Fail,
                  fmt::format("有降级（产物 degraded=1 共 {} 条；生成侧被纠正 {} 处）但账不可见："
                              "`degradations.jsonl` 与 `cost_report.json` 都不存在",
                              degraded, genCorrections));
    }
    if (degraded > 0 || genCorrections > 0) {
        return Mk("K28", CheckOutcome::Pass,
                  fmt::format("降级已可见（产物 {} 条 / 生成侧 {} 处）", degraded, genCorrections));
    }
    return Mk("K28", CheckOutcome::Pass, "无降级");
}

// ———— K29 ————
[[nodiscard]] bool IsAssetConsumableStatus(std::string_view status) {
    return status == "SHEET_READY" || status == "WARDROBE_READY" || status == "READY";
}

[[nodiscard]] CheckResult CheckK29(const ReadCtx& c) {
    const std::string sql =
        c.ord > 0 ? "SELECT a.id,v.status FROM visual_artifacts a JOIN visual_assets v "
                    "ON v.id=a.asset_id WHERE a.layer IN ('stage','shot') "
                    "AND (a.chapter_scope=0 OR a.chapter_scope<=?1) "
                    "AND (a.chapter_to=0 OR a.chapter_to>=?1)"
                  : "SELECT a.id,v.status FROM visual_artifacts a JOIN visual_assets v "
                    "ON v.id=a.asset_id WHERE a.layer IN ('stage','shot')";
    auto st = c.db->Prepare(sql);
    if (!st) {
        return Mk("K29", CheckOutcome::Missing, "查询 visual_artifacts 失败");
    }
    if (c.ord > 0) {
        (void)st->BindInt(1, c.ord);
    }
    std::string bad;
    int n = 0;
    int total = 0;
    while (true) {
        auto s = st->Step();
        if (!s || *s != db::sqlite::StepResult::Row) {
            break;
        }
        ++total;
        const std::string status = st->ColumnText(1);
        if (!IsAssetConsumableStatus(status)) {
            ++n;
            if (n <= 3) {
                bad += fmt::format("{}产物 #{} 的资产 status={}", bad.empty() ? "" : "；",
                                   st->ColumnInt(0), status);
            }
        }
    }
    if (total == 0) {
        return Mk("K29", CheckOutcome::NotApplicable, "本章没有作为参考图的资产产物");
    }
    if (n > 0) {
        return Mk("K29", CheckOutcome::Fail,
                  fmt::format("`11` §2.6.1 S1：{} 条参考图产物的资产未就绪（需 SHEET_READY 起）：{}", n,
                              bad));
    }
    return Mk("K29", CheckOutcome::Pass, fmt::format("{} 条参考图产物的资产均已就绪", total));
}

// ———— `04` §2.5 的 sha1（K23 的算法）————
[[nodiscard]] std::uint32_t Rotl32(std::uint32_t v, int bits) noexcept {
    return (v << bits) | (v >> (32 - bits));
}

// ———— 自检用：把一条违规断言收敛成「加数据 → 只查那一条」————
[[nodiscard]] const CheckResult* Probe(ValidationReport& report, std::string_view id) {
    return report.Find(id);
}

} // namespace

// ═══════════════════════ 公开接口 ═══════════════════════

std::string_view CheckOutcomeName(CheckOutcome outcome) noexcept {
    switch (outcome) {
    case CheckOutcome::Pass: return "pass";
    case CheckOutcome::Fail: return "fail";
    case CheckOutcome::Missing: return "missing";
    case CheckOutcome::NotApplicable: return "n/a";
    }
    return "n/a";
}

bool CheckPassed(const CheckResult& result) noexcept {
    if (result.outcome == CheckOutcome::Pass || result.outcome == CheckOutcome::NotApplicable) {
        return true;
    }
    return result.severity == "low"; // `07` §2.3：low 记录但放行
}

std::string_view CheckAvailabilityName(CheckAvailability availability) noexcept {
    switch (availability) {
    case CheckAvailability::Library: return "library";
    case CheckAvailability::Artifact: return "artifact";
    case CheckAvailability::ContractInput: return "contract";
    }
    return "library";
}

std::span<const CheckSpec> CheckCatalog() noexcept { return kCatalog; }

std::vector<std::string_view> AllCheckIds() {
    std::vector<std::string_view> ids;
    ids.reserve(std::size(kCatalog));
    for (const CheckSpec& spec : kCatalog) {
        ids.push_back(spec.check_id);
    }
    return ids;
}

bool VerifiersComplete() noexcept {
    if (std::size(kCatalog) != 29) {
        return false;
    }
    for (std::size_t i = 0; i < std::size(kCatalog); ++i) {
        if (kCatalog[i].check_id != fmt::format("K{:02}", i + 1)) {
            return false; // 目录必须正好是 K01…K29，无缺号无占位
        }
    }
    return true;
}

bool AllChecksPass(std::span<const CheckResult> results) noexcept {
    for (const CheckResult& r : results) {
        if (!CheckPassed(r)) {
            return false;
        }
    }
    return true;
}

const CheckResult* ValidationReport::Find(std::string_view checkId) const noexcept {
    for (const CheckResult& r : checks) {
        if (r.check_id == checkId) {
            return &r;
        }
    }
    return nullptr;
}

bool ValidationReport::Ok() const noexcept { return AllChecksPass(checks); }

std::vector<std::string> ValidationReport::FailedIds() const {
    std::vector<std::string> ids;
    for (const CheckResult& r : checks) {
        if (!CheckPassed(r)) {
            ids.push_back(r.check_id);
        }
    }
    return ids;
}

std::vector<std::string> ValidationReport::RanIds() const {
    std::vector<std::string> ids;
    for (const CheckResult& r : checks) {
        if (r.outcome != CheckOutcome::NotApplicable) {
            ids.push_back(r.check_id);
        }
    }
    return ids;
}

std::vector<std::string> ValidationReport::SkippedIds() const {
    std::vector<std::string> ids;
    for (const CheckResult& r : checks) {
        if (r.outcome == CheckOutcome::NotApplicable) {
            ids.push_back(r.check_id);
        }
    }
    return ids;
}

std::string ValidationReport::Describe() const {
    std::string out = fmt::format("K01–K29 校验（章 #{}）：{} 条，{} 不通过", chapter_id, checks.size(),
                                  FailedIds().size());
    for (const CheckResult& r : checks) {
        if (CheckPassed(r)) {
            continue;
        }
        out += fmt::format("；{}({}) {}：{}", r.check_id, CheckOutcomeName(r.outcome), r.severity,
                           r.detail);
    }
    return out;
}

std::string ValidationReport::Json() const {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return "{}";
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "chapter_id", chapter_id);
    yyjson_mut_obj_add_bool(doc, root, "ok", Ok());
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    for (const CheckResult& r : checks) {
        yyjson_mut_val* item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, item, "check_id", r.check_id.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "name", r.name.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "outcome",
                                  std::string{CheckOutcomeName(r.outcome)}.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "severity", r.severity.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "detail", r.detail.c_str());
        (void)yyjson_mut_arr_add_val(arr, item);
    }
    (void)yyjson_mut_obj_add_val(doc, root, "checks", arr);
    const char* text = yyjson_mut_write(doc, 0, nullptr);
    std::string out = text == nullptr ? std::string{"{}"} : std::string{text};
    if (text != nullptr) {
        free(const_cast<char*>(text));
    }
    yyjson_mut_doc_free(doc);
    return out;
}

std::string Sha1Hex(std::string_view data) {
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<unsigned char> msg(data.begin(), data.end());
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8u;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<unsigned char>((bits >> (i * 8)) & 0xFF));
    }
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[80] = {};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = Rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = Rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rotl32(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    return fmt::format("{:08x}{:08x}{:08x}{:08x}{:08x}", h[0], h[1], h[2], h[3], h[4]);
}

std::string ComputeInputStateHash(db::sqlite::Database& db, RowId chapter_id, std::string_view chain,
                                  std::string_view stage) {
    // `04` §2.5 的输入清单是**封闭列表**，顺序固定。⚠️ 与规格的两处偏差（已在 `Plan/证据.md` 记账）：
    //   ① `writing_style` / `author_rules` 表**没有 `updated` 列** → 用整行内容代替时间戳；
    //   ② 「相关 entity ids」的**相关性打分**（`04` §2.3）尚未落地 → 用 `created_chapter<=N` 全量排序代替。
    std::string canon;
    canon += "contract_version=1\n";
    canon += fmt::format("chain={}\n", chain);
    canon += fmt::format("stage={}\n", stage);
    canon += fmt::format("chapter_id={}\n", chapter_id);
    canon += fmt::format("pov_entity_id={}\n",
                         IntSql1(db, "SELECT pov_entity_id FROM chapters WHERE id=?1", chapter_id));
    const auto appendRows = [&canon, &db](std::string_view tag, std::string_view sql, RowId bind) {
        canon += std::string{tag} + "=";
        auto st = db.Prepare(sql);
        if (st) {
            if (bind > 0) {
                (void)st->BindInt(1, bind);
            }
            bool first = true;
            while (true) {
                auto s = st->Step();
                if (!s || *s != db::sqlite::StepResult::Row) {
                    break;
                }
                canon += fmt::format("{}{}:{}", first ? "" : ",", st->ColumnInt(0),
                                     st->ColumnText(1));
                first = false;
            }
        }
        canon += "\n";
    };
    appendRows("entities", "SELECT id,updated FROM entities WHERE created_chapter<=?1 ORDER BY id",
               chapter_id);
    appendRows("foreshadows", "SELECT id,status FROM foreshadowings ORDER BY id", 0);
    appendRows("plots", "SELECT id,status FROM plots ORDER BY id", 0);
    appendRows("mysteries", "SELECT id,status FROM mysteries ORDER BY id", 0);
    {
        canon += "fields=";
        if (auto st = db.Prepare("SELECT scope,entity_kind,field_key,value_type,updated FROM field_defs "
                                 "ORDER BY field_key");
            st) {
            bool first = true;
            while (true) {
                auto s = st->Step();
                if (!s || *s != db::sqlite::StepResult::Row) {
                    break;
                }
                canon += fmt::format("{}{}:{}:{}:{}:{}", first ? "" : ",", st->ColumnText(0),
                                     st->ColumnText(1), st->ColumnText(2), st->ColumnText(3),
                                     st->ColumnInt(4));
                first = false;
            }
        }
        canon += "\n";
    }
    {
        canon += "writing_style=";
        if (auto st = db.Prepare("SELECT pov_mode,sentence_len,density,dialogue_ratio,action_ratio,"
                                 "thought_ratio,env_ratio,humor,serious,pacing,note FROM writing_style "
                                 "WHERE id=1");
            st) {
            auto s = st->Step();
            if (s && *s == db::sqlite::StepResult::Row) {
                canon += fmt::format("{},{},{},{:.4f},{:.4f},{:.4f},{:.4f},{},{},{},{}", st->ColumnText(0),
                                     st->ColumnText(1), st->ColumnText(2), st->ColumnDouble(3),
                                     st->ColumnDouble(4), st->ColumnDouble(5), st->ColumnDouble(6),
                                     st->ColumnInt(7), st->ColumnInt(8), st->ColumnText(9),
                                     st->ColumnText(10));
            }
        }
        canon += "\n";
    }
    appendRows("author_rules",
               "SELECT id,severity FROM author_rules WHERE severity='error' ORDER BY id", 0);
    return Sha1Hex(canon);
}

CheckResult CheckStateHashMatch(std::string_view stored_hash, std::string_view current_hash) {
    if (stored_hash.empty()) {
        return Mk("K23", CheckOutcome::NotApplicable, "产物没有记录 input_state_hash");
    }
    if (stored_hash == current_hash) {
        return Mk("K23", CheckOutcome::Pass,
                  fmt::format("input_state_hash 一致（{}）", std::string{current_hash.substr(0, 12)}));
    }
    return Mk("K23", CheckOutcome::Fail,
              fmt::format("不变式 I9：PromptArtifact.input_state_hash={} 与当前状态 {} 不一致 → 不得复用，"
                          "必须重新生成",
                          stored_hash, current_hash));
}

CheckResult CheckShotContinuity(std::span<const ShotStateSnapshot> shots) {
    return ImplShotContinuity(shots);
}

CheckResult CheckBeatTimeline(std::span<const BeatSpan> beats, double duration_s) {
    return ImplBeatTimeline(beats, duration_s);
}

std::vector<CheckResult> CheckGeneration(const GenerationCheckInput& gen) {
    return ImplGeneration(gen);
}

ValidationReport RunChapterChecks(db::sqlite::Database& db, const CheckInputs& in) {
    ValidationReport report;
    report.chapter_id = in.chapter_id;

    // K13 的快照目录：优先用显式传入的（`NovelCommit` 只知道 `snapshot_dir`），否则按工程根约定
    const std::filesystem::path snapDir =
        !in.snapshot_dir.empty()
            ? in.snapshot_dir
            : (in.project_dir.empty() ? std::filesystem::path{}
                                      : in.project_dir / "snapshots");

    ReadCtx c;
    c.db = &db;
    c.chapter = in.chapter_id;
    c.ord = in.chapter_ord > 0 ? in.chapter_ord
                               : static_cast<int>(IntSql1(db, "SELECT ord FROM chapters WHERE id=?1",
                                                          in.chapter_id));
    c.projectDir = &in.project_dir;
    c.snapshotDir = &snapDir;
    c.wordTarget = in.word_target > 0 ? in.word_target : 3000;
    // v9（S13）：受检对象**优先用调用方给的**（桥 / 生成侧可直接给），没给就从库读 ——
    // 于是 K09 / K22 / K23 / K24 由 `contract-input` 升为 `library`（数据来源落地）。
    // 注意这两个局部量必须活到函数结束（`ReadCtx` 里的 span / string_view 指向它们）。
    const std::vector<ShotStateSnapshot> shotsFromDb =
        in.shots.empty() ? LoadShotSnapshots(db, in.chapter_id) : std::vector<ShotStateSnapshot>{};
    const PromptHashSource hashFromDb =
        in.prompt_state_hash.empty() ? LoadPromptHash(db, in.chapter_id) : PromptHashSource{};
    c.shots = in.shots.empty() ? std::span<const ShotStateSnapshot>(shotsFromDb) : in.shots;
    c.beats = in.beats;
    c.duration = in.scene_duration_s;
    c.promptHash = in.prompt_state_hash.empty() ? std::string_view{hashFromDb.hash}
                                               : in.prompt_state_hash;
    c.chain = in.prompt_state_hash.empty() ? std::string_view{hashFromDb.chain} : in.prompt_chain;
    c.stage = in.prompt_state_hash.empty() ? std::string_view{hashFromDb.stage} : in.prompt_stage;
    c.gen = &in.gen;
    c.orphanStaleSeconds = in.orphan_stale_seconds;

    // StateDiff：内存优先；没有就尝试读 `work/ch<NNN>/12_state_diff.json`（`03` 的阶段产物）
    StateDiff owned;
    if (in.diff != nullptr) {
        c.diff = in.diff;
    } else if (!in.project_dir.empty() && c.ord > 0) {
        const std::filesystem::path file =
            in.project_dir / "work" / fmt::format("ch{:03}", c.ord) / "12_state_diff.json";
        if (const auto text = util::ReadFileBytes(file); text && StateDiffFromJson(*text, owned)) {
            c.diff = &owned;
        }
    }

    report.checks.push_back(CheckK01(c));
    report.checks.push_back(CheckK02(c));
    report.checks.push_back(CheckK03(c));
    report.checks.push_back(CheckK04(c));
    report.checks.push_back(CheckK05(c));
    report.checks.push_back(CheckK06(c));
    report.checks.push_back(CheckK07(c));
    report.checks.push_back(CheckK08(c));
    report.checks.push_back(ImplShotContinuity(c.shots));
    report.checks.push_back(CheckK10(c));
    report.checks.push_back(CheckK11(c));
    report.checks.push_back(CheckK12(c));
    report.checks.push_back(CheckK13(c));
    report.checks.push_back(CheckK14(c));
    report.checks.push_back(CheckK15(c));
    report.checks.push_back(CheckK16(c));
    report.checks.push_back(CheckK17(c));
    report.checks.push_back(CheckK18(c));
    for (CheckResult& r : ImplGeneration(*c.gen)) {
        report.checks.push_back(std::move(r));
    }
    report.checks.push_back(CheckK22(c));
    report.checks.push_back(CheckK23(c));
    // K24：显式给了 beat 就用它，否则逐镜读 `shots.timeline_json`（v9 起）
    report.checks.push_back(c.beats.empty() ? CheckTimelineFromDb(db, in.chapter_id)
                                            : ImplBeatTimeline(c.beats, c.duration));
    report.checks.push_back(CheckK25(c));
    report.checks.push_back(CheckK26(c));
    report.checks.push_back(CheckK27(c));
    report.checks.push_back(CheckK28(c));
    report.checks.push_back(CheckK29(c));
    return report;
}

// ═══════════════════════ 自检 ═══════════════════════
int RunChecksSelfCheck() {
    int fails = 0;
    const auto expect = [&fails](bool cond, std::string_view what) {
        if (!cond) {
            ++fails;
            log::Error("K 校验自检 FAIL：{}", what);
        }
    };

    // ① 目录完整性 + `auto` 前置②
    expect(CheckCatalog().size() == 29, "目录必须是 29 条");
    expect(VerifiersComplete(), "VerifiersComplete 必须为真（29/29）");
    for (std::size_t i = 0; i < CheckCatalog().size(); ++i) {
        const CheckSpec& spec = CheckCatalog()[i];
        expect(spec.check_id == fmt::format("K{:02}", i + 1), "check_id 必须是 K01…K29 顺序");
        expect(!spec.name.empty() && !spec.severity.empty() && !spec.scope.empty() &&
                   !spec.data_source.empty(),
               "每条都必须有 name/severity/scope/data_source（`06` §4 判据）");
    }

    // ② sha1 向量（`04` §2.5 指定算法）
    expect(Sha1Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1(空串)");
    expect(Sha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1(abc)");
    expect(Sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
               "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
           "sha1(FIPS 长串)");

    // ③ 空库 + 完整工程产物：29 条全 NA/Pass，不阻断
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("K 校验自检：内存库打开失败 {}", r.error().message);
        return fails + 1;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("K 校验自检：建表失败 {}", r.error().message);
        return fails + 1;
    }
    std::error_code ec;
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path(ec) / "shine_kcheck_selfcheck";
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp / "work" / "ch001", ec);
    std::filesystem::create_directories(tmp / "snapshots", ec);
    const std::string diffJson =
        R"({"contract_version":1,"producer":"selfcheck","chapter_id":1,"no_change_declared":true})";
    (void)util::WriteFileBytes(tmp / "work" / "ch001" / "12_state_diff.json", diffJson);
    (void)util::WriteFileBytes(tmp / "snapshots" / "ch001.json", "{}");

    (void)mem.Exec("INSERT INTO chapters(id,ord,title,status) VALUES(1,1,'第 1 章','done')");
    CheckInputs in;
    in.chapter_id = 1;
    in.chapter_ord = 1;
    in.project_dir = tmp;

    {
        ValidationReport r = RunChapterChecks(mem, in);
        expect(r.checks.size() == 29, "报告必须含 29 条（含空真）");
        expect(r.Ok(), "干净工程 + 空库不得阻断（NotApplicable 空真放行）");
        expect(r.Find("K02") != nullptr &&
                   r.Find("K02")->outcome == CheckOutcome::NotApplicable,
               "无引用时 K02 应为 NotApplicable（不是 Pass）");
        expect(r.Find("K12") != nullptr && r.Find("K12")->outcome == CheckOutcome::Pass,
               "有 work 产物时 K12 应 Pass");
        expect(r.Find("K13") != nullptr && r.Find("K13")->outcome == CheckOutcome::Pass,
               "有快照时 K13 应 Pass");
    }
    const auto probe = [&mem, &in](std::string_view id) -> CheckResult {
        ValidationReport report = RunChapterChecks(mem, in);
        const CheckResult* r = Probe(report, id);
        return r == nullptr ? CheckResult{} : *r;
    };

    // ④ 逐条构造触发（每条只断言自己）
    (void)mem.Exec("INSERT INTO entities(kind,name,status) VALUES('person','死者','dead')");
    (void)mem.Exec("INSERT INTO event_participants(event_id,entity_id,role) VALUES(1,1,'actor')");
    expect(probe("K04").outcome == CheckOutcome::Fail, "K04 死者当行动者应 fail");

    (void)mem.Exec("INSERT INTO causal_links(cause_event_id,effect_event_id) VALUES(1,2)");
    (void)mem.Exec("INSERT INTO causal_links(cause_event_id,effect_event_id) VALUES(2,1)");
    expect(probe("K06").outcome == CheckOutcome::Fail, "K06 因果环应 fail");

    (void)mem.Exec("INSERT INTO entity_ownerships(owner_id,item_id,to_chapter) VALUES(10,20,0)");
    (void)mem.Exec("INSERT INTO entity_ownerships(owner_id,item_id,to_chapter) VALUES(11,20,0)");
    (void)mem.Exec("INSERT INTO character_status(entity_id,chapter_id,location_id) VALUES(30,1,100)");
    (void)mem.Exec("INSERT INTO character_status(entity_id,chapter_id,location_id) VALUES(30,1,200)");
    expect(probe("K07").outcome == CheckOutcome::Fail, "K07 同章两地/物品双主应 fail");

    (void)mem.Exec("INSERT INTO chapters(ord,title) VALUES(1,'重复序')");
    expect(probe("K08").outcome == CheckOutcome::Fail, "K08 ord 不严格递增应 fail");

    (void)mem.Exec("INSERT INTO scenes(chapter_id,ord,location_id) VALUES(1,1,999)");
    expect(probe("K02").outcome == CheckOutcome::Fail, "K02 引用不存在实体应 fail");
    (void)mem.Exec("INSERT INTO scenes(chapter_id,ord,location_id) VALUES(1,2,1)");
    expect(probe("K03").outcome == CheckOutcome::Fail, "K03 location_id 指向 person 应 fail");
    (void)mem.Exec("INSERT INTO scenes(chapter_id,ord,pov_entity_id) VALUES(1,3,40)");
    expect(probe("K16").outcome == CheckOutcome::Fail, "K16 POV 不在 scene_cast 应 fail");

    (void)mem.Exec("INSERT INTO entity_fields(entity_id,field_key,value_text) "
                   "VALUES(1,'unregistered_key','v')");
    expect(probe("K14").outcome == CheckOutcome::Fail, "K14 未登记键应 fail");
    (void)mem.Exec("INSERT INTO field_defs(field_key,value_type,is_system,status) "
                   "VALUES('strength','number',0,'CANON')");
    (void)mem.Exec("INSERT INTO entity_fields(entity_id,field_key,value_text) "
                   "VALUES(1,'strength','abc')");
    expect(probe("K15").outcome == CheckOutcome::Fail, "K15 number 值非法应 fail");

    (void)mem.Exec("INSERT INTO entities(kind,name) VALUES('unicorn','独角兽')");
    expect(probe("K26").outcome == CheckOutcome::Fail, "K26 未登记 kind 应 fail");

    (void)mem.Exec("INSERT INTO generated_images(job_id,status,updated) VALUES('orphan1','RUNNING',0)");
    expect(probe("K27").outcome == CheckOutcome::Fail, "K27 孤儿出图任务应 fail");

    (void)mem.Exec("UPDATE chapters SET words=100 WHERE id=1");
    {
        const CheckResult r = probe("K25");
        expect(r.outcome == CheckOutcome::Fail && r.severity == "low",
               "K25 字数越界应 fail 但记 low（放行）");
        // 「low 放行」要独立断言（此时库里已积了别的违规，不能拿整份报告的 Ok() 说话）
        expect(CheckPassed(r), "K25 的 low 必须放行（G2 不因 low 阻断）");
        const CheckResult onlyLow[1] = {r};
        expect(AllChecksPass(onlyLow), "只含 low 失败的 K01–K29 报告必须 Ok()");
    }

    (void)mem.Exec("INSERT INTO foreshadowings(title,status,setup_ch,payoff_ch,importance) "
                   "VALUES('超期伏笔','PLANTED',1,200,10)");
    expect(probe("K10").outcome == CheckOutcome::Fail, "K10 超期阻断应 fail");

    (void)mem.Exec("INSERT INTO character_knowledge(entity_id,fact_kind,fact_id,knows,chapter_known) "
                   "VALUES(1,'secret',1,1,999)");
    expect(probe("K05").outcome == CheckOutcome::Fail, "K05 未来知情应 fail");

    (void)mem.Exec("INSERT INTO visual_assets(entity_id,kind,name,status) "
                   "VALUES(1,'character','未就绪', 'PENDING')");
    (void)mem.Exec("INSERT INTO visual_artifacts(asset_id,layer,status) VALUES(1,'shot','DONE')");
    expect(probe("K29").outcome == CheckOutcome::Fail, "K29 资产未就绪当参考图应 fail");
    expect(probe("K28").outcome == CheckOutcome::Pass, "K28 无降级时应 pass");

    // K09 / K24 / K19–K21 / K22：ContractInput 三组 + K23
    {
        in.shots = {};
        expect(probe("K09").outcome == CheckOutcome::NotApplicable, "K09 无对象时空真");
        expect(probe("K22").outcome == CheckOutcome::NotApplicable, "K22 无镜时空真");
        const ShotStateSnapshot bad0{10, 1, "{}", R"({"lighting":"夜"})", {}};
        const ShotStateSnapshot bad1{11, 2, R"({"lighting":"昼"})", "{}", {}};
        const ShotStateSnapshot bad2{12, 3, R"({"lighting":"昼"})", "{}", {777}};
        in.shots = std::span<const ShotStateSnapshot>(&bad0, 1);
        expect(probe("K09").outcome == CheckOutcome::Pass, "K09 单镜无相邻应 pass");
        const ShotStateSnapshot arr[3] = {bad0, bad1, bad2};
        in.shots = arr;
        expect(probe("K09").outcome == CheckOutcome::Fail, "K09 起止状态不接应 fail");
        expect(probe("K22").outcome == CheckOutcome::Missing, "K22 角色无视觉资产应 missing");
        in.shots = {};

        const BeatSpan beats[2] = {{0.0, 1.0}, {1.5, 2.0}};
        in.beats = beats;
        in.scene_duration_s = 3.0;
        expect(probe("K24").outcome == CheckOutcome::Fail, "K24 Beat 未覆盖且重叠应 fail");
        in.beats = {};
        in.scene_duration_s = 0.0;

        in.prompt_state_hash = "deadbeef";
        expect(probe("K23").outcome == CheckOutcome::Fail, "K23 哈希不一致应 fail");
        in.prompt_state_hash.clear();
        expect(probe("K23").outcome == CheckOutcome::NotApplicable, "K23 无产物哈希时空真");

        in.gen = GenerationCheckInput{};
        expect(probe("K19").outcome == CheckOutcome::NotApplicable, "K19 无图时空真");
        in.gen.has_graph = true;
        in.gen.graph_ok = false;
        in.gen.graph_detail = "object_info 未就绪";
        expect(probe("K19").outcome == CheckOutcome::Fail, "K19 图校验不通过应 fail");
        in.gen.has_sanitize = true;
        in.gen.size_corrections = 1;
        in.gen.ref_truncations = 1;
        {
            const CheckResult k20 = probe("K20");
            expect(k20.outcome == CheckOutcome::Fail && k20.severity == "low",
                   "K20 自动纠正应 fail 但记 low");
            expect(probe("K21").outcome == CheckOutcome::Fail, "K21 参考图截断应 fail high");
            expect(probe("K28").outcome == CheckOutcome::Fail, "K28 有降级但账不可见应 fail");
        }
        in.gen.degradations_recorded = true;
        expect(probe("K28").outcome == CheckOutcome::Pass, "K28 记账后应 pass");
        in.gen = GenerationCheckInput{};
    }

    // K01 / K11 / K17：靠 StateDiff
    {
        StateDiff d;
        d.chapter_id = 1;
        d.no_change_declared = true;
        d.entities.push_back(NewEntityDelta{"en:1", "person", "不该有", "", "active", 1});
        in.diff = &d;
        expect(probe("K01").outcome == CheckOutcome::Fail, "K01 no_change 却有子数组应 fail");
        d.no_change_declared = false;
        d.entities.clear();

        RowId foreshadow = 0;
        if (auto st = mem.Prepare("SELECT id FROM foreshadowings LIMIT 1"); st) {
            auto s = st->Step();
            if (s && *s == db::sqlite::StepResult::Row) {
                foreshadow = st->ColumnInt(0);
            }
        }
        expect(foreshadow > 0, "自检先决：应有一条 foreshadowings");
        (void)mem.Exec("UPDATE foreshadowings SET status='RESOLVED' WHERE id=1");
        ForeshadowDelta fd;
        fd.op = "progress";
        fd.foreshadow_id = foreshadow;
        fd.status.clear();
        d.foreshadows.push_back(fd);
        expect(probe("K11").outcome == CheckOutcome::Fail, "K11 已回收再推进应 fail");
        d.foreshadows.clear();

        ItemDelta id;
        id.op = "acquire";
        id.item_id = 20;
        id.owner_id = 11;
        d.items.push_back(id);
        expect(probe("K17").outcome == CheckOutcome::Fail, "K17 acquire 前已持有应 fail");
        d.items.clear();
        in.diff = nullptr;
        expect(probe("K17").outcome == CheckOutcome::NotApplicable, "K17 无 diff 时空真");
    }

    // K18：相邻场跨两地需多天但 time_label 相同
    {
        (void)mem.Exec("DELETE FROM scenes");
        (void)mem.Exec("INSERT INTO scenes(chapter_id,ord,location_id,time_label) "
                       "VALUES(1,1,100,'第三天'),(1,2,200,'第三天')");
        (void)mem.Exec("INSERT INTO location_distances(from_id,to_id,days_estimate) VALUES(100,200,5)");
        expect(probe("K18").outcome == CheckOutcome::Fail, "K18 时间不可信应 fail");
    }

    // K12 / K13：产物缺失（不改 chapter_ord，仍看 ch001）
    {
        std::filesystem::remove(tmp / "work" / "ch001" / "12_state_diff.json", ec);
        std::filesystem::remove(tmp / "snapshots" / "ch001.json", ec);
        in.project_dir = tmp;
        expect(probe("K12").outcome == CheckOutcome::Fail, "K12 缺 StateDiff 产物应 fail");
        expect(probe("K13").outcome == CheckOutcome::Fail, "K13 缺快照应 fail");
    }

    // K09 / K22 / K23 / K24 的**库来源**（v9/S13）：不给显式对象也要能判（`shots` + `prompt_artifacts`）
    {
        (void)mem.Exec("INSERT INTO scenes(id,chapter_id,ord,title) VALUES(901,1,1,'库来源场')");
        (void)mem.Exec("INSERT INTO shots(id,scene_id,ord,start_state_json,end_state_json,"
                       "character_ids_json,timeline_json) "
                       "VALUES(901,901,1,'{\"lighting\":\"夜\"}','{\"lighting\":\"夜\"}','[]','{}')");
        (void)mem.Exec("INSERT INTO shots(id,scene_id,ord,start_state_json,end_state_json,"
                       "character_ids_json,timeline_json) "
                       "VALUES(902,901,2,'{\"lighting\":\"昼\"}','{\"lighting\":\"昼\"}','[]','{}')");
        in.shots = {}; // 不给显式对象 → 强制走库
        in.beats = {};
        in.prompt_state_hash.clear();

        expect(probe("K09").outcome == CheckOutcome::Fail,
               "K09 库来源：相邻镜起止状态不接应 fail");
        (void)mem.Exec("UPDATE shots SET start_state_json='{\"lighting\":\"夜\"}' WHERE id=902");
        expect(probe("K09").outcome == CheckOutcome::Pass, "K09 库来源：接上即 pass");

        (void)mem.Exec("UPDATE shots SET timeline_json="
                       "'{\"duration_s\":3.0,\"beats\":[{\"begin_s\":0,\"end_s\":1.0}]}' "
                       "WHERE id=901");
        expect(probe("K24").outcome == CheckOutcome::Fail, "K24 库来源：未覆盖 duration 应 fail");
        (void)mem.Exec("UPDATE shots SET timeline_json="
                       "'{\"duration_s\":3.0,\"beats\":[{\"begin_s\":0,\"end_s\":3.0}]}' "
                       "WHERE id=901");
        expect(probe("K24").outcome == CheckOutcome::Pass, "K24 库来源：覆盖合规即 pass");

        (void)mem.Exec("UPDATE shots SET character_ids_json='[999999]' WHERE id=901");
        expect(probe("K22").outcome == CheckOutcome::Missing,
               "K22 库来源：出场角色无视觉资产应 missing");
        (void)mem.Exec("UPDATE shots SET character_ids_json='[]' WHERE id=901");

        // K23 的库来源：先按**当前状态**算出哈希存进去 → 应 Pass；再改成错的 → 应 Fail
        const std::string h = ComputeInputStateHash(mem, 1, "visual", "V10");
        (void)mem.Exec(fmt::format("INSERT INTO prompt_artifacts(chapter_id,shot_id,chain,stage,"
                                   "input_state_hash,prompt,created,updated) "
                                   "VALUES(1,901,'visual','V10','{}','p',1,1)",
                                   h));
        expect(probe("K23").outcome == CheckOutcome::Pass, "K23 库来源：哈希一致应 pass");
        (void)mem.Exec("UPDATE prompt_artifacts SET input_state_hash='deadbeef' WHERE chapter_id=1");
        expect(probe("K23").outcome == CheckOutcome::Fail, "K23 库来源：哈希不一致应 fail");
    }

    // K06/K07/K08/K10/K25/K26/K27/K04/K14/K15/K16/K29 的「干净时 Pass」不逐个反证（上面已覆盖 fail 分支）
    std::filesystem::remove_all(tmp, ec);

    if (fails == 0) {
        log::Info("K 校验自检通过（K01–K29 目录 29/29 + sha1 向量 + 空真不阻断 + 逐条触发）");
    }
    return fails;
}

} // namespace shine::novelcore

