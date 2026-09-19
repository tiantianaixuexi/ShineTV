#include "video/H3WorkflowBuilder.h"

#include "core/Log.h"
#include "util/Encoding.h"
#include "util/Strings.h"
#include "video/CharacterAsset.h"
#include "video/MentionResolver.h"

#include <yyjson.h>

#include <cmath>
#include <cstdlib>
#include <map>
#include <string_view>
#include <utility>

namespace shine::video {
namespace {

constexpr std::size_t kProjectLevel = static_cast<std::size_t>(-1); // 工程级告警的 shotIndex

// ———————————————————————————————————————————————————————————————— API JSON 写入小工具
//
// ComfyUI 的 API 格式是**外来格式**（不是我们自己的存盘结构），所以这里手写 yyjson —— 属
// `Doc/RULES-LANG.md` §13.6 允许的例外（与 `comfy::NodeTypeDef` 同一理由）。

[[nodiscard]] yyjson_mut_val* JStr(yyjson_mut_doc* doc, std::string_view text) {
    return yyjson_mut_strncpy(doc, text.data(), text.size());
}

[[nodiscard]] yyjson_mut_val* JSint(yyjson_mut_doc* doc, std::int64_t value) { return yyjson_mut_sint(doc, value); }

[[nodiscard]] yyjson_mut_val* JReal(yyjson_mut_doc* doc, double value) { return yyjson_mut_real(doc, value); }

// 连线值：`["<上游节点 id>", <上游输出槽位>]`
[[nodiscard]] yyjson_mut_val* JLink(yyjson_mut_doc* doc, std::string_view nodeId, int slot) {
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_val(arr, JStr(doc, nodeId));
    yyjson_mut_arr_add_val(arr, JSint(doc, slot));
    return arr;
}

struct Draft {
    yyjson_mut_doc* doc = nullptr;
    yyjson_mut_val* root = nullptr;
    NodeIdPool ids;
};

void AddNode(Draft& d, std::string& outId, std::string_view classType,
             const std::vector<std::pair<std::string, yyjson_mut_val*>>& inputs) {
    outId = d.ids.Next();
    yyjson_mut_val* node = yyjson_mut_obj(d.doc);
    yyjson_mut_val* in = yyjson_mut_obj(d.doc);
    for (const std::pair<std::string, yyjson_mut_val*>& item : inputs) {
        // 键一律用 `yyjson_mut_strncpy` **拷贝**（`yyjson_mut_obj_add_val` 不拷贝 C 字符串，
        // 传 `std::string::c_str()` 会在函数返回后悬垂）
        yyjson_mut_obj_add(in, JStr(d.doc, item.first), item.second);
    }
    yyjson_mut_obj_add(node, JStr(d.doc, "class_type"), JStr(d.doc, classType));
    yyjson_mut_obj_add(node, JStr(d.doc, "inputs"), in);
    yyjson_mut_obj_add(d.root, JStr(d.doc, outId), node);
}

[[nodiscard]] std::string Pad3(std::size_t value) {
    std::string text = util::FromInt(value);
    while (text.size() < 3) {
        text.insert(text.begin(), '0');
    }
    return text;
}

// 种子为 -1（用户要"随机"）时的**确定性派生值**：保证同输入两次编译一致；
// P5.5 提交前可以按 `Shot::EffectiveSeed() < 0` 判断并替换成真随机。
[[nodiscard]] std::int64_t DeterministicSeed(std::size_t shotIndex, std::string_view prompt, int width, int height,
                                             int length) {
    std::uint64_t hash = 1469598103934665603ULL; // FNV-1a 64 偏移基数
    const auto mix = [&hash](std::string_view text) {
        for (const char ch : text) {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ULL;
        }
    };
    mix(util::FromInt(shotIndex));
    mix(prompt);
    mix(util::FromInt(width));
    mix(util::FromInt(height));
    mix(util::FromInt(length));
    return static_cast<std::int64_t>(hash & 0x7fffffffffffffffULL);
}

// 记一条编译告警。`degradeKind` 非空 = 这是**降级**（K28）：同一条同时进两个视图 ——
// `warnings`（人读，日志/UI）与 `degradations`（类型化，进降级账 → 章级报告）。
void AddWarning(H3BuildResult& out, std::size_t shotIndex, std::string text,
                std::string_view degradeKind = {}) {
    out.warnings.push_back({shotIndex, text, std::string{degradeKind}});
    if (!degradeKind.empty()) {
        out.degradations.push_back({std::string{degradeKind}, std::move(text), shotIndex});
    }
}

} // namespace

H3BuildResult BuildH3Workflow(const H3BuildOptions& options) {
    H3BuildResult out;
    const VideoProject& project = options.project;

    // ———— ① 硬校验（任何一条不过 → 直接返回中文错误，**不产出任何 JSON**）————
    const char* missing = nullptr;
    if (project.unetName.empty()) {
        missing = "UNET";
    } else if (project.clipName.empty()) {
        missing = "CLIP";
    } else if (project.videoVaeName.empty()) {
        missing = "视频 VAE";
    } else if (project.audioVaeName.empty()) {
        missing = "音频 VAE";
    }
    if (missing != nullptr) {
        out.error = std::string("工程缺少「") + missing + "」模型名：请在侧栏「模型段」里填写后再编译";
        return out;
    }

    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < project.shots.size(); ++i) {
        if (project.shots[i].IsSubmittable()) {
            indices.push_back(i);
        }
    }
    if (indices.empty()) {
        out.error = "没有可提交的分镜：至少需要一个填了提示词、规格为正、参考图不超上限的分镜";
        return out;
    }
    if (options.dryRun) {
        log::Info("H3 编译器 dryRun：不校验素材文件是否存在，LoadImage 用文件名占位");
    }

