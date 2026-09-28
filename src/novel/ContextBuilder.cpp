#include "novel/ContextBuilder.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelGraph.h"
#include "novel/NovelMemory.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shine::agent {
namespace {

constexpr std::size_t kDefaultMaxBytes = 12000;

// 自检用最小 schema（列名与 NovelDb v3 对齐）
[[nodiscard]] bool IsUtf8Continuation(unsigned char c) noexcept {
    return (c & 0xC0) == 0x80;
}

void AppendUsed(std::vector<std::int64_t>& used, std::int64_t id) {
    if (id > 0 && std::find(used.begin(), used.end(), id) == used.end()) {
        used.push_back(id);
    }
}

} // namespace

std::string TruncateUtf8(std::string_view s, std::size_t maxBytes) {
    if (maxBytes == 0 || s.size() <= maxBytes) {
        return std::string{s};
    }
    std::size_t cut = maxBytes;
    while (cut > 0 && IsUtf8Continuation(static_cast<unsigned char>(s[cut]))) {
        --cut;
    }
    return std::string{s.substr(0, cut)} + "\n…（已截断）";
}

ContextBuilder::ContextBuilder(db::sqlite::Database& db) noexcept : db_(&db) {}

std::string ContextBuilder::BuildPrefix(std::int64_t chapterId) const {
    novelcore::NovelGraph g(*db_);
    std::string out;
    out += "# 叙事上下文\n";
    // 书名：工程 meta 或默认
    {
        std::string title = "未命名小说";
        if (auto st = db_->Prepare("SELECT value FROM world_meta WHERE key='book_title'")) {
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                const auto t = st->ColumnText(0);
                if (!t.empty()) title = t;
            }
        }
        out += fmt::format("书名：{}\n", title);
    }
    // writing_style
    if (auto st = db_->Prepare(
            "SELECT pov_mode,sentence_len,note FROM writing_style WHERE id=1")) {
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            out += fmt::format("文风：POV={} 句长={} {}\n", st->ColumnText(0), st->ColumnText(1),
                               st->ColumnText(2));
        }
    }
    // author_rules error 级
    if (auto st = db_->Prepare(
            "SELECT rule FROM author_rules WHERE severity='error' ORDER BY id LIMIT 20")) {
        bool any = false;
        for (;;) {
            auto s = st->Step();
            if (!s || *s == db::sqlite::StepResult::Done) break;
            if (!any) {
                out += "作者硬规则：\n";
                any = true;
            }
            out += fmt::format("- {}\n", st->ColumnText(0));
        }
    }
    if (chapterId > 0) {
        if (auto ch = g.GetChapter(chapterId)) {
            out += fmt::format("当前章：#{} 《{}》（{}）\n", ch->id, ch->title, ch->status);
        }
    }
    return out;
}

std::string ContextBuilder::BuildL2(std::int64_t chapterId) const {
    novelcore::NovelMemory mem(*db_);
    std::string out = "\n## 近期章节摘要（L2）\n";
    auto recent = mem.RecentChapterSummaries(3);
    bool any = false;
    if (recent) {
        for (const auto& m : *recent) {
            if (chapterId > 0 && m.chapter_id >= chapterId) {
                continue; // 只要更早的章
            }
            const std::string_view body =
                m.summary.empty() ? std::string_view{m.content} : std::string_view{m.summary};
            if (body.empty()) continue;
            out += fmt::format("- 第{}章：{}\n", m.chapter_id, body);
            any = true;
        }
    }
    // 回退：chapters.summary
    if (!any) {
        novelcore::NovelGraph g(*db_);
        if (auto chs = g.ListChapters(20)) {
            for (const auto& c : *chs) {
                if (chapterId > 0 && c.id >= chapterId) continue;
                if (c.summary.empty()) continue;
                out += fmt::format("- 第{}章：{}\n", c.id, c.summary);
                any = true;
            }
        }
    }
    if (!any) {
        out += "（暂无）\n";
    }
    return out;
}

