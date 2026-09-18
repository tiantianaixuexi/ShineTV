#pragma once
// shine::video —— MiniMax H3 工作流编译器（P5.4）
//
// 把 `VideoProject` + `Resolve()`（P5.2）编译成 **ComfyUI API JSON**：
//   `{ "<id>": { "class_type": "<ClassName>", "inputs": { "<输入名>": 值 | ["<上游id>", 槽位] } } }`
//
// 工作流形状（节点定义取自 ComfyUI 核心 `comfy_extras/nodes_minimax_h3.py`，输入名逐一核对过）：
//   UNETLoader → CLIPLoader(type=minimax) → VAELoader(video) → VAELoader(audio) → [LoraLoader] →
//   MiniMaxH3SigmaShift → KSamplerSelect(res_multistep) → [BasicScheduler(simple)，全片 steps/denoise 一致才共享]
//   每段：
//     ref2va  → MiniMaxH3ReferenceToVideo(clip, vae, audio_vae, prompt, w/h/length, ref_image_size,
//                                          `ref_images.ref_image_N`)
//     fl2va   → MiniMaxH3ImageToVideo(clip, vae, prompt, w/h/length, first_frame?)
//     → ConditioningZeroOut(正向) → CFGGuider → RandomNoise → SamplerCustomAdvanced(latent 取 conditioning **输出 1**)
//     → VAEDecode + VAEDecodeAudio → CreateVideo(fps) → SaveVideo
//   链式：上一段的 `VAEDecode` → `ImageFromBatch(batch_index = 上一段对齐帧数 - 1, length = 1)`；
//         ref2va 里作为**额外参考图**软锚定，fl2va 里接到 `first_frame` 硬锚定。第 1 段忽略 `chainFromPrevious`。
//
// 三条纪律：
//   1. **确定性**：节点 id 由 `NodeIdPool` 顺序发号、字段按固定顺序写入 → 同输入两次编译**逐字节一致**；
//      种子的 `-1`（随机）在这里用**确定性派生值**（P5.5 提交前可再换成真随机，见 `Shot::EffectiveSeed()`）。
//   2. **绝不产出半成品**：所有分镜先全部 `Resolve()` 成功，才动 JSON；任何失败直接返回中文 `error` 且 `apiJson` 为空。
//   3. **不碰用户工程**：只读 `project`（按值传入的副本），回填由 P5.5 负责。
#include "video/NodeIdPool.h"
#include "video/VideoProject.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace shine::video {

struct H3BuildOptions {
    VideoProject project;                   // 按值传（编译只读，不改原工程）
    std::filesystem::path projectDir;       // 角色资产根（`CharacterAssetDir(projectDir)`）
    std::filesystem::path mediaLibraryDir;  // 相对路径解析根
    std::string outputPrefix = "video/shine"; // `SaveVideo.filename_prefix` 前缀（后面自动补 `/shot_001`）

    // —— H3 侧固定/可调常量（默认值取自节点的官方默认）——
    double audioShift = 3.0;                // `MiniMaxH3SigmaShift.shift_audio`（视频 shift 逐段取 Shot.shift）
    std::string samplerName = "res_multistep"; // `KSamplerSelect`
    std::string schedulerName = "simple";      // `BasicScheduler`
    std::string refImageSize = "match";        // `MiniMaxH3ReferenceToVideo.ref_image_size`
    std::string videoFormat = "mp4";           // `SaveVideo.format`

    // 预览模式：不校验素材文件是否存在（`ResolveRequest::requireFiles=false`），LoadImage 用文件名占位
    bool dryRun = false;

    // 上传改名映射：**原始文件名 → 上传后 ComfyUI 里的文件名**（P5.5 S2）。
    // 有映射时 `LoadImage.image` 写上传后的名字；空映射 = 直接用原始文件名（dryRun 预览用）。
    // ⚠️ 这是"本次提交的工作副本"级别的覆盖，**不改用户工程字段**。
    std::map<std::string, std::string> uploadedNames;
};

struct H3BuildWarning {
    std::size_t shotIndex = 0;  // 0-based；`npos`（`static_cast<size_t>(-1)`）= 工程级告警
    std::string text;           // 中文，可直接显示
};

struct H3BuildResult {
    bool ok = false;
    std::string error;          // 中文；ok=false 时有效（此时 apiJson 一定为空）
    std::string apiJson;        // ok=true 时有效（pretty，稳定顺序）
    std::vector<H3BuildWarning> warnings;
    std::size_t nodeCount = 0;

    // 每个**被编译进去**的分镜对应的节点（P5.5 用它定位输出）
    struct ShotNodes {
        std::size_t shotIndex = 0;
        std::string conditioningId; // `MiniMaxH3ReferenceToVideo` / `MiniMaxH3ImageToVideo`
        std::string saveId;         // `SaveVideo`
        std::string filenamePrefix; // 该段的 filename_prefix（含 `/shot_00N`）
        int frameCount = 0;         // 对齐后的帧数
    };
    std::vector<ShotNodes> shotNodes;
};

[[nodiscard]] H3BuildResult BuildH3Workflow(const H3BuildOptions& options);

} // namespace shine::video
