#pragma once
// shine::gallery —— 图库条目模型（G-S5 S1）
//
// 只做"一份条目表 + 选中集 + 排序/过滤参数"的记账：**不碰磁盘、不碰 GPU、不碰 ImGui**，
// 所以能离线单测。条目由 `ImageScanner::ScanAsync` 的回调（UI 线程）灌进来。
//
// 线程纪律：本类**只在 UI 线程**读写（与 `gallery::Tick()`、图库视图绘制同线程）。
#include "gallery/GalleryTypes.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace shine::gallery {

// 排序键（G-S5 **只记录参数**；真正重排留给 G-S14 S3，届时只需在这里加一次 `std::sort`）
enum class SortKey { Name = 0, Size, Modified, Width, Height };

[[nodiscard]] const char* SortKeyLabel(SortKey key) noexcept;

// 过滤条件（G-S5 **只记录**，实装见 G-S14 S3）
struct GalleryFilter {
    std::string nameContains; // 文件名子串（UTF-8；比较时大小写不敏感）
    std::string format;       // 空 = 不限；否则按 `ImageInfo::format` 精确匹配（"png" / "jpeg" …）

    [[nodiscard]] bool active() const noexcept { return !nameContains.empty() || !format.empty(); }
};

class GalleryModel {
public:
    // —— 条目 ——
    void SetItems(std::vector<ImageInfo> items); // 整体替换，并**清空选中**（旧 id 已失效）
    void Clear() noexcept;
    [[nodiscard]] const std::vector<ImageInfo>& Items() const noexcept { return items_; }
    [[nodiscard]] bool Empty() const noexcept { return items_.empty(); }
    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] const ImageInfo* At(std::size_t index) const noexcept; // 越界 = nullptr
    [[nodiscard]] const ImageInfo* Find(ImageId id) const noexcept;      // 找不到 = nullptr
    [[nodiscard]] std::size_t IndexOf(ImageId id) const noexcept;        // 找不到 = Count()

    // —— 选中（顺序 = 点选顺序，最后一个 = "主选中"）——
    void SelectOnly(ImageId id);
    void ToggleSelect(ImageId id);
    void SelectRange(ImageId from, ImageId to); // 含两端（按两 id 在表中的下标区间）
    void ClearSelection() noexcept;
    [[nodiscard]] bool IsSelected(ImageId id) const noexcept;
    [[nodiscard]] std::size_t SelectionCount() const noexcept { return selection_.size(); }
    [[nodiscard]] std::span<const ImageId> Selection() const noexcept { return selection_; }
    [[nodiscard]] ImageId Primary() const noexcept;              // 空选中 = 0
    [[nodiscard]] const ImageInfo* PrimaryItem() const noexcept; // 空选中 = nullptr

    // —— 排序 / 过滤（G-S5 占位：只记录参数，不重排条目）——
    void SortBy(SortKey key, bool ascending = true) noexcept;
    void SetFilter(GalleryFilter filter);
    [[nodiscard]] SortKey Sort() const noexcept { return sortKey_; }
    [[nodiscard]] bool SortAscending() const noexcept { return sortAscending_; }
    [[nodiscard]] const GalleryFilter& Filter() const noexcept { return filter_; }

private:
    std::vector<ImageInfo> items_;
    std::vector<ImageId> selection_;
    SortKey sortKey_ = SortKey::Name;
    bool sortAscending_ = true;
    GalleryFilter filter_;
};

} // namespace shine::gallery
