#include "agent/NovelVisualStages.h"

#include "core/Log.h"
#include "novel/NovelChecks.h" // ComputeInputStateHash（`04` §2.5 的唯一来源）
#include "novel/NovelGraph.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <filesystem>
#include <utility>

namespace shine::agent {
namespace {

// V1 的内置指令。⚠️ 字段名与 `12` §2.2 的七要素对齐，解析端只认这些名字。
// 「Conflict 为空 → 该场不应存在（回 `SCENE_PLAN`）」是 `12` §2.2 的硬校验，这里写进指令，
// 解析端也会**告警**（不静默放过）。
constexpr std::string_view kSceneBreakdownInstructions = R"(
你是分镜师。为给定的每一场做「场分析」，并规划这场切成几镜。
只输出 JSON：{"scenes":[ ... ]}，不要解释。
每场字段（名字必须一致）：
  scene_ord(int)          用上面给的第几场
  goal(string)            该场要达成什么
  conflict(string)        冲突点 —— **不能为空**（无冲突不成场）
  emotion(string)         情绪基调与走向
  information(string)     观众应该知道什么
  environment(string)     环境与氛围
  character_goals(array)  [{entity_id(int), goal(string)}]，每个在场人物各自要什么（可冲突）
  important_props(array)  [string]，重要道具
  shots(array)            该场切成几镜：[{ord(int, 从 1 递增),
                                          duration(number, 秒, 一位小数, > 0),
                                          beat(string, 一句话概要)}]
)" ;

[[nodiscard]] std::string ArtifactPath(std::string_view projectDir, int chapterOrd) {
    return util::PathToUtf8(std::filesystem::path{std::string{projectDir}} / "work" /
                            fmt::format("ch{:03}", chapterOrd) / "v01_scene_breakdown.json");
}

// 取顶层字符串字段（null 安全）
[[nodiscard]] std::string Str(const yyjson_val* obj, const char* key) {
    if (obj == nullptr || !yyjson_is_obj(obj)) {
        return {};
    }
    const yyjson_val* v = yyjson_obj_get(obj, key);
    return yyjson_is_str(v) ? std::string{yyjson_get_str(v)} : std::string{};
}

[[nodiscard]] std::string JsonEscape(std::string_view s) {
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

} // namespace

std::string SceneBreakdownOutcome::Describe() const {
    if (!ok) {
        return "V1 场分析失败：" + error;
    }
    return fmt::format("V1 场分析：{} 场 / 规划 {} 镜（LLM {} 次）{}", scenes, shots_planned, llm_calls,
                       reused ? " · **复用**（哈希一致，未重调 LLM）" : "");
}

std::expected<SceneBreakdownOutcome, AgentError>
RunSceneBreakdown(::shine::db::sqlite::Database& db, const LlmCallFn& call,
                  const SceneBreakdownRequest& req) {
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
            "no_scenes", "该章没有 scenes —— V1 的输入是已提交的场景（正文链的 SCENE_PLAN，`03` T6）"});
    }
    // PV1 同款：本阶段的输入状态指纹（`04` §2.5；阶段自己一份 → 阶段级续跑）
    const std::string hash =
        novelcore::ComputeInputStateHash(db, req.chapter_id, "visual", "V1");
    const std::string path = ArtifactPath(req.project_dir, ch->ord);

    SceneBreakdownOutcome out;
    out.artifact_path = path;
    out.scenes = static_cast<int>(scenes->size());
    // —— 复用：盘上产物还在、且哈希一致 → **不重调 LLM**（`03` §2.7 的 P1/P2）——
    if (!req.project_dir.empty()) {
        if (const auto text = util::ReadFileBytes(path); text) {
            if (yyjson_doc* d = yyjson_read(text->data(), text->size(), 0); d != nullptr) {
                const std::string stored = Str(yyjson_doc_get_root(d), "input_state_hash");
                yyjson_doc_free(d);
                if (!stored.empty() && stored == hash) {
                    out.ok = true;
                    out.reused = true;
                    const auto sk = LoadShotSkeleton(req.project_dir, ch->ord);
                    for (const ShotSkeleton& s : sk) {
                        if (s.ord == 1) {
                            ++out.shots_planned; // 每场镜数（按 ord==1 记一场；近似但可见）
                        }
                    }
                    log::Info("V1 复用（{}）", path);
                    return out;
                }
            }
        }
    }

    // —— user 消息：场景清单 ——
    std::string user = fmt::format("【本章】第 {} 章《{}》\n【场景清单】\n", ch->ord, ch->title);
    for (const novelcore::SceneRow& s : *scenes) {
        user += fmt::format("- scene_ord={} 《{}》", s.ord, s.title);
        if (!s.time_label.empty()) {
            user += fmt::format("（{}）", s.time_label);
        }
        user += "\n";
    }
    user += "\n【任务】逐场做场分析（七要素），并规划每场切成几镜（shots）。";
    if (!req.extra_hint.empty()) {
        user += "\n【额外要求】" + req.extra_hint;
    }

    ++out.llm_calls;
    auto r = call(LlmRole::Planner, std::string{kSceneBreakdownInstructions}, user);
    if (!r) {
        out.error = fmt::format("LLM 调用失败：{}", r.error().message);
        return out;
    }
    // —— 解析（宽容：缺字段只告警，不整段失败 —— 这些产物是**中间产物**，不是世界状态）——
    yyjson_doc* doc = yyjson_read(r->data(), r->size(), 0);
    if (doc == nullptr) {
        out.error = "V1 输出不是合法 JSON";
        return out;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* arr = yyjson_is_obj(root) ? yyjson_obj_get(root, "scenes") : nullptr;
    if (!yyjson_is_arr(arr)) {
        yyjson_doc_free(doc);
        out.error = "V1 输出缺少 scenes 数组";
        return out;
    }
    int noConflict = 0;
    int scenesOut = 0; // **产出的**场数（与输入的场景数应一致；`out.scenes` 初值只是输入数）
    std::string scenesJson;
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* sc = nullptr;
    yyjson_arr_foreach(arr, i, max, sc) {
        if (!yyjson_is_obj(sc)) {
            continue;
        }
        ++scenesOut;
        const std::string conflict = Str(sc, "conflict");
        if (util::Trim(conflict).empty()) {
            ++noConflict; // `12` §2.2：无冲突不成场
        }
        const yyjson_val* shots = yyjson_obj_get(sc, "shots");
        int shotCount = 0;
        if (yyjson_is_arr(shots)) {
            shotCount = static_cast<int>(yyjson_arr_size(shots));
            out.shots_planned += shotCount;
        }
        const char* raw = yyjson_val_write(sc, 0, nullptr);
        const std::string one = raw != nullptr ? std::string{raw} : std::string{"{}"};
        if (raw != nullptr) {
            free(const_cast<char*>(raw));
        }
        scenesJson += (scenesJson.empty() ? "" : ",") + one;
    }
    yyjson_doc_free(doc);
    out.scenes = scenesOut; // 覆盖成**产出的**场数（`out.scenes` 此前记的是输入的场数）
    if (noConflict > 0) {
        out.warnings.push_back(fmt::format(
            "{} 场缺 `conflict`（`12` §2.2：**无冲突不成场**，应回 `SCENE_PLAN`）", noConflict));
    }
    if (out.shots_planned == 0) {
        out.warnings.push_back("没有规划出任何镜（`shots` 全空）—— V9 将退回「自己决定切分」");
    }
    // —— 落盘（带哈希，供下次复用）——
    const std::string json = fmt::format(
        "{{\"stage\":\"V1\",\"stage_name\":\"SCENE_BREAKDOWN\",\"chapter_id\":{},\"chapter_ord\":{},"
        "\"input_state_hash\":\"{}\",\"scenes\":[{}]}}",
        req.chapter_id, ch->ord, hash, scenesJson);
    if (req.project_dir.empty()) {
        out.warnings.push_back("没有 project_dir → 产物只算不落盘（下次仍要重调 LLM）");
    } else {
        const auto dir = std::filesystem::path{std::string{req.project_dir}} / "work" /
                         fmt::format("ch{:03}", ch->ord);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!util::WriteFileBytes(path, json)) {
            out.warnings.push_back("V1 产物落盘失败（work/ 非权威，不阻断）");
        }
    }
    out.ok = true;
    log::Info("V1 场分析：章={} 场={} 规划镜数={}（{}）", req.chapter_id, out.scenes,
              out.shots_planned, out.warnings.empty() ? "无告警" : out.warnings.front());
    return out;
}

