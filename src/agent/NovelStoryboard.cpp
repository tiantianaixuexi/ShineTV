#include "agent/NovelStoryboard.h"

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
#include <map>
#include <set>
#include <utility>

namespace shine::agent {
namespace {

// V1–V8 合并推演的内置指令。⚠️ 专项 Prompt 的完整版见 `13`；这里是最小可用版
// （字段名与 `02` §2.7 一致，模型按此产出，解析端也只认这些名字）。
constexpr std::string_view kStoryboardInstructions = R"(
你是分镜师。为给定的每一场，产出该场的叙事分镜 NarrativeShot[]。
只输出 JSON：{"shots":[ ... ]}，不要解释。
每个 shot 的字段（名字必须一致）：
  scene_ord(int)      该镜属于哪一场（用上面给的第几场）
  ord(int)            场内序号，从 1 递增
  duration(number)    秒，一位小数，> 0
  start_state(object) {characters:[{entity_id(int),position,facing,pose,hands,clothing,injury}],
                       props:[{entity_id(int),holder_id(int),state}],
                       lighting,environment,camera_position}
  end_state(object)   同 start_state；**下一镜的 start_state 必须等于本镜的 end_state**
  timeline(array)     [{begin_s,end_s}]，逐个不重叠、覆盖 [0,duration]
  prompt_text(string) 该镜的叙事描述（不是生成 prompt）
  negative_text(string)
  dialogue(array)     [{speaker_id(int),text}]
  transition(string)  cut|match_cut|dissolve|fade|whip_pan|camera_motion
  performance(object) 表演层（expression/eyes/breathing/posture/body_movement/hand_movement/
                      head_movement/weight_shift/walking/stopping/reaction/pause）
  spatial(object)     站位层（movement_path 等）
  camera(object)      镜头层（景别/机位/运动）
  audio(object)       声音设计
)" ;

[[nodiscard]] std::string JsonText(yyjson_val* v) {
    if (v == nullptr) {
        return {};
    }
    const char* t = yyjson_val_write(v, 0, nullptr);
    if (t == nullptr) {
        return {};
    }
    std::string out{t};
    free(const_cast<char*>(t));
    return out;
}

// 镜 → 该镜起点出场角色的 entity_id 集合（`06` K22 的受检对象就是 `character_ids_json`）
[[nodiscard]] std::string CharacterIdsJson(yyjson_val* startState) {
    std::set<std::int64_t> ids;
    if (yyjson_is_obj(startState)) {
        yyjson_val* chars = yyjson_obj_get(startState, "characters");
        if (yyjson_is_arr(chars)) {
            std::size_t i = 0;
            std::size_t max = 0;
            yyjson_val* c = nullptr;
            yyjson_arr_foreach(chars, i, max, c) {
                yyjson_val* id = yyjson_obj_get(c, "entity_id");
                if (yyjson_is_int(id) && yyjson_get_sint(id) > 0) {
                    ids.insert(yyjson_get_sint(id));
                }
            }
        }
    }
    std::string out = "[";
    bool first = true;
    for (const std::int64_t id : ids) {
        out += fmt::format("{}{}", first ? "" : ",", id);
        first = false;
    }
    return out + "]";
}

// `timeline` 数组 → `{"duration_s":N,"beats":[{begin_s,end_s}]}`（`02` §2.9，K24 的读法）
[[nodiscard]] std::string TimelineJson(yyjson_val* timeline, double duration) {
    if (!yyjson_is_arr(timeline)) {
        return "{}";
    }
    std::string beats = "[";
    bool first = true;
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* b = nullptr;
    yyjson_arr_foreach(timeline, i, max, b) {
        yyjson_val* begin = yyjson_obj_get(b, "begin_s");
        yyjson_val* end = yyjson_obj_get(b, "end_s");
        if (!yyjson_is_num(begin) || !yyjson_is_num(end)) {
            continue;
        }
        beats += fmt::format("{}{{\"begin_s\":{:.3f},\"end_s\":{:.3f}}}", first ? "" : ", ",
                             yyjson_get_num(begin), yyjson_get_num(end));
        first = false;
    }
    beats += "]";
    if (first) {
        return "{}"; // 一个合法 beat 都没有 → 落 `{}`（K24 会把它当"未产出"而不是"合规"）
    }
    return fmt::format("{{\"duration_s\":{:.3f},\"beats\":{}}}", duration, beats);
}

[[nodiscard]] std::filesystem::path StoryboardDir(const std::filesystem::path& project_dir, int ord) {
    return project_dir / "work" / fmt::format("ch{:03}", ord);
}

} // namespace

