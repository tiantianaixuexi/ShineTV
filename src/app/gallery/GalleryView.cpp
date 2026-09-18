// shine::app::gallery —— 图库视图（G-S5 列表 / G-S6 网格 / G-S7 异步缩略图）
//
// 布局：中央窗口「图库」= 工具条 + 缩略图网格；侧栏「图库」= 来源/目录/缩略图/缓存/统计。
// G-S7：缩略图走 `gallery::Thumbs()`（worker 解码 + UI 上传），网格只读状态与纹理句柄。
// 纪律：条目与选中只在 `gallery::Model()`（UI 线程）；缩略图纹理由 ThumbnailService 持有。
#include "app/gallery/GalleryView.h"
#include "app/gallery/FolderPicker.h"
#include "app/paint/PaintCanvasView.h" // P6.4 发送到画布

#include "app/UiState.h"
#include "app/ui/ThumbGrid.h"
#include "app/ui/Widgets.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/FileActions.h"
#include "gallery/Gallery.h"
#include "gallery/GalleryLayout.h"
#include "gallery/ImageLoader.h"
#include "gallery/ImageScanner.h"
#include "gallery/ExifOrientation.h"
#include "gallery/ThumbnailService.h"
#include "gallery/Viewer.h"
#include "gallery/cache/CpuThumbCache.h"
#include "gallery/cache/DiskThumbCache.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuTexture.h"
#include "gpu/GpuTextureManager.h"
#include "theme/Theme.h"
#include "util/Encoding.h"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace shine::app::gallery {
using ::shine::gallery::CacheBytes;
using ::shine::gallery::CloseViewer;
using ::shine::gallery::GalleryFilter;
using ::shine::gallery::GalleryModel;
using ::shine::gallery::GalleryState;
using ::shine::gallery::ImageId;
using ::shine::gallery::ImageInfo;
using ::shine::gallery::ImageLoader;
using ::shine::gallery::IsSourceAvailable;
using ::shine::gallery::ItemsGeneration;
using ::shine::gallery::LoadState;
using ::shine::gallery::LoadedCount;
using ::shine::gallery::Model;
using ::shine::gallery::OpenViewer;
using ::shine::gallery::RequestScan;
using ::shine::gallery::RescanCurrent;
using ::shine::gallery::SetThumbReadyCount;
using ::shine::gallery::SortKey;
using ::shine::gallery::SortKeyLabel;
using ::shine::gallery::SourceKind;
using ::shine::gallery::SourceLabel;
using ::shine::gallery::SourceUnavailableHint;
using ::shine::gallery::State;
using ::shine::gallery::ThumbPriority;
using ::shine::gallery::Thumbs;
using ::shine::gallery::UploadInFlight;
using ::shine::gallery::UploadToComfyInput;
using ::shine::gallery::LastUploadedName;
using ::shine::gallery::LastUploadError;
using ::shine::gallery::LastGraphDropPath;
using ::shine::gallery::ViewerImageId;
using ::shine::gallery::ViewerOpen;

// 「Shift 连选」的锚点（表中下标）
std::size_t g_anchor = 0;

// 查看器原图（G-S7 仍同步；G-S10 异步 + 缩略图占位）
std::unordered_map<ImageId, ::shine::gpu::GpuTextureHandle> g_full;
std::vector<ui::ThumbGridItem> g_gridItems;
std::uint64_t g_seenGeneration = 0;
bool g_openViewerEnv = false;
int g_lastGridColumns = 1;
// 当前缩略图边长（Ctrl+滚轮连续缩放；与 Settings::galleryThumbSize 同步）
float g_thumbSide = 0.f;
// 上一帧可见区间（ThumbGrid 返回）；G-S7 只对可见/预取带发请求，G-S9 再完整虚拟化
std::size_t g_visibleBegin = 0;
std::size_t g_visibleEnd = 0;
float g_lastScrollY = 0.f;
bool g_loggedFirstBand = false;
// G-S11：右键菜单与删除确认
bool g_ctxMenu = false;
ImageId g_ctxId = 0;
bool g_confirmDelete = false;
std::vector<ImageId> g_pendingDelete;

[[nodiscard]] std::string HumanSize(std::uint64_t bytes) {
    constexpr double kKiB = 1024.0;
    if (bytes >= 1024ull * 1024ull) {
        return fmt::format("{:.1f} MB", static_cast<double>(bytes) / (kKiB * kKiB));
    }
    if (bytes >= 1024ull) {
        return fmt::format("{:.1f} KB", static_cast<double>(bytes) / kKiB);
    }
    return fmt::format("{} B", bytes);
}

[[nodiscard]] ImVec4 Accent() {
    const theme::ThemeColors& c = theme::Current();
    return ImVec4(c.accent[0], c.accent[1], c.accent[2], 1.f);
}

[[nodiscard]] ImVec4 Danger() {
    const theme::ThemeColors& c = theme::Current();
    return ImVec4(c.danger[0], c.danger[1], c.danger[2], 1.f);
}

[[nodiscard]] ImVec4 Busy() {
    const theme::ThemeColors& c = theme::Current();
    return ImVec4(c.accentAlt[0], c.accentAlt[1], c.accentAlt[2], 1.f);
}