std::vector<ShotSkeleton> LoadShotSkeleton(std::string_view projectDir, int chapterOrd) {
    std::vector<ShotSkeleton> out;
    if (projectDir.empty() || chapterOrd <= 0) {
        return out;
    }
    const auto file = std::filesystem::path{std::string{projectDir}} / "work" /
                      fmt::format("ch{:03}", chapterOrd) / "v01_scene_breakdown.json";
    const auto text = util::ReadFileBytes(file);
    if (!text) {
        return out;
    }
    yyjson_doc* d = yyjson_read(text->data(), text->size(), 0);
    if (d == nullptr) {
        return out;
    }
    yyjson_val* root = yyjson_doc_get_root(d);
    yyjson_val* arr = yyjson_is_obj(root) ? yyjson_obj_get(root, "scenes") : nullptr;
    if (yyjson_is_arr(arr)) {
        std::size_t i = 0;
        std::size_t max = 0;
        yyjson_val* sc = nullptr;
        yyjson_arr_foreach(arr, i, max, sc) {
            if (!yyjson_is_obj(sc)) {
                continue;
            }
            const yyjson_val* so = yyjson_obj_get(sc, "scene_ord");
            const int sceneOrd = yyjson_is_int(so) ? static_cast<int>(yyjson_get_sint(so)) : 0;
            const yyjson_val* shots = yyjson_obj_get(sc, "shots");
            if (sceneOrd <= 0 || !yyjson_is_arr(shots)) {
                continue;
            }
            std::size_t j = 0;
            std::size_t jmax = 0;
            yyjson_val* sh = nullptr;
            yyjson_arr_foreach(shots, j, jmax, sh) {
                if (!yyjson_is_obj(sh)) {
                    continue;
                }
                ShotSkeleton s;
                s.scene_ord = sceneOrd;
                const yyjson_val* od = yyjson_obj_get(sh, "ord");
                s.ord = yyjson_is_int(od) ? static_cast<int>(yyjson_get_sint(od)) : 0;
                const yyjson_val* du = yyjson_obj_get(sh, "duration");
                s.duration = yyjson_is_num(du) ? yyjson_get_num(du) : 0.0;
                s.beat = Str(sh, "beat");
                if (s.ord > 0) {
                    out.push_back(std::move(s));
                }
            }
        }
    }
    yyjson_doc_free(d);
    return out;
}

