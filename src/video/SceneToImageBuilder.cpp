#include "video/SceneToImageBuilder.h"

#include "core/Log.h"
#include "util/Encoding.h"
#include "util/Strings.h"
#include "video/VideoTaskRunner.h"

#include <fmt/format.h>
#include <yyjson.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace shine::video {
namespace {

// —— API JSON 写入（与 H3WorkflowBuilder 同款，外来格式手写 yyjson）——

[[nodiscard]] yyjson_mut_val* JStr(yyjson_mut_doc* doc, std::string_view text) {
    return yyjson_mut_strncpy(doc, text.data(), text.size());
}
[[nodiscard]] yyjson_mut_val* JSint(yyjson_mut_doc* doc, std::int64_t value) { return yyjson_mut_sint(doc, value); }
[[nodiscard]] yyjson_mut_val* JReal(yyjson_mut_doc* doc, double value) { return yyjson_mut_real(doc, value); }
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
        yyjson_mut_obj_add(in, JStr(d.doc, item.first), item.second);
    }
    yyjson_mut_obj_add(node, JStr(d.doc, "class_type"), JStr(d.doc, classType));
    yyjson_mut_obj_add(node, JStr(d.doc, "inputs"), in);
    yyjson_mut_obj_add(d.root, JStr(d.doc, outId), node);
}

[[nodiscard]] std::filesystem::path AbsFromMedia(std::string_view raw, const std::filesystem::path& mediaDir) {
    std::filesystem::path p = util::PathFromUtf8(raw);
    if (!p.is_absolute() && !mediaDir.empty()) {
        p = mediaDir / p;
    }
    return p;
}

[[nodiscard]] std::string FileNameOf(std::string_view raw, const std::filesystem::path& mediaDir) {
    return util::FileNameToUtf8(AbsFromMedia(raw, mediaDir));
}

[[nodiscard]] std::string MappedName(std::string_view raw, const std::filesystem::path& mediaDir,
                                     const std::map<std::string, std::string>& uploadedNames) {
    const std::string fileName = FileNameOf(raw, mediaDir);
    if (const auto it = uploadedNames.find(fileName); it != uploadedNames.end()) {
        return it->second;
    }
    return fileName;
}

[[nodiscard]] std::int64_t ResolveSeed(std::int64_t seed, std::string_view prompt, int w, int h) {
    if (seed >= 0) {
        return seed;
    }
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::string_view text) {
        for (const char ch : text) {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ULL;
        }
    };
    mix(prompt);
    mix(util::FromInt(w));
    mix(util::FromInt(h));
    return static_cast<std::int64_t>(hash & 0x7fffffffffffffffULL);
}

[[nodiscard]] bool FileLooksPresent(std::string_view raw, const std::filesystem::path& mediaDir, bool dryRun) {
    if (dryRun || raw.empty()) {
        return !raw.empty();
    }
    std::error_code ec;
    return std::filesystem::exists(AbsFromMedia(raw, mediaDir), ec);
}

} // namespace

std::vector<std::string> CollectSceneUploads(const Shot& shot, const VideoProject& project,
                                             const std::filesystem::path& mediaLibraryDir) {
    std::vector<std::string> files;
    const auto push = [&](std::string_view raw) {
        if (raw.empty()) {
            return;
        }
        const std::filesystem::path abs = AbsFromMedia(raw, mediaLibraryDir);
        const std::string utf8 = util::PathToUtf8(abs);
        if (std::ranges::find(files, utf8) == files.end()) {
            files.push_back(utf8);
        }
    };
    // 颜色图：优先首帧，否则第一张参考图
    if (!shot.firstFramePath.empty()) {
        push(shot.firstFramePath);
    } else if (!shot.referenceImages.empty()) {
        push(shot.referenceImages.front());
    }
    push(project.sceneControlDepthPath);
    push(project.sceneControlNormalPath);
    return files;
}

