// shine::app::gallery —— 图库视图（G-S5 S3；自 gallery/ui 迁入 app）
//
// 布局：中央窗口「图库」= 工具条 + 条目表（`ImGuiListClipper`，列表形态）；
//       侧栏「图库」   = 来源 / 目录 / 缩略图 / 缓存 / 统计。
// 纪律：条目与选中只在 `gallery::Model()`（UI 线程）；来源可用性文案只用 G-S4 的
//       `IsSourceAvailable` / `SourceUnavailableHint`（**不再自己拼文案**）。
#include "app/gallery/GalleryView.h"
#include "app/gallery/FolderPicker.h"

#include "app/ui/Widgets.h"
#include "core/Settings.h"
#include "gallery/Gallery.h"
#include "gallery/ImageScanner.h"
#include "theme/Theme.h"
#include "util/Encoding.h"

#include <fmt/format.h>
#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace shine::app::gallery {
using ::shine::gallery::CacheBytes;
using ::shine::gallery::GalleryModel;
using ::shine::gallery::GalleryState;
using ::shine::gallery::ImageId;
using ::shine::gallery::ImageInfo;
using ::shine::gallery::IsSourceAvailable;
using ::shine::gallery::LoadedCount;
using ::shine::gallery::Model;
using ::shine::gallery::RequestScan;
using ::shine::gallery::RescanCurrent;
using ::shine::gallery::SortKeyLabel;
using ::shine::gallery::SourceKind;
using ::shine::gallery::SourceLabel;
using ::shine::gallery::SourceUnavailableHint;
using ::shine::gallery::State;
namespace {

// 「Shift 连选」的锚点（表中下标；普通单击 / Ctrl 加选都会把锚点挪到本行）
std::size_t g_anchor = 0;

// ⚠️ ImGui 的表设置（`[Table]`）**按列索引**存进 imgui.ini → 列数/列序一变就要换 id
constexpr const char* kListTableId = "##gallery_list_s5";

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

// 单击 = 只选它；Ctrl+单击 = 加减选；Shift+单击 = 从锚点连选
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

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", SourceLabel(st.source).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("·");
    ImGui::SameLine();
    if (st.scanning) {
        ImGui::TextColored(Accent(), "扫描中…");
    } else {
        ImGui::TextDisabled("%zu 张", Model().Count());
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // 排序/过滤：G-S5 只**显示**当前参数（模型已记录），实装见 G-S14 S3
    ImGui::TextDisabled("排序：%s%s", SortKeyLabel(Model().Sort()), Model().SortAscending() ? " ↑" : " ↓");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("排序与过滤在 G-S14 实装（本步模型已记录参数）");
    }
}

void DrawList() {
    const std::vector<ImageInfo>& items = Model().Items();
    constexpr int kColumns = 4;
    static const char* const kHeaders[kColumns] = {"文件名", "尺寸", "格式", "大小"};
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable(kListTableId, kColumns, kFlags, ImVec2(0.f, -26.f))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    for (int c = 0; c < kColumns; ++c) {
        ImGui::TableSetupColumn(kHeaders[c]);
    }
    ImGui::TableHeadersRow();

    GalleryModel& model = Model();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(items.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = static_cast<std::size_t>(row);
            const ImageInfo& info = items[index];
            ImGui::TableNextRow();
            ImGui::PushID(row);

            ImGui::TableSetColumnIndex(0);
            const std::string name = util::FileNameToUtf8(info.path);
            if (ImGui::Selectable(name.c_str(), model.IsSelected(info.id),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                HandleRowClick(index, info.id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", util::PathToUtf8(info.path).c_str());
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u × %u", info.width, info.height);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(info.format.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", HumanSize(info.fileSize).c_str());

            ImGui::PopID();
        }
    }
    ImGui::EndTable();

    // 底部状态条（G-S6 换网格后同一口径继续用）
    const GalleryState& st = State();
    const std::string elapsed = fmt::format("{:.0f} ms", st.seconds * 1000.0);
    ImGui::TextDisabled("共 %zu 张 · 已加载 %zu · 跳过 %zu · 扫描 %s", model.Count(), LoadedCount(), st.skipped,
                        elapsed.c_str());
    if (model.SelectionCount() > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("· 选中 %zu", model.SelectionCount());
    }
    if (st.truncated) {
        ImGui::SameLine();
        ImGui::TextColored(Busy(), "· 结果已截断（达条数上限）");
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
    // 当前来源不可用 → 把中文原因直接摆出来（口径统一走 G-S4 的 SourceUnavailableHint）
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
        SaveSettings();
    }
    ImGui::TextDisabled("本步先记尺寸档；网格与异步缩略图在 G-S6/S7 落地");
}

void DrawCacheSection() {
    app::ui::SectionText("缓存");
    app::ui::KvRow("纹理缓存", HumanSize(CacheBytes()));
    app::ui::KvRow("GPU 预算", fmt::format("{} MB", Settings().galleryGpuBudgetMB));
    app::ui::KvRow("CPU 预算", fmt::format("{} MB", Settings().galleryCpuBudgetMB));
    ImGui::TextDisabled("CPU 缩略图缓存在 G-S8 落地");
}

void DrawStatsSection() {
    const GalleryState& st = State();
    app::ui::SectionText("统计");
    app::ui::KvRow("条目", fmt::format("{}", Model().Count()));
    app::ui::KvRow("已加载", fmt::format("{}", LoadedCount()));
    app::ui::KvRow("跳过（探针失败）", fmt::format("{}", st.skipped));
    app::ui::KvRow("扫描耗时", fmt::format("{:.0f} ms", st.seconds * 1000.0));
    if (st.scanning) {
        ImGui::TextColored(Accent(), "扫描中…");
    }
}

} // namespace

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
        return;
    }
    DrawList();
}

void DrawGallerySidePanel() {
    DrawSourceSection();
    DrawDirectorySection();
    DrawThumbSection();
    DrawCacheSection();
    DrawStatsSection();
    ImGui::Spacing();
    ImGui::TextDisabled("列表在第 4 区的「图库」窗口（与「图」同区切换）。");
}

void DrawGalleryViewerWindow() {
    const ImageInfo* item = Model().PrimaryItem();
    if (item == nullptr) {
        app::ui::EmptyState("查看器：先在「图库」列表里选一张图");
        return;
    }
    ImGui::TextWrapped("%s", util::PathToUtf8(item->path).c_str());
    app::ui::KvRow("尺寸", fmt::format("{} × {}", item->width, item->height));
    app::ui::KvRow("格式", item->format);
    app::ui::KvRow("大小", HumanSize(item->fileSize));
    ImGui::Separator();
    ImGui::TextDisabled("完整的查看器（滚轮缩放 / 拖拽平移 / 方向键切换 / Esc）在 G-S10 落地。");
}

} // namespace shine::app::gallery
