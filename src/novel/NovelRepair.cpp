#include "novel/NovelRepair.h"

#include "novel/NovelNames.h" // R10 与 resolver **同一份**名字归一化口径

#include "core/Log.h"
#include "novel/NovelGraph.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <map>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace shine::novelcore {
namespace {

[[nodiscard]] std::string JsonEsc(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                o += fmt::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                o += c;
            }
        }
    }
    return o;
}

// 被持有物的合法 kind（契约：持有关系只对"物"成立）。`prop` 也在这本库里被用作物品。
constexpr std::array<std::string_view, 5> kItemKinds = {"item", "prop", "treasure", "clothing",
                                                        "resource"};

[[nodiscard]] bool IsItemKind(std::string_view k) {
    for (const auto& x : kItemKinds) {
        if (x == k) {
            return true;
        }
    }
    return false;
}

// 计数（带 1 个 id 参数）：查询失败返回 -1 —— **不静默当 0**（0 会让"关系数/出场数"看起来正常）
[[nodiscard]] RowId CountBy(db::sqlite::Database& db, std::string_view sql, RowId a) {
    auto st = db.Prepare(std::string{sql});
    if (!st) {
        return -1;
    }
    (void)st->BindInt(1, a);
    if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
        return st->ColumnInt(0);
    }
    return -1;
}

// 截断（按 **UTF-8 字符边界**回退，别把多字节切一半）
[[nodiscard]] std::string Shorten(std::string_view s, std::size_t n) {
    if (s.size() <= n) {
        return std::string{s};
    }
    std::size_t e = n;
    while (e > 0 && (static_cast<unsigned char>(s[e]) & 0xC0) == 0x80) {
        --e;
    }
    return std::string{s.substr(0, e)} + "…";
}

// 只读遍历（SQL 用字面量/内联，带参的另走 Prepare + Bind）
template <class F>
bool ForEachRows(db::sqlite::Database& db, std::string_view label, std::string_view sql, F&& fn) {
    std::string q{sql};
    auto st = db.Prepare(q);
    if (!st) {
        log::Error("一致性扫描：{} 准备失败 {}", label, st.error().message);
        return false;
    }
    while (true) {
        auto s = st->Step();
        if (!s) {
            log::Error("一致性扫描：{} 执行失败 {}", label, s.error().message);
            return false;
        }
        if (*s == db::sqlite::StepResult::Done) {
            break;
        }
        fn(*st);
    }
    return true;
}

[[nodiscard]] std::unordered_set<std::string> ParseRules(std::string_view csv) {
    std::unordered_set<std::string> out;
    std::size_t i = 0;
    while (i < csv.size()) {
        std::size_t j = csv.find(',', i);
        if (j == std::string_view::npos) {
            j = csv.size();
        }
        std::string tok{csv.substr(i, j - i)};
        // 去空白 + 转大写（调用方可能写 "r1, r2"）
        tok.erase(std::remove_if(tok.begin(), tok.end(),
                                 [](unsigned char c) { return std::isspace(c) != 0; }),
                  tok.end());
        for (char& c : tok) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (!tok.empty()) {
            out.insert(std::move(tok));
        }
        i = j + 1;
        if (j == csv.size()) {
            break;
        }
    }
    return out;
}

} // namespace

// ———— 报告 ————

std::string ConsistencyReport::ToJson() const {
    std::string out = fmt::format(
        R"({{"ok":{},"errors":{},"warnings":{},"fixable":{},"total":{},"issues":[)",
        ok() ? "true" : "false", errors, warnings, fixable, issues.size());
    for (std::size_t i = 0; i < issues.size(); ++i) {
        const auto& x = issues[i];
        if (i) {
            out += ",";
        }
        out += fmt::format(
            R"({{"rule":"{}","severity":"{}","table":"{}","row_id":{},"subject_id":{},"fixable":{},"detail":"{}","suggest":"{}"}})",
            x.rule, x.severity, x.table, x.row_id, x.subject_id, x.fixable ? "true" : "false",
            JsonEsc(x.detail), JsonEsc(x.suggest));
    }
    out += "]}";
    return out;
}

std::string ConsistencyReport::ToText() const {
    std::string out = fmt::format("一致性扫描：{} 条（error {} / warn {}，其中可机械修 {}）",
                                  issues.size(), errors, warnings, fixable);
    if (issues.empty()) {
        return out + " —— 库是干净的";
    }
    for (const auto& x : issues) {
        out += fmt::format("\n  [{}] {} {}#{}（subject={}）{}\n      修法：{}",
                           x.severity, x.rule, x.table, x.row_id, x.subject_id, x.detail,
                           x.fixable ? x.suggest + "（本工具可自动修）" : x.suggest);
    }
    return out;
}

// ———— 扫描（只读）————

