#include "media/VideoThumb.h"

#include "core/Async.h"
#include "core/Log.h"
#include "gpu/GpuTextureCache.h"    // 自检第三段：确认首帧真的上了 GPU
#include "gpu/GpuTextureManager.h"
#include "media/MediaLibrary.h" // 自检顺带刷历史 / 走一次"自动选中最近输出"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Shell.h"
#include "util/Strings.h"

// 必须在任何 windows.h 之前（util/Encoding.h 会带进 windows.h）
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shlwapi.h> // AssocQueryStringW（自检钩子用；WIN32_LEAN_AND_MEAN 不带它）

// 顺序要紧：shobjidl.h → ole2.h → objidl.h → wtypes.h 才有 PROPERTYKEY，propkey.h 依赖它
#include <shobjidl.h>

#include <propkey.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace shine::media {
namespace {

// 请求的缩略图边长（像素）。列表里显示 40px 上下；取 256 是为了高分屏/hover 放大不糊。
constexpr int kThumbPixels = 256;

// E_PENDING：缩略图正在后台提取（首次提取大文件时会出现）→ 稍等重试
constexpr HRESULT kErrorPending = static_cast<HRESULT>(0x8000000A);

// 每个 worker 线程初始化一次 COM（线程池线程会被复用；`async::Shutdown()` 故意不 join，
// 所以这个 thread_local 析构基本不会跑到，省一次 CoUninitialize 也无妨）。
void EnsureComOnThisThread() {
    static thread_local struct ComGuard {
        ComGuard() noexcept {
            const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            // RPC_E_CHANGED_MODE = 本线程已是别的套间 → 不初始化也能用（不 Uninitialize）
            usable = SUCCEEDED(hr);
        }
        ~ComGuard() {
            if (usable) {
                ::CoUninitialize();
            }
        }
        ComGuard(const ComGuard&) = delete;
        ComGuard& operator=(const ComGuard&) = delete;
        bool usable = false;
    } guard;
    (void)guard;
}

// HBITMAP（Shell 给的是 32bpp BGRA）→ `gallery::Image`（RGBA8）
[[nodiscard]] bool CopyBitmapToImage(HBITMAP bitmap, gallery::Image& out, std::string& error) {
    BITMAP info{};
    if (::GetObjectW(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
        error = "系统返回的缩略图无效";
        return false;
    }
    const auto width = static_cast<std::uint32_t>(info.bmWidth);
    const auto height = static_cast<std::uint32_t>(info.bmHeight);
    gallery::Image image = gallery::Image::Allocate(width, height);
    if (!image.valid()) {
        error = "缩略图像素分配失败";
        return false;
    }

    BITMAPINFO dib{};
    dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = info.bmWidth;
    dib.bmiHeader.biHeight = -info.bmHeight; // 负数 = 自顶向下，省一次翻转
    dib.bmiHeader.biPlanes = 1;
    dib.bmiHeader.biBitCount = 32;
    dib.bmiHeader.biCompression = BI_RGB;

    const HDC dc = ::CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
        error = "创建内存 DC 失败";
        return false;
    }
    const HGDIOBJ previous = ::SelectObject(dc, bitmap);
    const int lines = ::GetDIBits(dc, bitmap, 0, height, static_cast<void*>(image.data), &dib, DIB_RGB_COLORS);
    ::SelectObject(dc, previous);
    ::DeleteDC(dc);
    if (lines == 0) {
        error = "读取缩略图像素失败";
        return false;
    }

    // BGRA → RGBA；Shell 的缩略图 alpha 常常是 0（会画成全透明）→ 一律当不透明
    for (std::size_t i = 0; i + 3 < image.bytes; i += 4) {
        std::byte* pixel = image.data + i;
        std::swap(pixel[0], pixel[2]);
        pixel[3] = std::byte{255};
    }
    out = std::move(image);
    return true;
}

// 分辨率：Shell 属性 `System.Video.FrameWidth / FrameHeight`（取不到就保持 0）
void ReadVideoSize(IShellItem* item, int& width, int& height) {
    IShellItem2* item2 = nullptr;
    if (FAILED(item->QueryInterface(IID_PPV_ARGS(&item2))) || item2 == nullptr) {
        return;
    }
    ULONG w = 0;
    ULONG h = 0;
    if (SUCCEEDED(item2->GetUInt32(PKEY_Video_FrameWidth, &w))) {
        width = static_cast<int>(w);
    }
    if (SUCCEEDED(item2->GetUInt32(PKEY_Video_FrameHeight, &h))) {
        height = static_cast<int>(h);
    }
    item2->Release();
}

[[nodiscard]] std::string HResultText(HRESULT hr) {
    std::string text = util::WinErrorMessage(static_cast<unsigned long>(hr)); // W 版取消息，天然 UTF-8
    if (text.empty()) {
        return fmt::format("0x{:08X}", static_cast<unsigned>(hr));
    }
    return fmt::format("0x{:08X} {}", static_cast<unsigned>(hr), text);
}

} // namespace

