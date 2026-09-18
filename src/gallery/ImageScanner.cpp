#include "gallery/ImageScanner.h"

#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/MetaProbe.h"
#include "util/Encoding.h"
#include "util/Strings.h"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace shine::gallery {
namespace {

// ImageId 全进程唯一发号：跨多次扫描不重复（选择状态 / 缩略图缓存 key 都靠它）
std::atomic<ImageId> g_nextId{1};

// 最近一次发起的扫描请求号：只在 UI 线程写；worker 只读它判断自己是否已被取代
std::atomic<std::uint64_t> g_latestRequest{0};

// 探针失败日志最多逐条打这么多，其余只计数（避免一个坏目录刷屏）
constexpr std::size_t kProbeFailLogLimit = 20;

// "*.PNG" / ".png" / "png" → "png"
[[nodiscard]] std::string NormalizeExt(std::string_view ext) {
    std::string_view s = util::Trim(ext);
    if (!s.empty() && s.front() == '*') {
        s.remove_prefix(1);
    }
    if (!s.empty() && s.front() == '.') {
        s.remove_prefix(1);
    }
    return util::ToLower(s);
}

class ExtFilter {
public:
    explicit ExtFilter(const std::vector<std::string>& requested) {
        const std::vector<std::string> fallback = DefaultImageExtensions();
        const std::vector<std::string>& list = requested.empty() ? fallback : requested;
        for (const std::string& raw : list) {
            std::string normalized = NormalizeExt(raw);
            if (!normalized.empty()) {
                set_.insert(std::move(normalized));
            }
        }
    }

    [[nodiscard]] bool empty() const noexcept { return set_.empty(); }

    [[nodiscard]] bool Match(const std::filesystem::path& file) const {
        return set_.contains(NormalizeExt(util::PathToUtf8(file.extension())));
    }

private:
    std::unordered_set<std::string> set_;
};

// 枚举迭代 + 逐项探针；返回时 `result` 已填好。
// **纯 worker 侧逻辑**：不碰 UI，不碰网络；`Image` 一次都不分配（零像素解码）。
[[nodiscard]] ScanResult ScanDirectory(const ScanOptions& opt, std::uint64_t seq) {
    const auto startedAt = std::chrono::steady_clock::now();
    ScanResult result;
    result.source = opt.source;

    if (opt.root.empty()) {
        result.error = "未选择目录";
        return result;
    }
    std::error_code ec;
    if (!std::filesystem::exists(opt.root, ec)) {
        result.error = fmt::format("目录不存在：{}", util::PathToUtf8(opt.root));
        return result;
    }
    if (!std::filesystem::is_directory(opt.root, ec)) {
        result.error = fmt::format("不是目录：{}", util::PathToUtf8(opt.root));
        return result;
    }

    const ExtFilter filter{opt.extensions};
    if (filter.empty()) {
        result.error = "没有可扫描的图片扩展名";
        return result;
    }

    result.items.reserve(std::min<std::size_t>(opt.maxItems, 4096));
    std::size_t failLogged = 0;

    // 返回 false = 停止枚举（命中条数上限）
    const auto consider = [&](const std::filesystem::directory_entry& entry) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc)) {
            return true; // 子目录 / 设备 / 符号链接
        }
        const std::filesystem::path path = entry.path();
        if (!filter.Match(path)) {
            return true;
        }
        if (result.items.size() >= opt.maxItems) {
            result.truncated = true; // 上限只算**成功入列的图**（探针失败的不占额度）
            return false;
        }
        ++result.filesSeen;

        ImageInfo info;
        try {
            info = ProbeFile(path, g_nextId.fetch_add(1, std::memory_order_relaxed));
        } catch (const std::exception& e) {
            ++result.skipped;
            if (failLogged++ < kProbeFailLogLimit) {
                log::Warn("probe 异常：{} err={}", util::PathToUtf8(path), e.what());
            }
            return true;
        }
        if (!info.valid()) {
            ++result.skipped;
            if (failLogged++ < kProbeFailLogLimit) {
                log::Warn("probe failed: {}（图片头无法识别或读不了）", util::PathToUtf8(path));
            }
            return true;
        }
        info.source = opt.source;
        result.items.push_back(std::move(info));
        return true;
    };

    const auto superseded = [seq] {
        return seq != g_latestRequest.load(std::memory_order_relaxed);
    };

    // 迭代 + 逐项处理；被更新的请求取代则提前退出（省掉无用的剩余工作）
    const auto walk = [&](auto& it, const auto& end) {
        std::error_code stepEc;
        while (it != end) {
            if (!consider(*it)) {
                return;
            }
            if (superseded()) {
                result.cancelled = true;
                return;
            }
            it.increment(stepEc);
            if (stepEc) {
                log::Warn("扫描中断：{} err={}", util::PathToUtf8(opt.root), stepEc.message());
                return;
            }
        }
    };

    constexpr auto kOptions = std::filesystem::directory_options::skip_permission_denied;
    if (opt.recursive) {
        std::filesystem::recursive_directory_iterator it(opt.root, kOptions, ec);
        if (ec) {
            result.error = fmt::format("无法读取目录：{}（{}）", util::PathToUtf8(opt.root), ec.message());
            return result;
        }
        walk(it, std::filesystem::recursive_directory_iterator{});
    } else {
        std::filesystem::directory_iterator it(opt.root, kOptions, ec);
        if (ec) {
            result.error = fmt::format("无法读取目录：{}（{}）", util::PathToUtf8(opt.root), ec.message());
            return result;
        }
        walk(it, std::filesystem::directory_iterator{});
    }

    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
    // 日志格式对齐 Plan/任务/G-参考.md §8：`scanned 10000 files, probe ok 9998, failed 2, 821 ms (root=…)`
    log::Info("scanned {} files, probe ok {}, failed {}, {:.0f} ms (root={})", result.filesSeen, result.items.size(),
              result.skipped, result.seconds * 1000.0, util::PathToUtf8(opt.root));
    if (result.truncated) {
        log::Warn("扫描条数达到上限 {}，结果已截断（root={}）", opt.maxItems, util::PathToUtf8(opt.root));
    }
    return result;
}