ConsistencyReport ScanConsistency(db::sqlite::Database& db) {
    ConsistencyReport rep;
    const auto add = [&rep](ConsistencyIssue x) {
        if (x.severity == "warn") {
            ++rep.warnings;
        } else {
            ++rep.errors;
        }
        if (x.fixable) {
            ++rep.fixable;
        }
        rep.issues.push_back(std::move(x));
    };

    // R1：持有区间无效（to<=from，且非"至今"）—— 空区间不表达任何事实
    (void)ForEachRows(
        db, "R1",
        "SELECT id,item_id,owner_id,from_chapter,to_chapter FROM entity_ownerships "
        "WHERE to_chapter<>0 AND to_chapter<=from_chapter ORDER BY id",
        [&](db::sqlite::Statement& st) {
            const RowId id = st.ColumnInt(0);
            add(ConsistencyIssue{
                .rule = "R1",
                .severity = "error",
                .table = "entity_ownerships",
                .row_id = id,
                .subject_id = st.ColumnInt(1),
                .detail = fmt::format("持有区间无效：物品#{} 的持有者#{} 记为第 {} 章起、第 {} 章止",
                                      st.ColumnInt(1), st.ColumnInt(2), st.ColumnInt(3),
                                      st.ColumnInt(4)),
                .suggest = "删除本行（空区间什么都不表达）",
                .fixable = true});
        });

    // R2：**活跃**持有的持有者不是人物（`person`）—— 契约里只有人持有物
    (void)ForEachRows(
        db, "R2",
        "SELECT o.id,o.item_id,o.owner_id,o.from_chapter,e.kind,e.name FROM entity_ownerships o "
        "JOIN entities e ON e.id=o.owner_id WHERE o.to_chapter=0 AND e.kind<>'person' ORDER BY o.id",
        [&](db::sqlite::Statement& st) {
            add(ConsistencyIssue{
                .rule = "R2",
                .severity = "error",
                .table = "entity_ownerships",
                .row_id = st.ColumnInt(0),
                .subject_id = st.ColumnInt(1),
                .detail = fmt::format("活跃持有的持有者不是人物：物品#{} 被 #{}（kind={}「{}」）持有",
                                      st.ColumnInt(1), st.ColumnInt(2), st.ColumnText(4),
                                      st.ColumnText(5)),
                .suggest = "关闭本行（`to_chapter = from_chapter + 1`）—— 历史脏行无从推断正确持有者",
                .fixable = true});
        });

    // R3：同一物品多行活跃持有（破坏 I5「唯一持有者」）
    {
        std::vector<std::pair<RowId, int>> multi; // item_id → 活跃行数
        (void)ForEachRows(db, "R3-scan",
                          "SELECT item_id,COUNT(*) FROM entity_ownerships WHERE to_chapter=0 "
                          "GROUP BY item_id HAVING COUNT(*)>1 ORDER BY item_id",
                          [&](db::sqlite::Statement& st) {
                              multi.emplace_back(st.ColumnInt(0), static_cast<int>(st.ColumnInt(1)));
                          });
        for (const auto& [item, n] : multi) {
            std::vector<std::tuple<RowId, RowId, RowId>> rows; // id, owner, from
            auto st = db.Prepare(std::string{"SELECT id,owner_id,from_chapter FROM entity_ownerships "
                                             "WHERE item_id=?1 AND to_chapter=0 "
                                             "ORDER BY from_chapter DESC, id DESC"});
            if (!st) {
                log::Error("一致性扫描：R3 准备失败 {}", st.error().message);
                continue;
            }
            (void)st->BindInt(1, item);
            while (true) {
                auto s = st->Step();
                if (!s) {
                    break;
                }
                if (*s == db::sqlite::StepResult::Done) {
                    break;
                }
                rows.emplace_back(st->ColumnInt(0), st->ColumnInt(1), st->ColumnInt(2));
            }
            if (rows.size() < 2) {
                continue;
            }
            // 保留 from_chapter 最大者（并列取 id 最大，即排序第一行）
            const auto& keep = rows[0];
            for (std::size_t i = 1; i < rows.size(); ++i) {
                const auto& [id, owner, from] = rows[i];
                add(ConsistencyIssue{
                    .rule = "R3",
                    .severity = "error",
                    .table = "entity_ownerships",
                    .row_id = id,
                    .subject_id = item,
                    .detail = fmt::format(
                        "物品#{} 有 {} 行活跃持有；本行（持有者#{}，第 {} 章起）将被关闭，"
                        "保留 持有者#{}（第 {} 章起）",
                        item, rows.size(), owner, from, std::get<1>(keep), std::get<2>(keep)),
                    .suggest = "关闭本行：`to_chapter = MAX(保留者.from_chapter, 本行.from_chapter+1)`"
                               "（与提交路径的转移语义一致）",
                    .fixable = true});
            }
        }
    }

    // R4：持有者指向不存在的实体
    (void)ForEachRows(db, "R4",
                      "SELECT o.id,o.item_id,o.owner_id FROM entity_ownerships o "
                      "LEFT JOIN entities e ON e.id=o.owner_id WHERE e.id IS NULL ORDER BY o.id",
                      [&](db::sqlite::Statement& st) {
                          add(ConsistencyIssue{
                              .rule = "R4",
                              .severity = "error",
                              .table = "entity_ownerships",
                              .row_id = st.ColumnInt(0),
                              .subject_id = st.ColumnInt(1),
                              .detail = fmt::format("持有者 #{} 在 entities 里不存在（物品#{}）",
                                                    st.ColumnInt(2), st.ColumnInt(1)),
                              .suggest = "删除本行（悬空外键无意义）",
                              .fixable = true});
                      });

    // R5：被持有物指向不存在的实体
    (void)ForEachRows(db, "R5",
                      "SELECT o.id,o.item_id,o.owner_id FROM entity_ownerships o "
                      "LEFT JOIN entities e ON e.id=o.item_id WHERE e.id IS NULL ORDER BY o.id",
                      [&](db::sqlite::Statement& st) {
                          add(ConsistencyIssue{
                              .rule = "R5",
                              .severity = "error",
                              .table = "entity_ownerships",
                              .row_id = st.ColumnInt(0),
                              .subject_id = st.ColumnInt(1),
                              .detail = fmt::format("被持有物 #{} 在 entities 里不存在（持有者#{}）",
                                                    st.ColumnInt(1), st.ColumnInt(2)),
                              .suggest = "删除本行（悬空外键无意义）",
                              .fixable = true});
                      });

    // R6：被持有物不是物品类（warn，只报告 —— 可能是我们还没见过的合法 kind）
    (void)ForEachRows(
        db, "R6",
        "SELECT o.id,o.item_id,e.kind,e.name FROM entity_ownerships o JOIN entities e ON "
        "e.id=o.item_id ORDER BY o.id",
        [&](db::sqlite::Statement& st) {
            const std::string k = st.ColumnText(2);
            if (IsItemKind(k)) {
                return;
            }
            add(ConsistencyIssue{
                .rule = "R6",
                .severity = "warn",
                .table = "entity_ownerships",
                .row_id = st.ColumnInt(0),
                .subject_id = st.ColumnInt(1),
                .detail = fmt::format("被持有物 #{} 的 kind={}（「{}」）不是物品类（期望 {}）",
                                      st.ColumnInt(1), k, st.ColumnText(3), "item/prop/treasure/…"),
                .suggest = "人工确认：要么该实体 kind 写错，要么这条持有关系本就该表达别的东西",
                .fixable = false});
        });

    // R7：事件参与者 / 事件本身悬空
    (void)ForEachRows(
        db, "R7",
        "SELECT p.id,p.event_id,p.entity_id FROM event_participants p "
        "WHERE NOT EXISTS(SELECT 1 FROM entities e WHERE e.id=p.entity_id) "
        "   OR NOT EXISTS(SELECT 1 FROM entities e WHERE e.id=p.event_id) ORDER BY p.id",
        [&](db::sqlite::Statement& st) {
            add(ConsistencyIssue{
                .rule = "R7",
                .severity = "error",
                .table = "event_participants",
                .row_id = st.ColumnInt(0),
                .subject_id = st.ColumnInt(1),
                .detail = fmt::format("参与者行悬空：event_id={} / entity_id={} 至少一个不存在",
                                      st.ColumnInt(1), st.ColumnInt(2)),
                .suggest = "别手工删引用行 —— 正确修法是修那一章的 StateDiff 后重跑该章 EXTRACT",
                .fixable = false});
        });

    // R8：事件地点悬空
    (void)ForEachRows(
        db, "R8",
        "SELECT d.entity_id,d.location_id FROM event_details d WHERE d.location_id<>0 AND "
        "NOT EXISTS(SELECT 1 FROM entities e WHERE e.id=d.location_id) ORDER BY d.entity_id",
        [&](db::sqlite::Statement& st) {
            add(ConsistencyIssue{
                .rule = "R8",
                .severity = "error",
                .table = "event_details",
                .row_id = st.ColumnInt(0),
                .subject_id = st.ColumnInt(0),
                .detail = fmt::format("事件 #{} 的地点 #{} 不存在", st.ColumnInt(0),
                                      st.ColumnInt(1)),
                .suggest = "重跑该章 EXTRACT（地点应来自库内真实 location id 或 `location_temp_id`）",
                .fixable = false});
        });

    // R9：关系边端点悬空
    (void)ForEachRows(db, "R9",
                      "SELECT r.id,r.from_id,r.to_id FROM relations r "
                      "WHERE NOT EXISTS(SELECT 1 FROM entities e WHERE e.id=r.from_id) "
                      "   OR NOT EXISTS(SELECT 1 FROM entities e WHERE e.id=r.to_id) ORDER BY r.id",
                      [&](db::sqlite::Statement& st) {
                          add(ConsistencyIssue{
                              .rule = "R9",
                              .severity = "error",
                              .table = "relations",
                              .row_id = st.ColumnInt(0),
                              .subject_id = st.ColumnInt(1),
                              .detail = fmt::format("关系边端点悬空：#{} → #{}",
                                                    st.ColumnInt(1), st.ColumnInt(2)),
                              .suggest = "重跑该章 EXTRACT（关系两端必须是库内真实实体 id）",
                              .fixable = false});
                      });

    // R10：**同 kind + 同 `name_norm` 的重复实体**（候选重复，**永不自动合并**）★ T3a
    // 口径：`name_norm` 用 `NormalizeEntityName()`（= resolver 用的**同一份**实现，见 `NovelNames.h`）——
    // 不许在 SQL 里自己写 lower/replace，否则"扫描说没重复、解析说'有歧义'"这种漂移极难查。
    // 为什么**只报不修**：`撑伞人影` ×2 很可能确实是同一角色的重复声明，但 `李默` ×2 也可能是**两个不同的人**
    // ⇒ 这是**语义决策**，机器不替作者做（用户口径：「系统可以发现歧义，但不要擅自消除歧义」）。
    // ⚠️ 只查**同 kind**：`person「灯塔」/ location「灯塔」/ item「灯塔」` 完全可以合法共存。
    {
        struct Ent {
            RowId id = 0;
            std::string name;
            int created = 0;
            std::string summary;
        };
        std::map<std::string, std::vector<Ent>> groups; // key = kind + '\x1f' + name_norm
        (void)ForEachRows(db, "R10-scan",
                          "SELECT id,kind,name,created_chapter,summary FROM entities "
                          "WHERE kind<>'event' ORDER BY kind,name,id",
                          [&](db::sqlite::Statement& st) {
                              const std::string norm = NormalizeEntityName(st.ColumnText(2));
                              if (norm.empty()) {
                                  return;
                              }
                              groups[st.ColumnText(1) + "\x1f" + norm].push_back(
                                  Ent{st.ColumnInt(0), st.ColumnText(2),
                                      static_cast<int>(st.ColumnInt(3)), st.ColumnText(4)});
                          });
        for (const auto& [key, list] : groups) {
            if (list.size() < 2) {
                continue;
            }
            std::string detail =
                fmt::format("同名同 kind 的实体有 {} 个（**候选重复，需人/AI 确认**）：", list.size());
            std::string ids;
            for (const Ent& e : list) {
                if (!ids.empty()) {
                    ids += ",";
                }
                ids += std::to_string(e.id);
                detail += fmt::format(
                    "\n    #{}（第{}章建）关系 {} / 出场 {} / 知情 {} · {}", e.id, e.created,
                    CountBy(db, "SELECT COUNT(*) FROM relations WHERE from_id=?1 OR to_id=?1", e.id),
                    CountBy(db, "SELECT COUNT(DISTINCT chapter_id) FROM character_status WHERE "
                                "entity_id=?1",
                            e.id),
                    CountBy(db, "SELECT COUNT(*) FROM character_knowledge WHERE entity_id=?1", e.id),
                    Shorten(e.summary, 36));
            }
            add(ConsistencyIssue{
                .rule = "R10",
                .severity = "warn",
                .table = "entities",
                .row_id = 0,
                .subject_id = list.front().id,
                .detail = detail,
                .suggest = fmt::format("**本工具不修、也绝不自动 merge**：可能是同一实体的重复声明，"
                                       "也可能是两个同名的人/物/地点 ⇒ 请人工确认后自行执行 merge SQL。"
                                       "候选 id：{}",
                                       ids),
                .fixable = false});
        }
    }

    return rep;
}