    // 模式混用：允许（每段各自成链），但成片风格可能不一致 → 告警
    bool hasReference = false;
    bool hasFirstLast = false;
    for (const std::size_t i : indices) {
        (project.shots[i].mode == ShotMode::Reference ? hasReference : hasFirstLast) = true;
    }
    if (hasReference && hasFirstLast) {
        out.warnings.push_back({kProjectLevel,
                                "分镜里同时存在 ref2va（参考图）与 fl2va（首末帧）两种模式：H3 允许编译，但两种模式的成片风格可能不一致"});
    }

    // ———— ② 先把**所有**分镜解析完（引用/角色/素材），失败就直接返回 ————
    struct PreparedShot {
        std::size_t index = 0;
        Shot shot;                 // 解析后的工作副本（提示词已改写、forcedSeed 已回填）
        std::vector<std::string> orderedImages;
    };
    std::vector<PreparedShot> prepared;
    prepared.reserve(indices.size());
    for (const std::size_t i : indices) {
        const Shot& source = project.shots[i];
        ResolveRequest req;
        req.shot = source;
        req.characterDir = CharacterAssetDir(options.projectDir);
        req.mediaLibraryDir = options.mediaLibraryDir;
        req.requireFiles = !options.dryRun;

        ResolveResult resolved = Resolve(req);
        const std::string label = source.title.empty() ? ("#" + util::FromInt(i + 1)) : source.title;
        if (!resolved.ok) {
            out.error = "第 " + util::FromInt(i + 1) + " 段「" + label + "」引用解析失败：" + resolved.error;
            return out;
        }
        for (const std::string& warning : resolved.warnings) {
            out.warnings.push_back({i, "第 " + util::FromInt(i + 1) + " 段「" + label + "」：" + warning});
        }
        // 解析器带回来的**类型化降级**（参考图被截断、锁定种子被统一…）转进本结果的降级账
        for (const GenerationDegradation& item : resolved.degradations) {
            out.degradations.push_back(
                {item.kind, "第 " + util::FromInt(i + 1) + " 段「" + label + "」：" + item.detail, i});
        }
        PreparedShot item;
        item.index = i;
        item.shot = std::move(resolved.shot);
        item.orderedImages = std::move(resolved.orderedImages);
        prepared.push_back(std::move(item));
    }

    // ———— ③ 建图 ————
    Draft draft;
    draft.doc = yyjson_mut_doc_new(nullptr);
    draft.root = yyjson_mut_obj(draft.doc);
    yyjson_mut_doc_set_root(draft.doc, draft.root);
    yyjson_mut_doc* doc = draft.doc;

    std::string unetId;
    AddNode(draft, unetId, "UNETLoader",
            {{"unet_name", JStr(doc, project.unetName)}, {"weight_dtype", JStr(doc, "default")}});

    std::string clipId;
    AddNode(draft, clipId, "CLIPLoader",
            {{"clip_name", JStr(doc, project.clipName)}, {"type", JStr(doc, "minimax")}});

    std::string videoVaeId;
    AddNode(draft, videoVaeId, "VAELoader", {{"vae_name", JStr(doc, project.videoVaeName)}});

    std::string audioVaeId;
    AddNode(draft, audioVaeId, "VAELoader", {{"vae_name", JStr(doc, project.audioVaeName)}});

