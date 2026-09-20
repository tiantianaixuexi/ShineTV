#include "agent/NovelVisualStages.h"

#include "agent/AgentKit.h" // S41：V4 试点走 Agent 工具循环（多轮 + MCP 工具白名单）
#include <chrono>           // S46：网络退避重试
#include <thread>           // S46：网络退避重试
#include "core/Log.h"
#include "openai/OpenAIClient.h" // S41：LlmCreateRaw（返回原始响应体，供工具循环解析）
#include "novel/NovelChecks.h" // ComputeInputStateHash（`04` §2.5 的唯一来源）
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h" // S49：阶段产物落库（stage_artifacts）
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h" // S38：ExtractJsonObject（宽容提取 LLM 输出的 JSON 正文）
#include "util/Strings.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <filesystem>
#include <map>      // S49：键 → 该镜原文（落库用）
#include <set>      // S47：期望镜清单的键集合（与 V1 骨架对账）
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
                    // S51：**复用也落库**（同 V2–V7）—— V1 的形状是 `scenes[].shots[]`，
                    // 用刚读回的 `sk` 补写（只在库里还没有 V1 行时才写）。
                    if (!sk.empty()) {
                        novelcore::NovelVisual vis(db);
                        bool need = true;
                        if (auto ex = vis.ListStageArtifacts(req.chapter_id, "V1"); ex) {
                            need = ex->empty();
                        }
                        if (need) {
                            std::vector<novelcore::StageArtifactRow> rows;
                            rows.reserve(sk.size());
                            for (const ShotSkeleton& s : sk) {
                                novelcore::StageArtifactRow row;
                                row.chapter_id = req.chapter_id;
                                row.stage = "V1";
                                row.scene_ord = s.scene_ord;
                                row.shot_ord = s.ord;
                                row.payload_json =
                                    fmt::format(R"({{"ord":{},"duration":{:.2f}}})", s.ord,
                                                s.duration);
                                row.input_state_hash = hash;
                                rows.push_back(std::move(row));
                            }
                            if (auto wr = vis.ReplaceStageArtifacts(req.chapter_id, "V1", rows);
                                !wr) {
                                out.warnings.push_back(fmt::format(
                                    "V1 复用：回填 stage_artifacts 失败（不阻断）：{}",
                                    wr.error().message));
                            }
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
    // S38：**先宽容提取** —— LLM 的输出常带 markdown 围栏或前后说明文字，直接 `yyjson_read`
    // 全文会当场判非法（真实跑就撞上了：MiniMax-M3 在 V6 上返回的不是纯 JSON，链断在 V6）。
    // S46：**再加一次重试**（与 V2–V7 对齐）—— 原先**只有 V1 没有重试**，真跑就撞上了：
    // MiniMax 偶发返回带 ```json 围栏的输出，宽容提取也救不回来，而 V1 是链头 ⇒
    // **整条链从第一步就断**（后面 6 个阶段的 Agent 全没机会上场）。
    std::string json1 = util::json::ExtractJsonObject(*r);
    yyjson_doc* doc = yyjson_read(json1.data(), json1.size(), 0);
    if (doc == nullptr) {
        log::Warn("V1 输出不是合法 JSON → **重试一次**（模型偶发生成非法 JSON）");
        auto r2 = call(LlmRole::Planner, std::string{kSceneBreakdownInstructions}, user);
        if (r2) {
            ++out.llm_calls;
            *r = *r2;
            json1 = util::json::ExtractJsonObject(*r);
            doc = yyjson_read(json1.data(), json1.size(), 0);
        }
    }
    if (doc == nullptr) {
        // S38：失败**留原始输出**（否则无从诊断"模型到底回了个什么"）
        if (!req.project_dir.empty()) {
            std::error_code ec;
            const auto d = std::filesystem::path{req.project_dir} / "work" /
                           fmt::format("ch{:03}", ch->ord);
            std::filesystem::create_directories(d, ec);
            (void)util::WriteFileBytes(d / "v01_raw_failed.txt", *r);
        }
        out.error = fmt::format("V1 输出不是合法 JSON（原始 {} 字，前 300 字：{}）", r->size(),
                                r->substr(0, 300));
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
    // S49：**V1 骨架落库**（与 V2–V7 的 `stage_artifacts` 行**同构**）—— 一镜一行，
    // 这样"查某镜的六阶段设计"能一句话把 V1 也带出来（有几镜、时长多少）。
    // ⚠️ 用 `LoadShotSkeleton` 读回刚写的产物（不重复解析 `doc` —— 这里 `doc` 已经释放了）。
    if (!req.project_dir.empty()) {
        const auto sk = LoadShotSkeleton(req.project_dir, ch->ord);
        if (!sk.empty()) {
            std::vector<novelcore::StageArtifactRow> rows;
            rows.reserve(sk.size());
            for (const ShotSkeleton& s : sk) {
                novelcore::StageArtifactRow row;
                row.chapter_id = req.chapter_id;
                row.stage = "V1";
                row.scene_ord = s.scene_ord;
                row.shot_ord = s.ord;
                row.payload_json =
                    fmt::format(R"({{"ord":{},"duration":{:.2f}}})", s.ord, s.duration);
                row.input_state_hash = hash;
                rows.push_back(std::move(row));
            }
            if (auto wr = novelcore::NovelVisual(db).ReplaceStageArtifacts(req.chapter_id, "V1", rows);
                !wr) {
                out.warnings.push_back(
                    fmt::format("V1 骨架落库失败（不阻断，盘上产物仍在）：{}", wr.error().message));
            }
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

std::optional<VisualStageId> ParseVisualStage(std::string_view text) noexcept {
    // S35：归一化 —— 去空白、转大写、去掉 `V` 前缀；只认 `V1`…`V7` / `1`…`7`（其余 → nullopt）
    std::string t;
    for (const char c : text) {
        if (c == ' ' || c == '\t') {
            continue;
        }
        t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (!t.empty() && t.front() == 'V') {
        t.erase(t.begin());
    }
    if (t.size() != 1 || t[0] < '1' || t[0] > '7') {
        return std::nullopt;
    }
    switch (t[0]) {
    case '1':
        return VisualStageId::V1SceneBreakdown;
    case '2':
        return VisualStageId::V2DirectorIntent;
    case '3':
        return VisualStageId::V3Performance;
    case '4':
        return VisualStageId::V4Spatial;
    case '5':
        return VisualStageId::V5Camera;
    case '6':
        return VisualStageId::V6Timeline;
    default:
        return VisualStageId::V7Audio;
    }
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
**若你有工具可用**：先用它查库确认这一场有哪些角色、他们的位置/朝向/道具 —— **别猜**。
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

// S42：见 `StageSystemPrompt`（放在所有 `kV*Spec` 之后 —— 它要引用它们）。

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

// S42：`StageSystemPrompt` 的定义放在本文件末尾（匿名 namespace **之外**，
// 否则内部链接 ⇒ `AgentKit.cpp` 链接不到）。

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

// S47：**期望的镜清单**（来自 V1 骨架 —— 阶段链里"镜"的**权威定义**）。
// ⚠️ 起因（真跑诊断）：Agent 模式把 user 精简成"本章 + 场景清单"后，**模型不知道每场几镜**，
//    于是每个阶段各编自己的镜数 —— 实测 V1 骨架是"场1=5 镜 / 场2=5 镜（共 10）"，而同一轮
//    跑出来的是 V2=4 / V3=4 / V4=3 / V5=15 / V6=6 / V7=4，**全都不一样**。
//    这种错**单阶段看不出来**（每个阶段内部都自洽），**只有跨阶段对账**才暴露 —— 所以两头都要做：
//    ① 把骨架（场/镜 ord + 时长）**明写进 user**；② 解析端按它**对账**（缺失/多余都告警）。
// ⚠️ 时长一起给：V6 的 beats 必须对着 duration 排。
[[nodiscard]] std::pair<std::string, std::set<std::pair<int, int>>>
ShotSkeleton(const std::filesystem::path& project_dir, int chapter_ord) {
    std::pair<std::string, std::set<std::pair<int, int>>> out;
    const auto path =
        project_dir / "work" / fmt::format("ch{:03}", chapter_ord) / "v01_scene_breakdown.json";
    const auto text = util::ReadFileBytes(path);
    if (!text) {
        return out; // 没跑过 V1（没骨架）→ 空；调用方**不阻断**（同上游缺失的既有策略）
    }
    yyjson_doc* d = yyjson_read(text->data(), text->size(), 0);
    if (d == nullptr) {
        return out;
    }
    yyjson_val* root = yyjson_doc_get_root(d);
    yyjson_val* scenes = yyjson_is_obj(root) ? yyjson_obj_get(root, "scenes") : nullptr;
    if (yyjson_is_arr(scenes)) {
        std::size_t si = 0;
        std::size_t sn = 0;
        yyjson_val* s = nullptr;
        yyjson_arr_foreach(scenes, si, sn, s) {
            if (!yyjson_is_obj(s)) {
                continue;
            }
            const yyjson_val* so = yyjson_obj_get(s, "scene_ord");
            const int sceneOrd = yyjson_is_int(so) ? static_cast<int>(yyjson_get_sint(so)) : 0;
            if (sceneOrd <= 0) {
                continue;
            }
            const yyjson_val* ti = yyjson_obj_get(s, "title");
            const std::string title = yyjson_is_str(ti) ? std::string{yyjson_get_str(ti)} : "";
            out.first += fmt::format("- scene_ord={} 《{}》\n", sceneOrd, title);
            yyjson_val* shots = yyjson_obj_get(s, "shots");
            if (!yyjson_is_arr(shots)) {
                continue;
            }
            std::size_t ji = 0;
            std::size_t jn = 0;
            yyjson_val* j = nullptr;
            yyjson_arr_foreach(shots, ji, jn, j) {
                if (!yyjson_is_obj(j)) {
                    continue;
                }
                const yyjson_val* od = yyjson_obj_get(j, "ord");
                const int ord = yyjson_is_int(od) ? static_cast<int>(yyjson_get_sint(od)) : 0;
                if (ord <= 0) {
                    continue;
                }
                const yyjson_val* du = yyjson_obj_get(j, "duration");
                const double dur = yyjson_is_num(du) ? yyjson_get_num(du) : 0.0;
                out.first += fmt::format("    ord={} (duration={:.1f}s)\n", ord, dur);
                out.second.insert({sceneOrd, ord});
            }
        }
    }
    yyjson_doc_free(d);
    return out;
}

// S51：**复用分支的落库补写**。复用（哈希命中、不重调 LLM）时原先**直接 return、跳过落库**
// ⇒ **S49 之前跑过的老工程**库里永远没有该阶段的行（数据只在盘上）。
// ⚠️ 只在"库里还没有该阶段的行"时才补：省一次读盘，也避免每次复用都把库重写一遍。
// ⚠️ 形状：读盘上产物的 `items[]`（V2–V7 的形状；V1 是 `scenes[].shots[]`，由调用方单独处理）。
[[nodiscard]] bool BackfillStageArtifacts(db::sqlite::Database& db, novelcore::RowId chapterId,
                                          std::string_view stage, std::string_view hash,
                                          const std::filesystem::path& artifact) {
    novelcore::NovelVisual vis(db);
    if (auto existing = vis.ListStageArtifacts(chapterId, stage);
        existing && !existing->empty()) {
        return true; // 已落过（正常路径），不必补
    }
    const auto text = util::ReadFileBytes(artifact);
    if (!text) {
        return false;
    }
    yyjson_doc* d = yyjson_read(text->data(), text->size(), 0);
    if (d == nullptr) {
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(d);
    yyjson_val* items = yyjson_is_obj(root) ? yyjson_obj_get(root, "items") : nullptr;
    std::vector<novelcore::StageArtifactRow> rows;
    if (yyjson_is_arr(items)) {
        std::size_t i = 0;
        std::size_t n = 0;
        yyjson_val* it = nullptr;
        yyjson_arr_foreach(items, i, n, it) {
            if (!yyjson_is_obj(it)) {
                continue;
            }
            const yyjson_val* so = yyjson_obj_get(it, "scene_ord");
            const yyjson_val* od = yyjson_obj_get(it, "ord");
            const int sc = yyjson_is_int(so) ? static_cast<int>(yyjson_get_sint(so)) : 0;
            const int ok = yyjson_is_int(od) ? static_cast<int>(yyjson_get_sint(od)) : 0;
            if (sc <= 0 || ok <= 0) {
                continue;
            }
            const char* raw = yyjson_val_write(it, 0, nullptr);
            novelcore::StageArtifactRow r;
            r.chapter_id = chapterId;
            r.stage = std::string{stage};
            r.scene_ord = sc;
            r.shot_ord = ok;
            r.payload_json = raw != nullptr ? std::string{raw} : "{}";
            if (raw != nullptr) {
                free(const_cast<char*>(raw));
            }
            r.input_state_hash = std::string{hash};
            rows.push_back(std::move(r));
        }
    }
    yyjson_doc_free(d);
    return rows.empty() || vis.ReplaceStageArtifacts(chapterId, stage, rows).has_value();
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
            // S51：**复用也落库**（补老工程的缺口）—— 原先复用分支**直接 return、跳过落库**，
            // 于是 S49 之前跑过的工程库里永远没有该阶段的行（数据只在盘上）。
            if (!BackfillStageArtifacts(db, req.chapter_id, code, hash, artifact)) {
                log::Warn("{} 复用：回填 stage_artifacts 失败（不阻断 —— 盘上产物仍在）", code);
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
    // —— 取 LLM 输出：默认**单轮**；`--agent-tools` → 走 **Agent 工具循环**（多轮）——
    // S46：从"只 V4 试点"**放开到 V2–V7 全阶段**（每阶段一个 Agent，白名单统一只读四件套）。
    // V1 没有 Agent（`StageAgentId` 返回空）⇒ 即使开了开关也走单轮。
    std::string rText;
    // S47：**期望镜清单**（V1 骨架，函数级 —— 下发与对账两头都要用）。没跑过 V1 ⇒ 空，不阻断。
    const auto [skelText, skelKeys] =
        ShotSkeleton(std::filesystem::path{req.project_dir}, ch->ord);
    const std::string_view agentId = StageAgentId(req.stage);
    if (req.use_agent_tools && !agentId.empty()) {
        // S41 试点 / S46 推广：让模型**自己用工具查库**（按需），而不是我们预先猜它要什么、塞满 prompt。
        // S48：把 `project_dir` 交给 kit —— 工具 `get_chapter_shots` 要读盘上的 V1 骨架
        agent::AgentKit kit(db, /*allowWrite=*/false, req.project_dir);
        if (auto seeded = kit.EnsureSchemaAndSeed(); !seeded) {
            out.error = fmt::format("{} Agent 初始化失败：{}", agentId, seeded.error().message);
            return out;
        }
        agent::AgentRunRequest areq;
        areq.agent_id = std::string{agentId}; // S46：与 `BuiltinAgents()` 同源（`StageAgentId`）
        areq.chapter_id = req.chapter_id;
        // S43：**精简 user** —— 走 Agent 就别再"把上游产物一股脑塞进来"。
        // 只给"本章 + 场景清单"（它必须知道的范围），**角色/位置/道具让它自己去查**
        //（`get_entity` / `list_entities`）。这才是"按需取数"：我们不再预先猜它要什么。
        {
            std::string lean =
                fmt::format("【本章】第 {} 章《{}》\n【场景清单】\n", ch->ord, ch->title);
            if (auto sc2 = novelcore::NovelGraph(db).ListScenes(req.chapter_id); sc2) {
                for (const novelcore::SceneRow& s : *sc2) {
                    lean += fmt::format("- scene_ord={} 《{}》\n", s.ord, s.title);
                }
            }
            // S48 修正（S47 的做法是权宜）：**骨架不再明写进 user** —— 那正是 S43 想砍掉的
            // 「喂数据」。改成让模型**自己查**（工具 `get_chapter_shots`）：user 里只保留
            // "**必须按骨架来**"这条**要求** + 告诉它**去哪拿**。
            // ⚠️ 分工：**有哪几镜是事实 ⇒ 能查**；它是**合同 ⇒ 靠对账**（下面的骨架比对）保证，
            //    **不靠 prompt**。模型万一不查，产出会与骨架对不上 ⇒ **对账会报出来**（可见，不静默）。
            if (!skelText.empty()) {
                lean += fmt::format(
                    "\n【必须遵守的合同】本章的场/镜骨架（每场几镜、每镜的 ord 与 duration）"
                    "**不得增删或改动** —— 先用工具 `get_chapter_shots`（chapter_id={}）把它"
                    "**查出来**，再逐镜按它输出。\n",
                    req.chapter_id);
            }
            lean += fmt::format("\n【任务】{}\n", spec.task);
            lean += "\n【提示】这一场有哪些角色、他们的位置/朝向/道具/性格 —— **用工具查，别猜**。\n";
            areq.user_text = lean;
        }
        agent::ToolLoopStats stats;
        const auto create = [](std::string_view ins, std::string_view in, std::string_view tj)
            -> std::expected<std::string, std::string> {
            // S46：**网络层退避重试**。真跑实测：Agent 循环连发 5 轮都成功，第 6 轮 MiniMax
            // 偶发 `ssl handshake failed`（**TLS 握手层**，不是请求形状的问题 —— 前 5 轮同样
            // 的 body 都过了）。一次抖动就中断**整条阶段链**（V1→V7 会白跑）太脆，故重试 3 次。
            // ⚠️ 退避**要够长**：真跑实测 1.5s 的退避连续 3 次都过不去 —— 现象是"同一分钟里
            //    第 6 次请求开始被持续拒绝"（V2 的 1 次 + V3 的 5 次 ≈ 6），像是**服务端短时频率
            //    限制**，而不是瞬时抖动。所以退避按 4s / 10s / 20s（总 ~34s）走。
            static constexpr int kBackoffMs[] = {4000, 10000, 20000};
            std::string lastErr;
            for (int attempt = 0; attempt < 4; ++attempt) {
                if (attempt > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffMs[attempt - 1]));
                }
                auto rr = openai::LlmCreateRaw(ins, in, tj);
                if (rr) {
                    return *rr;
                }
                lastErr = rr.error().message;
                log::Warn("LlmCreateRaw 失败（第 {} 次）：{}", attempt + 1, lastErr);
            }
            return std::unexpected(lastErr);
        };
        auto run = kit.Run(areq, create, &stats);
        if (!run) {
            out.error = fmt::format("{} Agent 失败：{}", agentId, run.error().message);
            return out;
        }
        std::string tools;
        for (const std::string& t : run->used_tools) {
            tools += (tools.empty() ? "" : ", ") + t;
        }
        out.llm_calls += stats.steps + 1; // 记账：多轮的**每一次**都算（`09` §2.4 的预算要看得见）
        log::Info("{} {}（**Agent 模式**）：工具 {} 次 / 轮次 {} → {}", VisualStageCode(req.stage),
                  VisualStageName(req.stage), stats.callLog.size(), run->tool_steps,
                  tools.empty() ? "(没用工具)" : tools);
        rText = run->output_text;
    } else {
        ++out.llm_calls;
        auto r = call(LlmRole::Planner, std::string{spec.instructions}, user);
        if (!r) {
            out.error = fmt::format("LLM 调用失败：{}", r.error().message);
            return out;
        }
        rText = *r;
    }
    // —— 解析 `items[]`（中间产物：宽进严出 —— 结构不对就报错，字段缺只告警）——
    // S38：**先宽容提取**（同 V1；真实跑 V6 就是被"模型输出带围栏/说明"卡死的）
    std::string jsonTxt = util::json::ExtractJsonObject(rText);
    yyjson_doc* doc = yyjson_read(jsonTxt.data(), jsonTxt.size(), 0);
    // S43：**解析失败自动重试一次**（单轮模式）—— LLM 偶发生成**非法 JSON**：真实案例是
    // 字符串里写了**裸引号**（`"肩部随对方指向"关门"牌"`）⇒ 整份作废。模型有随机性，
    // 重试往往就过了；两次都不行才报错（**不静默**，且落盘原文供诊断）。
    // ⚠️ Agent 模式不在此重试（重跑一次工具循环的成本高，且要重复那套组装代码）—— 记账。
    // S47 修正：这里的判断**原先写死在 V4**（`stage == V4Spatial`），S46 推广到全阶段后**过时**了
    // —— 不改就会出现"V5 的 Agent 失败后又用单轮重试一次"这种错配（重试的形态跟原调用不一致）。
    if (doc == nullptr && !(req.use_agent_tools && !agentId.empty())) {
        log::Warn("{} 输出不是合法 JSON → **重试一次**（模型偶发生成非法 JSON）", code);
        ++out.llm_calls;
        if (auto r2 = call(LlmRole::Planner, std::string{spec.instructions}, user); r2) {
            rText = *r2;
            jsonTxt = util::json::ExtractJsonObject(rText);
            doc = yyjson_read(jsonTxt.data(), jsonTxt.size(), 0);
        }
    }
    // S47：**Agent 模式也要有重试**（哪怕退化成单轮）。⚠️ 原先刻意"Agent 不重试"，理由是
    // "重跑一次工具循环成本高" —— 但真跑实测把这个取舍证伪了：V2 在 Agent 模式下输出 8738 字
    // 时 JSON **被损坏**（很可能撞 `max_tokens` 截断）⇒ **一次就断掉整条链**（后面 5 个阶段
    // 全没机会上场），比"多跑一次"贵得多。这里退化成**单轮重试**（复用已经拼好的完整 `user`，
    // 不为省钱再跑一遍工具循环）；能救回链就是赚。
    if (doc == nullptr && req.use_agent_tools && !agentId.empty()) {
        log::Warn("{}（Agent 模式）输出不是合法 JSON → **退化成单轮重试一次**", code);
        ++out.llm_calls;
        if (auto r3 = call(LlmRole::Planner, std::string{spec.instructions}, user); r3) {
            rText = *r3;
            jsonTxt = util::json::ExtractJsonObject(rText);
            doc = yyjson_read(jsonTxt.data(), jsonTxt.size(), 0);
        }
    }
    if (doc == nullptr) {
        // S38：失败**留原始输出**到盘上（`vNN_raw_failed.txt`），并把前 300 字带进错误信息
        // —— 真实跑最需要的就是"模型到底回了什么"，原先只有一句"不是合法 JSON"没法查。
        if (!req.project_dir.empty()) {
            std::error_code ec;
            const auto d = std::filesystem::path{req.project_dir} / "work" /
                           fmt::format("ch{:03}", ch->ord);
            std::filesystem::create_directories(d, ec);
            (void)util::WriteFileBytes(d / fmt::format("{}_raw_failed.txt", code), rText);
        }
        out.error = fmt::format("{} 输出不是合法 JSON（原始 {} 字，前 300 字：{}）", code,
                                rText.size(), rText.substr(0, 300));
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
    std::set<std::pair<int, int>> gotKeys; // S47：本阶段实际产出的键（与 V1 骨架对账用）
    // S49：键 → 该镜这一条的**原文**（落库要用；与 `gotKeys` 同源，避免再解析一遍）
    std::map<std::pair<int, int>, std::string> payloadByKey;
    std::size_t i = 0;
    std::size_t max = 0;
    yyjson_val* it = nullptr;
    yyjson_arr_foreach(items, i, max, it) {
        if (!yyjson_is_obj(it)) {
            continue;
        }
        const yyjson_val* od = yyjson_obj_get(it, "ord");
        const yyjson_val* so = yyjson_obj_get(it, "scene_ord");
        const int ordV = yyjson_is_int(od) ? static_cast<int>(yyjson_get_sint(od)) : 0;
        const int sceneV = yyjson_is_int(so) ? static_cast<int>(yyjson_get_sint(so)) : 0;
        if (ordV <= 0) {
            ++missingOrd; // 缺 `ord` 就无法与镜对齐（下游按 `(scene_ord, ord)` 用）
        } else if (sceneV > 0) {
            gotKeys.insert({sceneV, ordV});
        }
        ++out.items;
        const char* raw = yyjson_val_write(it, 0, nullptr);
        const std::string one = raw != nullptr ? std::string{raw} : std::string{"{}"};
        if (raw != nullptr) {
            free(const_cast<char*>(raw));
        }
        itemsJson += (itemsJson.empty() ? "" : ",") + one;
        if (sceneV > 0 && ordV > 0) {
            payloadByKey[{sceneV, ordV}] = one; // S49：落库用（与该镜的键同源）
        }
    }
    yyjson_doc_free(doc);
    if (missingOrd > 0) {
        out.warnings.push_back(fmt::format("{} 条缺 `ord`（无法与镜对齐，下游按 `(scene_ord, ord)` 取用）",
                                           missingOrd));
    }
    if (out.items == 0) {
        out.warnings.push_back("没有产出任何条目（`items` 为空）");
    }
    // —— S49：**落库**（"库是唯一权威"，盘只是可重建的副本）——
    // ⚠️ 起因（S37 记的账）：这些产物**已经是世界状态的一部分**（V9 消费它们出分镜、V10 从
    //    V4 产物读空间层、S34 逐镜比对），**却只躺在盘上** ⇒ 想"按镜查上游设计"只能**扫盘再解析**。
    // 现在"一镜一行"落进 `stage_artifacts` ⇒ 查某镜的六阶段设计就是一句 `WHERE`。
    // ⚠️ **不阻断**：落库失败只告警（盘上产物仍是权威的续跑凭据；库是**查询入口**）。
    if (!gotKeys.empty()) {
        std::vector<novelcore::StageArtifactRow> rows;
        rows.reserve(gotKeys.size());
        for (const auto& k : gotKeys) {
            novelcore::StageArtifactRow row;
            row.chapter_id = req.chapter_id;
            row.stage = std::string{code};
            row.scene_ord = k.first;
            row.shot_ord = k.second;
            const auto hit = payloadByKey.find(k);
            row.payload_json = hit != payloadByKey.end() ? hit->second : "{}";
            row.input_state_hash = hash;
            rows.push_back(std::move(row));
        }
        if (auto wr = novelcore::NovelVisual(db).ReplaceStageArtifacts(req.chapter_id, code, rows);
            !wr) {
            out.warnings.push_back(
                fmt::format("阶段产物落库失败（不阻断，盘上产物仍在）：{}", wr.error().message));
        }
    }
    // S47：**与 V1 骨架对账**（跨阶段口径）。⚠️ 这是 S46 推广 Agent 模式后暴露的**真回归**：
    // user 精简成"场景清单"后模型不知道每场几镜 ⇒ 每阶段各编一套（实测 4/4/3/15/6/4 而骨架是 10）。
    // 单阶段内部自洽、**只有跨阶段对账**才看得出来 ⇒ 就地对账 + 告警（**不失败**：中间产物宽容）。
    if (!skelKeys.empty() && !gotKeys.empty()) {
        int lack = 0;
        int extra = 0;
        for (const auto& k : skelKeys) {
            if (gotKeys.find(k) == gotKeys.end()) {
                ++lack;
            }
        }
        for (const auto& k : gotKeys) {
            if (skelKeys.find(k) == skelKeys.end()) {
                ++extra;
            }
        }
        if (lack > 0 || extra > 0) {
            out.warnings.push_back(fmt::format(
                "与 V1 骨架**对不上**：缺 {} 镜 / 多 {} 镜（骨架 {} 镜 vs 本阶段 {} 镜）"
                " —— 下游一律按 `(scene_ord, ord)` 取用，对不齐会错位",
                lack, extra, skelKeys.size(), gotKeys.size()));
            log::Warn("{} 与 V1 骨架对不上：缺 {} / 多 {}（骨架 {} vs 本阶段 {}）", code, lack,
                      extra, skelKeys.size(), gotKeys.size());
        }
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
                                                            std::string_view extra_hint,
                                                            VisualStageId up_to, bool use_agent_tools) {
    StagesOutcome out;
    const VisualStageId order[] = {VisualStageId::V1SceneBreakdown, VisualStageId::V2DirectorIntent,
                                   VisualStageId::V3Performance,   VisualStageId::V4Spatial,
                                   VisualStageId::V5Camera,        VisualStageId::V6Timeline,
                                   VisualStageId::V7Audio};
    for (const VisualStageId st : order) {
        // S35 `--up-to`：到指定阶段为止（枚举按 V1→V7 顺序声明，可直接比大小）
        if (static_cast<int>(st) > static_cast<int>(up_to)) {
            out.detail += fmt::format("· （`--up-to {}` 到此为止，V{}–V7 未跑）\n",
                                      VisualStageCode(up_to),
                                      static_cast<int>(up_to) - static_cast<int>(VisualStageId::V1SceneBreakdown) + 1);
            break;
        }
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
                                 .extra_hint = std::string{extra_hint},
                                 .use_agent_tools = use_agent_tools});
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

    // ⑤ S35：**`--up-to`**（调试用：只跑到某阶段，省 LLM 调用）
    {
        const auto up = RunAllVisualStages(mem, mock, ch.value_or(0), dir, "",
                                           VisualStageId::V2DirectorIntent);
        expect(up.has_value() && up->ok, "S35：`--up-to V2` 应成功");
        if (up) {
            expect(up->detail.find("--up-to V2") != std::string::npos &&
                       up->detail.find("到此为止") != std::string::npos,
                   "S35：应显式说明到 V2 为止（V3–V7 未跑）");
            expect(up->stages_run == 2,
                   fmt::format("S35：应只跑 2 个阶段（实际 {}）", up->stages_run));
        }
        const auto p4 = ParseVisualStage("v4");
        expect(p4.has_value() && *p4 == VisualStageId::V4Spatial, "S35：`ParseVisualStage` 接受 v4");
        expect(!ParseVisualStage("V9").has_value() && !ParseVisualStage("abc").has_value(),
               "S35：非法 `--up-to` 应被拒（V9 / abc）");
    }

    // ⑥ S49：**阶段产物落库**（`stage_artifacts`，schema v12）—— 落进去、按镜能查回来、
    //    **整阶段重写不留残行**（这是"按镜查上游设计"能信的前提）
    {
        novelcore::NovelVisual vis(mem);
        const auto cid = ch.value_or(0);
        novelcore::StageArtifactRow r1;
        r1.chapter_id = cid;
        r1.stage = "V1";
        r1.scene_ord = 1;
        r1.shot_ord = 1;
        r1.payload_json = R"({"ord":1,"duration":3.0})";
        std::vector<novelcore::StageArtifactRow> rows{r1};
        novelcore::StageArtifactRow r2 = r1;
        r2.shot_ord = 2;
        r2.payload_json = R"({"ord":2,"duration":2.0})";
        rows.push_back(r2);
        expect(vis.ReplaceStageArtifacts(cid, "V1", rows).has_value(), "S49：阶段产物落库应成功");
        auto back = vis.ListStageArtifacts(cid, "V1");
        expect(back.has_value() && back->size() == 2,
               fmt::format("S49：应查回 2 行（实际 {}）", back.has_value() ? back->size() : 0));
        // **整阶段重写**：只留 1 行 ⇒ 旧的第 2 行**必须消失**（增量 upsert 会留残行，
        // 而残行会让"按镜查上游设计"读到**过期数据** —— 比没有更坏）
        rows.pop_back();
        expect(vis.ReplaceStageArtifacts(cid, "V1", rows).has_value(), "S49：整阶段重写应成功");
        auto back2 = vis.ListStageArtifacts(cid, "V1");
        expect(back2.has_value() && back2->size() == 1,
               fmt::format("S49：重写后只应剩 1 行、**不留残行**（实际 {}）",
                           back2.has_value() ? back2->size() : 0));
        auto forShot = vis.ListStageArtifactsForShot(cid, 1, 1);
        // ⚠️ 断言用 `>= 1`（不是 `== 1`）：本函数**前面**的阶段链自检（⑤）跑过
        // `RunAllVisualStages`、在同一个库里也落了 V1 行 —— 这里只验"**按镜查得到**"
        // 这件事本身（`(scene_ord, shot_ord)` 过滤生效 + 拿得到该镜的设计）。
        expect(forShot.has_value() && !forShot->empty() &&
                   forShot->front().stage == "V1" &&
                   forShot->front().payload_json.find("duration") != std::string::npos,
               fmt::format("S49：**按镜查询**应能拿到该镜的阶段设计（实际 行数={} 首行 stage='{}' "
                           "payload='{}' err='{}'）",
                           forShot.has_value() ? forShot->size() : 0,
                           forShot.has_value() && !forShot->empty() ? forShot->front().stage : "?",
                           forShot.has_value() && !forShot->empty() ? forShot->front().payload_json
                                                                    : "?",
                           forShot.has_value() ? "" : forShot.error().message));
    }

    // ⑦ S51：**复用分支的落库补写**（`BackfillStageArtifacts`）—— 老工程（S49 落库之前跑过的）
    //    数据只在盘上，复用分支原先**直接 return、跳过落库** ⇒ 补写是必需的（且必须**幂等**）。
    {
        const auto cid2 = ch.value_or(0);
        novelcore::NovelVisual vis(mem);
        const auto artDir = std::filesystem::path{dir} / "work" / "ch001";
        std::filesystem::create_directories(artDir, ec);
        const std::string art =
            R"-({"stage":"V6","items":[{"scene_ord":1,"ord":1,"duration":3.0}]})-";
        expect(util::WriteFileBytes(artDir / "v06_timeline.json", art), "S51：写盘上产物");
        expect(BackfillStageArtifacts(mem, cid2, "V6", "h6", artDir / "v06_timeline.json"),
               "S51：**库空 + 盘上有产物 ⇒ 补写应成功**");
        auto r1 = vis.ListStageArtifacts(cid2, "V6");
        expect(r1.has_value() && r1->size() == 1 && r1->front().shot_ord == 1,
               fmt::format("S51：补写后应能查到 1 行（实际 {}）", r1.has_value() ? r1->size() : 0));
        // **幂等**：库里已有 ⇒ 直接返回 true、**不重写**（否则每次复用都白写一遍库）
        expect(BackfillStageArtifacts(mem, cid2, "V6", "h6", artDir / "v06_timeline.json"),
               "S51：第二次补写也应成功（幂等）");
        auto r2 = vis.ListStageArtifacts(cid2, "V6");
        expect(r2.has_value() && r2->size() == 1, "S51：补写幂等（不应重复写）");
        // 库空 + 盘上**没有**产物 ⇒ 返回 false（**不瞎写**）
        expect(!BackfillStageArtifacts(mem, cid2, "V7", "h7", artDir / "不存在.json"),
               "S51：库空且盘上产物缺失 ⇒ 应返回 false（不瞎写空数据）");
        std::filesystem::remove_all(dir, ec);
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

// S42：**阶段 system 提示词的唯一来源**（放在匿名 namespace **之外** —— 上面的 `kV*Spec` 都是
// 内部链接，但本函数要给 `AgentKit.cpp` 用，所以必须外部链接）。
// 起因：V4 走 Agent 时在 `AgentKit::DefaultPromptFor` 里**又抄了一份**提示词 ⇒ 两处必须同步、
// 迟早分叉。现在只此一处，`AgentKit` 直接取。
std::string_view StageSystemPrompt(VisualStageId stage) noexcept {
    switch (stage) {
    case VisualStageId::V1SceneBreakdown:
        return kSceneBreakdownInstructions;
    case VisualStageId::V2DirectorIntent:
        return kV2Spec.instructions;
    case VisualStageId::V3Performance:
        return kV3Spec.instructions;
    case VisualStageId::V4Spatial:
        return kV4Spec.instructions;
    case VisualStageId::V5Camera:
        return kV5Spec.instructions;
    case VisualStageId::V6Timeline:
        return kV6Spec.instructions;
    case VisualStageId::V7Audio:
        return kV7Spec.instructions;
    }
    return {};
}

std::string_view StageAgentId(VisualStageId stage) noexcept {
    // S46：与 `BuiltinAgents()` 注册的名字**必须一致**（同源，别再手写字符串）。
    // V1 刻意不给 Agent：它是链路起点（输入是章节正文），没有"可查的上游事实"。
    switch (stage) {
    case VisualStageId::V2DirectorIntent: return "v2_director_intent";
    case VisualStageId::V3Performance: return "v3_performance";
    case VisualStageId::V4Spatial: return "v4_spatial";
    case VisualStageId::V5Camera: return "v5_camera";
    case VisualStageId::V6Timeline: return "v6_timeline";
    case VisualStageId::V7Audio: return "v7_audio";
    default: return {};
    }
}

} // namespace shine::agent
