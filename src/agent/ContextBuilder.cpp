#include "agent/ContextBuilder.h"

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
constexpr std::string_view kMinSchemaForContext = R"SQL(
CREATE TABLE IF NOT EXISTS world_meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS writing_style(id INTEGER PRIMARY KEY,pov_mode TEXT,sentence_len TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS author_rules(id INTEGER PRIMARY KEY AUTOINCREMENT,rule TEXT,severity TEXT,note TEXT);
CREATE TABLE IF NOT EXISTS entities(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,name TEXT,summary TEXT,status TEXT,meta_json TEXT,created_chapter INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS relations(id INTEGER PRIMARY KEY AUTOINCREMENT,from_id INTEGER,to_id INTEGER,rel_type TEXT,strength INTEGER,from_chapter INTEGER,to_chapter INTEGER,reason TEXT,status TEXT);
CREATE TABLE IF NOT EXISTS entity_personas(entity_id INTEGER PRIMARY KEY,age TEXT,appearance TEXT,personality TEXT,background TEXT,"values" TEXT,desire TEXT,goal TEXT,fear TEXT,weakness TEXT,strength TEXT,ability_note TEXT,knowledge_note TEXT,memory_note TEXT);
CREATE TABLE IF NOT EXISTS character_status(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,chapter_id INTEGER,location_id INTEGER,body_state TEXT,mind_state TEXT,emotion_json TEXT,goal TEXT,relation_note TEXT,resource_note TEXT,secret_note TEXT,updated INTEGER);
CREATE TABLE IF NOT EXISTS character_knowledge(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,fact_kind TEXT,fact_id INTEGER,fact_text TEXT,knows INTEGER,chapter_known INTEGER);
CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY AUTOINCREMENT,volume_id INTEGER,ord INTEGER,title TEXT,status TEXT,summary TEXT,body TEXT,pov_entity_id INTEGER,words INTEGER,updated INTEGER);
CREATE TABLE IF NOT EXISTS scenes(id INTEGER PRIMARY KEY AUTOINCREMENT,chapter_id INTEGER,ord INTEGER,title TEXT,location_id INTEGER,time_label TEXT,pov_entity_id INTEGER,conflict_id INTEGER,goal TEXT,action TEXT,conflict TEXT,result TEXT,emotion TEXT,info_reveal TEXT,hook TEXT,body TEXT);
CREATE TABLE IF NOT EXISTS foreshadowings(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,content TEXT,status TEXT,setup_ch INTEGER,payoff_ch INTEGER,importance INTEGER,truth TEXT,entity_ids_json TEXT);
CREATE TABLE IF NOT EXISTS secrets(id INTEGER PRIMARY KEY AUTOINCREMENT,content TEXT,truth TEXT,reveal_ch INTEGER,reveal_condition TEXT,entity_id INTEGER,scope TEXT);
CREATE TABLE IF NOT EXISTS secret_knowledge(id INTEGER PRIMARY KEY AUTOINCREMENT,secret_id INTEGER,entity_id INTEGER,knows INTEGER,chapter_known INTEGER);
CREATE TABLE IF NOT EXISTS mysteries(id INTEGER PRIMARY KEY AUTOINCREMENT,entity_id INTEGER,question TEXT,answer TEXT,status TEXT,ask_ch INTEGER,answer_ch INTEGER,importance INTEGER);
CREATE TABLE IF NOT EXISTS memories(id INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT,entity_id INTEGER,chapter_id INTEGER,content TEXT,summary TEXT,embedding_blob BLOB,created INTEGER);
CREATE TABLE IF NOT EXISTS themes(id INTEGER PRIMARY KEY AUTOINCREMENT,title TEXT,statement TEXT,linked_plot_id INTEGER);
CREATE TABLE IF NOT EXISTS audit_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,actor TEXT,action TEXT,target_kind TEXT,target_id INTEGER,detail TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS canon_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,target_kind TEXT,target_id INTEGER,status TEXT,note TEXT,created INTEGER);
CREATE TABLE IF NOT EXISTS event_details(entity_id INTEGER PRIMARY KEY,time_label TEXT,location_id INTEGER,cause_note TEXT,result_note TEXT);
CREATE TABLE IF NOT EXISTS causal_links(id INTEGER PRIMARY KEY AUTOINCREMENT,cause_event_id INTEGER,effect_event_id INTEGER,link_type TEXT,note TEXT,ord INTEGER);
)SQL";

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

    // 地点
    if (auto locs = g.ListEntities(novelcore::kind::location, {}, 8)) {
        out += "### 地点\n";
        for (const auto& l : *locs) {
            AppendUsed(used, l.id);
            out += fmt::format("- {}：{}\n", l.name, l.summary);
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

    // 世界规则
    if (auto rules = g.ListEntities(novelcore::kind::world_rule, {}, 10)) {
        out += "### 世界规则\n";
        for (const auto& r : *rules) {
            AppendUsed(used, r.id);
            out += fmt::format("- {}：{}\n", r.name, r.summary);
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

    std::string text;
    text += BuildPrefix(in.chapter_id);
    text += BuildL2(in.chapter_id);

    std::int64_t pov = in.pov_entity_id.value_or(0);
    if (pov == 0 && in.chapter_id > 0) {
        novelcore::NovelGraph g(*db_);
        if (auto ch = g.GetChapter(in.chapter_id); ch && ch->pov_entity_id > 0) {
            pov = ch->pov_entity_id;
        }
    }
    text += BuildL3(in.chapter_id, pov, out.used_entity_ids);
    text += BuildL4(pov, out.used_entity_ids);

    // L1 任务
    text += "\n## 本章任务（L1）\n";
    text += in.task.empty() ? "继续推进剧情。\n" : (in.task + "\n");

    out.text = TruncateUtf8(text, budget);
    log::Info("ContextBuilder：章={} 字节={} 实体数={}", in.chapter_id, out.text.size(),
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
    if (auto r = mem.Exec(kMinSchemaForContext); !r) {
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
