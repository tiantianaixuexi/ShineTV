#pragma once
// shine::media::VideoThumb —— 视频"首帧缩略图"（P5.6 S2）
//
// 为什么走 Windows Shell 缩略图，而不是自己解码：
//   * ComfyUI 服务端**没有**视频首帧接口 —— core `/view?preview=` 只对 PIL 能打开的文件有效
//     （`server.py` 里是 `Image.open(file)`），VHS 3.x 也删掉了 `/vhs/get_thumbnail`（只剩 `/vhs/viewvideo`）；
//   * 本项目里没有视频解码器（不引 ffmpeg / 不引预编译 dll，见 `MEMORY.md` §2）。
//   → 用系统的缩略图提供程序（资源管理器里视频能出图就是它）：`IShellItemImageFactory`；
//     分辨率用 Shell 属性 `System.Video.FrameWidth/FrameHeight`。
//
// 约束：
//   * **必须有本地文件**（远端产物先落媒体缓存：`MediaLibrary::EnsureLocal`）；
//   * `VideoThumbnailSync` **只能在 worker 线程调用**（COM + 缩略图提取可能几百 ms，不能卡 UI）；
//   * 本模块自己按需 `CoInitializeEx`（线程级缓存，不要求调用方先初始化 COM）。
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "gallery/Image.h"

namespace shine::media {

struct VideoThumbResult {
    gallery::Image image; // 失败时 `valid() == false`
    int width = 0;        // 视频分辨率（0 = 系统没给出来）
    int height = 0;
    std::string error; // 中文，可直接显示；成功时为空
    double ms = 0.0;   // 提取耗时（日志/证据用）
};

using VideoThumbCb = std::function<void(VideoThumbResult)>; // **UI 线程**回调

// 同步提取（**worker 线程专用**；自检程序可直接调，但不要在 UI 线程调）
[[nodiscard]] VideoThumbResult VideoThumbnailSync(const std::filesystem::path& file);

// 异步：worker 提取 → `PostToUi` → 回调在 UI 线程执行
void VideoThumbnailAsync(const std::filesystem::path& file, VideoThumbCb cb);

// —— P5.6 验收自检钩子（验收用；未设环境变量时**立即返回**，只做一次 getenv + 静态标志）——
//
// `SHINE_P56_SELFTEST=<本地视频文件>`：把 P5.6 的三条判据跑一遍（**不依赖 ComfyUI**），
// 报告写 `%TEMP%\shine_p56_report.txt`：
//   ① 首帧缩略图：Windows Shell 拿到有效图 + 视频分辨率；
//   ② 文件缺失：取首帧/播放都给中文提示且**不崩**；
//   ③ 播放：查该扩展名的系统关联程序（`AssocQueryStringW`，**不真的拉起播放器**）；
//      设 `SHINE_P56_SELFTEST_PLAY=1` 时额外真调一次 `ShellOpen`（会打开系统播放器，仅人工验收用）。
void MaybeRunP56SelfTest();

} // namespace shine::media