VideoThumbResult VideoThumbnailSync(const std::filesystem::path& file) {
    const auto start = std::chrono::steady_clock::now();
    VideoThumbResult out;
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) {
        out.error = "文件不存在（可能已移动或删除）：" + util::PathToUtf8(file);
        return out;
    }
    EnsureComOnThisThread();

    IShellItem* item = nullptr;
    HRESULT hr = ::SHCreateItemFromParsingName(file.wstring().c_str(), nullptr, IID_PPV_ARGS(&item));
    if (FAILED(hr) || item == nullptr) {
        out.error = "系统无法识别该文件：" + HResultText(hr);
        return out;
    }
    ReadVideoSize(item, out.width, out.height);

    IShellItemImageFactory* factory = nullptr;
    hr = item->QueryInterface(IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr) && factory != nullptr) {
        // 首次提取大文件时系统会返回 E_PENDING（后台正在生成）→ 等一会儿重试几次
        constexpr int kMaxAttempts = 5;
        HBITMAP bitmap = nullptr;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
            const SIZE size{kThumbPixels, kThumbPixels};
            bitmap = nullptr;
            // 不用 `SIIGBF_BIGGERSIZEOK`：它会让提供程序直接给**原始尺寸**的位图（4K 视频就是 33MB 显存），
            // 列表/预览只要 256px 就够（实测 704x1280 的样本会返回 141x256）。
            hr = factory->GetImage(size, SIIGBF_THUMBNAILONLY, &bitmap);
            if (hr != kErrorPending) {
                break;
            }
            ::Sleep(150);
        }
        if (SUCCEEDED(hr) && bitmap != nullptr) {
            std::string copyError;
            if (!CopyBitmapToImage(bitmap, out.image, copyError)) {
                out.error = copyError;
            }
        } else if (hr == kErrorPending) {
            out.error = "系统还在生成缩略图（稍后再看）";
        } else {
            out.error = "系统没给出首帧缩略图（该文件可能不是视频，或缩略图提供程序不可用）";
        }
        if (bitmap != nullptr) {
            ::DeleteObject(bitmap);
        }
        factory->Release();
    } else {
        out.error = "系统缩略图接口不可用：" + HResultText(hr);
    }
    item->Release();

    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return out;
}

void VideoThumbnailAsync(const std::filesystem::path& file, VideoThumbCb cb) {
    async::RunOnWorker([path = file, cb = std::move(cb)]() mutable {
        VideoThumbResult result = VideoThumbnailSync(path);
        async::PostToUi([result = std::move(result), cb = std::move(cb)]() mutable { cb(std::move(result)); });
    });
}

