// R-S0：从 App.cpp 机械搬出 —— 函数体一字未改（只去掉了参数默认值，声明在头里）。
// P5.6：视频条目改为显示"首帧缩略图 + 系统播放器打开"（P4.3 S3 里"视频 → 系统播放器"的落地）。
#include "app/views/preview/DrawPreviewPanel.h"
#include "app/AppIncludes.h"

#include "util/Shell.h" // P5.6：交给系统播放器

namespace shine::app {

void DrawPreviewPanel() {
    // P4.3：真图预览（优先级：生成中预览 > 选中项 > 最近生成结果）
    auto& library = media::MediaLibrary::Instance();
    auto& session = comfy::ComfySession::Instance();
    const bool running = session.Busy() == comfy::BusyState::Running || session.Busy() == comfy::BusyState::Queued;
    const media::MediaItem* target = library.Selected() != nullptr ? library.Selected() : library.Latest();
    const bool showPreviewFrame = running && library.PreviewActive();
    // P5.6：视频条目显示的是**首帧缩略图**（Windows Shell），播放交给系统播放器（P4.3 S3 的"视频 → 系统播放器"）
    const bool videoTarget = target != nullptr && target->kind == "video";

    ImGui::TextDisabled("媒体预览");
    ImGui::SameLine();
    if (showPreviewFrame) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "· 生成中（%s）",
                           library.PreviewFromWs() ? "ws-frame" : "fallback");
    } else if (target != nullptr) {
        ImGui::TextDisabled("· %s", target->fileName.c_str());
    }
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float previewH = std::max(120.f, avail.y * 0.72f);
    ImGui::BeginChild("##preview", ImVec2(avail.x, previewH), true);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area = ImGui::GetContentRegionAvail();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& tc = theme::Current();

    // 深色底 + 棋盘格（透明 PNG 可见）
    draw->AddRectFilled(origin, ImVec2(origin.x + area.x, origin.y + area.y),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(tc.childBg[0] * 0.75f, tc.childBg[1] * 0.75f, tc.childBg[2] * 0.75f, 1.f)),
                        4.f);
    constexpr float kChecker = 12.0f;
    for (float y = 0.0f; y < area.y; y += kChecker) {
        for (float x = 0.0f; x < area.x; x += kChecker) {
            const bool odd = (static_cast<int>(x / kChecker) + static_cast<int>(y / kChecker)) % 2 != 0;
            if (!odd) {
                continue;
            }
            draw->AddRectFilled(ImVec2(origin.x + x, origin.y + y),
                                ImVec2(origin.x + std::min(x + kChecker, area.x), origin.y + std::min(y + kChecker, area.y)),
                                IM_COL32(60, 60, 66, 90));
        }
    }

    const gpu::GpuTextureHandle texture =
        showPreviewFrame ? library.PreviewTexture() : (target ? library.TextureFor(*target) : gpu::GpuTextureHandle{});
    gpu::GpuTexture* gpuTexture = texture ? gpu::Textures().Get(texture) : nullptr;
    if (gpuTexture != nullptr) {
        // 保持宽高比（contain）
        const float sx = area.x / static_cast<float>(gpuTexture->width());
        const float sy = area.y / static_cast<float>(gpuTexture->height());
        const float scale = std::min(sx, sy);
        const ImVec2 size{static_cast<float>(gpuTexture->width()) * scale,
                          static_cast<float>(gpuTexture->height()) * scale};
        const ImVec2 pos{origin.x + (area.x - size.x) * 0.5f, origin.y + (area.y - size.y) * 0.5f};
        ImGui::SetCursorScreenPos(pos);
        ImGui::Image(ImTextureRef(gpuTexture->imgui_id()), size);
        draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(120, 120, 130, 160), 2.f, 0, 1.0f);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%ux%u · %.0f%%%s", gpuTexture->width(), gpuTexture->height(), scale * 100.0f,
                              showPreviewFrame ? "\n生成中预览帧"
                              : videoTarget    ? "\n视频首帧缩略图（双击/「播放」用系统播放器打开）"
                              : (target != nullptr && !target->localPath.empty()
                                     ? "\n本地缓存"
                                     : "\n来自 ComfyUI /view（查看器在 G 线 S10 提供）"));
        }
    } else {
        const char* text =
            showPreviewFrame ? "生成中：等待预览帧…"
            : videoTarget    ? (target->localPath.empty()
                                    ? "视频未下载到本地：先下载才能取首帧缩略图"
                                    : "正在取视频首帧缩略图…（Windows Shell）")
            : (target != nullptr ? "正在加载图片…（下载 → 解码 → 上传）"
                                 : (session.State() == comfy::ConnectionState::Connected
                                        ? "还没有结果：跑一次生成，或到「输出」页刷新历史"
                                        : "未连接 ComfyUI：连接后可预览输出"));
        const ImVec2 ts = ImGui::CalcTextSize(text);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + (area.x - ts.x) * 0.5f, origin.y + (area.y - ts.y) * 0.5f));
        ImGui::TextDisabled("%s", text);
    }
    if (showPreviewFrame) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.f, origin.y + 8.f));
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "生成中…（完成后覆盖层自动消失）");
    }
    ImGui::EndChild();
    ImGui::Spacing();
    if (target != nullptr) {
        if (videoTarget) {
            // 视频：尺寸取 Shell 属性给的**视频分辨率**（不是缩略图尺寸）
            if (target->width > 0 && target->height > 0) {
                ImGui::TextDisabled("视频尺寸：%d×%d · 缓存：%s", target->width, target->height,
                                    target->localPath.empty() ? "未缓存" : "已缓存");
            } else {
                ImGui::TextDisabled("视频尺寸：— · 缓存：%s", target->localPath.empty() ? "未缓存" : "已缓存");
            }
            ImGui::BeginDisabled(target->localPath.empty());
            if (ImGui::Button(target->localPath.empty() ? "播放（先下载）" : "播放（系统播放器）", ImVec2(-1, 0))) {
                if (const std::string error = util::ShellOpen(target->localPath); !error.empty()) {
                    log::Warn("{}", error);
                }
            }
            ImGui::EndDisabled();
        } else {
            ImGui::TextDisabled("尺寸：%ux%u · 缓存：%s", gpuTexture != nullptr ? gpuTexture->width() : 0,
                                gpuTexture != nullptr ? gpuTexture->height() : 0,
                                target->localPath.empty() ? "未缓存" : "已缓存");
        }
        if (ImGui::Button("下载到本地缓存", ImVec2(-1, 0))) {
            library.EnsureLocal(*target, [](bool, std::filesystem::path) {});
        }
    }
    ImGui::TextDisabled("活动任务：%s",
                        session.Queue().ActivePromptId().empty() ? "无" : session.Queue().ActivePromptId().c_str());
    ImGui::TextDisabled("纹理缓存：%.1f MB / %zu 项 · 命中率 %.0f%%",
                        static_cast<double>(gpu::TextureCache().Bytes()) / (1024.0 * 1024.0),
                        gpu::TextureCache().Count(), gpu::TextureCache().HitRate() * 100.0);
}

} // namespace shine::app