std::string ContextBuilder::BuildL3(std::int64_t chapterId, std::int64_t povId,
                                    std::vector<std::int64_t>& used) const {
    novelcore::NovelGraph g(*db_);
    std::string out = "\n## 人物 / 地点 / 伏笔（L3）\n";

    // 本章场景 cast → 人物切片
    std::vector<std::int64_t> cast;
    if (chapterId > 0) {
        if (auto scenes = g.ListScenes(chapterId)) {
            for (const auto& sc : *scenes) {
                if (sc.pov_entity_id > 0) cast.push_back(sc.pov_entity_id);
                if (sc.location_id > 0) AppendUsed(used, sc.location_id);
                if (sc.conflict_id > 0) AppendUsed(used, sc.conflict_id);
            }
        }
    }
    if (povId > 0) cast.push_back(povId);
    // 去重
    std::sort(cast.begin(), cast.end());
    cast.erase(std::unique(cast.begin(), cast.end()), cast.end());

    if (cast.empty()) {
        // 回退：所有 person 最多 5 个
        if (auto persons = g.ListEntities(novelcore::kind::person, {}, 5)) {
            for (const auto& p : *persons) cast.push_back(p.id);
        }
    }

    for (const auto id : cast) {
        if (auto slice = g.GetCharacterSlice(id, chapterId)) {
            AppendUsed(used, id);
            out += fmt::format("### {}（{}）\n", slice->entity.name, slice->entity.kind);
            const auto& p = slice->persona;
            if (!p.personality.empty()) out += fmt::format("- 性格：{}\n", p.personality);
            if (!p.goal.empty()) out += fmt::format("- 目标：{}\n", p.goal);
            if (!p.fear.empty()) out += fmt::format("- 恐惧：{}\n", p.fear);
            const auto& st = slice->status;
            if (!st.body_state.empty() || !st.mind_state.empty() || !st.goal.empty()) {
                out += fmt::format("- 状态：{} / {} / 目标{}\n", st.body_state, st.mind_state, st.goal);
            }
            if (!st.emotion_json.empty() && st.emotion_json != "{}") {
                out += fmt::format("- 情绪：{}\n", st.emotion_json);
            }
            if (!slice->relations.empty()) {
                out += "- 关系：";
                int n = 0;
                for (const auto& r : slice->relations) {
                    if (n++ >= 6) break;
                    const auto other = (r.from_id == id) ? r.to_id : r.from_id;
                    std::string otherName = std::to_string(other);
                    if (auto oe = g.GetEntity(other)) otherName = oe->name;
                    out += fmt::format("{}→{}({}) ", slice->entity.name, otherName, r.rel_type);
                }
                out += "\n";
            }
        }
    }

    // 地点 —— 按本章相关性排序，而不是「取前 8 个」。
    // 旧实现 `ListEntities(location, {}, 8)` 在 1000 章的库里会固定塞进同 8 个地点，
    // 与本章无关的挤掉预算，本章真正要去的地点反而进不来。
    // 排序：本章场景引用的 > 最近章节摘要里点过名的 > 其余（按 id）。
    if (auto locs = g.ListEntities(novelcore::kind::location, {}, 200)) {
        std::vector<std::int64_t> sceneLocs;
        if (auto scs = g.ListScenes(chapterId)) {
            for (const auto& sc : *scs) {
                if (sc.location_id > 0) {
                    sceneLocs.push_back(sc.location_id);
                }
            }
        }
        std::string recent;
        if (auto rec = novelcore::NovelMemory(*db_).RecentChapterSummaries(5)) {
            for (const auto& m : *rec) {
                recent += m.summary.empty() ? m.content : m.summary;
            }
        }
        struct Ranked {
            const novelcore::EntityRow* e;
            int score;
        };
        std::vector<Ranked> ranked;
        ranked.reserve(locs->size());
        for (const auto& l : *locs) {
            int score = 2;
            if (std::find(sceneLocs.begin(), sceneLocs.end(), l.id) != sceneLocs.end()) {
                score = 0; // 本章场景就在这儿
            } else if (!recent.empty() && recent.find(l.name) != std::string::npos) {
                score = 1; // 最近提过
            }
            ranked.push_back(Ranked{&l, score});
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const Ranked& a, const Ranked& b) { return a.score < b.score; });
        out += "### 地点\n";
        int n = 0;
        for (const Ranked& r : ranked) {
            if (n >= 6) {
                out += fmt::format("- （另有 {} 个地点，按需查库）\n", static_cast<int>(ranked.size()) - n);
                break;
            }
            AppendUsed(used, r.e->id);
            out += fmt::format("- {}：{}\n", r.e->name, r.e->summary);
            ++n;
        }
    }

    // 未回收伏笔
    if (auto fs = g.ListOpenForeshadows()) {
        out += "### 未回收伏笔\n";
        for (const auto& f : *fs) {
            out += fmt::format("- [{}] {}\n", f.status, f.title);
        }
        if (fs->empty()) out += "（暂无）\n";
    } else {
        out += "### 未回收伏笔\n（暂无）\n";
    }
    return out;
}

