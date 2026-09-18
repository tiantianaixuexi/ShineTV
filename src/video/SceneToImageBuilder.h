#pragma once
// shine::video —— 分镜图生成工作流编译器（P5.7）
//
// SD1.5 线（与 H3 的 32 对齐不同：**宽高按 64 对齐**）：
//   CheckpointLoaderSimple → CLIPTextEncode(正/负)
//   → [LoadImage(颜色) → ImageScale(lanczos) → VAEEncode] 或 [EmptyLatentImage]
//   → [可选 ControlNet 串接] → KSampler(dpmpp_2m, karras) → VAEDecode
//   → SaveImage + PreviewImage
//
// 降级（S4）：
//   * 缺 checkpoint → 中文错误，**不产出 JSON**；
//   * 无颜色/参考图 → `EmptyLatentImage` 纯文生图（denoise 强制 1.0）+ 告警；
//   * 缺 ControlNet 或控制图 → 自动走无 ControlNet 路径 + 告警。
//
// 纪律同 H3：确定性发号、失败不产半成品 JSON、只读工程副本。
#include "video/H3WorkflowBuilder.h" // H3BuildWarning
#include "video/VideoProject.h"
#include "video/VideoTaskRunner.h" // VideoTaskState / StartSceneImage 回调

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace shine::video {

// SD 分镜图宽高对齐粒度（与 H3 的 kSizeMultiple=32 不同）
inline constexpr int kSceneSizeMultiple = 64;

[[nodiscard]] constexpr int AlignSceneSize(int value) noexcept {
    if (value <= 0) {
        return 0;
    }
    return ((value + kSceneSizeMultiple - 1) / kSceneSizeMultiple) * kSceneSizeMultiple;
}

struct SceneToImageOptions {
    Shot shot;                 // 提示词 / 首帧或参考图（取第一张作颜色图）
    VideoProject project;      // 读 scene* 字段（checkpoint / ControlNet / 采样…）
    std::filesystem::path mediaLibraryDir;
    std::string outputPrefix;  // 空则用 project.sceneOutputPrefix
    bool dryRun = false;       // 不校验素材存在；LoadImage 用文件名占位
    std::map<std::string, std::string> uploadedNames; // 原始文件名 → 上传后名字
};

struct SceneToImageResult {
    bool ok = false;
    std::string error; // 中文；ok=false 时有效
    std::string apiJson;
    std::vector<H3BuildWarning> warnings;
    std::string saveId;
    std::string previewId;
    std::string filenamePrefix;
    bool usedImg2Img = false;     // 是否走 LoadImage+VAEEncode
    bool usedControlNet = false;
    bool degraded = false;        // 触发了降级（无图纯文生图 / 无 ControlNet）
    int width = 0;                // 对齐 64 后
    int height = 0;
    std::int64_t seed = 0;        // 实际写入 KSampler 的种子
};

[[nodiscard]] SceneToImageResult BuildSceneToImageWorkflow(const SceneToImageOptions& options);

// 收集要上传的本地图片（颜色图 + 控制图，已按 mediaLibraryDir 解析成绝对路径）
[[nodiscard]] std::vector<std::string> CollectSceneUploads(const Shot& shot, const VideoProject& project,
                                                           const std::filesystem::path& mediaLibraryDir);

// 通过 `VideoTaskRunner` 提交某一镜的分镜图任务（UI 线程调用）
[[nodiscard]] bool StartSceneImage(const VideoProject& project, std::size_t shotIndex,
                                   const std::filesystem::path& mediaLibraryDir,
                                   std::function<void(const VideoTaskState&)> onFinish = {});

// 离线自检：返回 0 = 全部 PASS（`SHINE_SCENE_IMAGE_CHECK=1` 时 App 可调用）
[[nodiscard]] int RunSceneToImageSelfCheck();

} // namespace shine::video
