#include "novel/NovelInit.h"

#include "agent/AgentKit.h" // 15 个内置 Agent（`10` §2.3 N13）
#include "core/Log.h"
#include "novel/NovelDb.h" // ApplyCanonicalSchema（自检）
#include "novel/NovelFields.h"
#include "novel/NovelGraph.h"
#include "novel/NovelStageLedger.h" // WrapStageArtifact（产物包装格式复用）
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Time.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <cstdlib>

namespace shine::novelcore {
namespace {

// `10` §2.2 的 I1–I16（`artifact` 是本实现定义的落盘名，见 `10` §2.8 的落盘约定）
constexpr InitStageSpec kInitStages[] = {
    {"INIT_CONCEPT", "I1_concept.json", true},
    {"INIT_WORLD", "I2_world.json", true},
    {"INIT_POWER", "I3_power.json", true},
    {"INIT_CHARACTERS", "I4_characters.json", true},
    {"INIT_RELATIONS", "I5_relations.json", true},
    {"INIT_LOCATIONS", "I6_locations.json", true},
    {"INIT_FACTIONS", "I7_factions.json", true},
    {"INIT_ITEMS", "I8_items.json", true},
    {"INIT_SECRETS", "I9_secrets.json", true},
    {"INIT_MYSTERIES", "I10_mysteries.json", true},
    {"INIT_PLOTS", "I11_plots.json", true},
    {"INIT_STYLE", "I12_style.json", true},
    {"INIT_FORESHADOW_SEED", "I13_foreshadow_seed.json", true},
    {"INIT_VOLUMES", "I14_volumes.json", true},
    {"INIT_GATE", "I15_gate.json", false},
    {"INIT_COMMIT", "I16_commit.json", false},
};

// 查询成功返回 true；失败（列名/表缺失等）返回 false —— **不把"查不出来"当成"通过"**
[[nodiscard]] bool QueryI64(db::sqlite::Database& db, std::string_view sql, std::int64_t* out) {
    auto st = db.Prepare(sql);
    if (!st) {
        return false;
    }
    auto s = st->Step();
    if (!s || *s != db::sqlite::StepResult::Row) {
        return false;
    }
    *out = st->ColumnInt(0);
    return true;
}

[[nodiscard]] std::int64_t Count(db::sqlite::Database& db, std::string_view sql) {
    std::int64_t n = 0;
    return QueryI64(db, sql, &n) ? n : 0;
}

// IO：`init/` 下的产物（复用 `NovelStageLedger` 的包装格式，`input_state_hash` 传空 ——
// 初始化链的输入是"作者的概念"，不是库状态，P1 的哈希判定在这里不适用）
[[nodiscard]] bool WriteInitArtifact(const std::filesystem::path& project_dir,
                                     std::string_view file, std::string_view payload) {
    if (project_dir.empty() || file.empty()) {
        return false;
    }
    const std::filesystem::path dir = InitDir(project_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string text = WrapStageArtifact("INIT", payload, std::string_view{});
    return util::WriteFileBytes(dir / std::string{file}, text);
}

} // namespace

std::span<const InitStageSpec> InitStageCatalog() noexcept { return kInitStages; }

std::filesystem::path InitDir(const std::filesystem::path& project_dir) {
    return project_dir / "init";
}

std::string InitReport::Describe() const {
    if (passed) {
        return fmt::format("初始化门禁通过（N1–N14 全满足）");
    }
    return fmt::format("初始化门禁未通过：{} 条不满足（{}）", failures.size(),
                       [this] {
                           std::string ids;
                           for (const InitFailure& f : failures) {
                               ids += (ids.empty() ? "" : ",") + f.n_id;
                           }
                           return ids;
                       }());
}

std::string InitReport::Json() const {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        return "{}";
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "passed", passed);
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    for (const InitFailure& f : failures) {
        yyjson_mut_val* item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, item, "n_id", f.n_id.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "detail", f.detail.c_str());
        yyjson_mut_obj_add_strcpy(doc, item, "fix_hint", f.fix_hint.c_str());
        (void)yyjson_mut_arr_add_val(arr, item);
    }
    (void)yyjson_mut_obj_add_val(doc, root, "failures", arr);
    const char* text = yyjson_mut_write(doc, 0, nullptr);
    std::string out = text == nullptr ? std::string{"{}"} : std::string{text};
    if (text != nullptr) {
        free(const_cast<char*>(text));
    }
    yyjson_mut_doc_free(doc);
    return out;
}

