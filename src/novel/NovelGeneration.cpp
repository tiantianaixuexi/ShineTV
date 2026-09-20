#include "novel/NovelGeneration.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Strings.h"
#include "video/NovelShotBridge.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <map>

namespace shine::novelcore {
namespace {

// `references_json`（`PromptArtifact` 的参考图列表；元素是 `visual_assets.sheet_rel_path`）
[[nodiscard]] std::vector<std::string> ParseRefs(std::string_view json) {
    std::vector<std::string> out;
    yyjson_doc* d = yyjson_read(json.data(), json.size(), 0);
    if (d == nullptr) {
        return out;
    }
    yyjson_val* root = yyjson_doc_get_root(d);
    if (yyjson_is_arr(root)) {
        std::size_t i = 0;
        std::size_t max = 0;
        yyjson_val* v = nullptr;
        yyjson_arr_foreach(root, i, max, v) {
            if (yyjson_is_str(v)) {
                const std::string s = yyjson_get_str(v);
                if (!util::Trim(s).empty()) {
                    out.push_back(s);
                }
            }
        }
    }
    yyjson_doc_free(d);
    return out;
}

// 该镜的"主角色资产"：`visual_artifacts` **要求 `asset_id > 0`**（`UpsertArtifact` 会拒绝 0），
// 所以用 `character_ids_json` 的第一个角色去解析视觉资产。没有 → 0（不写 shot 层产物，
// 但出图与 `generated_images` 账照旧，并在 warnings 里说明）。
[[nodiscard]] RowId PrimaryAssetOf(NovelVisual& visual, std::string_view characterIdsJson) {
    yyjson_doc* d = yyjson_read(characterIdsJson.data(), characterIdsJson.size(), 0);
    if (d == nullptr) {
        return 0;
    }
    RowId entity = 0;
    yyjson_val* root = yyjson_doc_get_root(d);
    if (yyjson_is_arr(root) && yyjson_arr_size(root) > 0) {
        yyjson_val* v = yyjson_arr_get(root, 0);
        if (yyjson_is_int(v)) {
            entity = yyjson_get_sint(v);
        }
    }
    yyjson_doc_free(d);
    if (entity <= 0) {
        return 0;
    }
    if (auto a = visual.FindAssetByEntity(entity); a) {
        return a->id;
    }
    return 0;
}

} // namespace

std::string GenerationOutcome::Describe() const {
    return fmt::format("{} 镜（新提交 {} / 跳过 {} / 失败 {}）", shots_seen, images_submitted,
                       skipped_existing, failed);
}

GenerationOutcome RunChapterGeneration(::shine::db::sqlite::Database& db, RowId chapter_id,
                                       std::string_view project_dir, bool force) {
    GenerationOutcome out;
    if (!db.isOpen()) {
        out.error = "数据库未打开";
        return out;
    }
    if (chapter_id <= 0) {
        out.error = "需要 chapter_id";
        return out;
    }
    NovelVisual visual(db);
    // V11 的输入就是 V10 的产出：**每个镜的最新版** PromptArtifact。
    // `ListPromptArtifacts` 按 `updated DESC,id DESC` 返回 → 第一条即最新版（PV3 后同镜会有多行）。
    auto arts = visual.ListPromptArtifacts(chapter_id);
    if (!arts || arts->empty()) {
        out.error = "该章没有 PromptArtifact —— 先跑 V10（`--novel-prompt`）";
        return out;
    }
    std::map<RowId, PromptArtifactRow> latest;
    for (const PromptArtifactRow& r : *arts) {
        if (r.chain == "visual" && r.shot_id > 0 && latest.find(r.shot_id) == latest.end()) {
            latest[r.shot_id] = r;
        }
    }
    // K20/K21 的账（`06` §2.3 的 `contract-input` 两类：**结论产生在生成侧**）—— 结束时落
    // `work/ch<NNN>/generation_checks.json`，给 K 校验当盘上数据源。
    bool sizeAligned = true;
    bool refLimitOk = true;
    int refsMax = 0;
    std::vector<std::string> issueIds;

    for (const auto& [shotId, art] : latest) {
        ++out.shots_seen;
        GenerationShotOutcome so;
        so.shot_id = shotId;
        so.prompt_artifact_id = art.id;
        so.prompt_version = art.version;
        // 幂等：该镜已有产物（PV5 的 `generation_ref` 正好当"已出图"标志用）→ 不重复烧出图
        if (!force && !art.generation_ref.empty()) {
            ++out.skipped_existing;
            so.ok = true;
            so.error = fmt::format("已出图（generation_ref={}）→ 跳过", art.generation_ref);
            out.shots.push_back(so);
            continue;
        }
        auto shot = visual.GetShot(shotId);
        if (!shot) {
            // **单镜失败不阻断**（`09` §2.2 的隔离）：记 error，继续下一镜
            so.error = fmt::format("镜 #{} 不在库里（shots 行缺失）", shotId);
            ++out.failed;
            out.shots.push_back(so);
            continue;
        }
        const std::vector<std::string> refs = ParseRefs(art.references_json);
        refsMax = std::max(refsMax, static_cast<int>(refs.size()));
        // —— 桥：提示词 → `VideoProject`（K20/K21 的纠正与记账在 `ToGenShot` 里，不重复实现）——
        // `11` §2.5 明确：prompt **必须来自 PromptArtifact**，不得直接用叙事 `prompt_text`。
        video::NarrativeSceneInput sceneIn;
        sceneIn.sceneTitle = fmt::format("scene{}", shot->scene_id);
        video::NarrativeShotInput shotIn;
        shotIn.shotId = fmt::format("shot{}", shotId); // 稳定键（seed 由它派生，禁止纯随机）
        shotIn.ord = shot->ord;
        shotIn.prompt = art.prompt;
        shotIn.negativePrompt = art.negative;
        shotIn.referenceImages = refs;
        shotIn.characterAssetPaths = refs; // `11` §2.5：角色资产 → 生成侧的角色图（同源）
        sceneIn.shots.push_back(shotIn);
        const video::ToGenShotResult bridge =
            video::ToGenShot(video::VideoProject::MakeDefault(), sceneIn, {});
        if (!bridge.ok) {
            so.error = fmt::format("桥转换失败：{}", bridge.error);
            ++out.failed;
            out.shots.push_back(so);
            continue;
        }
        for (const video::GenCheckIssue& iss : bridge.issues) {
            if (iss.checkId == "K20") {
                sizeAligned = false;
            }
            if (iss.checkId == "K21") {
                refLimitOk = false;
            }
            issueIds.push_back(iss.checkId);
            out.warnings.push_back(
                fmt::format("镜 #{} {} [{}] {}", shotId, iss.checkId, iss.severity, iss.detail));
        }
        if (!bridge.degradations.empty()) {
            out.warnings.push_back(fmt::format("镜 #{} 有 {} 条降级（详见生成侧账）", shotId,
                                               bridge.degradations.size()));
        }
        int width = 0;
        int height = 0;
        int steps = 0;
        if (!bridge.project.shots.empty()) {
            // `ToGenShot` 产出的是 **K20 纠正后**的值 → 出图直接用它（不再各算一遍对齐）
            const video::Shot& s = bridge.project.shots.front();
            width = s.width;
            height = s.height;
            steps = s.steps;
        }
        // —— 提交出图队列（`RunImageJob`：assemble → queue 行 → Backend.Generate）——
        ImageJobInput ji;
        ji.prompt = art.prompt;
        ji.negative = art.negative;
        ji.shot_id = shotId;
        ji.chapter_id = chapter_id;
        ji.scene_id = shot->scene_id;
        ji.width = width;
        ji.height = height;
        ji.steps = steps;
        auto img = RunImageJob(db, ji);
        if (!img) {
            so.error = fmt::format("出图失败：{}", img.error().message);
            ++out.failed;
            out.shots.push_back(so);
            continue;
        }
        so.generated_image_id = img->id;
        so.job_id = img->job_id;
        so.rel_path = img->rel_path;
        // —— 写 shot 层产物：`visual_artifacts.prompt_artifact_id` 从此**不再是空指针** ——
        const RowId assetId = PrimaryAssetOf(visual, shot->character_ids_json);
        if (assetId > 0) {
            VisualArtifactRow va;
            va.asset_id = assetId;
            va.layer = "shot";
            va.chapter_scope = chapter_id;
            va.rel_path = img->rel_path;
            va.prompt_artifact_id = art.id;
            va.job_id = img->job_id;
            va.status = (img->status == "FAILED") ? "FAILED" : "DONE";
            if (auto vid = visual.UpsertArtifact(va); vid) {
                so.visual_artifact_id = *vid;
                // —— PV5 回填：`PromptArtifact.generation_ref` → 指向生成结果（**双向可查**）——
                PromptArtifactRow upd = art;
                upd.generation_ref = fmt::format("va:{}", *vid);
                if (auto w = visual.UpsertPromptArtifact(upd); !w) {
                    out.warnings.push_back(fmt::format("镜 #{} 回填 generation_ref 失败：{}", shotId,
                                                       w.error().message));
                }
            } else {
                out.warnings.push_back(fmt::format("镜 #{} 写 shot 层产物失败：{}", shotId,
                                                   vid.error().message));
            }
        } else {
            // 没有可挂资产 → 不写 shot 层产物（`visual_artifacts` 硬要求 `asset_id>0`），
            // 但 `generation_ref` **照样回填**：直接指向 `generated_images` 行（前缀 `img:`）——
            // 这更贴 `02` §2.11 的 `GenerationResult`（出图任务就是生成结果）。
            // ⚠️ 若不这么做，这类镜**每次跑都会重出图**（幂等判据正是 `generation_ref` 非空）——
            // S27 真跑时踩到：run1 写了产出、run2 却对这两镜又提交了一次出图。
            PromptArtifactRow upd = art;
            upd.generation_ref = fmt::format("img:{}", img->id);
            if (auto w = visual.UpsertPromptArtifact(upd); !w) {
                out.warnings.push_back(fmt::format("镜 #{} 回填 generation_ref 失败：{}", shotId,
                                                   w.error().message));
            }
            out.warnings.push_back(fmt::format(
                "镜 #{} 没有可挂的视觉资产 → 不写 shot 层产物（`visual_artifacts` 要求 "
                "asset_id>0）；`generation_ref` 直接指向 `generated_images`（img:{}）",
                shotId, img->id));
        }
        so.ok = (img->status != "FAILED");
        if (so.ok) {
            ++out.images_submitted;
        } else {
            ++out.failed;
            so.error = img->error.empty() ? "出图后端报 FAILED" : img->error;
        }
        out.shots.push_back(so);
    }
    // —— 盘上账：K19–K21 的事实（`06` §2.3 的 `contract-input` 类的数据源）——
    // K19 要 Comfy `/object_info`：本轮 V11 未必出到 Comfy（后端可为 mock/openai）→ **如实记 false**，
    // 不假装"图校验过了"（`06` §2.3 的四态里，没跑就是没跑）。
    if (!project_dir.empty()) {
        int chapterOrd = 0;
        if (auto st = db.Prepare("SELECT ord FROM chapters WHERE id=?1"); st) {
            (void)st->BindInt(1, chapter_id);
            if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                chapterOrd = static_cast<int>(st->ColumnInt(0));
            }
        }
        if (chapterOrd > 0) {
            const auto dir = std::filesystem::path{std::string{project_dir}} / "work" /
                             fmt::format("ch{:03}", chapterOrd);
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::string ids;
            for (const std::string& id : issueIds) {
                if (!ids.empty()) {
                    ids += ",";
                }
                ids += fmt::format("\"{}\"", id);
            }
            const std::string j = fmt::format(
                "{{\"stage\":\"V11\",\"chapter_id\":{},\"shots\":{},\"has_graph\":false,"
                "\"graph_ok\":false,\"size_aligned\":{},\"ref_limit_ok\":{},\"refs_max\":{},"
                "\"issue_count\":{},\"issue_ids\":[{}]}}",
                chapter_id, out.shots_seen, sizeAligned ? "true" : "false",
                refLimitOk ? "true" : "false", refsMax, issueIds.size(), ids);
            if (util::WriteFileBytes(dir / "generation_checks.json", j)) {
                out.checks_path = util::PathToUtf8(dir / "generation_checks.json");
            } else {
                out.warnings.push_back("generation_checks.json 落盘失败（work/ 非权威，不阻断）");
            }
        }
    }
    // 把**每镜**的失败原因收进 `warnings`（`11` §2.7 W2「降级必须可见」的同款要求：
    // 失败也必须可见）—— 否则调用方只看到"没有产出任何出图任务"，无从知道是哪一镜、为什么
    // （S27 真跑时踩到：CLI 打了全局错误却没打每镜原因，排查只能靠猜）。
    for (const GenerationShotOutcome& s : out.shots) {
        if (!s.ok && !s.error.empty()) {
            out.warnings.push_back(fmt::format("镜 #{}（PromptArtifact #{}）：{}", s.shot_id,
                                               s.prompt_artifact_id, s.error));
        }
    }
    out.ok = out.images_submitted > 0 || out.skipped_existing > 0;
    if (!out.ok && out.error.empty()) {
        out.error = fmt::format("没有产出任何出图任务（{}）", out.Describe());
    }
    if (!out.error.empty()) {
        return out;
    }
    log::Info("V11 出图产出：章={} 镜={} 新提交={} 跳过={} 失败={}", chapter_id, out.shots_seen,
              out.images_submitted, out.skipped_existing, out.failed);
    return out;
}