// ———— 修复 ————

std::string RepairOutcome::ToJson() const {
    std::string acts = "[";
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (i) {
            acts += ",";
        }
        acts += fmt::format("\"{}\"", JsonEsc(actions[i]));
    }
    acts += "]";
    return fmt::format(
        R"({{"ok":{},"dry_run":{},"scanned":{},"fixed":{},"skipped":{},"error":"{}","actions":{}}})",
        ok() ? "true" : "false", dry_run ? "true" : "false", scanned, fixed, skipped,
        JsonEsc(error), acts);
}

RepairOutcome RepairConsistency(db::sqlite::Database& db, bool dry_run, std::string_view rules,
                                std::string_view actor) {
    RepairOutcome out;
    out.dry_run = dry_run;
    // 默认（`rules` 空）= 全部**可机械修**的规则；显式给 rules 时也只认 R1–R5（其余一律跳过并计数）。
    const std::unordered_set<std::string> want =
        rules.empty() ? std::unordered_set<std::string>{"R1", "R2", "R3", "R4", "R5"}
                      : ParseRules(rules);

    const ConsistencyReport rep = ScanConsistency(db);
    // 单条执行器：`sql` 带 `?1..?n`，`vals` 顺序绑定；返回是否成功
    const auto run = [&db](std::string_view sql, std::initializer_list<RowId> vals,
                           std::string& err) {
        auto st = db.Prepare(std::string{sql});
        if (!st) {
            err = st.error().message;
            return false;
        }
        int i = 1;
        for (const RowId v : vals) {
            (void)st->BindInt(i++, v);
        }
        auto s = st->Step();
        if (!s) {
            err = s.error().message;
            return false;
        }
        return true;
    };
    const auto scalar = [&db](std::string_view sql, std::initializer_list<RowId> vals) -> RowId {
        auto st = db.Prepare(std::string{sql});
        if (!st) {
            return -1;
        }
        int i = 1;
        for (const RowId v : vals) {
            (void)st->BindInt(i++, v);
        }
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            return st->ColumnInt(0);
        }
        return -1;
    };

    // ⚠️ dry_run **绝不进写路径**：`run` 是本函数唯一的写入口，所有调用点都用 `!write ||` 短路。
    //（曾经写成"先把写做完、最后 `if (dry_run) return`" —— 那样 dry_run 是假的、会真改库。
    //  自检里"dry_run 后行数不变"的断言就是为了钉住这件事。）
    const bool write = !dry_run;

    for (const auto& x : rep.issues) {
        if (!x.fixable || want.count(x.rule) == 0) {
            ++out.skipped;
            continue;
        }
        ++out.scanned;
        std::string err;
        bool done = false;
        switch (x.rule[1]) {
        case '1': // 空/倒挂区间 → 删
            done = !write || run("DELETE FROM entity_ownerships WHERE id=?1", {x.row_id}, err);
            if (done) {
                out.actions.push_back(
                    fmt::format("R1 entity_ownerships#{}：删除（区间无效）", x.row_id));
            }
            break;
        case '2': { // 活跃持有的持有者不是 person → 关闭
            const RowId from = scalar("SELECT from_chapter FROM entity_ownerships WHERE id=?1",
                                      {x.row_id});
            if (from < 0) {
                break;
            }
            const RowId to = from + 1;
            done = !write || run("UPDATE entity_ownerships SET to_chapter=?1, note=note||?2 "
                                 "WHERE id=?3",
                                 {to, 0, x.row_id}, err);
            if (done) {
                out.actions.push_back(fmt::format(
                    "R2 entity_ownerships#{}：关闭持有（to_chapter={}）", x.row_id, to));
            }
            break;
        }
        case '3': { // 多行活跃 → 保留最新，关闭其余
            const RowId keeperFrom = scalar(
                "SELECT from_chapter FROM entity_ownerships WHERE item_id=?1 AND to_chapter=0 "
                "ORDER BY from_chapter DESC, id DESC LIMIT 1",
                {x.subject_id});
            const RowId myFrom =
                scalar("SELECT from_chapter FROM entity_ownerships WHERE id=?1", {x.row_id});
            if (keeperFrom < 0 || myFrom < 0) {
                break;
            }
            const RowId to = std::max(keeperFrom, myFrom + 1);
            done = !write || run("UPDATE entity_ownerships SET to_chapter=?1, note=note||?2 "
                                 "WHERE id=?3",
                                 {to, 0, x.row_id}, err);
            if (done) {
                out.actions.push_back(
                    fmt::format("R3 entity_ownerships#{}：关闭旧持有（to_chapter={}，保留物品#{} "
                                "第 {} 章起的持有者）",
                                x.row_id, to, x.subject_id, keeperFrom));
            }
            break;
        }
        case '4':
        case '5':
            done = !write || run("DELETE FROM entity_ownerships WHERE id=?1", {x.row_id}, err);
            if (done) {
                out.actions.push_back(
                    fmt::format("{} entity_ownerships#{}：删除（悬空外键）", x.rule, x.row_id));
            }
            break;
        default:
            break;
        }
        if (err.empty() && !done && out.error.empty()) {
            // 前置读失败（行可能已被别处删掉）—— 不算错误，跳过
            ++out.skipped;
            --out.scanned;
            continue;
        }
        if (!err.empty()) {
            out.error = fmt::format("{} 执行失败：{}", x.rule, err);
            log::Error("一致性修复：{}", out.error);
            break;
        }
        ++out.fixed;
    }

    if (dry_run) {
        return out; // 上面把每条动作都"预演"出来了（值全是算的，没有一个字节落库）
    }

    // 真写：记一条审计（谁、改了几条、每条动作）。dry_run 不记（它没有产生事实变更）。
    if (out.fixed > 0) {
        NovelGraph g(db);
        std::string detail = fmt::format(R"({{"fixed":{},"scanned":{},"skipped":{})", out.fixed,
                                        out.scanned, out.skipped);
        detail += R"(,"actions":[)";
        for (std::size_t i = 0; i < out.actions.size(); ++i) {
            if (i) {
                detail += ",";
            }
            detail += fmt::format("\"{}\"", JsonEsc(out.actions[i]));
        }
        detail += "]}";
        (void)g.LogAudit(actor, "consistency_repair", "project", 0, detail);
    }
    return out;
}