InitReport CheckInitGate(db::sqlite::Database& db) {
    InitReport rep;
    const auto fail = [&rep](std::string id, std::string detail, std::string hint) {
        rep.failures.push_back({std::move(id), std::move(detail), std::move(hint)});
    };
    const auto countOr = [&db](std::string_view sql) { return Count(db, sql); };
    std::int64_t n = 0;

    // N1 `world_meta` 含 `book_title`
    if (countOr("SELECT COUNT(*) FROM world_meta WHERE key='book_title' AND value<>''") < 1) {
        fail("N1", "world_meta 没有 book_title", "设置书名（`INIT_CONCEPT`）");
    }
    // N2 `writing_style` 有且仅有一行（id=1），且 pov_mode / pacing 非空
    {
        std::int64_t rows = 0;
        std::int64_t ok = 0;
        const bool q1 = QueryI64(db, "SELECT COUNT(*) FROM writing_style", &rows);
        const bool q2 = QueryI64(
            db, "SELECT COUNT(*) FROM writing_style WHERE id=1 AND pov_mode<>'' AND pacing<>''", &ok);
        if (!q1 || !q2) {
            fail("N2", "writing_style 查询失败", "检查 schema（应到 v9）");
        } else if (rows != 1 || ok != 1) {
            fail("N2", fmt::format("writing_style 行数={}（应为 1）且 id=1 的 pov_mode/pacing 非空={}",
                                   rows, ok != 0),
                 "写一行文风（`INIT_STYLE`）");
        }
    }
    // N3 `author_rules` 至少 1 条 severity='error'
    if (countOr("SELECT COUNT(*) FROM author_rules WHERE severity='error'") < 1) {
        fail("N3", "author_rules 没有 severity='error' 的硬规则", "至少加 1 条文风硬规则（`INIT_STYLE`）");
    }
    // N4 `entities(kind='person')` >= 1
    if (countOr("SELECT COUNT(*) FROM entities WHERE kind='person'") < 1) {
        fail("N4", "没有任何 person 实体（主角）", "建主角（`INIT_CHARACTERS`）");
    }
    // N5 主角有 entity_personas 且 goal/desire/fear 非空
    // 口径（如实标注）：`world_meta.protagonist_id` 指认，否则取 id 最小的 person
    {
        std::int64_t missing = 0;
        const bool q = QueryI64(
            db,
            "SELECT COUNT(*) FROM entities e WHERE e.kind='person' AND e.id = ("
            "  SELECT CAST(value AS INTEGER) FROM world_meta WHERE key='protagonist_id' AND value<>'' "
            "  UNION ALL SELECT id FROM entities WHERE kind='person' ORDER BY id LIMIT 1) "
            "AND NOT EXISTS(SELECT 1 FROM entity_personas p WHERE p.entity_id=e.id "
            "  AND p.goal<>'' AND p.desire<>'' AND p.fear<>'')",
            &missing);
        if (!q) {
            fail("N5", "主角人设查询失败", "检查 entity_personas 表");
        } else if (missing > 0 || countOr("SELECT COUNT(*) FROM entities WHERE kind='person'") < 1) {
            fail("N5", "主角缺 entity_personas，或 goal/desire/fear 有空",
                 "补人设三字段（`INIT_CHARACTERS`）");
        }
    }
    // N6 `entities(kind='location')` >= 1
    if (countOr("SELECT COUNT(*) FROM entities WHERE kind='location'") < 1) {
        fail("N6", "没有任何 location 实体（初始地点）", "建初始地点（`INIT_LOCATIONS`）");
    }
    // N7 `plots` 至少 1 条 kind='main' 且 target_ch > 0
    if (countOr("SELECT COUNT(*) FROM plots WHERE kind='main' AND target_ch>0") < 1) {
        fail("N7", "没有 kind='main' 且 target_ch>0 的剧情线", "建主线并给目标章数（`INIT_PLOTS`）");
    }
    // N8 `mysteries` >= 1
    if (countOr("SELECT COUNT(*) FROM mysteries") < 1) {
        fail("N8", "没有任何 mysteries（第 1 章会缺信息节奏）", "建至少 1 条谜团（`INIT_MYSTERIES`）");
    }
    // N9 `secrets` 至少 1 条 scope='world'
    if (countOr("SELECT COUNT(*) FROM secrets WHERE scope='world'") < 1) {
        fail("N9", "没有 scope='world' 的 secret", "建世界级秘密（`INIT_SECRETS`）");
    }
    // N10 每卷 ord 唯一且连续（从 1 起）
    {
        std::int64_t vols = 0;
        if (!QueryI64(db, "SELECT COUNT(*) FROM volumes", &vols)) {
            fail("N10", "volumes 查询失败", "检查 schema");
        } else if (vols > 0) {
            std::int64_t bad = 0;
            const bool q = QueryI64(
                db,
                "SELECT COUNT(*) FROM (SELECT ord, ROW_NUMBER() OVER (ORDER BY ord) AS rn "
                "  FROM volumes) WHERE ord <> rn",
                &bad);
            if (!q || bad > 0) {
                fail("N10", fmt::format("volumes 的 ord 不连续或不唯一（{} 卷，{} 条不符）", vols, bad),
                     "卷号从 1 起连续（`INIT_VOLUMES`）");
            }
        } else {
            fail("N10", "没有任何卷", "建至少 1 卷（`INIT_VOLUMES`）");
        }
    }
    // N11 `foreshadowings(status='PLANNED')`：setup_ch < payoff_ch 且 setup_ch >= 1
    if (countOr("SELECT COUNT(*) FROM foreshadowings WHERE status='PLANNED' AND "
                "NOT (setup_ch >= 1 AND payoff_ch > setup_ch)") > 0) {
        fail("N11", "有 PLANNED 伏笔的 setup_ch/payoff_ch 不满足 setup_ch>=1 且 payoff_ch>setup_ch",
             "修伏笔章号（`INIT_FORESHADOW_SEED`）");
    }
    // N12 `field_defs` 里 is_system=1 的种子 >= 11 条
    if (countOr("SELECT COUNT(*) FROM field_defs WHERE is_system=1") < 11) {
        fail("N12", fmt::format("field_defs 系统种子只有 {} 条（应 ≥ 11）",
                                countOr("SELECT COUNT(*) FROM field_defs WHERE is_system=1")),
             "跑 `NovelFields::EnsureSchema`（打开工程时会自动跑）");
    }
    // N13 `agent_defs` 里 15 个内置 Agent 齐备且 enabled=1
    if (countOr("SELECT COUNT(*) FROM agent_defs WHERE enabled=1") < 15) {
        fail("N13", fmt::format("agent_defs 启用中的只有 {} 条（应 ≥ 15）",
                                countOr("SELECT COUNT(*) FROM agent_defs WHERE enabled=1")),
             "跑 `AgentKit::EnsureSchemaAndSeed`（打开工程时会自动跑）");
    }
    // N14 悬空引用（`06` K02/K03 同口径；这里是初始化阶段会涉及的那几列）
    {
        std::int64_t dangling = 0;
        const bool q = QueryI64(
            db,
            "SELECT (SELECT COUNT(*) FROM scenes s WHERE s.location_id>0 AND NOT EXISTS("
            "  SELECT 1 FROM entities e WHERE e.id=s.location_id))"
            " + (SELECT COUNT(*) FROM scenes s WHERE s.pov_entity_id>0 AND NOT EXISTS("
            "  SELECT 1 FROM entities e WHERE e.id=s.pov_entity_id))"
            " + (SELECT COUNT(*) FROM event_participants p WHERE p.entity_id>0 AND NOT EXISTS("
            "  SELECT 1 FROM entities e WHERE e.id=p.entity_id))"
            " + (SELECT COUNT(*) FROM relations r WHERE (r.from_id>0 AND NOT EXISTS("
            "  SELECT 1 FROM entities e WHERE e.id=r.from_id)) OR (r.to_id>0 AND NOT EXISTS("
            "  SELECT 1 FROM entities e WHERE e.id=r.to_id)))",
            &dangling);
        if (!q) {
            fail("N14", "悬空引用查询失败", "检查 schema（scenes / relations / event_participants）");
        } else if (dangling > 0) {
            fail("N14", fmt::format("有 {} 处悬空引用（指向不存在的实体）", dangling),
                 "补实体或改引用（`06` K02/K03 同口径）");
        }
    }
    rep.passed = rep.failures.empty();
    (void)n;
    return rep;
}