bool RunGenerationSelfCheck() {
    int fails = 0;
    const auto expect = [&fails](bool cond, const std::string& msg) {
        if (!cond) {
            log::Error("V11 自检 FAIL：{}", msg);
            ++fails;
        }
    };
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("V11 自检：内存库打开失败 {}", r.error().message);
        return false;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("V11 自检：建 schema 失败 {}", r.error().message);
        return false;
    }
    NovelGraph g(mem);
    NovelVisual v(mem);
    // 自检**不发网络**：出图后端临时换成 `mock`（P9 自检同款做法；默认配置可能是 `comfy`
    // → 那个后端是 stub，会直接报"将在 P9.2 接入"）。RAII 保证**任何提前 return 都会恢复**设置，
    // 否则会把后面的自检（甚至 GUI 的真实行为）带偏。
    struct SettingsGuard {
        AppSettings saved;
        SettingsGuard() : saved(Settings()) {}
        ~SettingsGuard() { Settings() = saved; }
        SettingsGuard(const SettingsGuard&) = delete;
        SettingsGuard& operator=(const SettingsGuard&) = delete;
    } settingsGuard;
    Settings().imageBackend = "mock";
    Settings().imageOutputRelDir = "visual/gen";
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章"});
    auto sc = g.UpsertScene({.chapter_id = ch.value_or(0), .ord = 1, .title = "库房"});
    auto pov = g.UpsertEntity({.kind = std::string{kind::person}, .name = "自检角色"});
    auto asset = v.UpsertAsset({.entity_id = pov.value_or(0),
                                .kind = "character",
                                .name = "自检角色",
                                .base_desc = "a wounded youth",
                                .sheet_rel_path = "visual/gen/selfcheck_sheet.png",
                                .status = "READY"});
    expect(asset.has_value(), "建资产");
    auto shot = v.UpsertShot({.scene_id = sc.value_or(0),
                              .ord = 1,
                              .duration_note = "3.0s",
                              .character_ids_json = fmt::format("[{}]", pov.value_or(0)),
                              .prompt_text = "他按住伤口",
                              .start_state_json = R"({"lighting":"夜"})",
                              .end_state_json = R"({"lighting":"夜"})",
                              .timeline_json =
                                  R"({"duration_s":3.0,"beats":[{"begin_s":0.0,"end_s":3.0}]})"});
    expect(shot.has_value(), "建镜");
    // V11 的输入：直接写一条 PromptArtifact（**不真跑 V10** —— 那是 V10 自检的事）
    const auto mkArt = [&](RowId shotId) {
        PromptArtifactRow a;
        a.chapter_id = ch.value_or(0);
        a.scene_id = sc.value_or(0);
        a.shot_id = shotId;
        a.target_kind = "shot";
        a.target_id = shotId;
        a.chain = "visual";
        a.stage = "V10";
        a.input_state_hash = "sha1:deadbeef";
        a.prompt = "a wounded youth, left arm bandaged, night warehouse";
        a.negative = "lowres";
        a.references_json = R"(["visual/gen/selfcheck_sheet.png"])";
        return v.UpsertPromptArtifact(a);
    };
    const auto artId = mkArt(shot.value_or(0));
    expect(artId.has_value(), "写 PromptArtifact 账");

    // ① 主路径：提交出图 → 写 shot 层产物 → 回填 generation_ref
    const GenerationOutcome first = RunChapterGeneration(mem, ch.value_or(0));
    expect(first.ok && first.images_submitted == 1 && first.failed == 0,
           fmt::format("V11 应提交 1 镜出图（实际 {}）", first.Describe()));
    expect(first.shots.size() == 1 && !first.shots.front().job_id.empty(),
           "出图任务应带 job_id");
    const RowId vaId = first.shots.empty() ? 0 : first.shots.front().visual_artifact_id;
    expect(vaId > 0, "应写 shot 层 visual_artifacts 行");
    // ② 双向可查（PV5）
    if (vaId > 0) {
        auto va = v.GetArtifact(vaId);
        expect(va.has_value() && va->prompt_artifact_id == artId.value_or(0) && va->layer == "shot",
               "visual_artifacts.prompt_artifact_id 指向该镜的 PromptArtifact（反向可查）");
        expect(va.has_value() && !va->job_id.empty(), "shot 层产物应带 job_id");
    }
    if (artId) {
        auto refreshed = v.GetPromptArtifact(*artId);
        expect(refreshed.has_value() && refreshed->generation_ref == fmt::format("va:{}", vaId),
               fmt::format("PV5：generation_ref 回填为 va:<id>（实际 [{}]）",
                           refreshed ? refreshed->generation_ref : std::string{"(读失败)"}));
    }
    // ③ 幂等：再跑一次 → 跳过（不重复烧出图）
    const GenerationOutcome second = RunChapterGeneration(mem, ch.value_or(0));
    expect(second.ok && second.images_submitted == 0 && second.skipped_existing == 1,
           fmt::format("幂等：已出图的镜应跳过（实际 {}）", second.Describe()));
    // ④ 单镜失败不阻断：再插一条指向**不存在**的镜的 PromptArtifact（force 让第一镜也真跑）
    const auto ghost = mkArt(9999);
    expect(ghost.has_value(), "写幽灵镜的 PromptArtifact 账");
    const GenerationOutcome third = RunChapterGeneration(mem, ch.value_or(0), {}, true);
    expect(third.shots_seen == 2 && third.failed == 1 && third.images_submitted == 1,
           fmt::format("单镜失败不阻断：2 镜应成功 1 / 失败 1（实际 {}）", third.Describe()));
    bool ghostReported = false;
    for (const GenerationShotOutcome& s : third.shots) {
        if (s.shot_id == 9999 && !s.error.empty()) {
            ghostReported = true;
        }
    }
    expect(ghostReported, "失败镜必须在结果里**显式**记账（不静默）");
    // ⑤ 没有 PromptArtifact 的章 → 明确报错（V11 的输入前提）
    auto ch2 = g.UpsertChapter({.ord = 2, .title = "第二章"});
    const GenerationOutcome bad = RunChapterGeneration(mem, ch2.value_or(0));
    expect(!bad.ok && bad.error.find("V10") != std::string::npos,
           "没有 PromptArtifact 的章应提示先跑 V10");

    if (fails == 0) {
        log::Info("V11 自检通过（提交出图 → shot 层产物 + generation_ref 双向可查 + 幂等 + 单镜隔离）");
    }
    return fails == 0;
}

} // namespace shine::novelcore