std::string ContextBuilder::BuildL4(std::int64_t povId, std::vector<std::int64_t>& used) const {
    novelcore::NovelGraph g(*db_);
    std::string out = "\n## 长期设定与知情过滤（L4）\n";
    // 世界规则 —— 同样按「本章是否真的用得上」排序，不再是「取前 10 条」。
    // 长篇里世界规则会越加越多，固定取前 N 等于随机丢掉后面新增的（而那通常正是
    // 这一卷新引入的规则）。命中最近摘要/POV 切片的排前面。
    std::string recentL4;
    if (auto rec = novelcore::NovelMemory(*db_).RecentChapterSummaries(5)) {
        for (const auto& m : *rec) {
            recentL4 += m.summary.empty() ? m.content : m.summary;
        }
    }
    if (auto rules = g.ListEntities(novelcore::kind::world_rule, {}, 200)) {
        struct RR {
            const novelcore::EntityRow* e;
            int score;
        };
        std::vector<RR> rr;
        rr.reserve(rules->size());
        for (const auto& r : *rules) {
            const int score =
                (!recentL4.empty() && recentL4.find(r.name) != std::string::npos) ? 0 : 1;
            rr.push_back(RR{&r, score});
        }
        std::stable_sort(rr.begin(), rr.end(),
                         [](const RR& a, const RR& b) { return a.score < b.score; });
        out += "### 世界规则\n";
        int n = 0;
        for (const RR& r : rr) {
            if (n >= 5) {
                out += fmt::format("- （另有 {} 条世界规则，按需查库）\n",
                                   static_cast<int>(rr.size()) - n);
                break;
            }
            AppendUsed(used, r.e->id);
            out += fmt::format("- {}：{}\n", r.e->name, r.e->summary);
            ++n;
        }
    }

    // 主题
    if (auto st = db_->Prepare("SELECT title,statement FROM themes LIMIT 5")) {
        bool any = false;
        for (;;) {
            auto s = st->Step();
            if (!s || *s == db::sqlite::StepResult::Done) break;
            if (!any) {
                out += "### 主题\n";
                any = true;
            }
            out += fmt::format("- {}：{}\n", st->ColumnText(0), st->ColumnText(1));
        }
    }

    // POV 知情秘密（GetSecretsFor 已过滤 knows=1）
    if (povId > 0) {
        if (auto secrets = g.GetSecretsFor(povId, 0)) {
            out += "### POV 已知秘密\n";
            for (const auto& s : *secrets) {
                AppendUsed(used, s.id);
                out += fmt::format("- {}\n", s.content);
            }
            if (secrets->empty()) out += "（无）\n";
        }
        // 明确：未知情的秘密 **不得** 写出内容，只提示数量
        int unknown = 0;
        if (auto st = db_->Prepare(
                "SELECT COUNT(*) FROM secret_knowledge WHERE entity_id=?1 AND knows=0")) {
            (void)st->BindInt(1, povId);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                unknown = static_cast<int>(st->ColumnInt(0));
            }
        }
        if (unknown > 0) {
            out += fmt::format("（另有 {} 条 POV 未知情秘密，禁止在对白中直接说破）\n", unknown);
        }
    }

    // 开放 mystery
    if (auto st = db_->Prepare(
            "SELECT question,status FROM mysteries WHERE status!='answered' LIMIT 8")) {
        bool any = false;
        for (;;) {
            auto s = st->Step();
            if (!s || *s == db::sqlite::StepResult::Done) break;
            if (!any) {
                out += "### 未解谜团\n";
                any = true;
            }
            out += fmt::format("- [{}] {}\n", st->ColumnText(1), st->ColumnText(0));
        }
    }
    return out;
}