void DropThumbCaches(bool clearCpu = true) {
    for (auto& [id, handle] : g_full) {
        ::shine::gpu::Textures().Release(handle);
    }
    g_full.clear();
    Thumbs().Reset(); // 只清图库 GPU key
    if (clearCpu) {
        ::shine::gallery::CpuThumbCache::Instance().Clear();
    }
    g_visibleBegin = 0;
    g_visibleEnd = 0;
    g_loggedFirstBand = false;
}

[[nodiscard]] ImTextureID FullId(const ImageInfo& info) {
    const auto it = g_full.find(info.id);
    if (it != g_full.end()) {
        if (::shine::gpu::GpuTexture* tex = ::shine::gpu::Textures().Get(it->second)) {
            return tex->imgui_id();
        }
        g_full.erase(it);
    }
    if (!::shine::gpu::Ready()) {
        return 0;
    }
    auto decoded = ImageLoader::Instance().Load(info.path);
    if (!decoded || !decoded->valid()) {
        return 0;
    }
    auto handle = ::shine::gpu::Textures().Upload(
        decoded->width, decoded->height, std::span<const std::byte>(decoded->data, decoded->bytes));
    if (!handle.has_value()) {
        log::Warn("查看器原图上传失败：{} —— {}", util::PathToUtf8(info.path),
                  ::shine::gpu::GpuErrorText(handle.error()));
        return 0;
    }
    g_full.emplace(info.id, *handle);
    if (::shine::gpu::GpuTexture* tex = ::shine::gpu::Textures().Get(*handle)) {
        return tex->imgui_id();
    }
    return 0;
}

void HandleRowClick(std::size_t index, ImageId id) {
    const ImGuiIO& io = ImGui::GetIO();
    GalleryModel& model = Model();
    if (io.KeyShift) {
        const std::vector<ImageInfo>& items = model.Items();
        if (g_anchor < items.size()) {
            model.SelectRange(items[g_anchor].id, id);
            return;
        }
    }
    if (io.KeyCtrl) {
        model.ToggleSelect(id);
    } else {
        model.SelectOnly(id);
    }
    g_anchor = index;
}

[[nodiscard]] std::vector<std::filesystem::path> SelectedPaths() {
    std::vector<std::filesystem::path> out;
    GalleryModel& model = Model();
    for (const ImageId id : model.Selection()) {
        if (const ImageInfo* info = model.Find(id)) {
            out.push_back(info->path);
        }
    }
    return out;
}

