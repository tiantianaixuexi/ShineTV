#include "app/output/OutputView.h"
#include "util/Encoding.h"
#include "util/Shell.h"

#include "comfy/ComfySession.h"
#include "core/Log.h"
#include "gpu/GpuTextureManager.h"
#include "media/MediaLibrary.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

namespace shine::app::output {
using namespace ::shine::media; // MediaLibrary / MediaItem / TextureState
namespace {

// 打开 / 定位统一走 `util/Shell.h`（P5.6 S1：错误文案只有一份，且是中文）
void OpenPath(const std::filesystem::path& path) {
    if (const std::string error = util::ShellOpen(path); !error.empty()) {
        log::Warn("{}", error);
    }
}

void RevealPath(const std::filesystem::path& path) {
    if (const std::string error = util::ShellReveal(path); !error.empty()) {
        log::Warn("{}", error);
    }
}

// ComfyUI 的 completed_at / create_time 是**秒**（epoch）
[[nodiscard]] std::string TimeText(std::int64_t seconds) {
    if (seconds <= 0) {
        return "—";
    }
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
    if (const std::tm* local = std::localtime(&t); local != nullptr) {
        tm = *local;
    }
    char buf[32];
    std::strftime(buf, sizeof(buf), "%m-%d %H:%M", &tm);
    return std::string{buf};
}

[[nodiscard]] const char* StateText(const MediaItem& item, TextureState state) {
    const bool video = item.kind == "video";
    switch (state) {
    case TextureState::Loading: return video ? "取首帧…" : "加载中…";
    case TextureState::Ready: return "";
    case TextureState::Failed: return video ? "首帧失败" : "缩略图失败";
    case TextureState::Unavailable: return video ? "—" : "—";
    case TextureState::Idle: return "…";
    }
    return "";
}

// P5.6 S2：视频显示分辨率（Shell 属性拿到的，拿不到就是 `—`）；图片条目保持原样（不加尺寸列）
[[nodiscard]] std::string SizeText(const MediaItem& item) {
    if (item.kind != "video") {
        return {};
    }
    if (item.width > 0 && item.height > 0) {
        return fmt::format("{}×{}", item.width, item.height);
    }
    return "—";
}

} // namespace

void DrawOutputView() {
    auto& library = MediaLibrary::Instance();
    auto& session = comfy::ComfySession::Instance();

    if (ImGui::SmallButton("刷新历史")) {
        library.Refresh(200);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 项 · 缓存 %.1f MB", library.Items().size(),
                        static_cast<double>(library.CachedBytes()) / (1024.0 * 1024.0));
    ImGui::SameLine();
    if (ImGui::SmallButton("清空缓存")) {
        library.ClearCache();
    }
    ImGui::SameLine();
    const std::string cacheDirText = util::PathToUtf8(library.CacheDir());
    ImGui::TextDisabled("· 缓存目录 %s", cacheDirText.c_str());
    ImGui::Separator();

    if (library.Items().empty()) {
        ImGui::TextDisabled("%s", session.State() == comfy::ConnectionState::Connected
                                      ? "暂无历史产物：跑一次生成后点「刷新历史」"
                                      : "未连接 ComfyUI：连接并生成后，这里会列出结果");
        return;
    }

    constexpr float kRowHeight = 52.0f;
    constexpr float kThumb = 40.0f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(library.Items().size()), kRowHeight);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const MediaItem& item = library.Items()[static_cast<std::size_t>(i)];
            ImGui::PushID(static_cast<int>(item.key));

            // 缩略图：只对可见行请求（TextureFor 内部异步拉取，未就绪时返回空句柄）
            // 视频条目走 P5.6 的"Windows Shell 首帧缩略图"（见 media/MediaLibrary.cpp 的 TextureForVideo）
            const gpu::GpuTextureHandle texture = library.TextureFor(item);
            if (texture) {
                if (gpu::GpuTexture* gpuTexture = gpu::Textures().Get(texture)) {
                    ImGui::Image(ImTextureRef(gpuTexture->imgui_id()), ImVec2(kThumb, kThumb));
                }
            } else {
                ImGui::Dummy(ImVec2(kThumb, kThumb));
                ImGui::SameLine();
                ImGui::TextDisabled("%s", StateText(item, library.StateOf(item.key)));
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("%s", item.fileName.c_str());
            const std::string promptShort =
                item.promptId.size() > 8 ? item.promptId.substr(0, 8) : item.promptId;
            const std::string timeText = TimeText(item.createdAtMillis);
            const std::string cacheText = item.localPath.empty() ? "未缓存" : "已缓存";
            const std::string sizeText = SizeText(item);
            if (sizeText.empty()) {
                ImGui::TextDisabled("%s · %s · prompt %s · %s", item.kind.c_str(), timeText.c_str(),
                                    promptShort.c_str(), cacheText.c_str());
            } else {
                ImGui::TextDisabled("%s · %s · %s · prompt %s · %s", item.kind.c_str(), sizeText.c_str(),
                                    timeText.c_str(), promptShort.c_str(), cacheText.c_str());
            }
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                // `path.string()` 走 ANSI 代码页会把中文路径变乱码 → 统一 util::PathToUtf8
                const std::string pathText = util::PathToUtf8(item.localPath);
                ImGui::SetTooltip("%s\n%s%s", item.fileName.c_str(),
                                  item.localPath.empty() ? "未缓存（双击下载）" : pathText.c_str(),
                                  library.ErrorOf(item.key).empty() ? "" : ("\n" + library.ErrorOf(item.key)).c_str());
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    library.Select(item.key); // 供右栏预览面板使用
                }
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (item.localPath.empty()) {
                        library.EnsureLocal(item, [](bool ok, std::filesystem::path path) {
                            if (ok) {
                                OpenPath(path);
                            }
                        });
                    } else {
                        OpenPath(item.localPath);
                    }
                    log::Info("打开结果 {}", item.fileName);
                }
            }

            if (ImGui::BeginPopupContextItem("output_ctx")) {
                const bool cached = !item.localPath.empty();
                if (ImGui::MenuItem("打开文件", nullptr, false, cached)) {
                    OpenPath(item.localPath);
                }
                if (ImGui::MenuItem("在资源管理器中显示", nullptr, false, cached)) {
                    RevealPath(item.localPath);
                }
                if (ImGui::MenuItem("复制路径")) {
                    const std::string pathText = util::PathToUtf8(item.localPath);
                    ImGui::SetClipboardText(cached ? pathText.c_str() : item.fileName.c_str());
                }
                if (ImGui::MenuItem("复制文件名")) {
                    ImGui::SetClipboardText(item.fileName.c_str());
                }
                if (ImGui::MenuItem("下载到本地缓存", nullptr, false, !cached)) {
                    library.EnsureLocal(item, [](bool, std::filesystem::path) {});
                }
                ImGui::Separator();
                if (ImGui::MenuItem("删除本地缓存", nullptr, false, cached)) {
                    std::error_code ec;
                    std::filesystem::remove(item.localPath, ec);
                    library.MarkUncached(item.key);
                    log::Info("已删除缓存 {}", item.fileName);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
}

} // namespace shine::app::output