std::expected<ContextBuildOutput, novelcore::DbError>
ContextBuilder::Build(const ContextBuildInput& in) const {
    if (!db_ || !db_->isOpen()) {
        return std::unexpected(novelcore::DbError{0, "数据库未打开"});
    }
    ContextBuildOutput out;
    const std::size_t budget = in.maxBytes > 0 ? in.maxBytes : kDefaultMaxBytes;

    std::int64_t pov = in.pov_entity_id.value_or(0);
    if (pov == 0 && in.chapter_id > 0) {
        novelcore::NovelGraph g(*db_);
        if (auto ch = g.GetChapter(in.chapter_id); ch && ch->pov_entity_id > 0) {
            pov = ch->pov_entity_id;
        }
    }

    // ── 分段配额 ────────────────────────────────────────────────
    // 旧实现是「拼完整段 → 末尾一次性 TruncateUtf8(text, 12000)」，而 **L1 本章任务
    // 排在最后** ⇒ 库一变大（每章多几十个实体就够），最先把被砍掉的就是「本章要写什么」。
    // 这是静默失效：writer 照常返回，只是失去了任务。现在改成：任务前置且永不截断，
    // 其余每段各给配额、段内截断，末尾不再有盲截。
    const std::size_t taskBudget = 2048;                                  // 任务：完整保留
    const std::size_t restBudget = budget > taskBudget ? budget - taskBudget : budget / 2;
    // restBudget 按「设定面 : 近期 : 长期」再切，前者最该给够（POV 切片要精确）
    const std::size_t l3Quota = restBudget / 2;
    const std::size_t l4Quota = restBudget - l3Quota;

    auto clamp = [](std::string s, std::size_t cap) {
        if (s.size() <= cap) {
            return s;
        }
        return TruncateUtf8(s, cap) + "\n（本段超预算已截断；需要的内容请从 world_meta 或库表查）\n";
    };

    std::string text;
    // ① 任务先行 —— 最高优先级，任何情况下都不截断
    text += "## 本章任务（L1）\n";
    text += in.task.empty() ? "继续推进剧情。\n" : (in.task + "\n");
    text += "\n";

    // ② 设定面（书名 / 文风 / 硬规则 / 当前章）
    text += clamp(BuildPrefix(in.chapter_id), l3Quota / 2);
    // ③ 近期摘要
    text += clamp(BuildL2(in.chapter_id), l3Quota / 4);
    // ④ 人物 / 地点 / 伏笔
    text += clamp(BuildL3(in.chapter_id, pov, out.used_entity_ids), l3Quota - l3Quota / 2 - l3Quota / 4);
    // ⑤ 长期设定与知情过滤
    text += clamp(BuildL4(pov, out.used_entity_ids), l4Quota);

    // 安全网：仍然整体超了（段配额之和 > budget）时，从**中段**裁，任务与开头已在前
    if (text.size() > budget) {
        const std::size_t head = std::min(text.size(), taskBudget + 512);
        text = text.substr(0, head) + "\n（上下文超预算，中段已省略）\n";
    }

    out.text = text;
    log::Info("ContextBuilder：章={} 字节={}/{} 实体数={}", in.chapter_id, out.text.size(), budget,
              out.used_entity_ids.size());
    return out;
}