SceneToImageResult BuildSceneToImageWorkflow(const SceneToImageOptions& options) {
    SceneToImageResult out;
    const VideoProject& project = options.project;
    const Shot& shot = options.shot;
    const std::filesystem::path& mediaDir = options.mediaLibraryDir;

    if (project.sceneCheckpoint.empty()) {
        out.error = "工程缺少分镜图「Checkpoint」：请在设置/工程里填写 SD1.5 checkpoint 后再出图（本机可能没有 SD 模型）";
        return out;
    }
    if (shot.prompt.empty()) {
        out.error = "该分镜没有提示词，无法生成分镜图";
        return out;
    }

    const int width = AlignSceneSize(project.sceneWidth > 0 ? project.sceneWidth : shot.width);
    const int height = AlignSceneSize(project.sceneHeight > 0 ? project.sceneHeight : shot.height);
    if (width <= 0 || height <= 0) {
        out.error = "分镜图宽高非法";
        return out;
    }
    out.width = width;
    out.height = height;

    if ((project.sceneWidth > 0 && project.sceneWidth != width) || (project.sceneHeight > 0 && project.sceneHeight != height)) {
        out.warnings.push_back({static_cast<std::size_t>(-1),
                                fmt::format("分镜图宽高已对齐到 {} 的倍数：{}x{} → {}x{}", kSceneSizeMultiple,
                                            project.sceneWidth, project.sceneHeight, width, height)});
    }

    // 颜色图
    std::string colorRaw = shot.firstFramePath;
    if (colorRaw.empty() && !shot.referenceImages.empty()) {
        colorRaw = shot.referenceImages.front();
    }
    const bool hasColor = FileLooksPresent(colorRaw, mediaDir, options.dryRun);
    if (!colorRaw.empty() && !hasColor) {
        out.warnings.push_back({static_cast<std::size_t>(-1), "颜色图不存在，已降级为纯文生图（EmptyLatentImage）"});
    }
    if (!hasColor) {
        out.degraded = true;
        out.warnings.push_back({static_cast<std::size_t>(-1), "未提供颜色/参考图：使用 EmptyLatentImage，denoise 强制 1.0"});
    }

    // ControlNet（可选，串接）
    const bool hasDepthCfg = !project.sceneControlNetDepth.empty();
    const bool hasNormalCfg = !project.sceneControlNetNormal.empty();
    const bool hasDepthImg = FileLooksPresent(project.sceneControlDepthPath, mediaDir, options.dryRun);
    const bool hasNormalImg = FileLooksPresent(project.sceneControlNormalPath, mediaDir, options.dryRun);
    if ((hasDepthCfg || hasNormalCfg) && !(hasDepthImg || hasNormalImg)) {
        out.warnings.push_back({static_cast<std::size_t>(-1), "配置了 ControlNet 但没有可用控制图：已降级为无 ControlNet"});
        out.degraded = true;
    }
    const bool useDepth = hasDepthCfg && hasDepthImg;
    const bool useNormal = hasNormalCfg && hasNormalImg;
    if ((hasDepthCfg || hasNormalCfg) && !(useDepth || useNormal)) {
        out.degraded = true;
    }

    double denoise = project.sceneDenoise;
    if (denoise <= 0.0 || denoise > 1.0) {
        denoise = hasColor ? 0.75 : 1.0;
    }
    if (!hasColor) {
        denoise = 1.0;
    }

    const std::int64_t seed = ResolveSeed(shot.EffectiveSeed(), shot.prompt, width, height);
    out.seed = seed;

    Draft draft;
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    draft.doc = doc;
    draft.root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, draft.root);

    // ① Checkpoint
    std::string ckptId;
    AddNode(draft, ckptId, "CheckpointLoaderSimple", {{"ckpt_name", JStr(doc, project.sceneCheckpoint)}});

    // ② CLIP 正/负
    std::string posId;
    AddNode(draft, posId, "CLIPTextEncode", {{"text", JStr(doc, shot.prompt)}, {"clip", JLink(doc, ckptId, 1)}});
    std::string negId;
    AddNode(draft, negId, "CLIPTextEncode",
            {{"text", JStr(doc, project.sceneNegativePrompt)}, {"clip", JLink(doc, ckptId, 1)}});

    // ③ latent：img2img 或 Empty
    std::string latentId;
    if (hasColor) {
        std::string loadImageId;
        AddNode(draft, loadImageId, "LoadImage",
                {{"image", JStr(doc, MappedName(colorRaw, mediaDir, options.uploadedNames))}});
        std::string scaleId;
        AddNode(draft, scaleId, "ImageScale",
                {{"image", JLink(doc, loadImageId, 0)},
                 {"upscale_method", JStr(doc, "lanczos")},
                 {"width", JSint(doc, width)},
                 {"height", JSint(doc, height)},
                 {"crop", JStr(doc, "disabled")}});
        AddNode(draft, latentId, "VAEEncode", {{"pixels", JLink(doc, scaleId, 0)}, {"vae", JLink(doc, ckptId, 2)}});
        out.usedImg2Img = true;
    } else {
        AddNode(draft, latentId, "EmptyLatentImage",
                {{"width", JSint(doc, width)}, {"height", JSint(doc, height)}, {"batch_size", JSint(doc, 1)}});
        out.usedImg2Img = false;
    }

    // ④ ControlNet 串接（depth → normal）
    std::string condPos = posId;
    std::string condNeg = negId;
    const auto applyControl = [&](std::string_view modelCfg, std::string_view imagePath) {
        std::string cnLoaderId;
        AddNode(draft, cnLoaderId, "ControlNetLoader", {{"control_net_name", JStr(doc, modelCfg)}});
        std::string cnImageId;
        AddNode(draft, cnImageId, "LoadImage",
                {{"image", JStr(doc, MappedName(imagePath, mediaDir, options.uploadedNames))}});
        std::string appliedPos;
        std::string appliedNeg;
        // ControlNetApplyAdvanced：positive/negative 各占一个输出
        AddNode(draft, appliedPos, "ControlNetApplyAdvanced",
                {{"positive", JLink(doc, condPos, 0)},
                 {"negative", JLink(doc, condNeg, 0)},
                 {"control_net", JLink(doc, cnLoaderId, 0)},
                 {"image", JLink(doc, cnImageId, 0)},
                 {"strength", JReal(doc, project.sceneControlStrength)},
                 {"start_percent", JReal(doc, 0.0)},
                 {"end_percent", JReal(doc, 1.0)}});
        // 同节点的 negative 输出槽位为 1
        condPos = appliedPos;
        condNeg = appliedPos; // 高级节点两路输出同 id 不同槽
        out.usedControlNet = true;
    };
    // 更稳妥：分别记 negative 槽。ComfyUI Advanced 的输出是 [positive, negative]
    // 我们在 KSampler 里用 JLink(appliedId, 0) / JLink(appliedId, 1)
    std::string controlNodeId;
    if (useDepth || useNormal) {
        std::string cnLoaderId;
        const std::string_view modelCfg = useDepth ? std::string_view(project.sceneControlNetDepth)
                                                   : std::string_view(project.sceneControlNetNormal);
        const std::string_view imagePath = useDepth ? std::string_view(project.sceneControlDepthPath)
                                                    : std::string_view(project.sceneControlNormalPath);
        AddNode(draft, cnLoaderId, "ControlNetLoader", {{"control_net_name", JStr(doc, modelCfg)}});
        std::string cnImageId;
        AddNode(draft, cnImageId, "LoadImage",
                {{"image", JStr(doc, MappedName(imagePath, mediaDir, options.uploadedNames))}});
        AddNode(draft, controlNodeId, "ControlNetApplyAdvanced",
                {{"positive", JLink(doc, posId, 0)},
                 {"negative", JLink(doc, negId, 0)},
                 {"control_net", JLink(doc, cnLoaderId, 0)},
                 {"image", JLink(doc, cnImageId, 0)},
                 {"strength", JReal(doc, project.sceneControlStrength)},
                 {"start_percent", JReal(doc, 0.0)},
                 {"end_percent", JReal(doc, 1.0)}});
        out.usedControlNet = true;
        // 第二路 ControlNet（normal）串在第一路之后
        if (useDepth && useNormal) {
            std::string cnLoader2;
            AddNode(draft, cnLoader2, "ControlNetLoader",
                    {{"control_net_name", JStr(doc, project.sceneControlNetNormal)}});
            std::string cnImage2;
            AddNode(draft, cnImage2, "LoadImage",
                    {{"image", JStr(doc, MappedName(project.sceneControlNormalPath, mediaDir, options.uploadedNames))}});
            std::string chained;
            AddNode(draft, chained, "ControlNetApplyAdvanced",
                    {{"positive", JLink(doc, controlNodeId, 0)},
                     {"negative", JLink(doc, controlNodeId, 1)},
                     {"control_net", JLink(doc, cnLoader2, 0)},
                     {"image", JLink(doc, cnImage2, 0)},
                     {"strength", JReal(doc, project.sceneControlStrength)},
                     {"start_percent", JReal(doc, 0.0)},
                     {"end_percent", JReal(doc, 1.0)}});
            controlNodeId = chained;
        }
        condPos = controlNodeId; // slot 0
        condNeg = controlNodeId; // slot 1（KSampler 里区分）
    }

    // ⑤ KSampler
    const std::string samplerName = project.sceneSampler.empty() ? std::string("dpmpp_2m") : project.sceneSampler;
    const std::string scheduler = project.sceneScheduler.empty() ? std::string("karras") : project.sceneScheduler;
    std::string ksId;
    if (out.usedControlNet) {
        AddNode(draft, ksId, "KSampler",
                {{"model", JLink(doc, ckptId, 0)},
                 {"positive", JLink(doc, condPos, 0)},
                 {"negative", JLink(doc, condPos, 1)},
                 {"latent_image", JLink(doc, latentId, 0)},
                 {"seed", JSint(doc, seed)},
                 {"steps", JSint(doc, project.sceneSteps > 0 ? project.sceneSteps : 20)},
                 {"cfg", JReal(doc, project.sceneCfg > 0.0 ? project.sceneCfg : 7.0)},
                 {"sampler_name", JStr(doc, samplerName)},
                 {"scheduler", JStr(doc, scheduler)},
                 {"denoise", JReal(doc, denoise)}});
    } else {
        AddNode(draft, ksId, "KSampler",
                {{"model", JLink(doc, ckptId, 0)},
                 {"positive", JLink(doc, posId, 0)},
                 {"negative", JLink(doc, negId, 0)},
                 {"latent_image", JLink(doc, latentId, 0)},
                 {"seed", JSint(doc, seed)},
                 {"steps", JSint(doc, project.sceneSteps > 0 ? project.sceneSteps : 20)},
                 {"cfg", JReal(doc, project.sceneCfg > 0.0 ? project.sceneCfg : 7.0)},
                 {"sampler_name", JStr(doc, samplerName)},
                 {"scheduler", JStr(doc, scheduler)},
                 {"denoise", JReal(doc, denoise)}});
    }

    // ⑥ Decode + Save + Preview
    std::string decodeId;
    AddNode(draft, decodeId, "VAEDecode", {{"samples", JLink(doc, ksId, 0)}, {"vae", JLink(doc, ckptId, 2)}});
    const std::string prefix = options.outputPrefix.empty()
                                   ? (project.sceneOutputPrefix.empty() ? std::string("scene/shine")
                                                                        : project.sceneOutputPrefix)
                                   : options.outputPrefix;
    out.filenamePrefix = prefix;
    AddNode(draft, out.saveId, "SaveImage",
            {{"filename_prefix", JStr(doc, prefix)}, {"images", JLink(doc, decodeId, 0)}});
    AddNode(draft, out.previewId, "PreviewImage", {{"images", JLink(doc, decodeId, 0)}});

    const char* json = yyjson_mut_write(doc, 0, nullptr);
    if (json == nullptr) {
        out.error = "分镜图 API JSON 序列化失败";
        yyjson_mut_doc_free(doc);
        return out;
    }
    out.apiJson = json;
    std::free(const_cast<char*>(json));
    yyjson_mut_doc_free(doc);
    out.ok = true;

    if (out.degraded) {
        log::Warn("分镜图编译降级：img2img={} controlNet={} {}x{} denoise={}", out.usedImg2Img, out.usedControlNet, width,
                  height, denoise);
    } else {
        log::Info("分镜图编译：{}x{} steps={} cfg={} denoise={} sampler={} seed={}", width, height,
                  project.sceneSteps > 0 ? project.sceneSteps : 20, project.sceneCfg > 0.0 ? project.sceneCfg : 7.0,
                  denoise, samplerName, seed);
    }
    return out;
}