std::string StoryboardOutcome::Describe() const {
    if (!ok) {
        return "分镜产出失败：" + error;
    }
    std::string s = fmt::format("分镜已落库：{} 场 / {} 镜（LLM {} 次）", scenes_covered, shots_written,
                                llm_calls);
    if (!warnings.empty()) {
        s += fmt::format("；{} 条提示：{}", warnings.size(), warnings.front());
    }
    return s;
}

std::expected<StoryboardOutcome, AgentError>
GenerateStoryboard(::shine::db::sqlite::Database& db, const LlmCallFn& call,
                   const StoryboardRequest& req) {
    if (!db.isOpen()) {
        return std::unexpected(AgentError{"no_db", "数据库未打开"});
    }
    if (req.chapter_id <= 0) {
        return std::unexpected(AgentError{"bad_args", "需要 chapter_id"});
    }
    if (!call) {
        return std::unexpected(AgentError{"no_llm", "未配置 LLM 回调"});
    }
    novelcore::NovelGraph g(db);
    auto ch = g.GetChapter(req.chapter_id);
    if (!ch) {
        return std::unexpected(AgentError{"not_found", ch.error().message});
    }
    auto scenes = g.ListScenes(req.chapter_id);
    if (!scenes || scenes->empty()) {
        return std::unexpected(AgentError{
            "no_scenes", "该章没有 scenes —— V9 的输入是已提交的场景（正文链的 SCENE_PLAN，`03` T6）"});
    }

    StoryboardOutcome out;
    // —— 组装 user 消息（场景清单 + 任务）——
    std::string user = fmt::format("【本章】第 {} 章《{}》\n【场景清单】\n", ch->ord, ch->title);
    for (const novelcore::SceneRow& s : *scenes) {
        user += fmt::format("- scene_ord={} 《{}》\n", s.ord, s.title);
    }
    user += "\n【任务】按上面的场景清单逐场产出 NarrativeShot[]。";
    if (!req.extra_hint.empty()) {
        user += "\n【额外要求】" + req.extra_hint;
    }

    // V1–V8 合并为一次推演：视觉链在 `09` §2.4 里是"中"档 → 这里用 `Planner` 角色（中档）
    ++out.llm_calls;
    auto r = call(LlmRole::Planner, std::string{kStoryboardInstructions}, user);
    if (!r) {
        return std::unexpected(AgentError{r.error().code, r.error().message});
    }
    const std::string text = ExtractOutputText(*r);
    if (util::Trim(text).empty()) {
        return std::unexpected(AgentError{"contract", "Storyboard 未返回内容"});
    }

    // —— 完整契约先落盘（`transition`/`performance`/`spatial`/`camera`/`audio` 这些**无专列**
    //    的字段靠它保真；V10 产生成 Prompt 时从这里读）——
    const std::filesystem::path dir = StoryboardDir(req.project_dir, ch->ord);
    if (!req.project_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!util::WriteFileBytes(dir / "storyboard.json", text)) {
            out.warnings.push_back("完整契约落盘失败（work/ 非权威，不阻断）");
        }
    }

    // —— 解析 ——
    yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
    if (doc == nullptr) {
        return std::unexpected(AgentError{"contract", "Storyboard 输出不是合法 JSON"});
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* shots = yyjson_is_obj(root) ? yyjson_obj_get(root, "shots") : nullptr;
    if (!yyjson_is_arr(shots)) {
        yyjson_doc_free(doc);
        return std::unexpected(AgentError{"contract", "Storyboard 输出缺少 shots 数组"});
    }

    // scene_ord → scene_id（`11` §2.5 的桥也按 ord 走，LLM 给 ord 比给数据库 id 稳）
    std::map<int, std::int64_t> sceneByOrd;
    for (const novelcore::SceneRow& s : *scenes) {
        sceneByOrd[s.ord] = s.id;
    }
    // 幂等：已有的 (scene_id, ord) → shot id（`UpsertShot` 靠 id 判更新，否则会插重复行）
    std::map<std::pair<std::int64_t, int>, std::int64_t> existing;
    novelcore::NovelVisual visual(db);
    if (auto list = visual.ListShotsByChapter(req.chapter_id); list) {
        for (const novelcore::ShotRow& s : *list) {
            existing[{s.scene_id, s.ord}] = s.id;
        }
    }

    std::set<std::int64_t> covered;
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* shot = nullptr;
    yyjson_arr_foreach(shots, i, max, shot) {
        if (!yyjson_is_obj(shot)) {
            continue;
        }
        yyjson_val* sceneOrdV = yyjson_obj_get(shot, "scene_ord");
        const int sceneOrd = yyjson_is_int(sceneOrdV) ? static_cast<int>(yyjson_get_sint(sceneOrdV)) : 0;
        auto it = sceneByOrd.find(sceneOrd);
        if (it == sceneByOrd.end()) {
            out.warnings.push_back(fmt::format("第 {} 个 shot 的 scene_ord={} 不在本章场景里 → 丢弃",
                                               i, sceneOrd));
            continue;
        }
        const std::int64_t sceneId = it->second;
        yyjson_val* ordV = yyjson_obj_get(shot, "ord");
        const int ord = yyjson_is_int(ordV) ? static_cast<int>(yyjson_get_sint(ordV)) : 0;
        if (ord <= 0) {
            out.warnings.push_back(fmt::format("场景 {} 有 shot 缺 ord → 丢弃", sceneOrd));
            continue;
        }
        yyjson_val* durV = yyjson_obj_get(shot, "duration");
        const double duration = yyjson_is_num(durV) ? yyjson_get_num(durV) : 0.0;
        if (!(duration > 0.0)) {
            out.warnings.push_back(
                fmt::format("场景 {} 第 {} 镜 duration={} → 丢弃（契约要求 > 0）", sceneOrd, ord,
                            duration));
            continue;
        }
        yyjson_val* startState = yyjson_obj_get(shot, "start_state");
        yyjson_val* endState = yyjson_obj_get(shot, "end_state");

        novelcore::ShotRow row;
        row.id = existing[{sceneId, ord}]; // 0 = 新增；> 0 = 覆盖同场同序号的那一镜
        row.scene_id = sceneId;
        row.ord = ord;
        row.duration_note = fmt::format("{:.1f}s", duration);
        row.start_state_json = yyjson_is_obj(startState) ? JsonText(startState) : "{}";
        row.end_state_json = yyjson_is_obj(endState) ? JsonText(endState) : "{}";
        row.timeline_json = TimelineJson(yyjson_obj_get(shot, "timeline"), duration);
        row.character_ids_json = CharacterIdsJson(startState);
        yyjson_val* pt = yyjson_obj_get(shot, "prompt_text");
        if (yyjson_is_str(pt)) {
            row.prompt_text = yyjson_get_str(pt);
        }
        yyjson_val* nt = yyjson_obj_get(shot, "negative_text");
        if (yyjson_is_str(nt)) {
            row.negative_text = yyjson_get_str(nt);
        }
        yyjson_val* dlg = yyjson_obj_get(shot, "dialogue");
        if (yyjson_is_arr(dlg) && yyjson_arr_size(dlg) > 0) {
            row.dialogue = JsonText(dlg);
        }
        auto wr = visual.UpsertShot(row);
        if (!wr) {
            out.warnings.push_back(fmt::format("场景 {} 第 {} 镜落库失败：{}", sceneOrd, ord,
                                               wr.error().message));
            continue;
        }
        ++out.shots_written;
        covered.insert(sceneId);
    }
    yyjson_doc_free(doc);

    // 无专列的契约字段：**显式说明**它们只存盘（不静默丢）
    out.warnings.push_back(
        "transition/performance/spatial/camera/audio 无对应列 → 只存 work/ch<NNN>/storyboard.json"
        "（V10 产 Prompt 时从那读）");
    out.scenes_covered = static_cast<int>(covered.size());
    out.ok = out.shots_written > 0;
    if (!out.ok) {
        return std::unexpected(
            AgentError{"contract", fmt::format("没有落库任何镜（{} 条提示）",
                                               out.warnings.empty() ? 0 : out.warnings.size())});
    }
    log::Info("分镜产出：章={} 场={} 镜={}（LLM {} 次）", req.chapter_id, out.scenes_covered,
              out.shots_written, out.llm_calls);
    return out;
}

