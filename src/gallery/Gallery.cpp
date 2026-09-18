#include "gallery/Gallery.h"

#include "comfy/ComfyHttp.h"
#include "comfy/ComfyTypes.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/ImageScanner.h"
#include "gallery/ThumbnailService.h"
#include "gallery/Viewer.h"
#include "gallery/cache/CpuThumbCache.h"
#include "gpu/GpuTextureCache.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"
#include "util/Strings.h"

#include <fmt/format.h>
#include <yyjson.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
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
std::uint64_t g_itemsGeneration = 0;
bool g_shutdown = false;
ImageId g_viewerId = 0;
std::size_t g_thumbReady = 0;
std::string g_lastUploadName;
std::string g_lastUploadError;
std::atomic<bool> g_uploadInFlight{false};
std::string g_lastGraphDrop;

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
    Thumbs().Init(); // G-S7：异步缩略图管线
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
    viewer::Close();
    Thumbs().Shutdown(); // G-S7：清缩略图状态（纹理若已被 ReleaseAll 则只清句柄）
}

void Tick() {
    if (g_shutdown) {
        return;
    }
    // G-S7：落地已完成结果 + 泵队列。
    // G-S8：GPU LRU 淘汰 —— **必须在本帧绘制之前**（`ImGui::Image` 只记 SRV 指针）。
    Thumbs().DrainCompleted();
    Thumbs().EvictCaches();
    viewer::Tick();
    g_thumbReady = Thumbs().ReadyCount();

    // G-S8 验收钩子（放在 Tick：不依赖图库 tab 是否可见）
    if (const char* raw = std::getenv("SHINE_GALLERY_CACHE_CHECK"); raw != nullptr && raw[0] == '1') {
        static int phase = 0;
        static std::size_t hits1 = 0;
        static std::size_t hits2 = 0;
        const std::size_t ready = Thumbs().ReadyCount();
        const std::size_t inflight = Thumbs().InFlightAndQueued();
        const auto& items = ModelRef().Items();
        // 用服务侧 Ready 数即可；条目尚未入服务时 ready=0
        if (phase == 0 && ready >= 8 && inflight == 0 && !items.empty()) {
            phase = 1;
            hits1 = Thumbs().ReplayFromCache();
            log::Info("G-S8 自检：第 1 次重放 CPU 命中 {}（{}）", hits1, Thumbs().CacheSummary());
        } else if (phase == 1 && inflight == 0) {
            phase = 2;
            hits2 = Thumbs().ReplayFromCache();
            // 重放后立刻从 CPU 重建 Ready（同步）
            Thumbs().OnDeviceLost();
            // 丢失后对模型里可见一批再 Request，验证 CPU → GPU 重建
            std::size_t rebuilt = 0;
            const std::uint32_t side = static_cast<std::uint32_t>(std::max(64, Settings().galleryThumbSize));
            for (std::size_t i = 0; i < items.size() && i < 80; ++i) {
                const ImageInfo& info = items[i];
                Thumbs().Request(info.id, info.path, side, ThumbPriority::High);
                if (Thumbs().State(info.id) == LoadState::Ready || Thumbs().Texture(info.id)) {
                    ++rebuilt;
                }
            }
            log::Info("G-S8 自检：第 2 次重放 CPU 命中 {} / Ready~{}；设备丢失后前 80 项重建 Ready={} CPU={:.1f}MB "
                      "GPU={:.1f}MB（{}）",
                      hits2, ready, rebuilt, Thumbs().CpuCacheBytes() / (1024.0 * 1024.0),
                      Thumbs().GpuCacheBytes() / (1024.0 * 1024.0), Thumbs().CacheSummary());
            if (hits2 >= 8 && rebuilt >= 8) {
                log::Info("G-S8 自检 OVERALL PASS：缓存重放命中 + 设备丢失后从 CPU 重建成功（重放 {}，重建 {}）",
                          hits2, rebuilt);
            } else if (hits2 >= 8) {
                log::Warn("G-S8 自检：重放 OK（{}）但重建偏少（{}）", hits2, rebuilt);
            } else {
                log::Warn("G-S8 自检：第 2 次重放命中偏低（{}）", hits2);
            }
            phase = 3;
        }
    }
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
        ++g_itemsGeneration;
        g_thumbReady = 0;
        g_viewerId = 0;
        log::Info("图库：不扫描「{}」—— {}", SourceLabel(kind), st.error);
        Thumbs().Reset();
        CpuThumbCache::Instance().Clear();
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
            ++g_itemsGeneration;
            g_thumbReady = 0;
            Thumbs().Reset();
            CpuThumbCache::Instance().Clear();
            log::Warn("图库：扫描失败 —— {}", result.error);
            return;
        }
        if (result.cancelled) {
            return;
        }
        ModelRef().SetItems(std::move(result.items));
        ++g_itemsGeneration; // 视图丢弃过期缩略图
        g_thumbReady = 0;
        Thumbs().Reset();
        CpuThumbCache::Instance().Clear(); // 新目录/新 id：CPU 缩略图一并作废
        if (g_viewerId != 0 && ModelRef().Find(g_viewerId) == nullptr) {
            g_viewerId = 0;
        }
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