// *********************** P5.6 验收自检（真实代码路径，不依赖 ComfyUI）***********************
namespace {

std::string g_p56Notes;
bool g_p56Done = false;  // 第一段（离线断言）已跑过
bool g_p56Pass = false;
int g_p56Stage = 0;      // 1 = 等"自动选中"落地；2 = 等媒体库首帧缩略图上屏；0 = 收尾
int g_p56WaitFrames = 0;
std::uint64_t g_p56ThumbKey = 0; // 第三段要等的纹理缓存 key
std::string g_p56SampleName;

void P56Note(std::string line) { g_p56Notes += line + "\n"; }

void WriteP56Report() {
    const std::filesystem::path report = std::filesystem::temp_directory_path() / L"shine_p56_report.txt";
    util::WriteFileBytes(report,
                         fmt::format("P5.6 结果预览自检：{}\n\n{}", g_p56Pass ? "PASS" : "FAIL", g_p56Notes));
    log::Warn("P5.6 自检报告已写出：{}（总体 {}）", util::PathToUtf8(report), g_p56Pass ? "PASS" : "FAIL");
}

// 该文件的系统关联程序（`AssocQueryStringW` 只查注册表，**不会拉起程序**）：
// 先按**完整路径**查（Windows 11 的 UserChoice 只有给路径才解析得出来），再退回按扩展名查。
[[nodiscard]] std::string AssociatedProgram(const std::filesystem::path& file) {
    const std::wstring full = file.wstring();
    const std::wstring ext = file.extension().wstring();
    for (const ASSOCSTR kind : {ASSOCSTR_EXECUTABLE, ASSOCSTR_FRIENDLYAPPNAME, ASSOCSTR_COMMAND}) {
        for (const std::wstring* key : {&full, &ext}) {
            if (key->empty()) {
                continue;
            }
            wchar_t buffer[MAX_PATH * 2] = {};
            auto length = static_cast<DWORD>(std::size(buffer));
            if (SUCCEEDED(::AssocQueryStringW(ASSOCF_NONE, kind, key->c_str(), L"open", buffer, &length)) &&
                buffer[0] != L'\0') {
                return util::Utf16ToUtf8(buffer);
            }
        }
    }
    return {};
}

} // namespace