bool StartSceneImage(const VideoProject& project, std::size_t shotIndex,
                     const std::filesystem::path& mediaLibraryDir,
                     std::function<void(const VideoTaskState&)> onFinish) {
    if (shotIndex >= project.shots.size()) {
        return false;
    }
    VideoJob job;
    job.label = "分镜图 · " + project.shots[shotIndex].title;
    job.shotIndex = shotIndex;
    const Shot shotCopy = project.shots[shotIndex];
    const VideoProject projectCopy = project;

    job.collectUploads = [shotCopy, projectCopy, mediaLibraryDir](std::string& error) -> std::vector<std::string> {
        if (projectCopy.sceneCheckpoint.empty()) {
            error = "工程缺少分镜图 Checkpoint（本机可能没有 SD/SDXL 模型，请先在工程/设置中配置）";
            return {};
        }
        if (shotCopy.prompt.empty()) {
            error = "该分镜没有提示词，无法生成分镜图";
            return {};
        }
        return CollectSceneUploads(shotCopy, projectCopy, mediaLibraryDir);
    };
    job.build = [shotCopy, projectCopy, mediaLibraryDir](const std::map<std::string, std::string>& uploadedNames,
                                                         std::string& error) -> std::string {
        SceneToImageOptions opt;
        opt.shot = shotCopy;
        opt.project = projectCopy;
        opt.mediaLibraryDir = mediaLibraryDir;
        opt.uploadedNames = uploadedNames;
        opt.dryRun = false;
        const SceneToImageResult built = BuildSceneToImageWorkflow(opt);
        if (!built.ok) {
            error = built.error;
            return {};
        }
        for (const H3BuildWarning& warning : built.warnings) {
            log::Warn("分镜图编译告警：{}", warning.text);
        }
        return built.apiJson;
    };
    job.onFinish = std::move(onFinish);
    return VideoTaskRunner::Instance().Start(std::move(job));
}