InitSkeletonResult RunInitSkeleton(db::sqlite::Database& db, const std::filesystem::path& project_dir,
                                   std::string_view book_title, int target_chapters) {
    InitSkeletonResult out;
    if (!db.isOpen()) {
        out.error = "数据库未打开";
        return out;
    }
    const int target = target_chapters > 0 ? target_chapters : 100;
    // 新库（CLI 从零初始化）也要能用 —— `ApplyCanonicalSchema` 幂等，已建库时是空操作
    if (auto r = NovelDb::ApplyCanonicalSchema(db); !r) {
        out.error = "建 schema 失败：" + r.error().message;
        return out;
    }
    NovelGraph g(db);

    // N12 / N13 的种子（打开工程时本就会跑；这里显式跑一次，让"新建工程"也能直接过门禁）
    agent::AgentKit kit(db, false);
    if (auto r = kit.EnsureSchemaAndSeed(); !r) {
        log::Warn("初始化骨架：Agent 种子失败 {}", r.error().message);
    } else {
        out.created.push_back("agent_defs#builtin");
    }
    (void)NovelFields::EnsureSchema(db);

    auto meta = g.SetWorldMeta("book_title", book_title.empty() ? std::string{"未命名小说"} : book_title);
    if (!meta) {
        out.error = meta.error().message;
        return out;
    }
    out.created.push_back("world_meta.book_title");

    WritingStyleRow style;
    style.pov_mode = "third_limited";
    style.pacing = "medium";
    if (auto r = g.UpsertWritingStyle(style); !r) {
        out.error = r.error().message;
        return out;
    }
    out.created.push_back("writing_style#1");

    if (auto r = g.UpsertAuthorRule({.rule = "不写 POV 角色认知范围之外的信息", .severity = "error",
                                     .note = "S21 骨架：可改"});
        !r) {
        out.error = r.error().message;
        return out;
    }
    out.created.push_back("author_rules#error");

    if (auto r = g.UpsertVolume({.title = "第一卷", .ord = 1, .summary = "（骨架：待填）"}); !r) {
        out.error = r.error().message;
        return out;
    }
    out.created.push_back("volumes#1");

    if (auto r = g.UpsertPlot({.kind = "main", .title = "主线", .status = "active", .intro_ch = 1,
                               .target_ch = target, .note = "S21 骨架：待填"});
        !r) {
        out.error = r.error().message;
        return out;
    }
    out.created.push_back("plots#main");

    if (auto r = g.UpsertMystery({.question = "（骨架：待填核心谜团）", .status = "open", .ask_ch = 1,
                                  .answer_ch = target, .importance = 50});
        !r) {
        out.error = r.error().message;
        return out;
    }
    out.created.push_back("mysteries#1");

    if (auto r = g.UpsertSecret(
            {.content = "（骨架：待填世界级秘密）", .truth = "（待填）", .scope = "world"});
        !r) {
        out.error = r.error().message;
        return out;
    }
    out.created.push_back("secrets#world");

    out.ok = true;
    // `I16 INIT_COMMIT`：把门禁结论落盘（`10` §2.7）。`R7` 的 canon_logs「已初始化」标志**不在这里写**
    // —— 骨架不是"已完成初始化"（内容类条件仍不满足），那面旗要等门禁真通过时再插。
    const InitReport gate = CheckInitGate(db);
    (void)WriteInitArtifact(project_dir, "I15_gate.json", gate.Json());
    (void)WriteInitArtifact(
        project_dir, "I16_commit.json",
        fmt::format(R"({{"skeleton":true,"book_title":"{}","target_chapters":{},"gate_passed":{}}})",
                    util::PathToUtf8(std::filesystem::path{book_title}), target,
                    gate.passed ? "true" : "false"));
    return out;
}