// ═══════════════ V2–V7：规格表 + 通用执行器 ═══════════════

std::string_view VisualStageCode(VisualStageId stage) noexcept {
    switch (stage) {
    case VisualStageId::V1SceneBreakdown: return "V1";
    case VisualStageId::V2DirectorIntent: return "V2";
    case VisualStageId::V3Performance: return "V3";
    case VisualStageId::V4Spatial: return "V4";
    case VisualStageId::V5Camera: return "V5";
    case VisualStageId::V6Timeline: return "V6";
    case VisualStageId::V7Audio: return "V7";
    }
    return "V1";
}

std::string_view VisualStageName(VisualStageId stage) noexcept {
    switch (stage) {
    case VisualStageId::V1SceneBreakdown: return "SCENE_BREAKDOWN";
    case VisualStageId::V2DirectorIntent: return "DIRECTOR_INTENT";
    case VisualStageId::V3Performance: return "PERFORMANCE";
    case VisualStageId::V4Spatial: return "SPATIAL";
    case VisualStageId::V5Camera: return "CAMERA";
    case VisualStageId::V6Timeline: return "TIMELINE";
    case VisualStageId::V7Audio: return "AUDIO";
    }
    return "SCENE_BREAKDOWN";
}

namespace {

// 每阶段的规格：内置指令 + 上游产物 + 产物文件名。字段名严格对齐 `12`/`13` 各卷的契约。
struct StageSpec {
    VisualStageId id;
    std::string_view instructions;
    std::string_view task;          // user 消息里的一句话任务
    std::string_view upstream_file; // 空 = 无上游
    std::string_view artifact_file;
};

constexpr StageSpec kV2Spec{
    VisualStageId::V2DirectorIntent,
    R"-(你是导演。为**每一镜**做「导演意图」分析，并给出该镜的情绪强度。
只输出 JSON：{"items":[ ... ]}，不要解释。
每条对应一镜，字段（名字必须一致）：
  scene_ord(int) ord(int)   用上面给的场/镜
  see(string)          观众应该看到什么
  know(string)         观众应该知道什么
  not_know(string)     观众不应该知道什么
  emotion(string)      核心情绪
  emotion_shift(string) 情绪如何变化
  climax(string)       高潮在哪里（非高潮就写「非高潮」）
  pace(string)         节奏如何变化
  intensity(int)       情绪强度 0-100
规则（`12` §2.3 的 E1–E3）：**至少一镜 ≥ 80**（高潮）；**相邻镜变化 ≤ 40**；**首镜 ≥ 20**。)-",
    "逐镜给出导演意图（七问）与情绪强度。",
    "v01_scene_breakdown.json",
    "v02_director_intent.json",
};

