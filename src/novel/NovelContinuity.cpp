#include "novel/NovelContinuity.h"

#include "core/Log.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace shine::novelcore {
namespace {

[[nodiscard]] std::string JEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

// ———— `state` JSON 的取值小工具（全部 null 安全）————

[[nodiscard]] std::string StrField(const yyjson_val* obj, const char* key) {
    if (obj == nullptr || !yyjson_is_obj(obj)) {
        return {};
    }
    const yyjson_val* v = yyjson_obj_get(obj, key);
    return yyjson_is_str(v) ? std::string{yyjson_get_str(v)} : std::string{};
}

[[nodiscard]] const yyjson_val* ObjField(const yyjson_val* obj, const char* key) {
    if (obj == nullptr || !yyjson_is_obj(obj)) {
        return nullptr;
    }
    const yyjson_val* v = yyjson_obj_get(obj, key);
    return yyjson_is_obj(v) ? v : nullptr;
}

[[nodiscard]] const yyjson_val* ArrField(const yyjson_val* obj, const char* key) {
    if (obj == nullptr || !yyjson_is_obj(obj)) {
        return nullptr;
    }
    const yyjson_val* v = yyjson_obj_get(obj, key);
    return yyjson_is_arr(v) ? v : nullptr;
}

struct CharState {
    std::string position;
    std::string facing;
    std::string pose;
    std::string hands;
    std::string clothing;
    std::string injury;
};

[[nodiscard]] std::map<std::int64_t, CharState> CharsOf(const yyjson_val* state) {
    std::map<std::int64_t, CharState> out;
    const yyjson_val* chars = ArrField(state, "characters");
    if (chars == nullptr) {
        return out;
    }
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* c = nullptr;
    yyjson_arr_foreach(chars, i, max, c) {
        const yyjson_val* idv = yyjson_is_obj(c) ? yyjson_obj_get(c, "entity_id") : nullptr;
        if (!yyjson_is_int(idv)) {
            continue;
        }
        const std::int64_t id = yyjson_get_sint(idv);
        CharState cs;
        cs.position = StrField(c, "position");
        cs.facing = StrField(c, "facing");
        cs.pose = StrField(c, "pose");
        cs.hands = StrField(c, "hands");
        cs.clothing = StrField(c, "clothing");
        cs.injury = StrField(c, "injury");
        out[id] = cs;
    }
    return out;
}

struct PropState {
    std::int64_t holder = 0;
    std::string state;
};

[[nodiscard]] std::map<std::int64_t, PropState> PropsOf(const yyjson_val* state) {
    std::map<std::int64_t, PropState> out;
    const yyjson_val* props = ArrField(state, "props");
    if (props == nullptr) {
        return out;
    }
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* p = nullptr;
    yyjson_arr_foreach(props, i, max, p) {
        const yyjson_val* idv = yyjson_is_obj(p) ? yyjson_obj_get(p, "entity_id") : nullptr;
        if (!yyjson_is_int(idv)) {
            continue;
        }
        PropState ps;
        const yyjson_val* h = yyjson_obj_get(p, "holder_id");
        ps.holder = yyjson_is_int(h) ? yyjson_get_sint(h) : 0;
        ps.state = StrField(p, "state");
        out[yyjson_get_sint(idv)] = ps;
    }
    return out;
}

// `work/ch<NNN>/storyboard.json` → `(scene_ord, ord)` → 该镜对象（doc 由调用方持有并释放）
[[nodiscard]] std::map<std::pair<int, int>, const yyjson_val*>
IndexStoryboard(const std::string& json, yyjson_doc** outDoc) {
    std::map<std::pair<int, int>, const yyjson_val*> out;
    *outDoc = nullptr;
    yyjson_doc* d = yyjson_read(json.data(), json.size(), 0);
    if (d == nullptr) {
        return out;
    }
    *outDoc = d;
    yyjson_val* root = yyjson_doc_get_root(d);
    yyjson_val* shots = yyjson_is_obj(root) ? yyjson_obj_get(root, "shots") : nullptr;
    if (!yyjson_is_arr(shots)) {
        return out;
    }
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* s = nullptr;
    yyjson_arr_foreach(shots, i, max, s) {
        if (!yyjson_is_obj(s)) {
            continue;
        }
        const yyjson_val* so = yyjson_obj_get(s, "scene_ord");
        const yyjson_val* od = yyjson_obj_get(s, "ord");
        const int sceneOrd = yyjson_is_int(so) ? static_cast<int>(yyjson_get_sint(so)) : 0;
        const int ord = yyjson_is_int(od) ? static_cast<int>(yyjson_get_sint(od)) : 0;
        if (sceneOrd > 0 && ord > 0) {
            out[{sceneOrd, ord}] = s;
        }
    }
    return out;
}

// `performance` 的这些字段任意一个非空 = "表演层解释了这次变化"（`12` §2.7 的 C3–C5/C8）
[[nodiscard]] bool AnyNonEmpty(const yyjson_val* perf, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        if (!StrField(perf, k).empty()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string ContinuityOutcome::Describe() const {
    return fmt::format("{} 镜 / {} 对相邻镜：违规 {} 条、无法核对 {} 条", shots_seen, pairs_checked,
                       failed, unverified);
}

ContinuityOutcome RunContinuityChecks(::shine::db::sqlite::Database& db, RowId chapter_id,
                                      std::string_view project_dir) {
    ContinuityOutcome out;
    if (!db.isOpen()) {
        out.error = "数据库未打开";
        return out;
    }
    if (chapter_id <= 0) {
        out.error = "需要 chapter_id";
        return out;
    }
    NovelVisual visual(db);
    auto shots = visual.ListShotsByChapter(chapter_id);
    if (!shots || shots->empty()) {
        out.error = "该章没有 shots —— 先跑 V9 `STORYBOARD`（`--novel-storyboard`）";
        return out;
    }
    out.shots_seen = static_cast<int>(shots->size());
    // 章序号（`work/ch<NNN>` 用序号）+ `scene_id → scene_ord`（故事板用 scene_ord 标识场）
    int chapterOrd = 0;
    if (auto st = db.Prepare("SELECT ord FROM chapters WHERE id=?1"); st) {
        (void)st->BindInt(1, chapter_id);
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            chapterOrd = static_cast<int>(st->ColumnInt(0));
        }
    }
    std::map<RowId, int> sceneOrdById;
    if (auto st = db.Prepare("SELECT id,ord FROM scenes WHERE chapter_id=?1"); st) {
        (void)st->BindInt(1, chapter_id);
        while (true) {
            auto s = st->Step();
            if (!s || *s == db::sqlite::StepResult::Done) {
                break;
            }
            sceneOrdById[st->ColumnInt(0)] = static_cast<int>(st->ColumnInt(1));
        }
    }
    // 故事板（`performance`/`spatial`/`camera`/`transition` 只存盘：`11` §2.2）
    std::map<std::pair<int, int>, const yyjson_val*> sb;
    yyjson_doc* sbDoc = nullptr;
    bool hasStoryboard = false;
    if (!project_dir.empty() && chapterOrd > 0) {
        const auto file = std::filesystem::path{std::string{project_dir}} / "work" /
                          fmt::format("ch{:03}", chapterOrd) / "storyboard.json";
        if (const auto text = util::ReadFileBytes(file); text) {
            sb = IndexStoryboard(*text, &sbDoc);
            hasStoryboard = sbDoc != nullptr;
        }
    }
    if (!hasStoryboard) {
        out.notes.push_back(
            "没有 work/ch<NNN>/storyboard.json（V9 只存盘的 performance/spatial/camera/transition 在此）"
            "→ 依赖它的 C1–C5/C8/C12 记 unverified（**不假装通过**）");
    }

    // 逐对相邻镜：本镜 `end_state` → 下一镜 `start_state`（`12` §2.7 的连续性口径）
    struct Parsed {
        yyjson_doc* doc = nullptr;
        const yyjson_val* start = nullptr;
        const yyjson_val* end = nullptr;
    };
    std::vector<Parsed> parsed(shots->size());
    const auto loadState = [](const std::string& json, yyjson_doc** docP, const yyjson_val** objP) {
        yyjson_doc* d = yyjson_read(json.data(), json.size(), 0);
        if (d == nullptr) {
            return;
        }
        *docP = d;
        yyjson_val* root = yyjson_doc_get_root(d);
        if (yyjson_is_obj(root)) {
            *objP = root;
        }
    };
    for (std::size_t i = 0; i < shots->size(); ++i) {
        loadState((*shots)[i].start_state_json, &parsed[i].doc, &parsed[i].start);
        // end_state 单独解析会多一个 doc —— 复用同一 doc 不现实（两串），故只解析 end 一次
    }
    // end_state：单独一批 doc（与 start 分开持有，便于同时判 C1–C12）
    std::vector<yyjson_doc*> endDocs(shots->size(), nullptr);
    std::vector<const yyjson_val*> ends(shots->size(), nullptr);
    for (std::size_t i = 0; i < shots->size(); ++i) {
        loadState((*shots)[i].end_state_json, &endDocs[i], &ends[i]);
    }

    const auto addIssue = [&out](const char* code, const char* severity, std::string detail) {
        out.issues.push_back(ContinuityIssue{code, severity, std::move(detail)});
        ++out.failed;
    };
    const auto addUnverified = [&out](const char* code, std::string reason) {
        out.notes.push_back(fmt::format("{}：{}", code, std::move(reason)));
        ++out.unverified;
    };

    for (std::size_t i = 0; i + 1 < shots->size(); ++i) {
        const ShotRow& cur = (*shots)[i];
        const ShotRow& next = (*shots)[i + 1];
        const yyjson_val* endState = ends[i];
        const yyjson_val* nextStart = parsed[i + 1].start;
        if (endState == nullptr || nextStart == nullptr) {
            continue; // 缺状态：由 K09 负责报（本模块不重复报同一件事）
        }
        ++out.pairs_checked;
        // 故事板：本镜的 transition + 下一镜的 performance/spatial/camera
        const auto curOrd = sceneOrdById.find(cur.scene_id);
        const auto nextOrd = sceneOrdById.find(next.scene_id);
        const yyjson_val* curSb =
            (curOrd != sceneOrdById.end() && hasStoryboard)
                ? sb[std::pair{curOrd->second, cur.ord}]
                : nullptr;
        const yyjson_val* nextSb =
            (nextOrd != sceneOrdById.end() && hasStoryboard)
                ? sb[std::pair{nextOrd->second, next.ord}]
                : nullptr;
        const yyjson_val* nextPerf = ObjField(nextSb, "performance");
        const yyjson_val* nextSpatial = ObjField(nextSb, "spatial");
        const yyjson_val* nextCamera = ObjField(nextSb, "camera");
        const std::string transition = StrField(curSb, "transition");
        const std::string movementPath = StrField(nextSpatial, "movement_path");

        const auto curEnd = CharsOf(endState);
        const auto nextChars = CharsOf(nextStart);

        // —— C1：出场角色集合（只允许增加 / 移除，且移除要有交代）——
        {
            std::set<std::int64_t> missing;
            for (const auto& [id, _] : curEnd) {
                if (nextChars.find(id) == nextChars.end()) {
                    missing.insert(id);
                }
            }
            if (!missing.empty() && transition.empty() && movementPath.empty()) {
                addIssue("C1", "high",
                         fmt::format("镜 #{}→#{}：{} 个角色离镜，但既无 `transition` 也无 "
                                     "`movement_path` 交代（`12` §2.7 C1）",
                                     cur.id, next.id, missing.size()));
            } else if (!missing.empty() && !hasStoryboard) {
                addUnverified("C1", "有角色离镜：需 `transition`/`movement_path` 核对（无故事板）");
            }
        }
        // —— C2–C5：位置 / 朝向 / 姿势 / 手部（变化需表演层或空间层解释）——
        for (const auto& [id, endCs] : curEnd) {
            const auto it = nextChars.find(id);
            if (it == nextChars.end()) {
                continue; // 离镜的角色由 C1 负责
            }
            const CharState& nc = it->second;
            const auto check = [&](const char* rule, const char* sev, const std::string& before,
                                   const std::string& after, std::initializer_list<const char*> keys,
                                   bool allowMovementPath, const char* what) {
                if (before == after || before.empty() || after.empty()) {
                    return;
                }
                const bool explained = AnyNonEmpty(nextPerf, keys) ||
                                       (allowMovementPath && !movementPath.empty());
                if (explained) {
                    return;
                }
                if (!hasStoryboard) {
                    addUnverified(rule, fmt::format("角色 #{} 的{}变了：需表演层核对（无故事板）", id,
                                                    what));
                    return;
                }
                addIssue(rule, sev,
                         fmt::format("镜 #{}→#{}：角色 #{} 的{}变了但表演层未解释（`12` §2.7 {}）",
                                     cur.id, next.id, id, what, rule));
            };
            check("C2", "high", endCs.position, nc.position, {}, true, "位置");
            check("C3", "medium", endCs.facing, nc.facing, {"head_movement", "body_movement"}, true,
                  "朝向");
            check("C4", "medium", endCs.pose, nc.pose, {"posture", "body_movement"}, false, "姿势");
            check("C5", "medium", endCs.hands, nc.hands, {"hand_movement"}, false, "手部");
            // —— C6/C7：服装 / 伤势（"只允许在明确事件后变化"—— 需要 `ChapterPlan.events`）——
            if (!endCs.clothing.empty() && !nc.clothing.empty() && endCs.clothing != nc.clothing) {
                addUnverified("C6",
                              fmt::format("角色 #{} 的服装变了：需核对 `ChapterPlan.events`（本模块读不到）",
                                          id));
            }
            if (!endCs.injury.empty() && !nc.injury.empty() && endCs.injury != nc.injury) {
                addUnverified("C7",
                              fmt::format("角色 #{} 的伤势变了：需语义判「单向加重 / 治疗后减轻」"
                                          "（本模块不做语义判断）",
                                          id));
            }
        }
        // —— C8/C9：道具的持有者 / 状态 ——
        {
            const auto curProps = PropsOf(endState);
            const auto nextProps = PropsOf(nextStart);
            for (const auto& [id, ps] : curProps) {
                const auto it = nextProps.find(id);
                if (it == nextProps.end()) {
                    continue;
                }
                if (ps.holder != it->second.holder && ps.holder != 0 && it->second.holder != 0) {
                    if (!hasStoryboard) {
                        addUnverified("C8", fmt::format("道具 #{} 的持有者变了：需表演层核对（无故事板）",
                                                        id));
                    } else if (!AnyNonEmpty(nextPerf, {"hand_movement"}) && movementPath.empty()) {
                        addIssue("C8", "medium",
                                 fmt::format("镜 #{}→#{}：道具 #{} 换了持有者，但表演层/空间层未解释"
                                             "（`12` §2.7 C8）",
                                             cur.id, next.id, id));
                    }
                }
                if (!ps.state.empty() && !it->second.state.empty() && ps.state != it->second.state) {
                    addUnverified("C9",
                                  fmt::format("道具 #{} 的状态变了：需核对事件（本模块读不到）", id));
                }
            }
        }
        // —— C10：光（同场变化不允许；跨场需 `time_label` 变化）——
        {
            const std::string lb = StrField(endState, "lighting");
            const std::string la = StrField(nextStart, "lighting");
            if (!lb.empty() && !la.empty() && lb != la && cur.scene_id == next.scene_id) {
                addIssue("C10", "medium",
                         fmt::format("镜 #{}→#{}：**同一场**内 lighting 由「{}」变「{}」"
                                     "（`12` §2.7 C10 只允许在 time_label 变化或光源事件时变）",
                                     cur.id, next.id, lb, la));
            }
        }
        // —— C11：环境（只允许在明确事件后变化 → 无事件数据 ⇒ unverified）——
        {
            const std::string eb = StrField(endState, "environment");
            const std::string ea = StrField(nextStart, "environment");
            if (!eb.empty() && !ea.empty() && eb != ea) {
                addUnverified("C11",
                              fmt::format("镜 #{}→#{}：environment 由「{}」变「{}」——需核对事件"
                                          "（本模块读不到）",
                                          cur.id, next.id, eb, ea));
            }
        }
        // —— C12：相机位置（"镜头位置也是连续量"，与下一镜 `camera.position` 一致）——
        {
            const std::string cam = StrField(nextStart, "camera_position");
            const std::string cam2 = StrField(nextCamera, "position");
            if (!cam.empty() && !cam2.empty() && cam != cam2) {
                addIssue("C12", "low",
                         fmt::format("镜 #{}→#{}：下一镜 start_state.camera_position「{}」与故事板 "
                                     "camera.position「{}」不一致（`12` §2.7 C12）",
                                     cur.id, next.id, cam, cam2));
            } else if (nextCamera == nullptr && hasStoryboard) {
                addUnverified("C12", "下一镜的故事板缺 `camera.position`");
            }
        }
    }

    // 释放
    for (Parsed& p : parsed) {
        if (p.doc != nullptr) {
            yyjson_doc_free(p.doc);
        }
    }
    for (yyjson_doc* d : endDocs) {
        if (d != nullptr) {
            yyjson_doc_free(d);
        }
    }
    if (sbDoc != nullptr) {
        yyjson_doc_free(sbDoc);
    }

    // —— 报告落盘（`12` §2.7 的 `ContinuityReport`；`12` §3 的 12-6 就是"没有它"）——
    if (!project_dir.empty() && chapterOrd > 0) {
        int checked = 0;
        // "有结论的"= 每条规则至少被评估过一次（这里按现状近似：pairs>0 且没有 unverified 的规则）
        for (const char* rule : {"C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10", "C11",
                                 "C12"}) {
            const bool skipped = std::any_of(out.notes.begin(), out.notes.end(),
                                             [rule](const std::string& n) {
                                                 return n.rfind(std::string{rule} + "：", 0) == 0;
                                             });
            if (!skipped) {
                ++checked;
            }
        }
        std::string issues;
        for (const ContinuityIssue& is : out.issues) {
            issues += fmt::format("{}{{\"code\":\"{}\",\"severity\":\"{}\",\"detail\":\"{}\"}}",
                                  issues.empty() ? "" : ",", is.code, is.severity,
                                  JEscape(is.detail));
        }
        std::string notes;
        for (const std::string& n : out.notes) {
            notes += fmt::format("{}\"{}\"", notes.empty() ? "" : ",", JEscape(n));
        }
        const std::string json = fmt::format(
            "{{\"stage\":\"V8\",\"stage_name\":\"CONTINUITY\",\"chapter_id\":{},\"shots\":{},"
            "\"pairs\":{},\"rules_checked\":{},\"failed\":{},\"unverified\":{},"
            "\"issues\":[{}],\"notes\":[{}]}}",
            chapter_id, out.shots_seen, out.pairs_checked, checked, out.failed, out.unverified, issues,
            notes);
        const auto dir = std::filesystem::path{std::string{project_dir}} / "work" /
                         fmt::format("ch{:03}", chapterOrd);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (util::WriteFileBytes(dir / "v08_continuity.json", json)) {
            out.report_path = util::PathToUtf8(dir / "v08_continuity.json");
        } else {
            out.notes.push_back("v08_continuity.json 落盘失败（work/ 非权威，不阻断）");
        }
    }
    out.ok = true;
    log::Info("V8 连续性：章={} {} 条违规 / {} 条无法核对（{}）", chapter_id, out.failed,
              out.unverified, out.Describe());
    return out;
}

bool RunContinuitySelfCheck() {
    int fails = 0;
    const auto expect = [&fails](bool cond, const std::string& msg) {
        if (!cond) {
            log::Error("V8 自检 FAIL：{}", msg);
            ++fails;
        }
    };
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("V8 自检：内存库打开失败 {}", r.error().message);
        return false;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("V8 自检：建 schema 失败 {}", r.error().message);
        return false;
    }
    NovelGraph g(mem);
    NovelVisual v(mem);
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章"});
    auto sc = g.UpsertScene({.chapter_id = ch.value_or(0), .ord = 1, .title = "库房"});
    auto pov = g.UpsertEntity({.kind = std::string{kind::person}, .name = "自检角色"});
    // 两镜同场：镜 1 的 end_state → 镜 2 的 start_state 故意**不一致**（姿势变了但表演层没解释）
    auto s1 = v.UpsertShot({.scene_id = sc.value_or(0),
                            .ord = 1,
                            .duration_note = "3.0s",
                            .character_ids_json = fmt::format("[{}]", pov.value_or(0)),
                            .start_state_json =
                                R"({"characters":[{"entity_id":1,"pose":"站","lighting":"夜"}],)"
                                R"("lighting":"夜"})",
                            .end_state_json =
                                R"({"characters":[{"entity_id":1,"pose":"坐"}],"lighting":"夜"})"});
    auto s2 = v.UpsertShot({.scene_id = sc.value_or(0),
                            .ord = 2,
                            .duration_note = "3.0s",
                            .character_ids_json = fmt::format("[{}]", pov.value_or(0)),
                            .start_state_json =
                                R"({"characters":[{"entity_id":1,"pose":"跑"}],)"
                                R"("lighting":"夜"})",
                            .end_state_json =
                                R"({"characters":[{"entity_id":1,"pose":"跑"}],"lighting":"夜"})"});
    expect(s1.has_value() && s2.has_value(), "建两镜");

    // ① 无工程目录：C4（姿势）变化 → 表演层读不到 ⇒ **unverified**（不假装通过）
    const ContinuityOutcome noDisk = RunContinuityChecks(mem, ch.value_or(0));
    expect(noDisk.ok && noDisk.pairs_checked == 1, fmt::format("应判 1 对相邻镜（{}）", noDisk.Describe()));
    expect(noDisk.failed == 0 && noDisk.unverified > 0,
           "无故事板时：不应误报违规，而应记 unverified");

    // ② 有故事板、但表演层为空 → C4 **fail**
    {
        std::error_code ec;
        const auto tmp = std::filesystem::temp_directory_path() / "shine_v8_selfcheck";
        std::filesystem::remove_all(tmp, ec);
        const auto dir = tmp / "work" / "ch001";
        std::filesystem::create_directories(dir, ec);
        const std::string sb =
            R"({"shots":[{"scene_ord":1,"ord":1,"duration":3.0,"transition":"cut","performance":{},)"
            R"("spatial":{"movement_path":"走向货架"},"camera":{"position":"中景"}},)"
            R"({"scene_ord":1,"ord":2,"duration":3.0,"transition":"cut",)"
            R"("performance":{},"spatial":{"movement_path":"冲向门口"},"camera":{"position":"中景"}}]})";
        expect(util::WriteFileBytes(dir / "storyboard.json", sb), "写临时故事板");
        // ⚠️ 同时把镜 2 的 start_state 的 camera_position 设成与 camera.position 不同 → C12
        (void)mem.Exec("UPDATE shots SET start_state_json="
                       "'{\"characters\":[{\"entity_id\":1,\"pose\":\"跑\"}],"
                       "\"lighting\":\"夜\",\"camera_position\":\"特写\"}' WHERE ord=2");
        const ContinuityOutcome onDisk = RunContinuityChecks(mem, ch.value_or(0), util::PathToUtf8(tmp));
        expect(onDisk.ok && onDisk.pairs_checked == 1, "有故事板时仍判 1 对");
        bool c4 = false;
        bool c12 = false;
        for (const ContinuityIssue& is : onDisk.issues) {
            if (is.code == "C4") {
                c4 = true;
            }
            if (is.code == "C12") {
                c12 = true;
            }
        }
        expect(c4, "C4：姿势变了但 performance.posture/body_movement 为空 → 违规");
        expect(c12, "C12：start_state.camera_position 与故事板 camera.position 不一致 → 违规");
        expect(!onDisk.report_path.empty(), "应落 v08_continuity.json");
        std::filesystem::remove_all(tmp, ec);
    }

    // ③ 没有 shots 的章 → 明确报错（V8 的输入前提）
    auto ch2 = g.UpsertChapter({.ord = 2, .title = "第二章"});
    const ContinuityOutcome bad = RunContinuityChecks(mem, ch2.value_or(0));
    expect(!bad.ok && bad.error.find("V9") != std::string::npos, "没有 shots 的章应提示先跑 V9");

    if (fails == 0) {
        log::Info("V8 连续性自检通过（相邻镜 C1–C12 三态判定 + 报告落盘）");
    }
    return fails == 0;
}

} // namespace shine::novelcore