void DrawGalleryContextMenu(const std::vector<ImageInfo>& items) {
    if (!g_ctxMenu) {
        return;
    }
    ImGui::OpenPopup("##gallery_ctx");
    g_ctxMenu = false;
    if (g_ctxId != 0) {
        // 右键未选中项 → 单选它；已选中则保留多选
        if (!Model().IsSelected(g_ctxId)) {
            Model().SelectOnly(g_ctxId);
        }
    }
    if (ImGui::BeginPopup("##gallery_ctx")) {
        GalleryModel& model = Model();
        const std::size_t nSel = model.SelectionCount();
        const ImageInfo* primary = model.PrimaryItem();
        if (primary != nullptr) {
            ImGui::TextDisabled("%s", util::FileNameToUtf8(primary->path).c_str());
            ImGui::Separator();
        }
        if (ImGui::MenuItem("在查看器中打开", "Enter") && primary != nullptr) {
            OpenViewer(primary->id);
            ::shine::app::State().showViewer = true;
        }
        if (ImGui::MenuItem("设为工作流输入", nullptr, false, primary != nullptr && !UploadInFlight()) &&
            primary != nullptr) {
            UploadToComfyInput(primary->path);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("上传到 ComfyUI input（type=input, overwrite=true）\n"
                              "成功后 LastUploadedName 可供 @image 引用");
        }
        if (ImGui::MenuItem("发送到画布", nullptr, false, primary != nullptr) && primary != nullptr) {
            if (!paint::LoadImageToCanvas(util::PathToUtf8(primary->path))) {
                log::Warn("图库：发送到画布失败 {}", util::PathToUtf8(primary->path));
            }
        }
        ImGui::Separator();
        const auto paths = SelectedPaths();
        const bool hasSel = !paths.empty();
        if (ImGui::MenuItem("复制文件路径", nullptr, false, hasSel)) {
            ::shine::gallery::file_actions::CopyPaths(paths);
        }
        if (ImGui::MenuItem("复制文件名", nullptr, false, hasSel)) {
            ::shine::gallery::file_actions::CopyNames(paths);
        }
        if (ImGui::MenuItem("在资源管理器中显示", nullptr, false, primary != nullptr) && primary != nullptr) {
            std::string err;
            if (!::shine::gallery::file_actions::RevealInExplorer(primary->path, &err) && !err.empty()) {
                log::Warn("图库：{}", err);
            }
        }
        if (ImGui::MenuItem("删除到回收站…", "Del", false, hasSel)) {
            const auto span = model.Selection();
            g_pendingDelete.assign(span.begin(), span.end());
            g_confirmDelete = true;
        }
        if (nSel > 1) {
            ImGui::Separator();
            ImGui::TextDisabled("已选中 %zu 项", nSel);
        }
        const std::string up = LastUploadedName();
        if (!up.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("最近上传：%s", up.c_str());
        }
        if (!LastUploadError().empty()) {
            ImGui::TextColored(Danger(), "上传失败：%s", LastUploadError().c_str());
        }
        ImGui::EndPopup();
    }

    if (g_confirmDelete) {
        ImGui::OpenPopup("确认删除");
        g_confirmDelete = false;
    }
    if (ImGui::BeginPopupModal("确认删除", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("将把 %zu 个文件删除到回收站？", g_pendingDelete.size());
        ImGui::TextDisabled("可从回收站恢复。");
        ImGui::Spacing();
        if (ImGui::Button("确认删除", ImVec2(120.f, 0.f))) {
            std::vector<std::filesystem::path> paths;
            for (const ImageId id : g_pendingDelete) {
                if (const ImageInfo* info = Model().Find(id)) {
                    paths.push_back(info->path);
                }
            }
            std::string err;
            if (::shine::gallery::file_actions::Recycle(paths, &err)) {
                log::Info("图库：回收站删除 {} 个文件", paths.size());
                RescanCurrent(); // 刷新列表
            } else if (!err.empty()) {
                log::Warn("图库：删除失败 —— {}", err);
            }
            g_pendingDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(80.f, 0.f))) {
            g_pendingDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// 工具条：窄窗口自动拆两行，避免按钮/元数据被裁切穿出
void DrawToolbar() {
    GalleryState& st = State();
    if (ImGui::Button("打开图片文件夹…")) {
        PickAndScanLocalFolder();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!IsSourceAvailable(st.source));
    if (ImGui::Button("刷新")) {
        RescanCurrent();
    }
    ImGui::EndDisabled();

    // G-S14：搜索 + 排序 + 扩展名过滤
    static char searchBuf[128] = {};
    static int sortIndex = 0;
    static bool sortAsc = true;
    static bool fPng = true;
    static bool fJpg = true;
    static bool fWebp = true;
    static bool fAvif = false;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.f);
    if (ImGui::InputTextWithHint("##gsearch", "搜索文件名…", searchBuf, sizeof(searchBuf))) {
        GalleryFilter f = Model().Filter();
        f.nameContains = searchBuf;
        Model().SetFilter(std::move(f));
        g_visibleBegin = g_visibleEnd = 0;
    }
    ImGui::SameLine();
    static const char* kSorts[] = {"文件名", "修改时间", "文件大小", "格式"};
    static const SortKey kSortKeys[] = {SortKey::Name, SortKey::Modified, SortKey::Size, SortKey::Format};
    ImGui::SetNextItemWidth(100.f);
    if (ImGui::Combo("##gsort", &sortIndex, kSorts, 4)) {
        Model().SortBy(kSortKeys[sortIndex], sortAsc);
        g_visibleBegin = g_visibleEnd = 0;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("升序", &sortAsc)) {
        Model().SortBy(kSortKeys[sortIndex], sortAsc);
        g_visibleBegin = g_visibleEnd = 0;
    }
    ImGui::SameLine();
    bool fmtDirty = false;
    fmtDirty |= ImGui::Checkbox("png", &fPng);
    ImGui::SameLine();
    fmtDirty |= ImGui::Checkbox("jpg", &fJpg);
    ImGui::SameLine();
    fmtDirty |= ImGui::Checkbox("webp", &fWebp);
    ImGui::SameLine();
    fmtDirty |= ImGui::Checkbox("avif", &fAvif);
    if (fmtDirty) {
        GalleryFilter f = Model().Filter();
        f.formats.clear();
        if (fPng) {
            f.formats.push_back("png");
        }
        if (fJpg) {
            f.formats.push_back("jpg");
            f.formats.push_back("jpeg");
        }
        if (fWebp) {
            f.formats.push_back("webp");
        }
        if (fAvif) {
            f.formats.push_back("avif");
        }
        Model().SetFilter(std::move(f));
        g_visibleBegin = g_visibleEnd = 0;
    }

    const std::string meta = fmt::format("{} · 显示 {} / 全部 {} · 排序 {}{} · {} 列 · 在飞/队列 {}",
                                         SourceLabel(st.source), Model().Count(), Model().Items().size(),
                                         SortKeyLabel(Model().Sort()), Model().SortAscending() ? "↑" : "↓",
                                         g_lastGridColumns, Thumbs().InFlightAndQueued());
    if (st.scanning) {
        ImGui::TextColored(Accent(), "扫描中…");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", meta.c_str());
    } else {
        ImGui::TextDisabled("%s", meta.c_str());
    }
}

void DrawGrid() {
    using ::shine::gallery::ComputeGrid;
    using ::shine::gallery::ComputeVisible;
    using ::shine::gallery::GridMetrics;
    using ::shine::gallery::ScrollDir;
    using ::shine::gallery::VisibleRange;

    GalleryModel& model = Model();
    const std::vector<ImageInfo>& items = model.View(); // G-S14：过滤+排序后的显示视图
    const float statusH = 28.f;
    const float viewH = std::max(80.f, ImGui::GetContentRegionAvail().y - statusH);
    if (g_thumbSide < 24.f) {
        g_thumbSide = static_cast<float>(std::clamp(Settings().galleryThumbSize, 64, 512));
    }
    const float thumbSize = g_thumbSide;
    const auto targetSide = static_cast<std::uint32_t>(thumbSize);

    if (g_seenGeneration != ItemsGeneration()) {
        DropThumbCaches(true);
        g_seenGeneration = ItemsGeneration();
        Thumbs().ResetRequestStats();
        g_loggedFirstBand = false;
        g_lastScrollY = 0.f;
    }

    // G-S9：用上一帧滚动信息算请求带（首帧 = 第一屏）
    const float availW = std::max(80.f, ImGui::GetContentRegionAvail().x);
    const GridMetrics metrics = ComputeGrid(availW, thumbSize, 1.f, items.size(), 6.f, 34.f);
    ScrollDir dir = ScrollDir::None;
    if (g_visibleEnd > g_visibleBegin) {
        // 用上一帧 scrollY 与更早值比方向；这里用 ThumbGrid 回写的 g_lastScrollY
    }
    const float scrollY = g_lastScrollY;
    if (g_lastScrollY > 0.5f && g_visibleEnd > g_visibleBegin) {
        // 方向在 frame 回写后再比；本帧请求带上一帧方向
    }
    // 首帧没有 scroll：只请求第一屏
    const VisibleRange band = items.empty()
                                  ? VisibleRange{}
                                  : (g_visibleEnd > g_visibleBegin
                                         ? VisibleRange{g_visibleBegin, g_visibleEnd}
                                         : ComputeVisible(metrics, 0.f, viewH, 1, ScrollDir::None));
    // 用 GalleryLayout 重算一版（含方向预取），与绘制区间取并集，避免漏请求
    static float s_prevScroll = 0.f;
    ScrollDir moveDir = ScrollDir::None;
    if (scrollY > s_prevScroll + 2.f) {
        moveDir = ScrollDir::Down;
    } else if (scrollY + 2.f < s_prevScroll) {
        moveDir = ScrollDir::Up;
    }
    const VisibleRange layoutBand = ComputeVisible(metrics, scrollY, viewH, 1, moveDir);
    const std::size_t reqB = band.empty() ? layoutBand.begin : std::min(band.begin, layoutBand.begin);
    const std::size_t reqE = std::max(band.end, layoutBand.end);
    const std::size_t drawB = g_visibleEnd > g_visibleBegin ? g_visibleBegin : layoutBand.begin;
    const std::size_t drawE = g_visibleEnd > g_visibleBegin ? g_visibleEnd : layoutBand.end;

    if (!g_loggedFirstBand && !items.empty() && Thumbs().WorkerStarts() > 0) {
        // 等首批派工后再打一行，便于验收「不是 2 万条」
        if (Thumbs().InFlightAndQueued() == 0 || Thumbs().WorkerStarts() >= 16) {
            log::Info("G-S9 首屏请求带 [{}..{}) / 共 {}；worker starts={} requestCalls={}", reqB, reqE, items.size(),
                      Thumbs().WorkerStarts(), Thumbs().RequestCalls());
            g_loggedFirstBand = true;
        }
    }

    if (g_gridItems.size() != items.size()) {
        g_gridItems.clear();
        g_gridItems.resize(items.size());
    }
    // O(n) 只同步 id（点击/命中用）；昂贵字段只填请求带
    for (std::size_t i = 0; i < items.size(); ++i) {
        g_gridItems[i].id = items[i].id;
    }

    for (std::size_t i = reqB; i < reqE && i < items.size(); ++i) {
        const ImageInfo& info = items[i];
        const bool visible = i >= drawB && i < drawE;
        const LoadState st = Thumbs().State(info.id);
        if (st == LoadState::NotLoaded) {
            Thumbs().Request(info.id, info.path, targetSide,
                             visible ? ThumbPriority::High : ThumbPriority::Normal);
        }
        const LoadState st2 = Thumbs().State(info.id);
        const auto handle = Thumbs().Texture(info.id);
        ui::ThumbGridItem& cell = g_gridItems[i];
        cell.id = info.id;
        cell.label = util::FileNameToUtf8(info.path);
        cell.sublabel = fmt::format("{}×{} · {}", info.width, info.height, info.format);
        cell.tooltip = fmt::format("{}\n{}×{} · {} · {}", util::PathToUtf8(info.path), info.width, info.height,
                                   info.format, HumanSize(info.fileSize));
        cell.dragPayload = util::PathToUtf8(info.path); // G-S11：可拖到节点图 / 分镜表
        cell.failed = st2 == LoadState::Failed;
        cell.placeholder = !cell.failed && !handle;
        cell.selected = model.IsSelected(info.id);
        cell.texture = 0;
        if (!cell.failed && handle) {
            if (::shine::gpu::GpuTexture* tex = ::shine::gpu::Textures().Get(handle)) {
                cell.texture = tex->imgui_id();
            }
        }
        cell.imageW = info.width;
        cell.imageH = info.height;
    }

    // 请求带之外的 Queued 降优先级（不取消）
    if (reqE > reqB && !items.empty()) {
        const std::size_t edge = std::min<std::size_t>(items.size(), reqE + 32);
        for (std::size_t i = edge; i < items.size(); ++i) {
            if (Thumbs().State(items[i].id) == LoadState::Queued) {
                Thumbs().Demote(items[i].id);
            }
        }
    }

    ui::ThumbGridStyle style;
    style.thumbSide = thumbSize;
    style.thumbSideMin = 64.f;
    style.thumbSideMax = 512.f;
    style.enableCtrlWheelZoom = true;
    style.gap = 6.f;
    style.pad = 4.f;
    const ui::ThumbGridOutcome frame = ui::DrawThumbGrid("gallery", g_gridItems, style, viewH);
    g_lastGridColumns = frame.columns;
    g_visibleBegin = frame.visibleBegin;
    g_visibleEnd = frame.visibleEnd;
    s_prevScroll = g_lastScrollY;
    g_lastScrollY = frame.scrollY;

    if (frame.zoomChanged) {
        g_thumbSide = frame.thumbSide;
        Settings().galleryThumbSize = static_cast<int>(std::lround(frame.thumbSide));
        SaveSettings();
        DropThumbCaches(false); // 换尺寸：GPU 作废，CPU 按 targetSize 自然 miss/hit
        SetThumbReadyCount(0);
        log::Info("图库：Ctrl+滚轮缩略图 → {} px（{} 列）—— 可见带将按新档重请求", Settings().galleryThumbSize,
                  frame.columns);
    }

    if (frame.clickedId != 0) {
        const std::size_t index = model.IndexOf(frame.clickedId);
        HandleRowClick(index < items.size() ? index : 0, frame.clickedId);
        if (Thumbs().State(frame.clickedId) == LoadState::Failed) {
            Thumbs().Retry(frame.clickedId);
            log::Info("图库：点击重试缩略图 id={}", frame.clickedId);
        }
    }
    if (frame.rightClickedId != 0) {
        g_ctxId = frame.rightClickedId;
        g_ctxMenu = true;
    }
    DrawGalleryContextMenu(items);
    if (frame.doubleClickedId != 0) {
        Model().SelectOnly(frame.doubleClickedId);
        OpenViewer(frame.doubleClickedId);
        ::shine::app::State().showViewer = true;
        log::Info("G-S10：双击打开查看器 id={}", frame.doubleClickedId);
    }

    if (!g_openViewerEnv) {
        if (const char* raw = std::getenv("SHINE_GALLERY_OPEN_VIEWER"); raw != nullptr && raw[0] == '1') {
            if (!items.empty()) {
                g_openViewerEnv = true;
                Model().SelectOnly(items.front().id);
                OpenViewer(items.front().id);
                ::shine::app::State().showViewer = true;
                log::Info("自检模式：打开图库查看器（SHINE_GALLERY_OPEN_VIEWER）");
            }
        }
    }

    // G-S10 自检：打开查看器后自动切几张并缩放，验证异步丢弃与缓存上限
    if (const char* raw = std::getenv("SHINE_GALLERY_VIEWER_CHECK"); raw != nullptr && raw[0] == '1') {
        static int phase = 0;
        static int steps = 0;
        if (::shine::gallery::viewer::IsOpen() && phase == 0) {
            phase = 1;
            log::Info("G-S10 自检：查看器已打开，开始连续切换");
        }
        if (phase == 1 && steps < 12) {
            if (::shine::gallery::viewer::Step(1)) {
                ++steps;
                if (steps == 4) {
                    // 中途滚轮缩放一次（模拟）
                    log::Info("G-S10 自检：已切换 {} 张，zoom={:.2f} fullCache={} loads={} discarded={}", steps,
                              ::shine::gallery::viewer::Get().zoom, ::shine::gallery::viewer::FullCacheCount(),
                              ::shine::gallery::viewer::FullLoads(), ::shine::gallery::viewer::DiscardedLoads());
                }
            }
        } else if (phase == 1 && steps >= 12) {
            phase = 2;
            const std::size_t cache = ::shine::gallery::viewer::FullCacheCount();
            const std::size_t loads = ::shine::gallery::viewer::FullLoads();
            const std::size_t disc = ::shine::gallery::viewer::DiscardedLoads();
            log::Info("G-S10 自检：切换 12 次后 fullCache={} loads={} discarded={} idx={} / {}", cache, loads, disc,
                      ::shine::gallery::viewer::Get().index + 1, ::shine::gallery::viewer::Get().total);
            if (cache <= 4 && loads >= 1) {
                log::Info("G-S10 自检 OVERALL PASS：原图缓存未无界上涨（cache={}≤4 loads={}）", cache, loads);
            } else {
                log::Warn("G-S10 自检：cache={} loads={} 需复核", cache, loads);
            }
        }
    }

    // G-S14 自检：统计磁盘命中 + 搜索/排序
    if (const char* raw = std::getenv("SHINE_GALLERY_S14_CHECK"); raw != nullptr && raw[0] == '1') {
        static int phase = 0;
        if (phase == 0 && !Model().Items().empty() && Thumbs().InFlightAndQueued() == 0) {
            phase = 1;
            log::Info("G-S14 自检：全量={} 显示={} 磁盘缓存 {}", Model().Items().size(), Model().View().size(),
                      ::shine::gallery::disk_cache::Summary());
            GalleryFilter f = Model().Filter();
            f.nameContains = "bulk";
            Model().SetFilter(f);
            log::Info("G-S14 自检：搜索 bulk → 显示 {}", Model().View().size());
            Model().SortBy(SortKey::Size, false);
            if (!Model().View().empty()) {
                log::Info("G-S14 自检：按大小降序首项 {} {}B", util::FileNameToUtf8(Model().View().front().path),
                          Model().View().front().fileSize);
            }
            f = GalleryFilter{};
            Model().SetFilter(f);
            Model().SortBy(SortKey::Name, true);
        } else if (phase >= 1 && Thumbs().InFlightAndQueued() == 0) {
            static int logN = 0;
            if (logN == 0) {
                ++logN;
                log::Info("G-S14 自检：磁盘命中率 {:.0f}%（{}）", ::shine::gallery::disk_cache::HitRate() * 100.0,
                          ::shine::gallery::disk_cache::Summary());
            }
        }
    }
    if (const char* raw = std::getenv("SHINE_GALLERY_S11_CHECK"); raw != nullptr && raw[0] == '1') {
        static int phase = 0;
        static int waited = 0;
        static std::filesystem::path s_firstPath;
        if (phase == 0 && !items.empty()) {
            phase = 1;
            const ImageInfo& info = items[0];
            s_firstPath = info.path;
            Model().SelectOnly(info.id);
            ::shine::gallery::file_actions::CopyPaths({info.path});
            ::shine::gallery::file_actions::CopyNames({info.path});
            std::string revealErr;
            const bool revealed = ::shine::gallery::file_actions::RevealInExplorer(info.path, &revealErr);
            ::shine::gallery::SetLastGraphDropPath(util::PathToUtf8(info.path));
            log::Info("G-S11 自检：复制路径/文件名；资源管理器定位 {}（{}）；模拟拖入节点图 {}", revealed ? "OK" : "FAIL",
                      revealErr.empty() ? "-" : revealErr, util::PathToUtf8(info.path));
            UploadToComfyInput(info.path);
            log::Info("G-S11 自检：已发起上传 {}", util::PathToUtf8(info.path));
        } else if (phase == 1) {
            ++waited;
            if (!UploadInFlight() && waited > 30) {
                phase = 2;
                const std::string name = LastUploadedName();
                const std::string err = LastUploadError();
                log::Info("G-S11 自检：上传结束 LastUploadedName={} LastUploadError={}", name.empty() ? "(空)" : name,
                          err.empty() ? "(空)" : err);
                if (!name.empty()) {
                    log::Info("G-S11 自检 OVERALL PASS：上传成功 name={}（type=input overwrite=true）", name);
                } else if (!err.empty()) {
                    log::Warn("G-S11 自检：上传失败（ComfyUI 未启动时预期）—— {}", err);
                }
                const std::filesystem::path tmp = s_firstPath.parent_path() / "_gs11_recycle.png";
                if (std::filesystem::exists(tmp)) {
                    std::string delErr;
                    if (::shine::gallery::file_actions::Recycle({tmp}, &delErr)) {
                        log::Info("G-S11 自检：回收站删除临时文件 OK（二次确认由 UI 弹窗，自检直接调 Recycle）");
                    } else {
                        log::Warn("G-S11 自检：回收站删除失败 —— {}", delErr);
                    }
                }
            }
        }
    }

    const std::size_t ready = Thumbs().ReadyCount();
    SetThumbReadyCount(ready);
    const GalleryState& st = State();
    ImGui::TextDisabled(
        "共 %zu 张 · 已加载 %zu · 失败 %zu · 在飞/队列 %zu · 可见[%zu..%zu) · worker %zu · %d 列 · %dpx",
        model.Count(), ready, Thumbs().FailedCount(), Thumbs().InFlightAndQueued(), frame.visibleBegin,
        frame.visibleEnd, Thumbs().WorkerStarts(), frame.columns, Settings().galleryThumbSize);
    if (model.SelectionCount() > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("· 选中 %zu", model.SelectionCount());
    }
    if (st.truncated) {
        ImGui::SameLine();
        ImGui::TextColored(Busy(), "· 结果已截断");
    }
}

void DrawSourceSection() {
    GalleryState& st = State();
    app::ui::SectionText("来源");
    constexpr SourceKind kKinds[3] = {SourceKind::Local, SourceKind::ComfyOutput, SourceKind::ComfyInput};
    for (const SourceKind kind : kKinds) {
        const bool available = IsSourceAvailable(kind);
        const std::string label = SourceLabel(kind);
        ImGui::BeginDisabled(!available);
        if (ImGui::RadioButton(label.c_str(), st.source == kind) && st.source != kind) {
            RequestScan(kind);
        }
        ImGui::EndDisabled();
        if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", SourceUnavailableHint(kind).c_str());
        }
    }
    if (!IsSourceAvailable(st.source)) {
        ImGui::TextColored(Danger(), "%s", SourceUnavailableHint(st.source).c_str());
    }
}

void DrawDirectorySection() {
    GalleryState& st = State();
    app::ui::SectionText("目录");
    const std::string root = st.root.empty() ? std::string("（未选择）") : util::PathToUtf8(st.root);
    ImGui::TextWrapped("%s", root.c_str());
    if (!st.message.empty()) {
        ImGui::TextDisabled("%s", st.message.c_str());
    }
    if (ImGui::Button("选择文件夹…", ImVec2(-1.f, 0.f))) {
        PickAndScanLocalFolder();
    }
    ImGui::BeginDisabled(!IsSourceAvailable(st.source));
    if (ImGui::Button("重新扫描", ImVec2(-1.f, 0.f))) {
        RescanCurrent();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("提示：Ctrl+O 也能打开图片文件夹");
}

void DrawThumbSection() {
    app::ui::SectionText("缩略图");
    static const char* const kLabels[] = {"128", "256", "512"};
    static const int kValues[] = {128, 256, 512};
    int index = 1;
    for (int i = 0; i < 3; ++i) {
        if (kValues[i] == Settings().galleryThumbSize) {
            index = i;
        }
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("##gallery_thumb_size", &index, kLabels, 3)) {
        Settings().galleryThumbSize = kValues[index];
        g_thumbSide = static_cast<float>(kValues[index]);
        SaveSettings();
        DropThumbCaches(false); // G-S9：换尺寸档不丢 CPU 缓存，按新 targetSize 重请求可见带
        SetThumbReadyCount(0);
        log::Info("图库：尺寸档 → {}px（列将重排）", kValues[index]);
    }
    ImGui::TextDisabled("Ctrl+滚轮缩放（64–512）；可见带虚拟化 G-S9");
}

void DrawCacheSection() {
    app::ui::SectionText("缓存");
    app::ui::KvRow("GPU 纹理", fmt::format("{} · {}", ::shine::gpu::Textures().TextureCount(),
                                           HumanSize(::shine::gpu::Textures().UsedBytes())));
    app::ui::KvRow("CPU LRU", fmt::format("{} · 命中 {:.0f}%", HumanSize(Thumbs().CpuCacheBytes()),
                                           Thumbs().CpuHitRate() * 100.0));
    app::ui::KvRow("GPU LRU", fmt::format("{} · 命中 {:.0f}%", HumanSize(Thumbs().GpuCacheBytes()),
                                           Thumbs().GpuHitRate() * 100.0));
    app::ui::KvRow("在飞/队列", fmt::format("{}", Thumbs().InFlightAndQueued()));
    app::ui::KvRow("GPU 预算", fmt::format("{} MB", Settings().galleryGpuBudgetMB));
    app::ui::KvRow("CPU 预算", fmt::format("{} MB", Settings().galleryCpuBudgetMB));
    if (ImGui::Button("缓存重放", ImVec2(-1.f, 0.f))) {
        const std::size_t hits = Thumbs().ReplayFromCache();
        log::Info("图库：缓存重放完成，CPU 命中 {}", hits);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("清掉图库 GPU 条目后重走请求，统计 CPU 命中 vs 回源解码");
    }
    if (ImGui::Button("模拟设备丢失", ImVec2(-1.f, 0.f))) {
        Thumbs().OnDeviceLost();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("释放 GPU 纹理并清 GPU 缓存；CPU 缩略图保留，滚动时自动重传");
    }
}

void DrawStatsSection() {
    const GalleryState& st = State();
    app::ui::SectionText("统计");
    app::ui::KvRow("条目", fmt::format("{}", Model().Count()));
    app::ui::KvRow("已加载", fmt::format("{}", Thumbs().ReadyCount()));
    app::ui::KvRow("失败", fmt::format("{}", Thumbs().FailedCount()));
    app::ui::KvRow("在飞/队列", fmt::format("{}", Thumbs().InFlightAndQueued()));
    app::ui::KvRow("worker 派工", fmt::format("{}", Thumbs().WorkerStarts()));
    app::ui::KvRow("请求带", fmt::format("[{}..{})", g_visibleBegin, g_visibleEnd));
    app::ui::KvRow("缓存", Thumbs().CacheSummary());
    app::ui::KvRow("磁盘缓存", ::shine::gallery::disk_cache::Summary());
    if (!LastUploadedName().empty()) {
        app::ui::KvRow("最近上传", LastUploadedName());
    }
    if (!LastUploadError().empty()) {
        ImGui::TextColored(Danger(), "上传失败：%s", LastUploadError().c_str());
    }
    if (!LastGraphDropPath().empty()) {
        app::ui::KvRow("拖入节点图", LastGraphDropPath());
    }
    app::ui::KvRow("跳过（探针失败）", fmt::format("{}", st.skipped));
    app::ui::KvRow("扫描耗时", fmt::format("{:.0f} ms", st.seconds * 1000.0));
    if (st.scanning) {
        ImGui::TextColored(Accent(), "扫描中…");
    }
}

void DrawGalleryWindow() {
    DrawToolbar();
    ImGui::Separator();
    if (Model().Empty()) {
        if (State().scanning) {
            app::ui::EmptyState("正在扫描目录…");
        } else if (!State().error.empty()) {
            ImGui::TextColored(Danger(), "%s", State().error.c_str());
        } else {
            app::ui::EmptyState("还没有图片：点上方「打开图片文件夹…」（或按 Ctrl+O）选一个目录");
        }
        SetThumbReadyCount(0);
        return;
    }
    DrawGrid();
}

void DrawGallerySidePanel() {
    DrawSourceSection();
    DrawDirectorySection();
    DrawThumbSection();
    DrawCacheSection();
    DrawStatsSection();
    ImGui::Spacing();
    ImGui::TextDisabled("缩略图网格在第 4 区的「图库」窗口（与「图」同区切换）。");
}

void DrawGalleryViewerWindow() {
    using ::shine::gallery::viewer::Get;

    if (!ViewerOpen() && Get().id == 0) {
        // 窗口被菜单打开但尚未选图
        const ImageInfo* primary = Model().PrimaryItem();
        if (primary == nullptr) {
            app::ui::EmptyState("查看器：双击网格缩略图打开（或先选一张图）\n"
                                "滚轮：以光标为锚点缩放 · 拖拽：平移 · F：适应 · 1：1:1 · ←→：切换 · Esc：关闭");
            return;
        }
        OpenViewer(primary->id);
        ::shine::app::State().showViewer = true;
    }

    auto& vs = const_cast<::shine::gallery::viewer::State&>(Get());
    const ImageId id = ViewerImageId();
    const ImageInfo* item = id != 0 ? Model().Find(id) : nullptr;
    if (item == nullptr) {
        app::ui::EmptyState("查看器：条目已失效，请重新在网格中双击");
        return;
    }

    // —— 键盘 ——
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::Button("关闭")) {
        CloseViewer();
        ::shine::app::State().showViewer = false;
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("F 适应") || ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        ::shine::gallery::viewer::Fit();
    }
    ImGui::SameLine();
    // 1:1 需要 fitScale，在下面算完后再响应；这里先占位按钮
    static bool s_wantActual = false;
    if (ImGui::Button("1:1") || ImGui::IsKeyPressed(ImGuiKey_1, false)) {
        s_wantActual = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("←") || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        (void)::shine::gallery::viewer::Step(-1);
        Model().SelectOnly(ViewerImageId());
        s_wantActual = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("→") || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        (void)::shine::gallery::viewer::Step(1);
        Model().SelectOnly(ViewerImageId());
        s_wantActual = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("滚轮缩放 · 拖拽平移 · 方向键切换");

    // 顶部浮层信息
    const auto& st = Get();
    const std::size_t idx1 = st.total == 0 ? 0 : st.index + 1;
    ImGui::TextDisabled("%s · %u×%u · %.0f%% · %zu / %zu%s", st.name.c_str(), st.imgW, st.imgH,
                        st.zoom * 100.f, idx1, st.total, st.loading ? " · 加载中…" : "");
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 32.f || avail.y < 32.f) {
        return;
    }

    const float contentW = avail.x;
    const float contentH = avail.y;
    const ImVec2 contentMin = ImGui::GetCursorScreenPos();
    const ImVec2 contentMax(contentMin.x + contentW, contentMin.y + contentH);

    const float imgW0 = item->width > 0 ? static_cast<float>(item->width) : 1.f;
    const float imgH0 = item->height > 0 ? static_cast<float>(item->height) : 1.f;
    const float fitScale = std::max(1e-4f, std::min(contentW / imgW0, contentH / imgH0));

    if (s_wantActual) {
        ::shine::gallery::viewer::ActualPixels(fitScale);
        s_wantActual = false;
    }

    // 滚轮锚点缩放
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouseInContent = io.MousePos.x >= contentMin.x && io.MousePos.x <= contentMax.x &&
                                io.MousePos.y >= contentMin.y && io.MousePos.y <= contentMax.y;
    if (mouseInContent && io.MouseWheel != 0.f && !io.KeyCtrl) {
        ::shine::gallery::viewer::OnWheel(io.MouseWheel, io.MousePos.x - contentMin.x, io.MousePos.y - contentMin.y,
                                          contentW, contentH, fitScale);
    }

    // 拖拽平移
    ImGui::InvisibleButton("##viewer_drag", avail);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) {
        const ImVec2 d = io.MouseDelta;
        ::shine::gallery::viewer::OnDrag(d.x, d.y);
    }

    // 选纹理：优先异步原图，否则缩略图占位
    ImTextureID tex = 0;
    const auto fullH = ::shine::gallery::viewer::FullTexture(id);
    if (fullH) {
        if (gpu::GpuTexture* t = gpu::Textures().Get(fullH)) {
            tex = t->imgui_id();
        }
    } else {
        const auto th = Thumbs().Texture(id);
        if (th) {
            if (gpu::GpuTexture* t = gpu::Textures().Get(th)) {
                tex = t->imgui_id();
            }
        }
    }

    const float absScale = fitScale * st.zoom;
    const float drawW = std::max(1.f, imgW0 * absScale);
    const float drawH = std::max(1.f, imgH0 * absScale);
    ::shine::gallery::viewer::ClampPan(contentW, contentH, drawW, drawH);
    const auto& st2 = Get();
    const float cx = contentMin.x + contentW * 0.5f + st2.panX;
    const float cy = contentMin.y + contentH * 0.5f + st2.panY;
    const ImVec2 imgMin(cx - drawW * 0.5f, cy - drawH * 0.5f);
    const ImVec2 imgMax(imgMin.x + drawW, imgMin.y + drawH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(contentMin, contentMax, true);
    ui::DrawCheckerboard(contentMin, contentMax);
    if (tex != 0) {
        dl->AddImage(ImTextureRef(tex), imgMin, imgMax);
    } else {
        const char* msg = st.loading ? "原图加载中…" : "原图不可用（详见日志）";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(contentMin.x + (contentW - ts.x) * 0.5f, contentMin.y + (contentH - ts.y) * 0.5f),
                    ImGui::GetColorU32(st.loading ? ImVec4(1, 1, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1)), msg);
    }
    dl->PopClipRect();
    // 不要 SetCursorScreenPos：InvisibleButton 已占用 avail，直接结束即可
}

} // namespace shine::app::gallery