std::size_t LoadedCount() noexcept { return g_thumbReady; }

void SetThumbReadyCount(std::size_t n) noexcept { g_thumbReady = n; }

std::uint64_t ItemsGeneration() noexcept { return g_itemsGeneration; }

void OpenViewer(ImageId id) {
    g_viewerId = id;
    viewer::Open(id); // G-S10：异步原图 + 缩放状态
}

void CloseViewer() noexcept {
    g_viewerId = 0;
    viewer::Close();
}

bool ViewerOpen() noexcept { return g_viewerId != 0 && viewer::IsOpen(); }

ImageId ViewerImageId() noexcept { return viewer::IsOpen() ? viewer::Get().id : g_viewerId; }

std::size_t CacheBytes() noexcept { return Thumbs().GpuCacheBytes(); }

std::filesystem::path CurrentRoot() noexcept { return StateRef().root; }

void UploadToComfyInput(const std::filesystem::path& path) {
    if (g_shutdown || path.empty()) {
        return;
    }
    if (g_uploadInFlight.load(std::memory_order_relaxed)) {
        log::Warn("图库：已有上传在进行，忽略 {}", util::PathToUtf8(path));
        return;
    }
    const std::string baseUrl = Settings().comfyBaseUrl;
    if (baseUrl.empty()) {
        g_lastUploadError = "未配置 ComfyUI 地址";
        log::Warn("图库：{}", g_lastUploadError);
        return;
    }
    const std::string fileName = util::FileNameToUtf8(path);
    const std::string pathUtf8 = util::PathToUtf8(path);
    g_uploadInFlight.store(true, std::memory_order_relaxed);
    g_lastUploadError.clear();
    log::Info("图库：开始上传为工作流输入 {}", pathUtf8);

    async::RunOnWorker([baseUrl, fileName, pathUtf8, path]() {
        const auto bytes = util::ReadFileBytes(path);
        if (!bytes) {
            async::PostToUi([pathUtf8]() {
                g_uploadInFlight.store(false, std::memory_order_relaxed);
                g_lastUploadError = "读文件失败：" + pathUtf8;
                log::Warn("图库：上传失败 —— {}", g_lastUploadError);
            });
            return;
        }
        const std::string url = comfy::BuildApiUrl(baseUrl, "/upload/image");
        // ComfyUI：type=input + overwrite=true → 可被 LoadImage / @image 引用
        const comfy::HttpResponse resp =
            comfy::HttpUploadImage(url, fileName, *bytes, {{"type", "input"}, {"overwrite", "true"}});
        std::string name;
        std::string err;
        if (!resp.ok) {
            err = resp.error.empty() ? resp.body : resp.error;
            if (err.empty()) {
                err = fmt::format("HTTP {}", resp.status);
            }
        } else {
            if (yyjson_doc* doc = yyjson_read(resp.body.data(), resp.body.size(), 0)) {
                name = util::json::GetStrCopy(yyjson_doc_get_root(doc), "name");
                yyjson_doc_free(doc);
            }
            if (name.empty()) {
                name = fileName;
            }
        }
        async::PostToUi([pathUtf8, name, err, status = resp.status, body = resp.body]() {
            g_uploadInFlight.store(false, std::memory_order_relaxed);
            if (!err.empty()) {
                g_lastUploadName.clear();
                g_lastUploadError = err;
                log::Warn("图库：上传失败 status={} —— {}\n响应片段：{}", status, err,
                          body.substr(0, static_cast<std::size_t>(std::min<std::size_t>(body.size(), 200))));
                return;
            }
            g_lastUploadName = name;
            g_lastUploadError.clear();
            log::Info("uploaded {} -> {}（type=input overwrite=true，@image 可用 LastUploadedName）", pathUtf8, name);
        });
    });
}

std::string LastUploadedName() noexcept { return g_lastUploadName; }
std::string LastUploadError() noexcept { return g_lastUploadError; }
bool UploadInFlight() noexcept { return g_uploadInFlight.load(std::memory_order_relaxed); }

std::string LastGraphDropPath() noexcept { return g_lastGraphDrop; }
void SetLastGraphDropPath(std::string path) { g_lastGraphDrop = std::move(path); }

} // namespace shine::gallery