bool RunInitSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("初始化自检：内存库打开失败 {}", r.error().message);
        return false;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("初始化自检：建表失败 {}", r.error().message);
        return false;
    }
    int fails = 0;
    const auto expect = [&fails](bool cond, std::string_view what) {
        if (!cond) {
            ++fails;
            log::Error("初始化自检 FAIL：{}", what);
        }
    };
    const auto has = [](const InitReport& rep, std::string_view id) {
        for (const InitFailure& f : rep.failures) {
            if (f.n_id == id) {
                return true;
            }
        }
        return false;
    };

    // ① 空库：门禁必须不通过，且把"内容类 + 种子"条件都点出来（不允许警告后放行）
    const InitReport empty = CheckInitGate(mem);
    expect(!empty.passed, "空库必须不通过门禁（`10` 差距 10-3 的判据）");
    for (const char* id : {"N1", "N2", "N3", "N4", "N5", "N6", "N7", "N8", "N9", "N10", "N12",
                           "N13"}) {
        expect(has(empty, id), fmt::format("空库应报 {} 不满足", id));
    }
    // N11 的语义是「**有** PLANNED 伏笔时章号必须合规」→ 空库本就该通过；
    // N14（悬空引用）同理。这里两条都要能"构造出 fail"才算真判定。
    expect(!has(empty, "N11") && !has(empty, "N14"),
           "空库：N11/N14 应通过（都是「存在则必须合规」型）");
    (void)mem.Exec("INSERT INTO foreshadowings(title,status,setup_ch,payoff_ch) "
                   "VALUES('自检伏笔','PLANNED',5,3)");
    expect(has(CheckInitGate(mem), "N11"), "PLANNED 伏笔 payoff_ch<=setup_ch → N11 应 fail");
    (void)mem.Exec("DELETE FROM foreshadowings");
    (void)mem.Exec("INSERT INTO scenes(chapter_id,ord,location_id) VALUES(1,1,99999)");
    expect(has(CheckInitGate(mem), "N14"), "场景指向不存在的地点 → N14 应 fail");
    (void)mem.Exec("DELETE FROM scenes");
    expect(empty.Json().find("\"passed\":false") != std::string::npos &&
               !empty.Describe().empty(),
           "门禁报告（JSON / 摘要）可序列化");

    // ② 骨架：**结构类**条件应通过，**内容类**仍不通过（骨架不伪造内容 —— `R5` 的本意）
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "shine_init_check";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const InitSkeletonResult sk = RunInitSkeleton(mem, dir, "自检骨架书", 60);
    expect(sk.ok, fmt::format("骨架应成功：{}", sk.error));
    expect(!sk.created.empty(), "骨架应记录建了什么");
    const InitReport after = CheckInitGate(mem);
    for (const char* id : {"N1", "N2", "N3", "N7", "N8", "N9", "N10", "N12", "N13"}) {
        expect(!has(after, id), fmt::format("骨架后 {} 应通过", id));
    }
    for (const char* id : {"N4", "N5", "N6"}) {
        expect(has(after, id), fmt::format("骨架后 {} 仍应不满足（骨架不伪造内容）", id));
    }
    expect(std::filesystem::exists(dir / "init" / "I15_gate.json", ec) &&
               std::filesystem::exists(dir / "init" / "I16_commit.json", ec),
           "I15_gate.json / I16_commit.json 应落盘（`10` §2.7）");
    expect(InitStageCatalog().size() == 16, "I1–I16 阶段表应 16 条");

    // ③ 补齐内容类条件 → 门禁转通过（N4/N5/N6）
    NovelGraph g(mem);
    auto pov = g.UpsertEntity({.kind = std::string{kind::person}, .name = "自检主角"});
    expect(pov.has_value(), "建主角应成功");
    (void)g.UpsertPersona({.entity_id = pov.value_or(0),
                           .desire = "真相",
                           .goal = "活下来",
                           .fear = "失去"});
    auto loc = g.UpsertEntity({.kind = std::string{kind::location}, .name = "自检地点"});
    expect(loc.has_value(), "建地点应成功");
    const InitReport full = CheckInitGate(mem);
    expect(full.passed, fmt::format("补齐内容后门禁应通过：{}", full.Describe()));

    std::filesystem::remove_all(dir, ec);
    if (fails == 0) {
        log::Info("初始化自检通过（N1–N14 逐条触发 + 骨架只给结构不伪造内容 + I15/I16 落盘）");
    }
    return fails == 0;
}

} // namespace shine::novelcore