    // ⚠️ yyjson 的可变对象用 `next/prev` 链兄弟节点：**同一个 val 只能进一个父对象**，
    // 复用同一个 `yyjson_mut_val*`（例如把 `modelLink` 挂到多个节点上）会把结构串坏。
    // 所以这里只存"上游 id + 槽位"，每次要用时**现建**一个连线数组。
    std::string modelSourceId = unetId;
    int modelSourceSlot = 0;
    std::string clipSourceId = clipId;
    int clipSourceSlot = 0;
    if (!project.loraName.empty()) {
        std::string loraId;
        AddNode(draft, loraId, "LoraLoader",
                {{"model", JLink(doc, modelSourceId, modelSourceSlot)},
                 {"clip", JLink(doc, clipSourceId, clipSourceSlot)},
                 {"lora_name", JStr(doc, project.loraName)},
                 {"strength_model", JReal(doc, project.loraStrength)},
                 {"strength_clip", JReal(doc, project.loraStrength)}});
        modelSourceId = loraId;
        modelSourceSlot = 0;
        clipSourceId = loraId;
        clipSourceSlot = 1;
    }

    // SigmaShift 是**模型级**的：整片只能有一个值 → 取首段，不一致就告警
    const double shiftVideo = prepared.front().shot.shift;
    for (const PreparedShot& item : prepared) {
        if (std::fabs(item.shot.shift - shiftVideo) > 1e-9) {
            // 用户设的 shift 被模型级约束统一掉 = 降级（不是纯提示）：成片与他的设定不一致
            AddWarning(out, item.index,
                       "本段 shift = " + util::FromDouble(item.shot.shift) +
                           "，但 MiniMaxH3SigmaShift 是模型级、整片只能有一个值：已取第 " +
                           util::FromInt(prepared.front().index + 1) + " 段的 " +
                           util::FromDouble(shiftVideo),
                       kDegradeParamUnified);
        }
    }
    std::string shiftId;
    AddNode(draft, shiftId, "MiniMaxH3SigmaShift",
            {{"model", JLink(doc, modelSourceId, modelSourceSlot)},
             {"shift_video", JReal(doc, shiftVideo)},
             {"shift_audio", JReal(doc, options.audioShift)}});

    std::string samplerId;
    AddNode(draft, samplerId, "KSamplerSelect", {{"sampler_name", JStr(doc, options.samplerName)}});

    // 共享调度器：全片 steps/denoise 完全一致才共享（否则每段一个 + 告警说明）
    bool uniformSchedule = true;
    for (const PreparedShot& item : prepared) {
        if (item.shot.steps != prepared.front().shot.steps ||
            std::fabs(item.shot.denoise - prepared.front().shot.denoise) > 1e-9) {
            uniformSchedule = false;
            out.warnings.push_back({item.index, "本段 steps/denoise 与首段不同：BasicScheduler 无法全片共享，已为本段单独建一个调度器"});
        }
    }
    std::string sharedSchedulerId;
    if (uniformSchedule) {
        AddNode(draft, sharedSchedulerId, "BasicScheduler",
                {{"model", JLink(doc, shiftId, 0)},
                 {"scheduler", JStr(doc, options.schedulerName)},
                 {"steps", JSint(doc, prepared.front().shot.steps)},
                 {"denoise", JReal(doc, prepared.front().shot.denoise)}});
    }

    // —— LoadImage 去重（按文件名）+ 同名不同路径告警 ——
    std::map<std::string, std::string> loadImageByFile;  // 文件名 → 节点 id
    std::map<std::string, std::string> absByFile;        // 文件名 → 首个绝对路径
    const auto loadImage = [&](const std::string& absoluteUtf8, std::string& nodeId) {
        const std::string fileName = util::PathToUtf8(util::PathFromUtf8(absoluteUtf8).filename());
        // ① 先查"同名不同路径"（**即使已经建过节点也要报警**，不能因为去重命中就跳过）
        const auto seen = absByFile.find(fileName);
        if (seen == absByFile.end()) {
            absByFile.emplace(fileName, absoluteUtf8);
        } else if (seen->second != absoluteUtf8) {
            // 同名不同路径 → 上传后会互相覆盖（结果可能错），属降级
            AddWarning(out, kProjectLevel,
                       "两个不同路径的文件同名（" + seen->second + " 与 " + absoluteUtf8 +
                           "）：ComfyUI 按**文件名**取图，上传后会互相覆盖，请重命名",
                       kDegradeNameCollision);
        }
        // ② 再按文件名去重
        const auto found = loadImageByFile.find(fileName);
        if (found != loadImageByFile.end()) {
            nodeId = found->second;
            return;
        }
        // 上传改名（P5.5 S2）：有映射就用上传后的名字，否则用原始文件名
        std::string apiName = fileName;
        if (const auto found = options.uploadedNames.find(fileName); found != options.uploadedNames.end()) {
            apiName = found->second;
        }
        AddNode(draft, nodeId, "LoadImage", {{"image", JStr(doc, apiName)}});
        loadImageByFile.emplace(fileName, nodeId);
    };

