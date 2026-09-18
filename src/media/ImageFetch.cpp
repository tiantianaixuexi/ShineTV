#include "media/ImageFetch.h"
#include "util/File.h"
#include "util/Encoding.h"

#include "comfy/ComfyClient.h"
#include "comfy/ComfyHttp.h"
#include "core/Async.h"
#include "core/Log.h"
#include "gallery/decoders/PngDecoder.h"

#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace shine::media {
namespace {

std::mutex g_inflightMutex;
std::unordered_set<std::uint64_t> g_inflight; // in-flight 去重（滚动/切换时不重复下载）

bool BeginInflight(std::uint64_t key) {
    if (key == 0) {
        return true; // 0 表示不参与去重
    }
    const std::lock_guard lock(g_inflightMutex);
    return g_inflight.insert(key).second;
}

void EndInflight(std::uint64_t key) {
    if (key == 0) {
        return;
    }
    const std::lock_guard lock(g_inflightMutex);
    g_inflight.erase(key);
}

} // namespace

void FetchAndDecodeAsync(std::string_view baseUrl, std::string_view fileName, std::string_view subfolder,
                         std::string_view type, std::uint64_t key, FetchedCb cb) {
    if (!BeginInflight(key)) {
        return; // 已在飞行中
    }
    // UI 线程先把参数拷成拥有型，再扔给 worker（跨线程不共享视图）
    const std::string base{baseUrl};
    const std::string name{fileName};
    const std::string sub{subfolder};
    const std::string kind{type};

    async::RunOnWorker([base, name, sub, kind, key, cb = std::move(cb)]() mutable {
        FetchedImage out;
        out.key = key;
        out.fileName = name;
        const auto t0 = std::chrono::steady_clock::now();
        const std::string url = comfy::BuildViewUrl(base, name, sub, kind);
        const auto bytes = comfy::HttpDownloadBinary(url, std::chrono::seconds{60});
        out.fetchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!bytes.has_value()) {
            out.error = bytes.error().message.empty() ? std::string{"下载失败"} : bytes.error().message;
            EndInflight(key);
            async::PostToUi([out = std::move(out), cb = std::move(cb)]() mutable { cb(std::move(out)); });
            return;
        }
        const auto t1 = std::chrono::steady_clock::now();
        auto image = gallery::decoders::PngDecoder::DecodeMemory(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes->data()), bytes->size()));
        out.decodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        if (!image.has_value()) {
            out.error = gallery::DecodeErrorText(image.error());
            EndInflight(key);
            async::PostToUi([out = std::move(out), cb = std::move(cb)]() mutable { cb(std::move(out)); });
            return;
        }
        out.image = std::move(*image);
        EndInflight(key);
        async::PostToUi([out = std::move(out), cb = std::move(cb)]() mutable { cb(std::move(out)); });
    });
}

void LoadLocalAndDecodeAsync(const std::filesystem::path& filePath, std::uint64_t key, FetchedCb cb) {
    if (!BeginInflight(key)) {
        return;
    }
    async::RunOnWorker([path = filePath, key, cb = std::move(cb)]() mutable {
        FetchedImage out;
        out.key = key;
        out.fileName = util::FileNameToUtf8(path);
        const auto t0 = std::chrono::steady_clock::now();
        std::ifstream file = util::OpenInput(path);
        if (!file) {
            out.error = "文件不存在或无法读取：" + util::PathToUtf8(path);
            EndInflight(key);
            async::PostToUi([out = std::move(out), cb = std::move(cb)]() mutable { cb(std::move(out)); });
            return;
        }
        std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        out.fetchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const auto t1 = std::chrono::steady_clock::now();
        auto image = gallery::decoders::PngDecoder::DecodeMemory(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        out.decodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        if (!image.has_value()) {
            out.error = gallery::DecodeErrorText(image.error());
        } else {
            out.image = std::move(*image);
        }
        EndInflight(key);
        async::PostToUi([out = std::move(out), cb = std::move(cb)]() mutable { cb(std::move(out)); });
    });
}

} // namespace shine::media