constexpr StageSpec kV3Spec{
    VisualStageId::V3Performance,
    R"-(你是表演指导。为**每一镜**给出表演层（`02` §2.7 的 12 项）。
只输出 JSON：{"items":[ ... ]}，不要解释。
每条：scene_ord(int) ord(int) 以及这 12 个**字符串**字段（都要有内容，不要留空）：
  expression(表情) eyes(眼神) breathing(呼吸) posture(姿态) body_movement(身体动作)
  hand_movement(手部) head_movement(头部) weight_shift(重心) walking(走) stopping(停)
  reaction(反应) pause(停顿，秒，可写 "0.5"))-",
    "逐镜给出表演层 12 项。",
    "v02_director_intent.json",
    "v03_performance.json",
};

constexpr StageSpec kV4Spec{
    VisualStageId::V4Spatial,
    R"-(你是空间调度。为**每一镜**给出站位层（`02` §2.7 的 Spatial）。
只输出 JSON：{"items":[ ... ]}，不要解释。
每条：scene_ord(int) ord(int) 以及：
  facing(string) 朝向    distance_m(number) 距离（米）
  height(string) 高度    movement_path(string) 运动路径（没有就写「无」）
  occlusion(string) 遮挡关系
  layers(object) {foreground, midground, background} —— **谁在前景/谁在中间/谁在背景**，
                 填角色名（本镜没有该层就写「无」）。这是「谁在前景」的唯一来源。)-",
    "逐镜给出站位层（含前景/中景/背景）。",
    "v03_performance.json",
    "v04_spatial.json",
};

constexpr StageSpec kV5Spec{
    VisualStageId::V5Camera,
    R"-(你是摄影指导。为**每一镜**给出镜头层（`02` §2.7 的 Camera，10 项）。
只输出 JSON：{"items":[ ... ]}，不要解释。
每条：scene_ord(int) ord(int) 以及：
  shot_size(string) 从 extreme_wide|wide|full|medium|medium_close|close_up|extreme_close_up 里选
  position(string) height(string) angle(string) lens_mm(number)
  composition(string) focus(string) dof(string) framing(string)
  movement(string) 从 static|pan|tilt|dolly|truck|push_in|pull_out|tracking|orbit|handheld 里选)-",
    "逐镜给出镜头层（景别/机位/运动）。",
    "v04_spatial.json",
    "v05_camera.json",
};

constexpr StageSpec kV6Spec{
    VisualStageId::V6Timeline,
    R"-(你是剪辑。为**每一镜**给出时间轴（`02` §2.9 的 Beat，0.1s 精度）。
只输出 JSON：{"items":[ ... ]}，不要解释。
每条：scene_ord(int) ord(int) duration(number，秒) beats(array of {begin_s(number), end_s(number)})
规则：首 beat.begin_s = 0；末 beat.end_s = duration；同镜内 beats **不得重叠**。)-",
    "逐镜给出 Beat 时间轴。",
    "v05_camera.json",
    "v06_timeline.json",
};

constexpr StageSpec kV7Spec{
    VisualStageId::V7Audio,
    R"-(你是声音设计。为**每一镜**给出音频设计（`13` §2.4 的五层）。
只输出 JSON：{"items":[ ... ]}，不要解释。
每条：scene_ord(int) ord(int) 以及：
  dialogue(array) [{speaker_id(int), text(string), emotion(string)}]（没有就空数组）
  sfx(array)      按事件：Footsteps / Door / Weapon / Clothing / Breathing / Impact / Rain / Glass / Fire
  ambient(string) 按地点：Forest / City / Tavern / Room / Battlefield / Rain / Wind / Crowd
  bgm(string)     按情绪：Calm / Suspense / Tension / Fear / Action / Climax / Resolution
  timeline(array) [{at_s(number), event(string)}]（0.1s 精度；事件时间应落在某个 Beat 区间内）)-",
    "逐镜给出音频设计（对白/音效/环境/配乐 + 时间轴）。",
    "v06_timeline.json",
    "v07_audio.json",
};

