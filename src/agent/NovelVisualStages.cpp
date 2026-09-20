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
        return std::string{R"-({"scenes":[{"scene_ord":1,"goal":"找到账本","conflict":"守卫醒了","emotion":"紧张","information":"账本在柜里","environment":"昏暗库房","character_goals":[{"entity_id":1,"goal":"拿到账本"}],"important_props":["账本"],"shots":[{"ord":1,"duration":3.0,"beat":"推门进入"},{"ord":2,"duration":2.5,"beat":"听见脚步"}]},{"scene_ord":2,"goal":"逃出","conflict":"","emotion":"急","information":"","environment":"走廊","character_goals":[],"important_props":[],"shots":[{"ord":1,"duration":4.0,"beat":"奔跑"}]}]})-"};
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
