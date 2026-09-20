#include "agent/NovelStoryboard.h"
#include "agent/NovelVisualStages.h" // S31：读取 V1 的镜骨架

#include "core/Log.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h" // S38：ExtractJsonObject（宽容提取 LLM 输出的 JSON 正文）
#include "util/Strings.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>
#include <cmath>
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

【**精简铁律**（S38；真实跑被硬截断后加的 —— 上限 4096 tokens，超了**整份 JSON 作废**）】
  · `performance` / `spatial` / `camera` / `audio` / `timeline` 这五层，上面【逐镜设计】
    **已经给全** ⇒ **一律不要再输出**（下游会直接从那些设计读，重复输出只会挤爆上限）。
  · 你**只输出**它们没覆盖的部分：`scene_ord` / `ord` / `duration` / `start_state` / `end_state` /
    `prompt_text` / `negative_text` / `dialogue` / `transition`。
  · 文字能短则短（`prompt_text` 一句话）。
)" ;

// S37：**按镜下发上游设计**的体积上限（字符）。超了就只发前几镜 + **明确记账**。
// 为什么还留上限：prompt 太长会挤掉正文/场景信息、白烧 token。但关键是**超限本身可见**
//（S36 的教训：静默截断会让 S34 的"阶段偏离"被误读成"没采纳"）。
// S40：**12000 → 40000** —— 实测 12000 会让 8 镜的章只下发到第 4 镜（`keys` 按场序排，
// 场 1 拼完就 `break`，**场 2 一镜都没下发**，而它们的"阶段偏离"因此不可信）。
// 现在 MiniMax 走 **Responses 端**（`max_output_tokens`，无 Chat 兼容端那个 4096 拘束），
// 输入也宽裕，所以放宽；仍留上限是因为"**超限必须可见**"这条要求没变。
constexpr std::size_t kStageDesignMaxChars = 40000;