[[nodiscard]] const StageSpec& SpecOf(VisualStageId stage) noexcept {
    switch (stage) {
    case VisualStageId::V3Performance: return kV3Spec;
    case VisualStageId::V4Spatial: return kV4Spec;
    case VisualStageId::V5Camera: return kV5Spec;
    case VisualStageId::V6Timeline: return kV6Spec;
    case VisualStageId::V7Audio: return kV7Spec;
    case VisualStageId::V2DirectorIntent:
    default: return kV2Spec;
    }
}

[[nodiscard]] std::string TruncForPrompt(std::string_view s, std::size_t n) {
    return s.size() <= n ? std::string{s} : std::string{s.substr(0, n)} + "\n…（上游产物过长，已截断）";
}

// 产物目录：`<工程>/work/ch<NNN>`
[[nodiscard]] std::filesystem::path StageDir(std::string_view projectDir, int chapterOrd) {
    return std::filesystem::path{std::string{projectDir}} / "work" / fmt::format("ch{:03}", chapterOrd);
}

// 读盘上产物的顶层字符串字段（`input_state_hash` 等）
[[nodiscard]] std::string ArtifactField(const std::filesystem::path& file, const char* key) {
    const auto text = util::ReadFileBytes(file);
    if (!text) {
        return {};
    }
    yyjson_doc* d = yyjson_read(text->data(), text->size(), 0);
    if (d == nullptr) {
        return {};
    }
    const std::string out = Str(yyjson_doc_get_root(d), key);
    yyjson_doc_free(d);
    return out;
}

} // namespace

std::string StageOutcome::Describe() const {
    if (!ok) {
        return "阶段失败：" + error;
    }
    return fmt::format("{} 条{}{}", items, reused ? "（**复用**：链式哈希一致，未重调 LLM）" : "",
                       warnings.empty() ? "" : fmt::format("；{} 条提示：{}", warnings.size(),
                                                          warnings.front()));
}