int RunSceneToImageSelfCheck() {
    int pass = 0;
    int fail = 0;
    const auto expect = [&](bool cond, std::string_view name) {
        if (cond) {
            ++pass;
            log::Info("scene-image PASS {}", name);
        } else {
            ++fail;
            log::Error("scene-image FAIL {}", name);
        }
    };

    VideoProject project;
    project.sceneCheckpoint = "sd15_test.safetensors";
    project.sceneWidth = 1300; // → 1344
    project.sceneHeight = 480; // 已是 64 倍数
    project.sceneSteps = 20;
    project.sceneCfg = 7.0;
    project.sceneDenoise = 0.75;
    project.sceneNegativePrompt = "lowres";

    Shot shot;
    shot.prompt = "a red fox in snow";
    shot.seed = 42;

    // 无 checkpoint
    {
        SceneToImageOptions opt;
        opt.shot = shot;
        opt.project = VideoProject{};
        const SceneToImageResult r = BuildSceneToImageWorkflow(opt);
        expect(!r.ok && !r.error.empty() && r.apiJson.empty(), "missing checkpoint → Chinese error, empty json");
    }
    // 无图 → EmptyLatent，两次编译一致
    {
        SceneToImageOptions opt;
        opt.shot = shot;
        opt.project = project;
        opt.dryRun = true;
        const SceneToImageResult a = BuildSceneToImageWorkflow(opt);
        const SceneToImageResult b = BuildSceneToImageWorkflow(opt);
        expect(a.ok && a.apiJson.find("EmptyLatentImage") != std::string::npos, "no image → EmptyLatentImage");
        expect(a.apiJson.find("CheckpointLoaderSimple") != std::string::npos, "has CheckpointLoaderSimple");
        expect(a.apiJson.find("dpmpp_2m") != std::string::npos && a.apiJson.find("karras") != std::string::npos,
               "sampler dpmpp_2m + karras");
        expect(a.width == 1344 && a.height == 512, "align 64: 1300x480 -> 1344x512");
        expect(a.apiJson == b.apiJson, "deterministic apiJson (two builds equal)");
        expect(a.degraded && !a.usedImg2Img, "degraded flag without image");
    }
    // dryRun 有首帧 → img2img
    {
        SceneToImageOptions opt;
        opt.shot = shot;
        opt.shot.firstFramePath = "color.png";
        opt.project = project;
        opt.dryRun = true;
        const SceneToImageResult r = BuildSceneToImageWorkflow(opt);
        expect(r.ok && r.usedImg2Img && r.apiJson.find("VAEEncode") != std::string::npos,
               "dryRun firstFrame → img2img VAEEncode");
        expect(r.apiJson.find("SaveImage") != std::string::npos && r.apiJson.find("PreviewImage") != std::string::npos,
               "SaveImage + PreviewImage");
    }
    // ControlNet 配置但无图 → 降级
    {
        SceneToImageOptions opt;
        opt.shot = shot;
        opt.project = project;
        opt.project.sceneControlNetDepth = "control_v11f1p_sd15_depth.safetensors";
        opt.dryRun = true;
        const SceneToImageResult r = BuildSceneToImageWorkflow(opt);
        expect(r.ok && !r.usedControlNet && r.degraded, "controlnet configured but no map → degrade");
    }

    log::Info("SCENE_IMAGE_SELF_CHECK pass={} fail={} {}", pass, fail, fail == 0 ? "PASS" : "FAIL");
    return fail;
}

} // namespace shine::video