// S36：参数收紧成 `const` —— `yyjson_val_write` 本身是**只读**的（它只是把 val 写成 JSON 文本），
// 但 yyjson 的签名没带 `const`，所以这里 `const_cast` 掉（不是真的改它）。
[[nodiscard]] std::string JsonText(const yyjson_val* v) {
    if (v == nullptr) {
        return {};
    }
    const char* t = yyjson_val_write(const_cast<yyjson_val*>(v), 0, nullptr);
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

// S56：**只保留 `kind=person` 的实体** —— `06` K03（`entity.kind_match`：location/person/item/
// conflict）要求 `shots.character_ids_json` **只放人物**。
// 🔴 起因（真跑确诊）：`start_state.characters` 来自**模型输出**，它会把无关实体塞进来
//（实测 11 处是 `universe` 世界观测实体）⇒ 原样落库 ⇒ K03 失败 ⇒ **G2=0 ⇒ 提交门禁不过
// ⇒ 状态从不回写 ⇒ auto 前置①永远不满足**（`--novel-run auto` 永远被拒）。
// 取舍：**过滤而不是拒绝**（中间产物宽容），但**必须记账**（剔除数进 warnings，不静默）。
[[nodiscard]] std::pair<std::string, int> PersonIdsOnly(db::sqlite::Database& db,
                                                        const std::string& idsJson) {
    yyjson_doc* d = yyjson_read(idsJson.data(), idsJson.size(), 0);
    if (d == nullptr) {
        return {idsJson, 0};
    }
    novelcore::NovelGraph g(db);
    std::string out = "[";
    bool first = true;
    int dropped = 0;
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* v = nullptr;
    yyjson_val* arr = yyjson_doc_get_root(d);
    if (yyjson_is_arr(arr)) {
        yyjson_arr_foreach(arr, i, max, v) {
            if (!yyjson_is_int(v)) {
                continue;
            }
            const std::int64_t id = yyjson_get_sint(v);
            auto e = g.GetEntity(id);
            if (!e || e->kind != "person") {
                ++dropped;
                continue;
            }
            out += fmt::format("{}{}", first ? "" : ",", id);
            first = false;
        }
    }
    yyjson_doc_free(d);
    return {out + "]", dropped};
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

// —— S34：阶段产物的**字段级比对**（比骨架更细）——
// 骨架只验"镜数/序号/时长"；这一层验"LLM 有没有真的采纳 V3–V5/V7 的设计"
//（前景是谁 / 景别 / 表情 / 环境音）。键与骨架一致：`(scene_ord, ord)`。
// ⚠️ 只挑**每阶段一个有代表性**的字段：全字段比对会被"措辞差异"淹没（LLM 会改写文案），
//    我们要抓的是"**根本没采纳**"，不是"字面不同"。
// ⚠️ `V2 DIRECTOR_INTENT` 仍**无法比对**：它的产物在 `NarrativeShot` 无承载字段（S33 已记账）。
[[nodiscard]] std::map<std::pair<int, int>, const yyjson_val*>
IndexStageItems(const std::filesystem::path& file, yyjson_doc** outDoc) {
    std::map<std::pair<int, int>, const yyjson_val*> out;
    *outDoc = nullptr;
    const auto text = util::ReadFileBytes(file);
    if (!text) {
        return out;
    }
    yyjson_doc* d = yyjson_read(text->data(), text->size(), 0);
    if (d == nullptr) {
        return out;
    }
    *outDoc = d;
    yyjson_val* root = yyjson_doc_get_root(d);
    yyjson_val* items = yyjson_is_obj(root) ? yyjson_obj_get(root, "items") : nullptr;
    if (!yyjson_is_arr(items)) {
        return out;
    }
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* it = nullptr;
    yyjson_arr_foreach(items, i, max, it) {
        if (!yyjson_is_obj(it)) {
            continue;
        }
        const yyjson_val* so = yyjson_obj_get(it, "scene_ord");
        const yyjson_val* od = yyjson_obj_get(it, "ord");
        const int sceneOrd = yyjson_is_int(so) ? static_cast<int>(yyjson_get_sint(so)) : 0;
        const int ord = yyjson_is_int(od) ? static_cast<int>(yyjson_get_sint(od)) : 0;
        if (sceneOrd > 0 && ord > 0) {
            out[{sceneOrd, ord}] = it;
        }
    }
    return out;
}

// `obj[key]`（顶层字符串；缺 → 空串）
[[nodiscard]] std::string TopStr(const yyjson_val* obj, const char* key) {
    if (obj == nullptr || !yyjson_is_obj(obj)) {
        return {};
    }
    const yyjson_val* v = yyjson_obj_get(obj, key);
    return yyjson_is_str(v) ? std::string{yyjson_get_str(v)} : std::string{};
}

// `obj[a][b]`（缺任一环 → 空串；不做类型强转）
[[nodiscard]] std::string NestedStr(const yyjson_val* obj, const char* a, const char* b) {
    if (obj == nullptr || !yyjson_is_obj(obj)) {
        return {};
    }
    const yyjson_val* o = yyjson_obj_get(obj, a);
    if (o == nullptr || !yyjson_is_obj(o)) {
        return {};
    }
    const yyjson_val* v = yyjson_obj_get(o, b);
    return yyjson_is_str(v) ? std::string{yyjson_get_str(v)} : std::string{};
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
    // —— S37：**上游阶段产物的索引**（跑过 `--novel-stages` 才有；键 `(scene_ord, ord)`）——
    // 一次读好 6 个阶段产物，供下面三处共用：① **按镜下发**（拼进 prompt，S37）
    // ② **落库**（V2 → `shots.intent_json`，S36）③ **一致性比对**（S34）。
    yyjson_doc* v2Doc = nullptr;
    yyjson_doc* v3Doc = nullptr;
    yyjson_doc* v4Doc = nullptr;
    yyjson_doc* v5Doc = nullptr;
    yyjson_doc* v6Doc = nullptr;
    yyjson_doc* v7Doc = nullptr;
    const auto stageItemDir = StoryboardDir(req.project_dir, ch->ord);
    const auto v2Items = IndexStageItems(stageItemDir / "v02_director_intent.json", &v2Doc);
    const auto v3Items = IndexStageItems(stageItemDir / "v03_performance.json", &v3Doc);
    const auto v4Items = IndexStageItems(stageItemDir / "v04_spatial.json", &v4Doc);
    const auto v5Items = IndexStageItems(stageItemDir / "v05_camera.json", &v5Doc);
    const auto v6Items = IndexStageItems(stageItemDir / "v06_timeline.json", &v6Doc);
    const auto v7Items = IndexStageItems(stageItemDir / "v07_audio.json", &v7Doc);
    // S40：下发统计（供 GUI 显示；块外声明，块内填）
    int sentShots = 0;
    int totalShots = 0;
    std::size_t designChars = 0;
    // —— S37：**按镜下发上游设计**（替代 S32 的"把整个产物文件截 3000 字"）——
    // ⚠️ 起因（S36 记账的隐患）：按**文件**截断 ⇒ 镜一多，**靠后的镜根本没看到**上游设计 ⇒
    //    S34 报的"阶段偏离"对它们**不是"没采纳"，而是"没看到"**（冤枉 LLM）。
    // 现在改成**逐镜紧凑对齐**：每镜一小段、六个阶段并排 —— 每镜都能看到**属于自己的**设计。
    // 结构上仍是**软约束**（LLM 仍可能偏离，S34 照样记账），但至少它**看到了**。
    {
        const std::pair<const char*, const std::map<std::pair<int, int>, const yyjson_val*>*> stages[] = {
            {"V2 导演意图", &v2Items}, {"V3 表演", &v3Items}, {"V4 空间", &v4Items},
            {"V5 镜头", &v5Items},   {"V6 时间轴", &v6Items}, {"V7 声音", &v7Items},
        };
        std::set<std::pair<int, int>> keys;
        for (const auto& [label, mp] : stages) {
            (void)label;
            for (const auto& [k, v] : *mp) {
                (void)v;
                keys.insert(k);
            }
        }
        std::string block;
        int sent = 0;
        bool cut = false;
        for (const auto& k : keys) {
            std::string one = fmt::format("- scene_ord={} ord={}\n", k.first, k.second);
            for (const auto& [label, mp] : stages) {
                if (const auto hit = mp->find(k); hit != mp->end()) {
                    one += fmt::format("    {}: {}\n", label, JsonText(hit->second));
                }
            }
            if (block.size() + one.size() > kStageDesignMaxChars) {
                cut = true;
                break;
            }
            block += one;
            ++sent;
        }
        sentShots = sent;
        totalShots = static_cast<int>(keys.size());
        designChars = block.size();
        if (sent > 0) {
            user += "\n【上游已定的**逐镜设计**】（按它来，**不要另起一套**）\n" + block;
            out.warnings.push_back(fmt::format("已按镜下发 {} 镜的上游设计（V2–V7）", sent));
            if (cut) {
                out.warnings.push_back(fmt::format(
                    "上游设计超 {} 字上限，只下发了前 {} 镜 —— 靠后的镜「阶段偏离」"
                    "**不代表没采纳**（它没看到）",
                    kStageDesignMaxChars, sent));
            }
        }
    }
    // —— S31：**V1 的镜骨架**（跑过 `--novel-stages` 就有）——
    // "镜的切分"这一步已**提前到 V1**（`12` §2.2 的场分析里就该定"这场切几镜"）；这里把骨架
    // 下发进 prompt。⚠️ 是**软约束**：LLM 仍可能偏离，解析端按 `(scene_ord, ord)` 归位、
    // 偏离不报错，但在 `warnings` 里看得见（`11` §2.7 W2 的同款要求）。
    const auto skeleton = LoadShotSkeleton(util::PathToUtf8(req.project_dir), ch->ord);
    if (!skeleton.empty()) {
        user += "\n【镜骨架（V1 `SCENE_BREAKDOWN` 已定）】按下面的场/镜产出，**不要改切分**：\n";
        for (const ShotSkeleton& s : skeleton) {
            user += fmt::format("- scene_ord={} ord={} duration≈{:.1f}s：{}\n", s.scene_ord, s.ord,
                                s.duration, s.beat);
        }
        out.warnings.push_back(fmt::format("已下发 V1 镜骨架（{} 镜）", skeleton.size()));
    }
    // ⚠️ S37：这里原先是 S32 的"**按文件下发**（每个产物截 3000 字）" —— 已**删除**，改成
    // 上面的"**按镜下发的紧凑化**"。原因（S36 记账的隐患）：按文件截断会让**靠后的镜根本没
    // 看到**设计 ⇒ S34 报的"阶段偏离"对它们不是"没采纳"，而是"没看到"（冤枉 LLM）。
    user += "\n【任务】按上面的场景清单逐场产出 NarrativeShot[]。";
    if (!req.extra_hint.empty()) {
        user += "\n【额外要求】" + req.extra_hint;
    }

    // S40：**下发统计落盘**（GUI 要显示"下发了 N/M 镜、多少字符、上限多少"）。
    // 起因：用户问"GUI 显示每阶段的 prompt/时间戳/输出了什么"，而"下发了几镜"原先只在一行日志里。
    // ⚠️ 真实踩过：`keys` 是 `std::set<pair<int,int>>`（**按场序排**），场 1 的 4 镜拼完就到
    //    12000 上限 ⇒ `break` ⇒ **场 2 的设计一镜都没下发**（我一度误判成"产物有问题"）。
    if (!req.project_dir.empty()) {
        std::error_code ec;
        const auto dd = StoryboardDir(req.project_dir, ch->ord);
        std::filesystem::create_directories(dd, ec);
        (void)util::WriteFileBytes(
            dd / "v9_dispatch.json",
            fmt::format(R"({{"sent":{},"total":{},"design_chars":{},"limit":{},)"
                        R"("system_chars":{},"user_chars":{}}})",
                        sentShots, totalShots, designChars, kStageDesignMaxChars,
                        std::string{kStoryboardInstructions}.size(), user.size()));
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
    // S38：**先宽容提取**（LLM 输出常带 markdown 围栏/前后说明 —— 真实跑 V6 就是被这个卡死的）
    const std::string jsonText = util::json::ExtractJsonObject(text);
    yyjson_doc* doc = yyjson_read(jsonText.data(), jsonText.size(), 0);
    if (doc == nullptr) {
        // 失败时原始输出**已经在盘上**（上面刚落 `storyboard.json`，保真不丢）+ 带前 300 字便于诊断
        return std::unexpected(AgentError{
            "contract", fmt::format("Storyboard 输出不是合法 JSON（原始 {} 字，前 300 字：{}）",
                                    text.size(), text.substr(0, 300))});
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

    // —— S33：**骨架一致性检查**（把"软约束"变成"**可检测的**软约束"）——
    // 下发骨架/阶段产物只是**建议**（LLM 可能不遵守）。这里逐镜比对，把偏离变成**可见的账**：
    // 骨架外新增 / 骨架内缺失 / `duration` 偏差过大。
    // ⚠️ **不急判 fail**（LLM 合并或拆镜有时是合理的，`03` §2.2 本就允许合并），
    //    但必须**看得见**（`11` §2.7 W2 的同款要求：不确定/偏离也要可见）。
    // ⚠️ V2 `DIRECTOR_INTENT` **无法这样查**：它的产物（七问 + `intensity`）在 `NarrativeShot`
    //    契约里**没有承载字段**（V3→`performance`、V4→`spatial`、V5→`camera`、V6→`timeline`、
    //    V7→`audio` 都有）。所以 V2 只能"影响判断"，落不了地也查不了 —— **如实记账**。
    std::map<std::pair<int, int>, double> skeletonByKey; // (scene_ord, ord) → duration
    for (const ShotSkeleton& s : skeleton) {
        skeletonByKey[{s.scene_ord, s.ord}] = s.duration;
    }
    std::set<std::pair<int, int>> skeletonSeen;
    int skeletonExtra = 0;
    int skeletonDurationMismatch = 0;

    // 阶段产物的索引已在前面读好（S37 把索引提到 prompt 构造之前 —— 下发要用）
    int stageMismatch = 0;
    std::vector<std::string> stageMismatchDetail;

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
        // S36：V2 `DIRECTOR_INTENT`（七问 + `intensity`）→ `shots.intent_json`。
        // **唯一来源是 V2 产物**（七问是 V2 的职责，不在 V9 的 LLM 输出里）；没跑 V2 → 留 `{}`。
        if (const auto it = v2Items.find({sceneOrd, ord}); it != v2Items.end()) {
            row.intent_json = JsonText(it->second);
        }
        {
            // S56：落库前**只留 person**（K03 的期望）；剔除了谁要记账（不静默改数据）
            const auto [ids, dropped] = PersonIdsOnly(db, CharacterIdsJson(startState));
            row.character_ids_json = ids;
            if (dropped > 0) {
                out.warnings.push_back(
                    fmt::format("scene_ord={} ord={}：`start_state.characters` 含 {} 个**非 person** "
                                "实体（`06` K03 要求这里只放人物），已剔除",
                                sceneOrd, ord, dropped));
            }
        }
        // S34：**阶段产物的字段级比对**（比骨架更细）—— 每阶段挑**一个有代表性**的字段：
        // V4 前景 / V5 景别 / V3 表情 / V7 环境音。抓的是"**根本没采纳**"，不是"措辞不同"
        //（LLM 会改写文案，全字段比对会被噪音淹没）。
        // ⚠️ 任一边为空**不判**（无法区分"没采纳"与"上游本来就没给"）；V2 仍无法比对（无承载字段）。
        if (!v3Items.empty() || !v4Items.empty() || !v5Items.empty() || !v7Items.empty()) {
            const auto k = std::make_pair(sceneOrd, ord);
            const auto note = [&](const std::string& want, const std::string& got, const char* label,
                                  const char* what) {
                if (want.empty() || got.empty() || want == got) {
                    return;
                }
                ++stageMismatch;
                if (stageMismatchDetail.size() < 3) {
                    stageMismatchDetail.push_back(
                        fmt::format("scene_ord={} ord={} 的{}与上游 {} 不一致（上游「{}」/ V9 实际「{}」）",
                                    sceneOrd, ord, what, label, want, got));
                }
            };
            const yyjson_val* spObj = yyjson_obj_get(shot, "spatial");
            const yyjson_val* camObj = yyjson_obj_get(shot, "camera");
            const yyjson_val* perfObj = yyjson_obj_get(shot, "performance");
            const yyjson_val* audObj = yyjson_obj_get(shot, "audio");
            if (const auto it = v4Items.find(k); it != v4Items.end()) {
                note(NestedStr(it->second, "layers", "foreground"), NestedStr(spObj, "layers", "foreground"),
                     "V4 SPATIAL", "前景");
            }
            if (const auto it = v5Items.find(k); it != v5Items.end()) {
                note(TopStr(it->second, "shot_size"), TopStr(camObj, "shot_size"), "V5 CAMERA", "景别");
            }
            if (const auto it = v3Items.find(k); it != v3Items.end()) {
                note(TopStr(it->second, "expression"), TopStr(perfObj, "expression"), "V3 PERFORMANCE",
                     "表情");
            }
            if (const auto it = v7Items.find(k); it != v7Items.end()) {
                note(TopStr(it->second, "ambient"), TopStr(audObj, "ambient"), "V7 AUDIO", "环境音");
            }
        }
        // S33：逐镜与 V1 骨架比对（有骨架才查）
        if (!skeletonByKey.empty()) {
            const auto key = std::make_pair(sceneOrd, ord);
            const auto sk = skeletonByKey.find(key);
            if (sk == skeletonByKey.end()) {
                ++skeletonExtra;
            } else {
                skeletonSeen.insert(key);
                if (sk->second > 0.0 && std::abs(sk->second - duration) > 0.2) {
                    ++skeletonDurationMismatch;
                    out.warnings.push_back(
                        fmt::format("骨架偏离：scene_ord={} ord={} 的 duration={:.1f}s 与 V1 骨架的 "
                                    "{:.1f}s 差 > 0.2s",
                                    sceneOrd, ord, duration, sk->second));
                }
            }
        }
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
    // S33：骨架比对汇总（**三类偏离全部进 warnings**，不静默）
    if (!skeletonByKey.empty()) {
        const int missing =
            static_cast<int>(skeletonByKey.size()) - static_cast<int>(skeletonSeen.size());
        if (missing > 0) {
            out.warnings.push_back(
                fmt::format("V1 骨架里的 {} 镜**没有**出现在 V9 输出里（骨架内缺失）", missing));
        }
        if (skeletonExtra > 0) {
            out.warnings.push_back(
                fmt::format("V9 输出了 {} 镜**不在** V1 骨架里（骨架外新增）", skeletonExtra));
        }
        out.skeleton_total = static_cast<int>(skeletonByKey.size());
        out.skeleton_missing = missing;
        out.skeleton_extra = skeletonExtra;
        out.skeleton_duration_mismatch = skeletonDurationMismatch;
        log::Info("骨架一致性：下发 {} 镜 / 缺失 {} / 新增 {} / 时长偏离 {}", out.skeleton_total,
                  out.skeleton_missing, out.skeleton_extra, out.skeleton_duration_mismatch);
    }
    // S35：**骨架硬校验**（`strict_skeleton`，默认关）—— 把 V1 的骨架从"下发给 LLM 的强建议"
    // 变成"**合同**"：三类偏离任一 > 0 就**判失败**（此前只是 warnings）。
    // ⚠️ 默认关是**刻意的**：真实工程常常没跑过 V1（没骨架可校），硬开会让 V9 全线失败。
    if (req.strict_skeleton) {
        if (skeletonByKey.empty()) {
            return std::unexpected(AgentError{
                "contract",
                fmt::format("骨架硬校验失败（strict_skeleton）：该章没有 V1 骨架可校 —— "
                            "先跑 `--novel-stages {} --up-to V1`", req.chapter_id)});
        }
        if (out.skeleton_missing > 0 || out.skeleton_extra > 0 ||
            out.skeleton_duration_mismatch > 0) {
            return std::unexpected(AgentError{
                "contract",
                fmt::format("骨架硬校验失败（strict_skeleton）：缺失 {} 镜 / 新增 {} 镜 / 时长偏离 {} 镜"
                            " —— V9 必须严格按 V1 骨架出镜（合同，不是建议）",
                            out.skeleton_missing, out.skeleton_extra,
                            out.skeleton_duration_mismatch)});
        }
        log::Info("骨架硬校验通过（strict_skeleton）：{} 镜全落在 V1 骨架内且时长一致",
                  out.skeleton_total);
    }
    // S34：阶段产物比对汇总 + 释放阶段性 doc
    for (yyjson_doc* d : {v2Doc, v3Doc, v4Doc, v5Doc, v6Doc, v7Doc}) {
        if (d != nullptr) {
            yyjson_doc_free(d);
        }
    }
    if (stageMismatch > 0) {
        for (const std::string& dd : stageMismatchDetail) {
            out.warnings.push_back("阶段偏离：" + dd);
        }
        out.warnings.push_back(fmt::format("V3–V7 产物与 V9 输出共 {} 处关键字段不一致（只列前 {} 条）",
                                           stageMismatch, stageMismatchDetail.size()));
        log::Warn("阶段产物一致性：{} 处未被采纳（V4 前景 / V5 景别 / V3 表情 / V7 环境音）",
                  stageMismatch);
    } else if (!v3Items.empty() || !v4Items.empty() || !v5Items.empty() || !v7Items.empty()) {
        log::Info("阶段产物一致性：已下发阶段的关键字段**全部被采纳**");
    }
    out.stage_mismatch = stageMismatch;
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
    std::string lastUser; // S37：捕获下发给 LLM 的 user 消息（验证"按镜下发"真的生效）
    LlmCallFn mock = [&](LlmRole role, std::string_view, std::string_view user)
        -> std::expected<std::string, AgentError> {
        ++calls;
        lastUser = std::string{user};
        expect(role == LlmRole::Planner, "视觉链走中档角色（Planner）");
        return wrapped;
    };

    // S33：**先放一份 V1 骨架**（与 mock 的两镜一致：3.0 / 2.5）—— 验证"一致时三类偏离为 0"
    {
        const auto v1Dir = dir / "work" / "ch001";
        std::filesystem::create_directories(v1Dir, ec);
        const std::string sk =
            R"-({"stage":"V1","input_state_hash":"h1","scenes":[{"scene_ord":1,"shots":[{"ord":1,"duration":3.0,"beat":"a"},{"ord":2,"duration":2.5,"beat":"b"}]}]})-";
        expect(util::WriteFileBytes(v1Dir / "v01_scene_breakdown.json", sk), "写 V1 骨架");
    }
    auto out = GenerateStoryboard(
        mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir, .extra_hint = "多用手持"});
    expect(out.has_value(), fmt::format("分镜应产出成功：{}", out.has_value() ? "" : out.error().message));
    if (out) {
        expect(out->shots_written == 2, "两镜都应落库");
        expect(out->scenes_covered == 1, "覆盖 1 场");
        expect(!out->warnings.empty(), "无专列字段要显式给出提示（不静默丢）");
    }
    // S33：骨架与输出**一致** → 三类偏离全 0（"软约束"的**可检测**化）
    if (out) {
        expect(out->skeleton_total == 2 && out->skeleton_missing == 0 && out->skeleton_extra == 0 &&
                   out->skeleton_duration_mismatch == 0,
               fmt::format("S33：骨架一致时偏离应为 0（实际 total={} miss={} extra={} dur={}）",
                           out->skeleton_total, out->skeleton_missing, out->skeleton_extra,
                           out->skeleton_duration_mismatch));
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
    // S33：**骨架不一致必须被发现** —— 这就是"LLM 会不会乱来"的答案：**不靠猜，靠查**。
    // 骨架 3 镜、mock 只产 2 镜 ⇒ 必须报 `skeleton_missing = 1` 且**进 warnings 可见**。
    {
        const auto v1Dir = dir / "work" / "ch001";
        std::filesystem::create_directories(v1Dir, ec);
        const std::string sk =
            R"-({"stage":"V1","input_state_hash":"h2","scenes":[{"scene_ord":1,"shots":[{"ord":1,"duration":3.0,"beat":"a"},{"ord":2,"duration":2.5,"beat":"b"},{"ord":3,"duration":2.0,"beat":"c"}]}]})-";
        expect(util::WriteFileBytes(v1Dir / "v01_scene_breakdown.json", sk), "写骨架（3 镜）");
        const auto out2 =
            GenerateStoryboard(mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir});
        expect(out2.has_value() && out2->skeleton_total == 3 && out2->skeleton_missing == 1,
               "S33：骨架 3 镜而输出 2 镜 → 必须报 skeleton_missing=1");
        bool reported = false;
        if (out2) {
            for (const std::string& w : out2->warnings) {
                if (w.find("骨架内缺失") != std::string::npos) {
                    reported = true;
                }
            }
        }
        expect(reported, "S33：骨架缺失必须在 warnings 里可见（不静默）");
        std::filesystem::remove_all(dir, ec);
    }

    // S34：**阶段产物的字段级比对** —— 造一份 V3 产物（表情=平静），而 mock 输出该镜是「警惕」
    // ⇒ 必须报"未被采纳"（`stage_mismatch > 0` 且进 warnings）。
    {
        const auto v1Dir = dir / "work" / "ch001";
        std::filesystem::create_directories(v1Dir, ec);
        const std::string v3 =
            R"-({"stage":"V3","input_state_hash":"h3","items":[{"scene_ord":1,"ord":1,"expression":"平静"}]})-";
        expect(util::WriteFileBytes(v1Dir / "v03_performance.json", v3), "写 V3 产物（表情=平静）");
        const auto out3 =
            GenerateStoryboard(mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir});
        expect(out3.has_value() && out3->stage_mismatch > 0,
               fmt::format("S34：V3 说表情「平静」而 V9 输出「警惕」⇒ 必须报 stage_mismatch>0（实际 {}）",
                           out3.has_value() ? out3->stage_mismatch : -1));
        bool reported = false;
        if (out3) {
            for (const std::string& w : out3->warnings) {
                if (w.find("阶段偏离") != std::string::npos) {
                    reported = true;
                }
            }
        }
        expect(reported, "S34：阶段偏离必须在 warnings 里可见（不静默）");
        std::filesystem::remove_all(dir, ec);
    }

    // S35：**`strict_skeleton`** —— 骨架偏离必须**判失败**（把"强建议"变成"合同"）。
    // 造 3 镜骨架而 mock 只出 2 镜 ⇒ 缺失 1 镜：严格模式失败、非严格模式只告警。
    {
        const auto sdir = dir / "work" / "ch001";
        std::filesystem::create_directories(sdir, ec);
        const std::string v1 =
            R"-({"scenes":[{"scene_ord":1,"goal":"g","shots":[{"ord":1,"duration":3.0,"beat":"a"},{"ord":2,"duration":2.5,"beat":"b"},{"ord":3,"duration":1.0,"beat":"c"}]}]})-";
        expect(util::WriteFileBytes(sdir / "v01_scene_breakdown.json", v1), "S35：写骨架（3 镜）");
        const auto strict = GenerateStoryboard(
            mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir, .strict_skeleton = true});
        expect(!strict.has_value() && strict.error().code == "contract",
               fmt::format("S35：strict_skeleton 下骨架缺失（3 镜骨架 vs 2 镜输出）必须判失败"
                           "（实际 {}）",
                           strict.has_value() ? "成功" : strict.error().code));
        const auto loose = GenerateStoryboard(
            mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir, .strict_skeleton = false});
        expect(loose.has_value() && loose->ok, "S35：非严格模式下同样输入应成功（只告警）");
        std::filesystem::remove_all(dir, ec);
    }

    // S36：**V2 `DIRECTOR_INTENT` 的承载**（S33 记的"无处可落"在此闭合）——
    // 造一份 V2 产物 → 断言七问真的进了 `shots.intent_json`（不再落库即丢）。
    {
        const auto sdir = dir / "work" / "ch001";
        std::filesystem::create_directories(sdir, ec);
        const std::string v2 =
            R"-({"stage":"V2","items":[{"scene_ord":1,"ord":1,"see":"看到门","know":"知道有人","not_know":"不知是谁","emotion":"警惕","emotion_shift":"松到紧","climax":"非高潮","pace":"渐紧","intensity":80}]})-";
        expect(util::WriteFileBytes(sdir / "v02_director_intent.json", v2), "S36：写 V2 产物");
        const auto o = GenerateStoryboard(mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir});
        expect(o.has_value() && o->ok, "S36：带 V2 产物重跑应成功");
        // S37：**按镜下发**（不是"把整个产物文件截 3000 字"）—— 每镜都要看到**属于自己的**设计
        expect(lastUser.find("上游已定的**逐镜设计**") != std::string::npos,
               "S37：应下发「逐镜设计」段（按镜组织，而非按文件）");
        expect(lastUser.find("V2 导演意图: ") != std::string::npos &&
                   lastUser.find("看到门") != std::string::npos,
               "S37：V2 的设计必须**按镜**出现在 prompt 里（带镜的 scene_ord/ord）");
        expect(lastUser.find("- scene_ord=1 ord=1") != std::string::npos,
               "S37：逐镜设计应按 `scene_ord/ord` 分条");
        expect(lastUser.find("【V3 PERFORMANCE 产物】") == std::string::npos,
               "S37：旧的「按文件下发」形态必须**消失**（否则截断问题还在）");
        novelcore::NovelVisual visFor(mem);
        auto shots = visFor.ListShotsByChapter(ch.value_or(0));
        expect(shots.has_value() && !shots->empty(), "S36：应有镜可查");
        if (shots && !shots->empty()) {
            expect(shots->front().intent_json.find("intensity") != std::string::npos &&
                       shots->front().intent_json.find("看到门") != std::string::npos,
                   fmt::format("S36：V2 的七问必须落进 `shots.intent_json`（实际「{}」）",
                               shots->front().intent_json));
        }
        std::filesystem::remove_all(dir, ec);
    }

    return fails == 0;
}

} // namespace shine::agent
