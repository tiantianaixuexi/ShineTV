#include "novel/NovelPromptGen.h"

#include "core/Log.h"
#include "novel/NovelChecks.h" // ComputeInputStateHash（`04` §2.5 的唯一来源）
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Json.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <map>

namespace shine::novelcore {
namespace {

// 该镜出场角色（`character_ids_json`）的参考图：`visual_assets.sheet_rel_path`（`11` §2.5 的
// `references`）—— 桥的注释说"这类解析由小说侧做完再传进来"，这里就是那个"小说侧"。
[[nodiscard]] std::string ResolveReferences(NovelVisual& visual, std::string_view characterIdsJson,
                                           int* resolved) {
    std::string out = "[";
    bool first = true;
    yyjson_doc* doc = yyjson_read(characterIdsJson.data(), characterIdsJson.size(), 0);
    if (doc != nullptr) {
        yyjson_val* root = yyjson_doc_get_root(doc);
        if (yyjson_is_arr(root)) {
            std::size_t i = 0;
            std::size_t max = 0;
            yyjson_val* v = nullptr;
            yyjson_arr_foreach(root, i, max, v) {
                if (!yyjson_is_int(v) || yyjson_get_sint(v) <= 0) {
                    continue;
                }
                auto asset = visual.FindAssetByEntity(yyjson_get_sint(v));
                if (!asset || asset->sheet_rel_path.empty()) {
                    continue;
                }
                out += fmt::format("{}\"{}\"", first ? "" : ",", asset->sheet_rel_path);
                first = false;
                if (resolved != nullptr) {
                    ++(*resolved);
                }
            }
        }
        yyjson_doc_free(doc);
    }
    return out + "]";
}

} // namespace

std::string PromptGenOutcome::Describe() const {
    if (!ok) {
        return "V10 提示词产出失败：" + error;
    }
    return fmt::format("V10 已产出：{} 镜（新写 {} / 复用 {}，参考图 {} 条）{}", shots_seen,
                       artifacts_written, reused, refs_resolved,
                       warnings.empty() ? std::string{} : fmt::format("；{} 条提示", warnings.size()));
}

PromptGenOutcome GeneratePromptArtifacts(::shine::db::sqlite::Database& db, RowId chapter_id) {
    PromptGenOutcome out;
    if (!db.isOpen()) {
        out.error = "数据库未打开";
        return out;
    }
    if (chapter_id <= 0) {
        out.error = "需要 chapter_id";
        return out;
    }
    NovelVisual visual(db);
    auto shots = visual.ListShotsByChapter(chapter_id);
    if (!shots || shots->empty()) {
        out.error = "该章没有 shots —— 先跑 V9 `STORYBOARD`（`NarrativeShot[]` → `shots`）";
        return out;
    }
    // PV1：本轮的输入状态指纹（chain=visual / stage=V10）
    const std::string hash = ComputeInputStateHash(db, chapter_id, "visual", "V10");
    // 已有的 V10 账（按 shot_id 索引）：用来判"复用"与"覆盖"
    std::map<RowId, PromptArtifactRow> existing;
    if (auto list = visual.ListPromptArtifacts(chapter_id); list) {
        for (const PromptArtifactRow& r : *list) {
            if (r.stage == "V10" && r.chain == "visual" && r.shot_id > 0) {
                existing[r.shot_id] = r;
            }
        }
    }

    for (const ShotRow& shot : *shots) {
        ++out.shots_seen;
        // 该镜的**全部**出场角色（`contract 02` §2.7 的 `StateSnapshot.characters[]` 是数组）——
        // `Assemble` 靠它们解析各自的资产 base 层与阶段外观（不传 → 拼不出角色外观，
        // prompt 只剩动作/镜头层）。S25 起**所有**角色都拼，不再只拼第一个。
        std::vector<RowId> shotChars;
        {
            yyjson_doc* d =
                yyjson_read(shot.character_ids_json.data(), shot.character_ids_json.size(), 0);
            if (d != nullptr) {
                yyjson_val* root = yyjson_doc_get_root(d);
                if (yyjson_is_arr(root)) {
                    std::size_t i = 0;
                    std::size_t max = 0;
                    yyjson_val* v = nullptr;
                    yyjson_arr_foreach(root, i, max, v) {
                        if (yyjson_is_int(v) && yyjson_get_sint(v) > 0) {
                            shotChars.push_back(yyjson_get_sint(v));
                        }
                    }
                }
                yyjson_doc_free(d);
            }
        }
        AssemblePromptInput in{.chapter_id = chapter_id,
                               .scene_id = shot.scene_id,
                               .shot_id = shot.id,
                               .camera_id = shot.camera_id,
                               .composition_id = shot.composition_id,
                               .lighting_id = shot.lighting_id};
        if (!shotChars.empty()) {
            in.character_id = shotChars.front(); // 主角色（视为主视角/前景）
            in.character_ids = shotChars;        // S25：**全部**出场角色，人人都要有外观
        }
        auto art = visual.Assemble(in);
        if (!art) {
            out.warnings.push_back(fmt::format("镜 #{} 组装失败：{}", shot.id, art.error().message));
            continue;
        }
        if (util::Trim(art->final_prompt).empty()) {
            out.warnings.push_back(fmt::format(
                "镜 #{} 没有可拼的视觉资产/层（final_prompt 为空）→ **跳过**（不产假产物）", shot.id));
            continue;
        }
        // PV2 / 不变式 I9：哈希一致 → **允许复用**（不重写）；不一致 → 重新生成
        auto it = existing.find(shot.id);
        if (it != existing.end() && it->second.input_state_hash == hash &&
            !it->second.prompt.empty()) {
            ++out.reused;
            continue;
        }
        PromptArtifactRow row;
        if (it != existing.end()) {
            row.id = it->second.id; // 覆盖同镜的上一版（表无 version 列 → 版本记在 model_hint）
        }
        row.chapter_id = chapter_id;
        row.scene_id = shot.scene_id;
        row.shot_id = shot.id;
        row.target_kind = "shot";
        row.target_id = shot.id;
        row.chain = "visual";
        row.stage = "V10";
        row.input_state_hash = hash;
        row.prompt = art->final_prompt;
        row.negative = art->negative_prompt;
        row.references_json = ResolveReferences(visual, shot.character_ids_json, &out.refs_resolved);
        row.model_hint = fmt::format("v={} layers={}", it != existing.end() ? 2 : 1,
                                     art->used_layer_ids.size());
        if (auto w = visual.UpsertPromptArtifact(row); !w) {
            out.warnings.push_back(fmt::format("镜 #{} 写账失败：{}", shot.id, w.error().message));
            continue;
        }
        ++out.artifacts_written;
        // `13` §2.3 的一致性检查（发色/人数/未登场…）：issue 只记不阻断（是否有质量门禁由人定）
        if (auto iss = visual.CheckConsistency(in, art->final_prompt); iss && !iss->empty()) {
            out.warnings.push_back(fmt::format("镜 #{} 一致性问题：{}", shot.id, iss->front()));
        }
    }

    out.ok = out.artifacts_written > 0 || out.reused > 0;
    if (!out.ok) {
        out.error = "没有产出任何 PromptArtifact";
    } else {
        log::Info("V10 提示词产出：章={} 镜={} 新写={} 复用={}", chapter_id, out.shots_seen,
                  out.artifacts_written, out.reused);
    }
    return out;
}

// ═══════════════════════ 自检 ═══════════════════════
bool RunPromptGenSelfCheck() {
    ::shine::db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("V10 自检：内存库打开失败 {}", r.error().message);
        return false;
    }
    if (auto r = NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("V10 自检：建表失败 {}", r.error().message);
        return false;
    }
    int fails = 0;
    const auto expect = [&fails](bool cond, std::string_view what) {
        if (!cond) {
            ++fails;
            log::Error("V10 自检 FAIL：{}", what);
        }
    };
    NovelGraph g(mem);
    NovelVisual v(mem);
    auto pov = g.UpsertEntity({.kind = std::string{kind::person}, .name = "自检角色"});
    expect(pov.has_value(), "建角色");
    auto ch = g.UpsertChapter({.ord = 1, .title = "第一章", .pov_entity_id = pov.value_or(0)});
    auto sc = g.UpsertScene({.chapter_id = ch.value_or(0), .ord = 1, .title = "第一场"});
    expect(sc.has_value(), "建场");