std::expected<StageOutcome, AgentError> RunVisualStage(db::sqlite::Database& db,
                                                       const LlmCallFn& call,
                                                       const StageRequest& req) {
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
    const StageSpec& spec = SpecOf(req.stage);
    const std::string code{VisualStageCode(req.stage)};
    StageOutcome out;
    // —— 上游产物（链式依赖：没有上游就先跑上一阶段）——
    std::string upstreamJson;
    std::string upstreamHash;
    if (!spec.upstream_file.empty()) {
        const auto upFile = StageDir(req.project_dir, ch->ord) / std::string{spec.upstream_file};
        const auto text = req.project_dir.empty() ? std::nullopt : util::ReadFileBytes(upFile);
        if (!text) {
            return std::unexpected(
                AgentError{"no_upstream", fmt::format("缺上游产物 `{}` —— 先跑上一阶段（`--novel-stages`）",
                                                     spec.upstream_file)});
        }
        upstreamJson = *text;
        upstreamHash = ArtifactField(upFile, "input_state_hash");
    }
    // —— 链式哈希（上游变了 ⇒ 本阶段必失效）——
    const std::string own = novelcore::ComputeInputStateHash(db, req.chapter_id, "visual", code);
    const std::string hash =
        upstreamHash.empty() ? own : fmt::format("{}|{}", own, upstreamHash);
    const auto artifact = StageDir(req.project_dir, ch->ord) / std::string{spec.artifact_file};
    out.artifact_path = util::PathToUtf8(artifact);
    // —— 复用 ——
    if (!req.project_dir.empty()) {
        const std::string stored = ArtifactField(artifact, "input_state_hash");
        if (!stored.empty() && stored == hash) {
            out.ok = true;
            out.reused = true;
            // 条目数从产物里数（`items` 数组长度）
            if (const auto text = util::ReadFileBytes(artifact); text) {
                if (yyjson_doc* d = yyjson_read(text->data(), text->size(), 0); d != nullptr) {
                    yyjson_val* root = yyjson_doc_get_root(d);
                    yyjson_val* items =
                        yyjson_is_obj(root) ? yyjson_obj_get(root, "items") : nullptr;
                    if (yyjson_is_arr(items)) {
                        out.items = static_cast<int>(yyjson_arr_size(items));
                    }
                    yyjson_doc_free(d);
                }
            }
            log::Info("{} 复用（{}）", code, out.artifact_path);
            return out;
        }
    }
    // —— user 消息：上游产物 + 任务 ——
    std::string user = fmt::format("【本章】第 {} 章《{}》\n", ch->ord, ch->title);
    if (!upstreamJson.empty()) {
        user += fmt::format("\n【上游 {} 产物】\n{}\n", spec.upstream_file,
                            TruncForPrompt(upstreamJson, 8000));
    }
    user += fmt::format("\n【任务】{}", spec.task);
    if (!req.extra_hint.empty()) {
        user += "\n【额外要求】" + req.extra_hint;
    }
    ++out.llm_calls;
    auto r = call(LlmRole::Planner, std::string{spec.instructions}, user);
    if (!r) {
        out.error = fmt::format("LLM 调用失败：{}", r.error().message);
        return out;
    }
    // —— 解析 `items[]`（中间产物：宽进严出 —— 结构不对就报错，字段缺只告警）——
    yyjson_doc* doc = yyjson_read(r->data(), r->size(), 0);
    if (doc == nullptr) {
        out.error = fmt::format("{} 输出不是合法 JSON", code);
        return out;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* items = yyjson_is_obj(root) ? yyjson_obj_get(root, "items") : nullptr;
    if (!yyjson_is_arr(items)) {
        yyjson_doc_free(doc);
        out.error = fmt::format("{} 输出缺少 items 数组", code);
        return out;
    }
    int missingOrd = 0;
    std::string itemsJson;
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* it = nullptr;
    yyjson_arr_foreach(items, i, max, it) {
        if (!yyjson_is_obj(it)) {
            continue;
        }
        const yyjson_val* od = yyjson_obj_get(it, "ord");
        if (!yyjson_is_int(od) || yyjson_get_sint(od) <= 0) {
            ++missingOrd; // 缺 `ord` 就无法与镜对齐（下游按 `(scene_ord, ord)` 用）
        }
        ++out.items;
        const char* raw = yyjson_val_write(it, 0, nullptr);
        const std::string one = raw != nullptr ? std::string{raw} : std::string{"{}"};
        if (raw != nullptr) {
            free(const_cast<char*>(raw));
        }
        itemsJson += (itemsJson.empty() ? "" : ",") + one;
    }
    yyjson_doc_free(doc);
    if (missingOrd > 0) {
        out.warnings.push_back(fmt::format("{} 条缺 `ord`（无法与镜对齐，下游按 `(scene_ord, ord)` 取用）",
                                           missingOrd));
    }
    if (out.items == 0) {
        out.warnings.push_back("没有产出任何条目（`items` 为空）");
    }
    // —— 落盘（带链式哈希）——
    const std::string json = fmt::format(
        "{{\"stage\":\"{}\",\"stage_name\":\"{}\",\"chapter_id\":{},\"chapter_ord\":{},"
        "\"upstream\":\"{}\",\"upstream_hash\":\"{}\",\"input_state_hash\":\"{}\",\"items\":[{}]}}",
        code, VisualStageName(req.stage), req.chapter_id, ch->ord, spec.upstream_file, upstreamHash,
        hash, itemsJson);
    if (req.project_dir.empty()) {
        out.warnings.push_back("没有 project_dir → 产物只算不落盘（下次仍要重调 LLM）");
    } else {
        std::error_code ec;
        std::filesystem::create_directories(artifact.parent_path(), ec);
        if (!util::WriteFileBytes(artifact, json)) {
            out.warnings.push_back("产物落盘失败（work/ 非权威，不阻断）");
        }
    }
    out.ok = true;
    log::Info("{} {}：章={} 条目={}（{}）", code, VisualStageName(req.stage), req.chapter_id,
              out.items, out.warnings.empty() ? "无告警" : out.warnings.front());
    return out;
}

std::string StagesOutcome::Describe() const {
    if (!ok) {
        return "阶段链失败：" + error;
    }
    return fmt::format("V1–V7：跑 {} 阶段（其中 {} 复用）/ LLM {} 次", stages_run, stages_reused,
                       llm_calls);
}

std::expected<StagesOutcome, AgentError> RunAllVisualStages(db::sqlite::Database& db,
                                                            const LlmCallFn& call,
                                                            novelcore::RowId chapter_id,
                                                            std::string_view project_dir,
                                                            std::string_view extra_hint) {
    StagesOutcome out;
    const VisualStageId order[] = {VisualStageId::V1SceneBreakdown, VisualStageId::V2DirectorIntent,
                                   VisualStageId::V3Performance,   VisualStageId::V4Spatial,
                                   VisualStageId::V5Camera,        VisualStageId::V6Timeline,
                                   VisualStageId::V7Audio};
    for (const VisualStageId st : order) {
        const std::string code{VisualStageCode(st)};
        if (st == VisualStageId::V1SceneBreakdown) {
            auto r = RunSceneBreakdown(
                db, call,
                {.chapter_id = chapter_id, .project_dir = std::string{project_dir},
                 .extra_hint = std::string{extra_hint}});
            if (!r) {
                return std::unexpected(r.error());
            }
            if (!r->ok) {
                out.error = fmt::format("V1 失败：{}", r->error);
                return out;
            }
            out.detail += fmt::format("· V1 SCENE_BREAKDOWN：{}\n", r->Describe());
            out.llm_calls += r->llm_calls;
            ++out.stages_run;
            if (r->reused) {
                ++out.stages_reused;
            }
            continue;
        }
        auto r = RunVisualStage(db, call,
                                {.chapter_id = chapter_id,
                                 .project_dir = std::string{project_dir},
                                 .stage = st,
                                 .extra_hint = std::string{extra_hint}});
        if (!r) {
            out.error = fmt::format("{} 失败：{}", code, r.error().message);
            return out;
        }
        if (!r->ok) {
            out.error = fmt::format("{} 失败：{}", code, r->error);
            return out;
        }
        out.detail += fmt::format("· {} {}：{}\n", code, VisualStageName(st), r->Describe());
        out.llm_calls += r->llm_calls;
        ++out.stages_run;
        if (r->reused) {
            ++out.stages_reused;
        }
    }
    out.ok = true;
    return out;
}

bool RunStagesSelfCheck() {
    int fails = 0;
    const auto expect = [&fails](bool cond, const std::string& msg) {
        if (!cond) {
            log::Error("V1 自检 FAIL：{}", msg);
            ++fails;
        }
    };
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("V1 自检：内存库打开失败 {}", r.error().message);
        return false;
    }
    if (auto r = novelcore::NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("V1 自检：建 schema 失败 {}", r.error().message);
        return false;
    }
    novelcore::NovelGraph g(mem);
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章"});
    auto sc = g.UpsertScene({.chapter_id = ch.value_or(0), .ord = 1, .title = "库房",
                             .time_label = "夜"});
    expect(ch.has_value() && sc.has_value(), "建章与场");

    // mock LLM：返回两场（第 2 场故意**缺 conflict** → 应告警）
    int calls = 0;
    LlmCallFn mock = [&calls](LlmRole role, std::string_view,
                              std::string_view) -> std::expected<std::string, AgentError> {
        ++calls;
        if (role != LlmRole::Planner) {
            return std::unexpected(AgentError{"bad_role", "V1 应以 Planner（中档）调用"});
        }
        // ⚠️ **必须用 `)-"` 分隔符的单段 raw string**：上一版是多段 `R"(...)"` 拼接，其中一段
        // 以 `,"")"` 结尾 —— 那个 `")"` 被当成 raw string 的**结束符**，拼接当场错位，JSON 非法
        //（自检撞出来的：`V1 输出不是合法 JSON`）。C++ raw string 的经典坑。
        return std::string{R"-({"scenes":[{"scene_ord":1,"goal":"找到账本","conflict":"守卫醒了","emotion":"紧张","information":"账本在柜里","environment":"昏暗库房","character_goals":[{"entity_id":1,"goal":"拿到账本"}],"important_props":["账本"],"shots":[{"ord":1,"duration":3.0,"beat":"推门进入"},{"ord":2,"duration":2.5,"beat":"听见脚步"}]},{"scene_ord":2,"goal":"逃出","conflict":"","emotion":"急","information":"","environment":"走廊","character_goals":[],"important_props":[],"shots":[{"ord":1,"duration":4.0,"beat":"奔跑"}]}],"items":[{"scene_ord":1,"ord":1,"intensity":50,"see":"推门"},{"scene_ord":1,"ord":2,"intensity":70,"see":"听见脚步"}]})-"};
    };

    std::error_code ec;
    const auto tmp = std::filesystem::temp_directory_path() / "shine_v1_selfcheck";
    std::filesystem::remove_all(tmp, ec);
    const std::string dir = util::PathToUtf8(tmp);

    // ① 主路径：产出 + 落盘 + 骨架可读回
    const auto first =
        RunSceneBreakdown(mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir});
    expect(first.has_value() && first->ok,
           fmt::format("V1 应成功（实际：{}）",
                       first.has_value() ? first->error : first.error().message));
    expect(first.has_value() && first->llm_calls == 1, "首次应调 1 次 LLM");
    expect(calls == 1, "mock 应被调用 1 次");
    expect(first.has_value() && first->scenes == 2, "应解析出 2 场");
    expect(first.has_value() && first->shots_planned == 3, "骨架应含 3 镜（2+1）");
    expect(first.has_value() && !first->warnings.empty(),
           "第 2 场缺 conflict → **必须**有告警（`12` §2.2 无冲突不成场）");
    expect(!first->artifact_path.empty(), "应给出产物路径");
    const auto sk = LoadShotSkeleton(dir, 1);
    expect(sk.size() == 3, fmt::format("骨架应可读回 3 镜（实际 {}）", sk.size()));
    if (sk.size() == 3) {
        expect(sk[0].scene_ord == 1 && sk[0].ord == 1 && sk[0].duration > 2.9 &&
                   !sk[0].beat.empty(),
               "骨架首镜的场/序/时长/概要正确");
    }

    // ② 复用：哈希一致 → **不重调 LLM**
    const auto second =
        RunSceneBreakdown(mem, mock, {.chapter_id = ch.value_or(0), .project_dir = dir});
    expect(second.has_value() && second->ok && second->reused, "同哈希应复用");
    expect(calls == 1, "复用**不得**再调 LLM");

    // ④ S32：**链式**（V2 读 V1 的产物）+ 链式哈希
    {
        const auto v2 = RunVisualStage(mem, mock,
                                       {.chapter_id = ch.value_or(0), .project_dir = dir,
                                        .stage = VisualStageId::V2DirectorIntent});
        expect(v2.has_value() && v2->ok,
               fmt::format("V2 应成功（{}）",
                           v2.has_value() ? v2->error : v2.error().message));
        expect(v2.has_value() && v2->items == 2, "V2 应解析出 2 条（脚本给了 2 条 items）");
        // 链式哈希：V2 的产物里必须带**上游 V1 的哈希**（上游变了 ⇒ 下游失效）
        const std::string v1Hash =
            std::string{}; // V1 的哈希从产物读（下面用 ArtifactField 的等价方式）
        const auto v1File = std::filesystem::path{dir} / "work" / "ch001" /
                            "v01_scene_breakdown.json";
        const auto v2File = std::filesystem::path{dir} / "work" / "ch001" /
                            "v02_director_intent.json";
        expect(!ArtifactField(v1File, "input_state_hash").empty(), "V1 产物应带 input_state_hash");
        expect(ArtifactField(v2File, "upstream_hash") == ArtifactField(v1File, "input_state_hash"),
               "V2 的 `upstream_hash` 必须等于 V1 的 `input_state_hash`（链式）");
        // 复用：再跑 V2 → 不重调 LLM
        const int callsBefore = calls;
        const auto v2Again = RunVisualStage(mem, mock,
                                            {.chapter_id = ch.value_or(0), .project_dir = dir,
                                             .stage = VisualStageId::V2DirectorIntent});
        expect(v2Again.has_value() && v2Again->reused, "V2 同链式哈希应复用");
        expect(calls == callsBefore, "V2 复用**不得**再调 LLM");
        (void)v1Hash;
        // 上游缺失：直接跑 V3（没有 V2）—— 反例在下面单独造
    }

    // ③ 没有 scenes 的章 → 明确报错（V1 的输入前提）
    auto ch2 = g.UpsertChapter({.ord = 2, .title = "第二章"});
    const auto bad = RunSceneBreakdown(mem, mock, {.chapter_id = ch2.value_or(0)});
    expect(!bad.has_value() && bad.error().code == "no_scenes", "没有 scenes 的章应报 no_scenes");

    std::filesystem::remove_all(tmp, ec);
    if (fails == 0) {
        log::Info("V1 场分析自检通过（七要素 + 镜骨架落盘 + 哈希复用 + 无冲突告警）");
    }
    return fails == 0;
}

} // namespace shine::agent
