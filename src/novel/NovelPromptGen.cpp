#include "novel/NovelPromptGen.h"

#include "core/Log.h"
#include "novel/NovelChecks.h" // ComputeInputStateHash（`04` §2.5 的唯一来源）
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h" // PathToUtf8（自检里传临时工程目录）
#include "util/File.h"     // S26：读盘上的 `work/ch<NNN>/storyboard.json`（spatial 层）
#include "util/Json.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <fmt/format.h>

#include <filesystem>
#include <map>

namespace shine::novelcore {
namespace {

// —— S26：空间层（`12` §2.5 / `02` §2.7 的 `Spatial`）——
// **谁在前景/谁在背景**本来就在契约里（`layers{foreground,midground,background}` +
// `facing` / `distance_m` / `occlusion`），但 V9 落地时这些"无专列"的字段**只存盘**
//（`work/ch<NNN>/storyboard.json`）—— `11` §2.2 明确"V10 产生成 Prompt 时从那读"。
// 本函数把 `spatial` 摊成一段 prompt 文本（走 `Assemble` 的第 10 层）。
[[nodiscard]] std::string SpatialToText(const yyjson_val* sp) {
    if (sp == nullptr || !yyjson_is_obj(sp)) {
        return {};
    }
    std::vector<std::string> parts;
    if (const yyjson_val* layers = yyjson_obj_get(sp, "layers");
        layers != nullptr && yyjson_is_obj(layers)) {
        for (const char* key : {"foreground", "midground", "background"}) {
            const yyjson_val* v = yyjson_obj_get(layers, key);
            if (v != nullptr && yyjson_is_str(v)) {
                const std::string s = yyjson_get_str(v);
                if (!util::Trim(s).empty()) {
                    parts.push_back(fmt::format("{}: {}", key, s));
                }
            }
        }
    }
    for (const char* key : {"facing", "occlusion", "height", "movement_path"}) {
        const yyjson_val* v = yyjson_obj_get(sp, key);
        if (v != nullptr && yyjson_is_str(v)) {
            const std::string s = yyjson_get_str(v);
            if (!util::Trim(s).empty()) {
                parts.push_back(fmt::format("{}: {}", key, s));
            }
        }
    }
    if (const yyjson_val* d = yyjson_obj_get(sp, "distance_m");
        d != nullptr && yyjson_is_num(d)) {
        parts.push_back(fmt::format("distance_m: {:.1f}", yyjson_get_num(d)));
    }
    std::string out;
    for (const std::string& p : parts) {
        out += (out.empty() ? "" : ", ") + p;
    }
    return out;
}

// 一次读盘 + 建索引：`(scene_ord, ord)` → 空间层文本（V9 落库用的就是这两个键）。
// 读不到（没跑过 V9 的盘 / 老数据 / 没传 `project_dir`）→ 空表，**不阻断** V10。
[[nodiscard]] std::map<std::pair<int, int>, std::string>
LoadSpatialIndex(std::string_view projectDir, int chapterOrd) {
    std::map<std::pair<int, int>, std::string> out;
    if (projectDir.empty() || chapterOrd <= 0) {
        return out;
    }
    // S35：**唯一来源 = V4 产物**（`work/ch<NNN>/v04_spatial.json`）。
    // ⚠️ 原先读的是 `storyboard.json` 的 `spatial` —— 那是 V9 把 V4 的结果**又抄了一遍**：
    //    同一个概念两条路（**V4 是产生方**、V9 的 storyboard 是消费方），**迟早分叉**。
    //    现在直接读产生方的产物；V4 没跑过（没跑 `--novel-stages`）→ 该层为空，**不阻断**。
    const auto path = std::filesystem::path{std::string{projectDir}} / "work" /
                      fmt::format("ch{:03}", chapterOrd) / "v04_spatial.json";
    const auto text = util::ReadFileBytes(path);
    if (!text) {
        return out;
    }
    yyjson_doc* doc = yyjson_read(text->data(), text->size(), 0);
    if (doc == nullptr) {
        return out;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    // V4 产物的形状与 V9 的 `shots[].spatial` **不同**：`items[]` 里每条**本身就是** spatial
    //（`12` §2.5 的字段直接在 item 上），不需要再取一层 `spatial`。
    yyjson_val* items = yyjson_is_obj(root) ? yyjson_obj_get(root, "items") : nullptr;
    if (yyjson_is_arr(items)) {
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
            if (sceneOrd <= 0 || ord <= 0) {
                continue;
            }
            const std::string t = SpatialToText(it);
            if (!t.empty()) {
                out[{sceneOrd, ord}] = t;
            }
        }
    }
    yyjson_doc_free(doc);
    return out;
}

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

PromptGenOutcome GeneratePromptArtifacts(::shine::db::sqlite::Database& db, RowId chapter_id,
                                         std::string_view project_dir) {
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
    // 已有的 V10 账（按 shot_id 索引）：用来判"复用"与新版本号。
    // ⚠️ v10 起同镜会有**多行**（PV3 多版本保留），`ListPromptArtifacts` 按 `updated DESC,id DESC`
    // 返回 → **第一条才是最新版**；所以只记第一条（后到的旧版本不许覆盖它）。
    std::map<RowId, PromptArtifactRow> existing;
    if (auto list = visual.ListPromptArtifacts(chapter_id); list) {
        for (const PromptArtifactRow& r : *list) {
            if (r.stage == "V10" && r.chain == "visual" && r.shot_id > 0 &&
                existing.find(r.shot_id) == existing.end()) {
                existing[r.shot_id] = r;
            }
        }
    }
    // 空间层的索引（`12` §2.5）：`work/ch<NNN>/storyboard.json` 用**章序号**做目录名、
    // 用 `(scene_ord, ord)` 标识镜 —— 这里把库里的 `scene_id` 映射回 `scene_ord`。
    int chapterOrd = 0;
    if (auto st = db.Prepare("SELECT ord FROM chapters WHERE id=?1"); st) {
        (void)st->BindInt(1, chapter_id);
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            chapterOrd = static_cast<int>(st->ColumnInt(0));
        }
    }
    std::map<RowId, int> sceneOrdById;
    if (auto st = db.Prepare("SELECT id,ord FROM scenes WHERE chapter_id=?1"); st) {
        (void)st->BindInt(1, chapter_id);
        while (true) {
            auto s = st->Step();
            if (!s || *s == db::sqlite::StepResult::Done) {
                break;
            }
            sceneOrdById[st->ColumnInt(0)] = static_cast<int>(st->ColumnInt(1));
        }
    }
    const auto spatialIndex = LoadSpatialIndex(project_dir, chapterOrd);

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
        // S26：空间层（第 10 层）—— 谁在前景/谁在背景 + 朝向/距离/遮挡
        if (const auto ordIt = sceneOrdById.find(shot.scene_id); ordIt != sceneOrdById.end()) {
            if (const auto si = spatialIndex.find({ordIt->second, shot.ord});
                si != spatialIndex.end()) {
                in.spatial_text = si->second;
            }
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
        // `13` §2.7 PV2/PV3（v10 起有专列）：**不覆盖上一版** —— 新写一行、`version + 1`，
        // 旧版本**全部保留**（用于对比与回滚）。取"最新版"的读法按 `updated DESC,id DESC`。
        row.version = it != existing.end() ? it->second.version + 1 : 1;
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
        // 版本已在 `version` 专列（v10）；这里只记"这条 prompt 由几层拼成"（诊断用）
        row.model_hint = fmt::format("layers={}", art->used_layer_ids.size());
        // PV5：生成结果关联**留空** —— 要等 **V11 出图接线**产出 `visual_artifacts` / 出图任务
        // 才有值（现在没有对象可指，不写假引用）
        row.generation_ref.clear();
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
    // ③b PV2/PV3（v10 的 `version` 列）：**新版写新行、旧版全部保留**（可对比/可回滚）
    {
        auto list3 = v.ListPromptArtifacts(ch.value_or(0));
        expect(list3.has_value() && list3->size() == 2, "PV3：重新生成应**新增**一行（不是覆盖）");
        if (list3 && list3->size() == 2) {
            expect(list3->front().version == 2 && list3->back().version == 1,
                   fmt::format("PV2/PV3：新版 version=2、旧版仍在（实际 {}/{}）",
                               list3->front().version, list3->back().version));
            expect(list3->front().input_state_hash != list3->back().input_state_hash,
                   "两版的 input_state_hash 应不同（哈希变了才重生成）");
            expect(list3->front().generation_ref.empty(),
                   "PV5：generation_ref 留空（要等 V11 出图接线才有对象可指）");
        }
    }

    // ③c S26：**空间层**（`12` §2.5）—— 谁在前景/谁在背景 → prompt 的第 10 层。
    // V9 把 `spatial` 只存盘，所以这里造一份**盘上产物**（`work/ch<NNN>/storyboard.json`）验证真读到了。
    {
        std::error_code ec;
        const auto tmp = std::filesystem::temp_directory_path() / "shine_v10_selfcheck";
        std::filesystem::remove_all(tmp, ec);
        int chOrd = 0;
        int scOrd = 0;
        {
            auto st = mem.Prepare("SELECT ord FROM chapters WHERE id=?1");
            auto st2 = mem.Prepare("SELECT ord FROM scenes WHERE id=?1");
            if (st && st2) {
                (void)st->BindInt(1, ch.value_or(0));
                (void)st2->BindInt(1, sc.value_or(0));
                if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
                    chOrd = static_cast<int>(st->ColumnInt(0));
                }
                if (auto s = st2->Step(); s && *s == db::sqlite::StepResult::Row) {
                    scOrd = static_cast<int>(st2->ColumnInt(0));
                }
            }
        }
        const auto sbDir = tmp / "work" / fmt::format("ch{:03}", chOrd);
        std::filesystem::create_directories(sbDir, ec);
        const std::string sb = fmt::format(
            R"({{"stage":"V4","items":[{{"scene_ord":{},"ord":1,"facing":"left",)"
            R"("distance_m":2.5,"occlusion":"前景人物半挡","layers":{{"foreground":"自检角色",)"
            R"("midground":"","background":"自检配角"}}}}]}})",
            scOrd);
        expect(util::WriteFileBytes(sbDir / "v04_spatial.json", sb),
               "S35：写临时 v04_spatial.json（**V4 产物** —— 空间层的唯一来源）");
        // 再改一次资产文案：保证哈希与上一步不同 → 必然走到"新写"分支
        if (asset) {
            (void)v.UpsertAsset({.id = *asset,
                                 .entity_id = pov.value_or(0),
                                 .kind = "character",
                                 .name = "自检角色",
                                 .base_desc = "a limping youth in dark coat",
                                 .sheet_rel_path = "visual/gen/selfcheck_sheet.png",
                                 .status = "READY"});
        }
        const PromptGenOutcome fourth =
            GeneratePromptArtifacts(mem, ch.value_or(0), util::PathToUtf8(tmp));
        expect(fourth.ok && fourth.artifacts_written == 1 && fourth.reused == 0,
               "S26：带 project_dir 重跑应新写一条");
        auto list4 = v.ListPromptArtifacts(ch.value_or(0));
        if (list4 && !list4->empty()) {
            const std::string& p = list4->front().prompt;
            expect(p.find("foreground: 自检角色") != std::string::npos,
                   "S26：**前景**角色进了 prompt（空间层 = 第 10 层）");
            expect(p.find("background: 自检配角") != std::string::npos,
                   "S26：**背景**角色也进了 prompt");
            expect(p.find("distance_m: 2.5") != std::string::npos, "S26：距离也进了 prompt");
        } else {
            expect(false, "S26：应有 V10 账可查");
        }
        // ③d PV4（S27）：改一层 `prompt_layers` 文本 → **哈希必须变** → 重算且 Prompt `version + 1`。
        // 这正是"有人把相机层从『中景』改成『特写』，旧 prompt 必须失效"的场景 ——
        // 不加 `prompt_layers` 进哈希就是 S25/S26 那类事故的第 4 次（改了不生效）。
        if (sc) {
            (void)v.SetLayer("scene", *sc, "scene", "夜色库房，货架投下长影");
            const PromptGenOutcome fifth =
                GeneratePromptArtifacts(mem, ch.value_or(0), util::PathToUtf8(tmp));
            expect(fifth.ok && fifth.artifacts_written == 1 && fifth.reused == 0,
                   "PV4：`prompt_layers` 变了 → 哈希变 → **重算**（不得复用）");
            auto list5 = v.ListPromptArtifacts(ch.value_or(0));
            expect(list5 && !list5->empty() && list5->front().version == 4,
                   fmt::format("PV4：layers 升版驱动 Prompt `version + 1`（期望 4，实际 {}）",
                               (list5 && !list5->empty()) ? list5->front().version : 0));
        }
        std::filesystem::remove_all(tmp, ec);
    }

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