    // 最小视觉输入：资产（带 sheet_rel_path）+ 一个视觉阶段
    auto asset = v.UpsertAsset({.entity_id = pov.value_or(0),
                                .kind = "character",
                                .name = "自检角色",
                                .base_desc = "a wounded youth in dark coat",
                                .sheet_rel_path = "visual/gen/selfcheck_sheet.png",
                                .status = "READY"});
    expect(asset.has_value(), "建资产");
    if (asset) {
        auto st = v.UpsertState({.asset_id = *asset,
                                 .stage_key = "S1",
                                 .stage_label = "初始",
                                 .from_chapter = 1,
                                 .appearance = "left arm bandaged",
                                 .canon_status = "CANON"});
        expect(st.has_value(), "建视觉阶段");
    }
    // S25：**第二个角色**（多角色的证据）—— 一镜两人时，两人都必须有自己的外观
    auto pov2 = g.UpsertEntity({.kind = std::string{kind::person}, .name = "自检配角"});
    expect(pov2.has_value(), "建配角");
    auto asset2 = v.UpsertAsset({.entity_id = pov2.value_or(0),
                                 .kind = "character",
                                 .name = "自检配角",
                                 .base_desc = "a tall woman in a red cloak",
                                 .sheet_rel_path = "visual/gen/selfcheck_sheet2.png",
                                 .status = "READY"});
    expect(asset2.has_value(), "建配角资产");
    if (asset2) {
        (void)v.UpsertState({.asset_id = *asset2,
                             .stage_key = "S1",
                             .from_chapter = 1,
                             .appearance = "red cloak, hood up",
                             .canon_status = "CANON"});
    }
    auto shot = v.UpsertShot({.scene_id = sc.value_or(0),
                              .ord = 1,
                              .duration_note = "3.0s",
                              .character_ids_json = fmt::format("[{},{}]", pov.value_or(0),
                                                                pov2.value_or(0)),
                              .prompt_text = "他按住伤口",
                              .start_state_json = R"({"lighting":"夜","environment":"库房"})",
                              .end_state_json = R"({"lighting":"夜","environment":"库房"})",
                              .timeline_json = R"({"duration_s":3.0,"beats":[{"begin_s":0.0,"end_s":3.0}]})"});
    expect(shot.has_value(), "建镜");