    // —— 逐段 ——
    std::string previousDecodeId;      // 上一段的 `VAEDecode`（链式取末帧用）
    int previousFrameCount = 0;        // 上一段**对齐后**的帧数
    for (const PreparedShot& item : prepared) {
        const Shot& shot = item.shot;
        const int width = AlignToMultiple(shot.width);
        const int height = AlignToMultiple(shot.height);
        const int length = AlignFrameCount(shot.length);
        const std::size_t no = item.index + 1;
        const std::string label = shot.title.empty() ? ("#" + util::FromInt(no)) : shot.title;

        // 链式：上一段末帧
        std::string chainedFrameId;
        if (shot.chainFromPrevious) {
            if (previousDecodeId.empty() || previousFrameCount <= 0) {
                AddWarning(out, item.index,
                           "第 " + util::FromInt(no) + " 段勾了「链式」，但它是第一段（或上一段不可用）：已忽略",
                           kDegradeChainIgnored);
            } else {
                AddNode(draft, chainedFrameId, "ImageFromBatch",
                        {{"image", JLink(doc, previousDecodeId, 0)},
                         {"batch_index", JSint(doc, previousFrameCount - 1)},
                         {"length", JSint(doc, 1)}});
            }
        }
        if (!chainedFrameId.empty() && !shot.firstFramePath.empty()) {
            AddWarning(out, item.index,
                       "第 " + util::FromInt(no) + " 段同时设了「链式」与「首帧图」：链式优先，已忽略首帧图",
                       kDegradeFirstFrameIgnored);
        }

        std::vector<std::pair<std::string, yyjson_mut_val*>> inputs;
        inputs.emplace_back("clip", JLink(doc, clipSourceId, clipSourceSlot));
        inputs.emplace_back("vae", JLink(doc, videoVaeId, 0));
        inputs.emplace_back("prompt", JStr(doc, shot.prompt));
        inputs.emplace_back("width", JSint(doc, width));
        inputs.emplace_back("height", JSint(doc, height));
        inputs.emplace_back("length", JSint(doc, length));

        std::string conditioningId;
        if (shot.mode == ShotMode::Reference) {
            inputs.emplace_back("audio_vae", JLink(doc, audioVaeId, 0));
            inputs.emplace_back("ref_image_size", JStr(doc, options.refImageSize));
            // ⚠️ Autogrow 子键在 API JSON 里是 `父前缀.子名`，而子名是 **`prefix + 序号`、序号从 0 开始**
            //（ComfyUI `comfy_api/latest/_io.py::_expand_schema_for_dynamic`：
            //  `names = [f"{prefix}{i}" for i in range(max)]`）。写成 1 基会让**第 9 张参考图落到
            //  `ref_image_9`（超出 max=9 的 0..8）被服务端**静默丢掉** —— 已由 `ApiGraphValidator` 兜住。
            int ordinal = 0;
            for (const std::string& image : item.orderedImages) {
                std::string loadId;
                loadImage(image, loadId);
                inputs.emplace_back("ref_images.ref_image_" + util::FromInt(ordinal), JLink(doc, loadId, 0));
                ++ordinal;
            }
            if (!chainedFrameId.empty()) {
                inputs.emplace_back("ref_images.ref_image_" + util::FromInt(ordinal), JLink(doc, chainedFrameId, 0));
                ++ordinal;
            }
            if (ordinal == 0) {
                // 这正是 K28 点名的降级：**无参考图 → 纯文生图**
                AddWarning(out, item.index,
                           "第 " + util::FromInt(no) +
                               " 段是参考图模式，但没有任何参考图也没接链式：等于纯文本出片（t2va）",
                           kDegradeNoReference);
            }
            AddNode(draft, conditioningId, "MiniMaxH3ReferenceToVideo", inputs);
        } else {
            std::string firstFrameId = chainedFrameId;
            if (firstFrameId.empty() && !shot.firstFramePath.empty()) {
                loadImage(shot.firstFramePath, firstFrameId);
            }
            if (!firstFrameId.empty()) {
                inputs.emplace_back("first_frame", JLink(doc, firstFrameId, 0));
            } else {
                AddWarning(out, item.index,
                           "第 " + util::FromInt(no) +
                               " 段是首末帧模式，但没有首帧图也没接链式：等于纯文本出片（t2va）",
                           kDegradeNoReference);
            }
            AddNode(draft, conditioningId, "MiniMaxH3ImageToVideo", inputs);
        }

        std::string zeroOutId;
        AddNode(draft, zeroOutId, "ConditioningZeroOut", {{"conditioning", JLink(doc, conditioningId, 0)}});

        std::string guiderId;
        AddNode(draft, guiderId, "CFGGuider",
                {{"model", JLink(doc, shiftId, 0)},
                 {"positive", JLink(doc, conditioningId, 0)},
                 {"negative", JLink(doc, zeroOutId, 0)},
                 {"cfg", JReal(doc, shot.cfg)}});

        std::int64_t seed = shot.EffectiveSeed();
        if (seed < 0) {
            seed = DeterministicSeed(item.index, shot.prompt, width, height, length);
            // **刻意不记为降级**：这是编译器文档化的确定性行为（本文件头 §1），不是"缺依赖"或"用户要的东西没给"。
            // 若记成降级，默认 seed=-1 会让每条分镜都往 K28 账里塞一条噪音，反而淹没真的降级。
            out.warnings.push_back({item.index, "第 " + util::FromInt(no) + " 段种子为 -1（随机）：本次编译用确定性派生值 " +
                                                    util::FromInt(seed) + "（P5.5 提交前可替换为真随机）"});
        }
        std::string noiseId;
        AddNode(draft, noiseId, "RandomNoise", {{"noise_seed", JSint(doc, seed)}});

        std::string sigmasId = sharedSchedulerId;
        if (sigmasId.empty()) {
            AddNode(draft, sigmasId, "BasicScheduler",
                    {{"model", JLink(doc, shiftId, 0)},
                     {"scheduler", JStr(doc, options.schedulerName)},
                     {"steps", JSint(doc, shot.steps)},
                     {"denoise", JReal(doc, shot.denoise)}});
        }

        std::string sampledId;
        AddNode(draft, sampledId, "SamplerCustomAdvanced",
                {{"noise", JLink(doc, noiseId, 0)},
                 {"guider", JLink(doc, guiderId, 0)},
                 {"sampler", JLink(doc, samplerId, 0)},
                 {"sigmas", JLink(doc, sigmasId, 0)},
                 // 条件节点输出 0 = positive、输出 **1 = AV latent**（视频 + 音频成对）
                 {"latent_image", JLink(doc, conditioningId, 1)}});

        std::string framesId;
        AddNode(draft, framesId, "VAEDecode",
                {{"samples", JLink(doc, sampledId, 0)}, {"vae", JLink(doc, videoVaeId, 0)}});

        std::string audioId;
        AddNode(draft, audioId, "VAEDecodeAudio",
                {{"samples", JLink(doc, sampledId, 0)}, {"vae", JLink(doc, audioVaeId, 0)}});

        std::string videoId;
        AddNode(draft, videoId, "CreateVideo",
                {{"images", JLink(doc, framesId, 0)},
                 {"fps", JReal(doc, project.fps)},
                 {"audio", JLink(doc, audioId, 0)}});

        const std::string prefix = options.outputPrefix + "/shot_" + Pad3(no);
        std::string saveId;
        AddNode(draft, saveId, "SaveVideo",
                {{"video", JLink(doc, videoId, 0)},
                 {"filename_prefix", JStr(doc, prefix)},
                 {"format", JStr(doc, options.videoFormat)}});

        out.shotNodes.push_back({item.index, conditioningId, saveId, prefix, length});

        previousDecodeId = framesId;
        previousFrameCount = length;
    }

    // ———— ④ 序列化（pretty + 固定插入顺序 → 同输入逐字节一致）————
    std::size_t textLength = 0;
    char* text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &textLength);
    if (text != nullptr) {
        out.apiJson.assign(text, textLength);
        std::free(text);
    }
    out.nodeCount = draft.ids.Count();
    yyjson_mut_doc_free(doc);

    if (out.apiJson.empty()) {
        out.error = "工作流序列化失败（内部错误）";
        return out;
    }

    out.ok = true;
    log::Info("H3 工作流编译完成：{} 个分镜 → {} 个节点、{} 字节、{} 条告警", prepared.size(), out.nodeCount,
              out.apiJson.size(), out.warnings.size());
    return out;
}

} // namespace shine::video