[[nodiscard]] std::string SourceDirSetting(SourceKind kind) {
    switch (kind) {
        case SourceKind::Local:
            return Settings().galleryLocalDir;
        case SourceKind::ComfyOutput:
            return Settings().comfyOutputDir;
        case SourceKind::ComfyInput:
            return Settings().comfyInputDir;
    }
    return {};
}

} // namespace

void ScanAsync(ScanOptions opt, std::move_only_function<void(ScanResult)> onDone) {
    const std::uint64_t seq = g_latestRequest.fetch_add(1, std::memory_order_relaxed) + 1;
    async::RunOnWorker([opt = std::move(opt), seq, onDone = std::move(onDone)]() mutable {
        ScanResult result = ScanDirectory(opt, seq);
        const std::string rootText = util::PathToUtf8(opt.root);
        async::PostToUi([seq, rootText = std::move(rootText), result = std::move(result),
                         onDone = std::move(onDone)]() mutable {
            if (seq != g_latestRequest.load(std::memory_order_relaxed)) {
                log::Info("扫描结果已被更新的请求取代，丢弃（root={}）", rootText);
                return;
            }
            if (onDone) {
                onDone(std::move(result));
            }
        });
    });
}

std::vector<std::string> DefaultImageExtensions() { return {"png", "jpg", "jpeg", "webp"}; }

std::string SourceLabel(SourceKind k) {
    switch (k) {
        case SourceKind::Local:
            return "本地文件夹";
        case SourceKind::ComfyOutput:
            return "Comfy 输出";
        case SourceKind::ComfyInput:
            return "Comfy 输入";
    }
    return "未知来源";
}

std::filesystem::path SourceRoot(SourceKind k) { return util::PathFromUtf8(SourceDirSetting(k)); }

bool IsSourceAvailable(SourceKind k) {
    const std::filesystem::path root = SourceRoot(k);
    if (root.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec);
}

std::string SourceUnavailableHint(SourceKind k) {
    const std::filesystem::path root = SourceRoot(k);
    if (root.empty()) {
        switch (k) {
            case SourceKind::Local:
                return "请先选择图片文件夹";
            case SourceKind::ComfyOutput:
                return "请在设置中填写 ComfyUI output 目录";
            case SourceKind::ComfyInput:
                return "请在设置中填写 ComfyUI input 目录";
        }
        return "来源未配置";
    }
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        return fmt::format("目录不存在：{}", util::PathToUtf8(root));
    }
    if (!std::filesystem::is_directory(root, ec)) {
        return fmt::format("不是目录：{}", util::PathToUtf8(root));
    }
    return {};
}

ScanOptions MakeScanOptions(SourceKind k) {
    ScanOptions opt;
    opt.source = k;
    opt.root = SourceRoot(k);
    opt.recursive = true;
    return opt;
}

} // namespace shine::gallery