// ———— 自检 ————

bool RunRepairSelfCheck() {
    int fail = 0;
    const auto expect = [&fail](bool cond, std::string_view name) {
        if (cond) {
            log::Info("S69 repair PASS {}", name);
        } else {
            ++fail;
            log::Error("S69 repair FAIL {}", name);
        }
    };

    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("S69 repair：开内存库失败");
        return false;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("S69 repair：建表失败 {}", r.error().message);
        return false;
    }

    NovelGraph g(mem);
    const RowId pA = g.UpsertEntity({.kind = "person", .name = "甲"}).value_or(0);
    const RowId pB = g.UpsertEntity({.kind = "person", .name = "乙"}).value_or(0);
    const RowId loc = g.UpsertEntity({.kind = "location", .name = "地点"}).value_or(0);
    const RowId it1 = g.UpsertEntity({.kind = "item", .name = "物品1"}).value_or(0);
    const RowId it2 = g.UpsertEntity({.kind = "item", .name = "物品2"}).value_or(0);
    const RowId it3 = g.UpsertEntity({.kind = "item", .name = "物品3"}).value_or(0);
    const RowId ev = g.UpsertEntity({.kind = "event", .name = "事件1"}).value_or(0);
    expect(pA > 0 && pB > 0 && loc > 0 && it1 > 0 && it2 > 0 && it3 > 0 && ev > 0,
           "自检前置：造 7 个实体");

    const auto ins = [&mem](std::string_view sql, std::initializer_list<RowId> vals) {
        auto st = mem.Prepare(std::string{sql});
        if (!st) {
            return false;
        }
        int i = 1;
        for (const RowId v : vals) {
            (void)st->BindInt(i++, v);
        }
        auto s = st->Step();
        return s.has_value();
    };
    const auto scalar = [&mem](std::string_view sql, std::initializer_list<RowId> vals) -> RowId {
        auto st = mem.Prepare(std::string{sql});
        if (!st) {
            return -1;
        }
        int i = 1;
        for (const RowId v : vals) {
            (void)st->BindInt(i++, v);
        }
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            return st->ColumnInt(0);
        }
        return -1;
    };
    const auto activeRow = [&scalar](RowId item) {
        return scalar("SELECT id FROM entity_ownerships WHERE item_id=?1 AND to_chapter=0", {item});
    };

    // 造 5 类可修脏行 + 3 类只报告的悬空引用
    expect(ins("INSERT INTO entity_ownerships(owner_id,item_id,from_chapter,to_chapter)"
               " VALUES(?1,?2,5,3)",
               {pA, it1}),
           "造脏：R1 区间倒挂（from=5,to=3）");
    expect(ins("INSERT INTO entity_ownerships(owner_id,item_id,from_chapter,to_chapter)"
               " VALUES(?1,?2,4,0)",
               {loc, it2}),
           "造脏：R2 活跃持有者非 person（location）");
    expect(ins("INSERT INTO entity_ownerships(owner_id,item_id,from_chapter,to_chapter)"
               " VALUES(?1,?2,1,0)",
               {pA, it3}),
           "造脏：R3 旧持有（from=1）");
    expect(ins("INSERT INTO entity_ownerships(owner_id,item_id,from_chapter,to_chapter)"
               " VALUES(?1,?2,6,0)",
               {pB, it3}),
           "造脏：R3 新持有（from=6）");
    expect(ins("INSERT INTO entity_ownerships(owner_id,item_id,from_chapter,to_chapter)"
               " VALUES(999999,?1,1,0)",
               {it1}),
           "造脏：R4 持有者不存在");
    expect(ins("INSERT INTO entity_ownerships(owner_id,item_id,from_chapter,to_chapter)"
               " VALUES(?1,888888,1,0)",
               {pA}),
           "造脏：R5 物品不存在");
    expect(ins("INSERT INTO event_participants(event_id,entity_id,role) VALUES(?1,777777,'actor')",
               {ev}),
           "造脏：R7 参与者悬空");
    expect(ins("INSERT INTO event_details(entity_id,time_label,location_id) VALUES(?1,'夜',888888)",
               {ev}),
           "造脏：R8 事件地点悬空");
    expect(ins("INSERT INTO relations(from_id,to_id,rel_type) VALUES(?1,777777,'ally')", {pA}),
           "造脏：R9 关系端点悬空");

    // 1) 扫描：应当抓到 8 条（5 可修 + 3 只报告），且 fixable=5
    const ConsistencyReport r1 = ScanConsistency(mem);
    expect(r1.errors == 8 && r1.warnings == 0 && r1.fixable == 5,
           fmt::format("扫描抓到 8 条 error / 5 条可修（实得 error={} warn={} fixable={}）",
                       r1.errors, r1.warnings, r1.fixable));
    const auto hasRule = [&r1](std::string_view id) {
        for (const auto& x : r1.issues) {
            if (x.rule == id) {
                return true;
            }
        }
        return false;
    };
    expect(hasRule("R1") && hasRule("R2") && hasRule("R3") && hasRule("R4") && hasRule("R5") &&
               hasRule("R7") && hasRule("R8") && hasRule("R9"),
           "扫描覆盖 R1–R5 + R7–R9");

    // 2) dry_run：算出计划但**一个字节都不写**
    const RowId rowsBefore = scalar("SELECT COUNT(*) FROM entity_ownerships", {});
    const RowId it2ActiveBefore = activeRow(it2);
    const RepairOutcome dry = RepairConsistency(mem, /*dry_run=*/true, {}, "self-check");
    const RowId rowsAfterDry = scalar("SELECT COUNT(*) FROM entity_ownerships", {});
    expect(dry.ok() && dry.dry_run && dry.fixed == 5 && dry.skipped == 3,
           fmt::format("dry_run：计划 5 条、跳过 3 条（实得 fixed={} skipped={} err={}）", dry.fixed,
                       dry.skipped, dry.error));
    expect(rowsAfterDry == rowsBefore && activeRow(it2) == it2ActiveBefore && dry.actions.size() == 5,
           "dry_run：库未被改动（行数/活跃行不变）且给出 5 条动作");

    // 3) apply：修掉 5 条
    const RepairOutcome app = RepairConsistency(mem, /*dry_run=*/false, {}, "self-check");
    expect(app.ok() && !app.dry_run && app.fixed == 5,
           fmt::format("apply：修 5 条（实得 {} err={}）", app.fixed, app.error));
    expect(scalar("SELECT COUNT(*) FROM entity_ownerships", {}) == rowsBefore - 3,
           "apply：R1/R4/R5 三行被删除（6→3）");
    expect(activeRow(it2) == -1, "apply：R2 行已关闭（不再活跃）");
    expect(scalar("SELECT to_chapter FROM entity_ownerships WHERE item_id=?1 AND to_chapter<>0",
                  {it3}) == 6,
           "apply：R3 把 from=1 的旧持有关到 `to_chapter=6`（= 保留者的起始章）");
    expect(scalar("SELECT from_chapter FROM entity_ownerships WHERE item_id=?1 AND to_chapter=0",
                  {it3}) == 6,
           "apply：R3 保留的正是 from=6 那行（活跃）");
    expect(scalar("SELECT COUNT(*) FROM entity_ownerships WHERE item_id=?1 AND to_chapter=0",
                  {it3}) == 1,
           "apply：I5 恢复 —— 物品3 只剩 1 行活跃持有");
    expect(scalar("SELECT COUNT(*) FROM audit_logs WHERE action='consistency_repair'", {}) == 1,
           "apply：写了一条 audit_logs（可回溯）");

    // 4) 复扫：可修问题清零，只剩 3 条"只报告"
    const ConsistencyReport r2 = ScanConsistency(mem);
    expect(r2.errors == 3 && r2.fixable == 0 && r2.warnings == 0,
           fmt::format("复扫：只剩 3 条只报告项（实得 error={} fixable={}）", r2.errors,
                       r2.fixable));

    // 5) 幂等：再修 0 条
    const RepairOutcome again = RepairConsistency(mem, /*dry_run=*/false, {}, "self-check");
    expect(again.ok() && again.fixed == 0 && again.scanned == 0 && again.skipped == 3,
           fmt::format("幂等：再修 0 条（实得 fixed={} scanned={}）", again.fixed, again.scanned));

    // 6) 干净库：无 issue 且 report.ok()
    db::sqlite::Database clean;
    if (clean.Open({.memory = true}) && NovelDb::ApplyCanonicalSchema(clean)) {
        const ConsistencyReport r3 = ScanConsistency(clean);
        expect(r3.issues.empty() && r3.ok(), "干净库：0 条问题且 ok()==true");
    } else {
        expect(false, "干净库：建库失败");
    }

    // 6b) ★ T3b：`entity_names` 主名**镜像**一致 + `force_new` **显式新建**（验收标准 4、5）
    {
        db::sqlite::Database nm;
        if (nm.Open({.memory = true}) && NovelDb::ApplyCanonicalSchema(nm)) {
            const auto cnt = [&nm](std::string_view sql) -> RowId {
                auto st = nm.Prepare(std::string{sql});
                if (!st) {
                    return -1;
                }
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    return st->ColumnInt(0);
                }
                return -1;
            };
            NovelGraph g2(nm);
            const RowId e1 = g2.UpsertEntity({.kind = "person", .name = "镜像甲"}).value_or(0);
            // 归一化同名（多一个空格）⇒ **复用**同一个 id（口径 = `NormalizeEntityName`）
            const RowId e2 = g2.UpsertEntity({.kind = "person", .name = "镜像 甲"}).value_or(0);
            expect(e1 > 0 && e1 == e2, "T3b：归一化同名（空格差异）⇒ **复用**同一个实体");
            expect(cnt("SELECT COUNT(*) FROM entities") == 1, "T3b：复用 ⇒ 库里仍只有 1 行");
            // ★ 显式新建：明知同名也建（"两个李默"是真实的写作需求）
            const RowId e3 = g2.UpsertEntity({.kind = "person", .name = "镜像甲", .force_new = true})
                                 .value_or(0);
            expect(e3 > 0 && e3 != e1, "T3b：`force_new=true` ⇒ **新建**出第二个同名实体");
            expect(cnt("SELECT COUNT(*) FROM entities") == 2, "T3b：两个同名 person 并存");
            // 验收标准 4：名字索引与实体表**镜像一致**（`is_primary=1` 行数 == `entities` 行数）
            expect(cnt("SELECT COUNT(*) FROM entity_names WHERE is_primary=1") ==
                       cnt("SELECT COUNT(*) FROM entities"),
                   "T3b：`entity_names` 主名行数 == `entities` 行数（镜像一致）");
            expect(cnt("SELECT COUNT(*) FROM audit_logs WHERE action='force_new_entity'") == 1,
                   "T3b：显式新建记了 `audit_logs(force_new_entity)`（含 conflict_ids，可回溯）");
        } else {
            expect(false, "T3b：建库失败");
        }
    }

    // 7) ★ T3a：R10 同名重复实体 —— **只报告、永不自动合并**（用户 6 条验收标准的 1–3）
    {
        const bool ok1 = ins("INSERT INTO entities(kind,name,summary,created_chapter) "
                             "VALUES('person','R10同名','第5章那个人',5)",
                             {});
        // ⚠️ 刻意带一个**空格**：走同一份 `NormalizeEntityName()` ⇒ 归一化后同名 ⇒ **必须**被 R10 抓到
        const bool ok2 = ins("INSERT INTO entities(kind,name,summary,created_chapter) "
                             "VALUES('person','R10 同名','第9章那个人',9)",
                             {});
        // 同名但**不同 kind**：合法的共存（`person「灯塔」`/`location「灯塔」`）⇒ **不得**进同一组
        const bool ok3 = ins("INSERT INTO entities(kind,name,summary,created_chapter) "
                             "VALUES('location','R10同名','一处地方',7)",
                             {});
        expect(ok1 && ok2 && ok3, "R10：造 2 个同名 person（其一带空格）+ 1 个同名 location");
        const RowId idA = scalar("SELECT id FROM entities WHERE kind='person' AND name='R10同名'", {});
        const RowId idB = scalar("SELECT id FROM entities WHERE kind='person' AND name='R10 同名'", {});
        const RowId idL = scalar("SELECT id FROM entities WHERE kind='location' AND name='R10同名'", {});
        expect(idA > 0 && idB > 0 && idL > 0, "R10：三行都建立了");

        const ConsistencyReport r4 = ScanConsistency(mem);
        bool found = false;
        bool fixable = true;
        for (const auto& x : r4.issues) {
            if (x.rule != "R10") {
                continue;
            }
            found = true;
            fixable = x.fixable;
            expect(x.detail.find(fmt::format("#{}", idA)) != std::string::npos &&
                       x.detail.find(fmt::format("#{}", idB)) != std::string::npos &&
                       x.detail.find(fmt::format("#{}", idL)) == std::string::npos,
                   "R10：命中**同 kind + 归一化同名**的两行，且**不含**同名的 location（只查同 kind）");
        }
        expect(found && !fixable, "R10：列出该组且 `fixable=false`（只报告，不提供机械修）");

        const RepairOutcome noMerge = RepairConsistency(mem, /*dry_run=*/false, {}, "self-check");
        expect(noMerge.ok() && noMerge.fixed == 0, "R10：`--apply` **一条都不修**（永不自动 merge）");
        expect(scalar("SELECT COUNT(*) FROM entities WHERE kind='person' AND name LIKE 'R10%同名'", {}) ==
                   2,
               "R10：两行**都还在**（既没被合并也没被删除）");
    }

    if (fail == 0) {
        log::Info("S69/T3a repair：自检全过（扫描 10 规则 / 修复 5 规则 / dry_run 只读 / 幂等 / R10 只报不并）");
    }
    return fail == 0;
}

} // namespace shine::novelcore