void MaybeRunP56SelfTest() {
    // —— 第二段：等"自动选中最近输出"落地（`Selected()` 非空），最多 ~10s ——
    if (g_p56Stage == 1) {
        ++g_p56WaitFrames;
        const MediaItem* selected = media::MediaLibrary::Instance().Selected();
        if (selected == nullptr) {
            if (g_p56WaitFrames > 600) {
                g_p56Stage = 0;
                g_p56Pass = false;
                P56Note(fmt::format("FAIL ③c 自动选中最近输出：等了 ~10s 仍没选中任何条目（历史 {} 项）",
                                    media::MediaLibrary::Instance().Items().size()));
                WriteP56Report();
            }
            return;
        }
        const bool ok = selected->fileName == g_p56SampleName;
        g_p56Pass = g_p56Pass && ok;
        P56Note(fmt::format("{} ③c 自动选中最近输出：历史 {} 项，当前选中 {}（期望 {}）", ok ? "PASS" : "FAIL",
                            media::MediaLibrary::Instance().Items().size(), selected->fileName, g_p56SampleName));
        // —— 顺带进第三段：用**媒体库真实路径**取一次首帧缩略图（=「输出」页/预览面板那一帧做的事）——
        g_p56Stage = 2;
        g_p56WaitFrames = 0;
        g_p56ThumbKey = selected->key;
        if (selected->localPath.empty()) {
            g_p56Stage = 0;
            P56Note("INFO ③d 样本没在本地缓存 →「输出」页显示 —（S2 的降级分支）；下载到本地后会自动出首帧");
        } else {
            (void)media::MediaLibrary::Instance().TextureFor(*selected); // 触发异步取首帧 + 上传
            P56Note("（接着等媒体库把首帧缩略图传上 GPU…）");
        }
        WriteP56Report();
        return;
    }
    // —— 第三段：首帧纹理应出现在 `gpu::TextureCache`（key = MediaItem::key）——
    if (g_p56Stage == 2) {
        ++g_p56WaitFrames;
        const gpu::GpuTextureHandle handle = gpu::TextureCache().Find(g_p56ThumbKey);
        const gpu::GpuTexture* texture = handle ? gpu::Textures().Get(handle) : nullptr;
        if (texture != nullptr) {
            g_p56Stage = 0;
            P56Note(fmt::format("PASS ③d 媒体库视频首帧已上屏：纹理 {}x{}（key={}，走 MediaLibrary::TextureFor → "
                                "VideoThumb → GpuTextureManager）",
                                texture->width(), texture->height(), g_p56ThumbKey));
            WriteP56Report();
        } else if (g_p56WaitFrames > 600) {
            g_p56Stage = 0;
            g_p56Pass = false;
            P56Note("FAIL ③d 媒体库取视频首帧超时（~10s 没上屏）");
            WriteP56Report();
        }
        return;
    }
    if (g_p56Done) {
        return;
    }
    const char* raw = std::getenv("SHINE_P56_SELFTEST");
    if (raw == nullptr || *raw == '\0') {
        return;
    }
    g_p56Done = true;
    // ⚠️ `getenv` 给的是 **ANSI 代码页**字节（本机 936），中文路径要先 `AcpToUtf8`
    const std::filesystem::path sample = util::PathFromUtf8(util::AcpToUtf8(raw));
    g_p56SampleName = util::FileNameToUtf8(sample);
    bool pass = true;

    const auto sampleBytes = util::ReadFileBytes(sample);
    P56Note(fmt::format("P5.6 自检（结果预览）：样本 {}", util::PathToUtf8(sample)));
    P56Note(fmt::format("  样本存在={} 大小={} 字节", std::filesystem::exists(sample) ? "是" : "否",
                        sampleBytes ? sampleBytes->size() : 0));

    // ① 首帧缩略图（真机：Windows Shell 缩略图提供程序）
    //    注：自检是一次性动作，这里在主线程同步跑（会阻塞几十~几百 ms），生产路径走 VideoThumbnailAsync
    const VideoThumbResult thumb = VideoThumbnailSync(sample);
    if (thumb.image.valid()) {
        P56Note(fmt::format("PASS ① 首帧缩略图：{}x{} 像素（视频分辨率 {}x{}，耗时 {:.0f}ms）", thumb.image.width,
                            thumb.image.height, thumb.width, thumb.height, thumb.ms));
    } else {
        pass = false;
        P56Note(fmt::format("FAIL ① 首帧缩略图拿不到：{}", thumb.error));
    }
    if (thumb.width > 0 && thumb.height > 0) {
        P56Note(fmt::format("PASS ①b 视频分辨率（Shell 属性 System.Video.FrameWidth/Height）：{}x{}", thumb.width,
                            thumb.height));
    } else {
        P56Note("INFO ①b 视频分辨率为 0（该文件没有 System.Video.FrameWidth/Height 属性，UI 会显示 —）");
    }

    // ② 文件缺失：不崩 + 中文提示
    const std::filesystem::path missing = sample.parent_path() / L"__shine_p56_不存在的文件__.mp4";
    const VideoThumbResult missingThumb = VideoThumbnailSync(missing);
    const bool thumbMissingOk = !missingThumb.image.valid() && missingThumb.error.find("文件不存在") != std::string::npos;
    pass = pass && thumbMissingOk;
    P56Note(fmt::format("{} ② 缺失文件取首帧：不崩且给中文提示「{}」", thumbMissingOk ? "PASS" : "FAIL",
                        missingThumb.error));

    const std::string openMissing = util::ShellOpen(missing);
    const bool openMissingOk = openMissing.find("文件不存在") != std::string::npos;
    pass = pass && openMissingOk;
    P56Note(fmt::format("{} ②b 缺失文件点「播放」：不崩且给中文提示「{}」", openMissingOk ? "PASS" : "FAIL", openMissing));

    // ③ 播放：查系统关联程序（不拉起播放器）
    const std::string associated = AssociatedProgram(sample);
    P56Note(fmt::format("{} ③ 「播放」走系统播放器：{} 的关联程序 = {}", associated.empty() ? "INFO" : "PASS",
                        util::PathToUtf8(sample.extension()),
                        associated.empty() ? "（本机查不到关联程序，播放时会由系统询问用哪个程序打开）" : associated));
    if (std::getenv("SHINE_P56_SELFTEST_PLAY") != nullptr) {
        const std::string error = util::ShellOpen(sample); // 真调：会打开系统播放器（人工验收用）
        P56Note(fmt::format("{} ③b 真调 ShellOpen（会拉起播放器）：{}", error.empty() ? "PASS" : "FAIL",
                            error.empty() ? "已交给系统播放器" : error));
    }

    g_p56Pass = pass;
    g_p56Stage = 1;
    g_p56WaitFrames = 0;
    P56Note("（接着等 /history 回来校验 ③c 自动选中最近输出…）");
    WriteP56Report();

    // 顺带给"截图验收"把状态摆好：刷一次 /history（否则「输出」页是空的），
    // 并走一遍 S3 的"自动选中最近输出"（优先按样本文件名匹配）。
    media::MediaLibrary::Instance().RefreshAndSelectLatest(g_p56SampleName);
}

} // namespace shine::media