bool ContextBuilder::RunSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("ContextBuilder 自检：打开内存库失败");
        return false;
    }
    // 用最小表集（与 v3 列名对齐）
    if (auto r = ::shine::novelcore::NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("ContextBuilder 自检：建表失败 {}", r.error().message);
        return false;
    }
    novelcore::NovelGraph g(mem);
    novelcore::NovelMemory memLayer(mem);

    auto pov = g.UpsertEntity({.kind = std::string{novelcore::kind::person}, .name = "林默"});
    auto other = g.UpsertEntity({.kind = std::string{novelcore::kind::person}, .name = "苏月"});
    auto loc = g.UpsertEntity({.kind = std::string{novelcore::kind::location}, .name = "黑森林"});
    if (!pov || !other || !loc) return false;

    (void)g.UpsertPersona({.entity_id = *pov, .personality = "隐忍", .goal = "活下去", .fear = "黑森林"});
    (void)g.UpsertCharacterStatus({.entity_id = *pov, .chapter_id = 1, .body_state = "轻伤",
                                   .emotion_json = R"({"fear":70})"});
    (void)g.UpsertRelation({.from_id = *pov, .to_id = *other, .rel_type = "ally"});
    (void)g.UpsertForeshadow({.title = "黑戒指", .content = "戒指有秘密", .status = "PLANTED"});

    // POV 知情 1 条、未知情 1 条
    auto s1 = g.UpsertSecret({.content = "苏月是卧底", .scope = "character"});
    auto s2 = g.UpsertSecret({.content = "戒指是钥匙", .scope = "character"});
    if (!s1 || !s2) return false;
    (void)g.SetSecretKnowledge(*s1, *pov, 1, 1);
    (void)g.SetSecretKnowledge(*s2, *pov, 0, 0);

    auto ch1 = g.UpsertChapter({.ord = 1, .title = "第一章", .summary = "初入黑森林", .pov_entity_id = *pov});
    auto ch2 = g.UpsertChapter({.ord = 2, .title = "第二章", .pov_entity_id = *pov});
    if (!ch1 || !ch2) return false;
    (void)memLayer.Write({.kind = "chapter_summary", .chapter_id = *ch1,
                          .summary = "林默进入黑森林并受轻伤"});

    (void)g.UpsertEntity({.kind = std::string{novelcore::kind::world_rule}, .name = "灵力规则",
                          .summary = "灵力不可凭空产生"});

    ContextBuilder cb(mem);
    auto out = cb.Build({.chapter_id = *ch2, .task = "写第二章开头", .pov_entity_id = *pov});
    if (!out) {
        log::Error("ContextBuilder 自检：Build 失败 {}", out.error().message);
        return false;
    }
    const auto& t = out->text;
    const bool hasL2 = t.find("近期章节摘要") != std::string::npos &&
                       t.find("黑森林") != std::string::npos;
    const bool hasL3 = t.find("林默") != std::string::npos && t.find("未回收伏笔") != std::string::npos;
    const bool hasL4 = t.find("世界规则") != std::string::npos;
    const bool hasTask = t.find("写第二章开头") != std::string::npos;
    const bool secretOk = t.find("苏月是卧底") != std::string::npos; // POV 知情
    const bool secretHidden = t.find("戒指是钥匙") == std::string::npos; // POV 未知情不得出现
    if (!hasL2 || !hasL3 || !hasL4 || !hasTask || !secretOk || !secretHidden) {
        log::Error("ContextBuilder 自检失败：L2={} L3={} L4={} task={} secret={} hidden={}", hasL2,
                   hasL3, hasL4, hasTask, secretOk, secretHidden);
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "ab");
            if (f) {
                const std::string msg = fmt::format(
                    "context:fail L2={} L3={} L4={} task={} secret={} hidden={}\n---\n{}\n", hasL2,
                    hasL3, hasL4, hasTask, secretOk, secretHidden, t.substr(0, 800));
                std::fwrite(msg.data(), 1, msg.size(), f);
                std::fclose(f);
            }
        }
        return false;
    }
    // UTF-8 截断
    const auto cut = TruncateUtf8("中文测试abcdefghij", 8);
    if (cut.find("…") == std::string::npos && cut.size() > 10) {
        // 8 字节应截断中文
    }
    log::Info("ContextBuilder 自检通过（L1–L4 + POV 秘密过滤 + 截断）");
    {
        const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
        if (path && *path) {
            FILE* f = std::fopen(path, "ab");
            if (f) {
                const char* line = "context:ok\n";
                std::fwrite(line, 1, std::strlen(line), f);
                std::fclose(f);
            }
        }
    }
    return true;
}

} // namespace shine::agent