// ═══════════════════════ 自检 ═══════════════════════
bool RunStoryboardSelfCheck() {
    ::shine::db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("分镜自检：内存库打开失败 {}", r.error().message);
        return false;
    }
    if (auto r = novelcore::NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("分镜自检：建表失败 {}", r.error().message);
        return false;
    }
    int fails = 0;
    const auto expect = [&fails](bool cond, std::string_view what) {
        if (!cond) {
            ++fails;
            log::Error("分镜自检 FAIL：{}", what);
        }
    };
    novelcore::NovelGraph g(mem);
    auto pov = g.UpsertEntity({.kind = std::string{novelcore::kind::person}, .name = "自检角色"});
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章", .pov_entity_id = pov.value_or(0)});
    expect(ch.has_value(), "建章");
    auto sc = g.UpsertScene({.chapter_id = ch.value_or(0), .ord = 1, .title = "第一场"});
    expect(sc.has_value(), "建场");

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "shine_sb_check";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    int calls = 0;
    // 两镜：第 2 镜的 start_state 等于第 1 镜的 end_state（契约 I7 —— 我们只保证落对，判定归 K09）
    const std::string payload = fmt::format(
        R"({{"shots":[
  {{"scene_ord":1,"ord":1,"duration":3.0,
    "start_state":{{"characters":[{{"entity_id":{},"position":"门口","facing":"内","pose":"站","hands":"空","clothing":"外衣","injury":""}}],"props":[],"lighting":"夜","environment":"库房","camera_position":"门外"}},
    "end_state":{{"characters":[{{"entity_id":{},"position":"货架前","facing":"内","pose":"蹲","hands":"箱","clothing":"外衣","injury":"左手擦伤"}}],"props":[],"lighting":"夜","environment":"库房","camera_position":"货架侧"}},
    "timeline":[{{"begin_s":0.0,"end_s":1.5}},{{"begin_s":1.5,"end_s":3.0}}],
    "prompt_text":"他推门进来","negative_text":"模糊","dialogue":[{{"speaker_id":{},"text":"谁在？"}}],
    "transition":"cut","performance":{{"expression":"警惕"}},"spatial":{{}},"camera":{{}},"audio":{{}}}},
  {{"scene_ord":1,"ord":2,"duration":2.5,
    "start_state":{{"characters":[{{"entity_id":{},"position":"货架前","facing":"内","pose":"蹲","hands":"箱","clothing":"外衣","injury":"左手擦伤"}}],"props":[],"lighting":"夜","environment":"库房","camera_position":"货架侧"}},
    "end_state":{{"characters":[{{"entity_id":{},"position":"门口","facing":"外","pose":"跑","hands":"箱","clothing":"外衣","injury":"左手擦伤"}}],"props":[],"lighting":"夜","environment":"雨夜街道","camera_position":"门外"}},
    "timeline":[{{"begin_s":0.0,"end_s":2.5}}],
    "prompt_text":"他抓起箱子冲出去","negative_text":"","dialogue":[],
    "transition":"whip_pan","performance":{{}},"spatial":{{}},"camera":{{}},"audio":{{}}}}
]}})",
        pov.value_or(0), pov.value_or(0), pov.value_or(0), pov.value_or(0), pov.value_or(0));
    // 用 yyjson 包一层（`ExtractOutputText` 认的是"`output_text` 是字符串"这个形状），
    // 避免手工转义把契约 JSON 写坏
    std::string wrapped;
    {
        yyjson_mut_doc* d = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* o = yyjson_mut_obj(d);
        yyjson_mut_doc_set_root(d, o);
        yyjson_mut_obj_add_strcpy(d, o, "output_text", payload.c_str());
        const char* t = yyjson_mut_write(d, 0, nullptr);
        if (t != nullptr) {
            wrapped = t;
            free(const_cast<char*>(t));
        }
        yyjson_mut_doc_free(d);
    }
    LlmCallFn mock = [&](LlmRole role, std::string_view, std::string_view)
        -> std::expected<std::string, AgentError> {
        ++calls;
        expect(role == LlmRole::Planner, "视觉链走中档角色（Planner）");
        return wrapped;
    };

    auto out = GenerateStoryboard(
        mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir, .extra_hint = "多用手持"});
    expect(out.has_value(), fmt::format("分镜应产出成功：{}", out.has_value() ? "" : out.error().message));
    if (out) {
        expect(out->shots_written == 2, "两镜都应落库");
        expect(out->scenes_covered == 1, "覆盖 1 场");
        expect(!out->warnings.empty(), "无专列字段要显式给出提示（不静默丢）");
    }
    novelcore::NovelVisual visual(mem);
    auto list = visual.ListShotsByChapter(ch.value_or(0));
    expect(list.has_value() && list->size() == 2, "shots 表应有 2 行");
    if (list && list->size() == 2) {
        const novelcore::ShotRow& s1 = (*list)[0];
        expect(s1.ord == 1 && s1.start_state_json.find("\"lighting\":\"夜\"") != std::string::npos,
               "第 1 镜的 start_state 落进了 start_state_json");
        expect(s1.character_ids_json.find("[") == 0 &&
                   s1.character_ids_json.find(std::to_string(pov.value_or(0))) != std::string::npos,
               "character_ids_json 从 start_state.characters 抽出（K22 的受检对象）");
        expect(s1.timeline_json.find("\"duration_s\":3.0") != std::string::npos &&
                   s1.timeline_json.find("begin_s") != std::string::npos,
               "timeline_json 是 {duration_s,beats}（K24 的读法）");
        expect(s1.duration_note == "3.0s", "duration 落 duration_note");
        expect(s1.dialogue.find("谁在？") != std::string::npos, "dialogue 落库");
        // 契约 I7 的数据侧检查：第 2 镜 start_state == 第 1 镜 end_state（判定归 K09，这里只保证数据在）
        expect((*list)[1].start_state_json == s1.end_state_json,
               "第 2 镜的 start_state 与第 1 镜的 end_state 一致（I7 的数据基础）");
    }
    expect(std::filesystem::exists(dir / "work" / "ch001" / "storyboard.json", ec),
           "完整契约应落盘 work/ch001/storyboard.json");

    // 幂等：再跑一次（同 mock）→ 不应插重复行
    auto again = GenerateStoryboard(
        mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir});
    expect(again.has_value(), "第二遍也应成功");
    auto list2 = visual.ListShotsByChapter(ch.value_or(0));
    expect(list2.has_value() && list2->size() == 2, "第二遍不应产生重复行（同场同序号覆盖）");

    // 没有场景的章 → 明确报错（V9 的输入前提）
    auto ch2 = g.UpsertChapter({.ord = 2, .title = "第二章"});
    auto bad = GenerateStoryboard(mem, mock, {.chapter_id = ch2.value_or(0), .project_dir = dir});
    expect(!bad.has_value() && bad.error().code == "no_scenes", "没有场景的章应报 no_scenes");

    std::filesystem::remove_all(dir, ec);
    if (fails == 0) {
        log::Info("分镜自检通过（V9：契约解析 + shots 落库 + K22/K24 数据源 + 幂等 + 完整契约落盘）");
    }
    return fails == 0;
}

} // namespace shine::agent
