---
id: modules.media-paint
kind: reference
status: current
scope: media-paint
source_of_truth:
  - src/media/MediaLibrary.h
  - src/media/Gallery.h
  - src/media/ImageLoader.h
  - src/media/ThumbnailService.h
  - src/media/decoders/IImageDecoder.h
  - src/media/cache/CpuThumbCache.h
  - src/media/cache/DiskThumbCache.h
  - src/paint/PaintCanvas.h
  - src/paint/PaintService.h
last_verified: 2026-09-25
---

# 媒体、图库与绘制

## 媒体/图库边界

`src/media` 负责图片/视频条目的发现、元数据、解码、缩略图、缓存和预览输入；`src/gpu` 负责 UI 线程的纹理资源。不要在 UI 控件中直接访问文件系统或第三方解码器。

`MediaLibrary` 订阅 ComfyUI 历史/预览事件，把条目合并到内存模型；`EnsureLocal` 在 worker 下载/写盘，UI 只接收结果和选择状态。`TextureState` 明确区分 `Unavailable`（来源本来没有缩略图）与 `Failed`（请求/解码失败）。

## 解码适配

`media/decoders/IImageDecoder` 是图片解码统一接口，具体实现位于：

- `PngDecoder` → libpng；
- `JpegDecoder` → libjpeg-turbo；
- `WebpDecoder` → libwebp。

第三方符号只允许出现在对应 decoder 适配层；业务层只接收 `media::Image`/错误结果。图片解码和大缓冲分配不能放在 UI 线程。

## 扫描与缩略图

- `ImageScanner::ScanAsync` 在 worker 扫描、探测和收集条目；通过 `async::PostToUi` 回调；
- `ThumbnailService` 和 `CpuThumbCache`/`DiskThumbCache` 管理缩略图生命周期；
- `MediaLibrary` 的条目 key 用于去重和缓存；
- 视频缩略图使用 Windows Shell 能力，不等于项目内嵌视频解码器。

## 路径与缓存

设置中的 `mediaCacheDir` 为空时回落到 AppData 下的媒体缓存目录。项目内相对路径必须由项目/媒体库根解析；不要用 `path.string()` 拼接窄字符路径。

## PaintCanvas

`paint::PaintCanvas` 是 CPU RGBA8 模型：

- 底图 + 初始快照 + 二值遮罩；
- 像素由 mimalloc 分配；
- UI 线程读写；
- `Revision()` 递增，UI 用它决定是否重传 GPU 纹理；
- `PaintStroke` 使用 UV 坐标，遮罩只保留 0/255。

`PaintService`/PNG codec 负责把画布接到生成链路；不要把遮罩语义改成 Qt 控件状态。
