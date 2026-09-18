#pragma once
// shine::media::FetchAndDecodeAsync —— 「下载 → 解码」异步流水线（P4.1 S5–S7，P4.2/P4.3 复用）
//
// 异步规范（见 `MEMORY.md`「异步任务规范」）：**下载与解码都在 worker 线程**，
// 结果 `PostToUi` 后回调在 UI 线程执行；上传 GPU 纹理由调用方在 UI 线程做（`gpu::Textures().Upload`）。
// 同一 key 的重复请求会被合并（in-flight 去重），避免滚动列表时重复下载。
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "gallery/Image.h"

namespace shine::media {

struct FetchedImage {
    std::uint64_t key = 0;
    gallery::Image image;   // 失败时 image.valid() == false（RGBA8，mimalloc 分配）
    std::string error;      // 中文，可直接显示
    std::string fileName;   // 供 UI 标题/hover
    double fetchMs = 0.0;   // 下载耗时（证据/日志用）
    double decodeMs = 0.0;  // 解码耗时
};

using FetchedCb = std::function<void(FetchedImage)>; // **UI 线程**回调

// 从 ComfyUI `/view` 取图并解码（worker）。回调用 `PostToUi` 投递。
void FetchAndDecodeAsync(std::string_view baseUrl, std::string_view fileName, std::string_view subfolder,
                         std::string_view type, std::uint64_t key, FetchedCb cb);

// 直接解码本地文件（worker）；本地图库/磁盘缓存用。
// 收 `std::filesystem::path`（**不是**窄字符串）：中文路径下 `ifstream(std::string)` 打不开
void LoadLocalAndDecodeAsync(const std::filesystem::path& filePath, std::uint64_t key, FetchedCb cb);

} // namespace shine::media