    // ① 成功路径：产账 + 参考图解析
    const PromptGenOutcome first = GeneratePromptArtifacts(mem, ch.value_or(0));
    expect(first.ok, fmt::format("V10 应成功：{}", first.error));
    expect(first.artifacts_written == 1, "应写 1 条 V10 账");
    expect(first.refs_resolved == 2, "参考图应从**两个**角色的 sheet_rel_path 解析出来");
    auto list = v.ListPromptArtifacts(ch.value_or(0));
    expect(list.has_value() && !list->empty(), "prompt_artifacts 应有行");
    if (list && !list->empty()) {
        const PromptArtifactRow& r = list->front();
        expect(r.chain == "visual" && r.stage == "V10" && r.shot_id == shot.value_or(0),
               "账的 chain/stage/shot_id 正确");
        expect(!r.input_state_hash.empty(), "PV1：必须带 input_state_hash");
        expect(!r.prompt.empty() && r.prompt.find("a wounded youth") != std::string::npos,
               "prompt 来自九层组装（含主角色的 base 层文案）");
        // S25（多角色）：**第二个角色也必须拼进去** —— 否则一镜两人时第 2 人隐形
        expect(r.prompt.find("a tall woman in a red cloak") != std::string::npos,
               "第二个出场角色的 base 层也在 prompt 里（S25 多角色）");
        expect(r.prompt.find("red cloak, hood up") != std::string::npos,
               "第二个角色的**阶段外观**（stage 层）也在 prompt 里");
        expect(r.references_json.find("selfcheck_sheet.png") != std::string::npos,
               "references_json 是该镜出场角色的资产 sheet");
    }

    // ② PV2/I9：同输入状态再跑一次 → **复用**（不重写）
    const PromptGenOutcome second = GeneratePromptArtifacts(mem, ch.value_or(0));
    expect(second.ok && second.reused == 1 && second.artifacts_written == 0,
           "同哈希再跑 → 复用（PV2：一致才允许跳过）");
    auto list2 = v.ListPromptArtifacts(ch.value_or(0));
    expect(list2.has_value() && list2->size() == 1, "复用不应产生第二条账");

    // ③ 变更输入状态（改资产文案 → 哈希变） → **必须重新生成**
    if (asset) {
        (void)v.UpsertAsset({.id = *asset,
                             .entity_id = pov.value_or(0),
                             .kind = "character",
                             .name = "自检角色",
                             .base_desc = "a wounded youth in torn coat",
                             .sheet_rel_path = "visual/gen/selfcheck_sheet.png",
                             .status = "READY"});
    }
    const PromptGenOutcome third = GeneratePromptArtifacts(mem, ch.value_or(0));
    expect(third.ok && third.artifacts_written == 1 && third.reused == 0,
           "输入状态变了 → 哈希不一致 → 重新生成（I9：不得复用）");

    // ④ 没有 shots 的章 → 明确报错（V10 的输入前提）
    auto ch2 = g.UpsertChapter({.ord = 2, .title = "第二章"});
    const PromptGenOutcome bad = GeneratePromptArtifacts(mem, ch2.value_or(0));
    expect(!bad.ok && bad.error.find("V9") != std::string::npos,
           "没有 shots 的章应提示先跑 V9");

    if (fails == 0) {
        log::Info("V10 自检通过（九层组装 → PromptArtifact 账 + 参考图解析 + PV2 复用 + I9 失效重算）");
    }
    return fails == 0;
}

} // namespace shine::novelcore
