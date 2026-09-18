#include "gallery/Gallery.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/ImageScanner.h"
#include "gpu/GpuTextureCache.h"
#include "util/Encoding.h"
#include "util/Strings.h"

#include <fmt/format.h>

#include <cstdint>
#include <utility>

namespace shine::gallery {
namespace {

GalleryModel& ModelRef() {
    static GalleryModel model;
    return model;
}

GalleryState& StateRef() {
    static GalleryState state;
    return state;
}

// 本模块自己的请求号（`ImageScanner` 内部的那套之外再兜一层）：来源**不可用**时
// `ScanAsync` 根本不会被调用（它内部的请求号也就不会前进），此时若旧扫描的回调回来了
// 就会覆盖刚清空的模型 → 每次 `RequestScan` 都领一个新号，回调里对不上号就丢弃。
std::uint64_t g_generation = 0;
bool g_shutdown = false;

[[nodiscard]] SourceKind SourceFromSetting(std::string_view text) {
    const std::string lowered = util::ToLower(text);
    if (lowered == "comfy_output") {
        return SourceKind::ComfyOutput;
    }
    if (lowered == "comfy_input") {
        return SourceKind::ComfyInput;
    }
    return SourceKind::Local;
}

[[nodiscard]] const char* SourceToSetting(SourceKind kind) noexcept {
    switch (kind) {
    case SourceKind::Local:
        return "local";
    case SourceKind::ComfyOutput:
        return "comfy_output";
    case SourceKind::ComfyInput:
        return "comfy_input";
    }
    return "local";
}

// 记住"上次来源"（下次启动沿用）；只有真的变了才落盘
void RememberSource(SourceKind kind) {
    const std::string wanted = SourceToSetting(kind);
    if (Settings().gallerySource == wanted) {
        return;
    }
    Settings().gallerySource = wanted;
    SaveSettings();
}

} // namespace

GalleryState& State() noexcept { return StateRef(); }
GalleryModel& Model() noexcept { return ModelRef(); }

void Init() {
    g_shutdown = false;
    GalleryState& st = StateRef();
    st.source = SourceFromSetting(Settings().gallerySource);
    // Comfy 来源没配目录时退回「本地」（避免一开局就是"不可用"，用户会以为坏了）
    if (st.source != SourceKind::Local && !IsSourceAvailable(st.source)) {
        log::Info("图库：{} 不可用，回退到「本地文件夹」（{}）", SourceLabel(st.source),
                  SourceUnavailableHint(st.source));
        st.source = SourceKind::Local;
    }
    st.root = SourceRoot(st.source);
    if (!IsSourceAvailable(st.source)) {
        st.message = SourceUnavailableHint(st.source);
        log::Info("图库：来源「{}」不可用 —— {}", SourceLabel(st.source), st.message);
        return;
    }
    RequestScan(st.source); // 开局自动扫一次（异步；结果回 UI 线程才写模型）
}

void Shutdown() {
    // 不 join 线程、不落盘：只把标志立起来，让"回来的"扫描回调知道别再写状态了。
    g_shutdown = true;
    ++g_generation;
    StateRef().scanning = false;
}

void Tick() {
    if (g_shutdown) {
        return;
    }
    // G-S5：列表不需要逐帧驱动（扫描结果由回调直接落模型）。
    // G-S7 起在这里 `ThumbnailService::DrainCompleted()`；G-S8 起纹理 LRU 淘汰也放这里
    // （**必须在本帧绘制之前**：`ImGui::Image` 只记 SRV 指针，本帧内释放 = use-after-free）。
}

bool RequestScan(SourceKind kind) {
    if (g_shutdown) {
        return false;
    }
    GalleryState& st = StateRef();
    RememberSource(kind);
    st.source = kind;
    st.root = SourceRoot(kind);

    if (!IsSourceAvailable(kind)) {
        ++g_generation; // 作废在飞的扫描（它回来时会丢弃自己的结果）
        st.scanning = false;
        st.error = SourceUnavailableHint(kind);
        st.message = st.error;
        ModelRef().Clear();
        log::Info("图库：不扫描「{}」—— {}", SourceLabel(kind), st.error);
        return false;
    }

    ScanOptions opt = MakeScanOptions(kind);
    const std::uint64_t generation = ++g_generation;
    st.scanning = true;
    st.error.clear();
    st.truncated = false;
    st.skipped = 0;
    st.filesSeen = 0;
    st.seconds = 0.0;
    st.message = fmt::format("正在扫描{}：{}", SourceLabel(kind), util::PathToUtf8(opt.root));
    log::Info("图库：开始扫描「{}」{}", SourceLabel(kind), util::PathToUtf8(opt.root));

    ScanAsync(std::move(opt), [generation](ScanResult result) {
        if (g_shutdown || generation != g_generation) {
            return; // 已被更新的请求/关闭作废（`ScanAsync` 自己也会丢，这里再兜一层）
        }
        GalleryState& s = StateRef();
        s.scanning = false;
        s.seconds = result.seconds;
        s.skipped = result.skipped;
        s.filesSeen = result.filesSeen;
        s.truncated = result.truncated;
        if (!result.error.empty()) {
            s.error = result.error;
            s.message = result.error;
            ModelRef().Clear();
            log::Warn("图库：扫描失败 —— {}", result.error);
            return;
        }
        if (result.cancelled) {
            return;
        }
        ModelRef().SetItems(std::move(result.items));
        s.message = fmt::format("{}：{} 张（扫描 {} 个文件，跳过 {}，{:.0f} ms）", SourceLabel(result.source),
                                ModelRef().Count(), result.filesSeen, result.skipped, result.seconds * 1000.0);
        if (result.truncated) {
            s.message += "，已达条数上限并截断";
        }
        log::Info("图库：{}", s.message);
    });
    return true;
}

bool RescanCurrent() { return RequestScan(StateRef().source); }

std::size_t ItemCount() noexcept { return ModelRef().Count(); }

std::size_t LoadedCount() noexcept {
    // G-S5 还没有缩略图管线（不解码像素）→ 恒 0；G-S7 换成 `ThumbnailService` 的就绪计数。
    return 0;
}

std::size_t CacheBytes() noexcept { return gpu::TextureCache().Bytes(); }

std::filesystem::path CurrentRoot() noexcept { return StateRef().root; }

} // namespace shine::gallery
