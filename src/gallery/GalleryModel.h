#pragma once
// shine::gallery —— 图库条目模型（G-S5 S1 / G-S14 S3 排序过滤）
//
// 只做"一份条目表 + 选中集 + 排序/过滤 + 视图"的记账：**不碰磁盘、不碰 GPU、不碰 ImGui**。
#include "gallery/GalleryTypes.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace shine::gallery {

enum class SortKey { Name = 0, Size, Modified, Width, Height, Format };

[[nodiscard]] const char* SortKeyLabel(SortKey key) noexcept;

struct GalleryFilter {
    std::string nameContains;          // 文件名子串（比较时大小写不敏感）
    std::vector<std::string> formats;  // 空 = 不限；否则按 format 精确匹配（小写）

    [[nodiscard]] bool active() const noexcept { return !nameContains.empty() || !formats.empty(); }
};

class GalleryModel {
public:
    // —— 条目（扫描结果，全量）——
    void SetItems(std::vector<ImageInfo> items);
    void Clear() noexcept;
    [[nodiscard]] const std::vector<ImageInfo>& Items() const noexcept { return items_; }
    // **显示用**：过滤 + 排序后的视图（G-S14）；空过滤时顺序 = 排序后的全量
    [[nodiscard]] const std::vector<ImageInfo>& View() const noexcept { return view_; }
    [[nodiscard]] bool Empty() const noexcept { return view_.empty(); }
    [[nodiscard]] std::size_t Count() const noexcept { return view_.size(); }
    [[nodiscard]] const ImageInfo* At(std::size_t index) const noexcept; // 相对 View
    [[nodiscard]] const ImageInfo* Find(ImageId id) const noexcept;      // 全量 items_
    [[nodiscard]] std::size_t IndexOf(ImageId id) const noexcept;        // 全量 items_ 下标

    // —— 选中 ——
    void SelectOnly(ImageId id);
    void ToggleSelect(ImageId id);
    void SelectRange(ImageId from, ImageId to);
    void ClearSelection() noexcept;
    [[nodiscard]] bool IsSelected(ImageId id) const noexcept;
    [[nodiscard]] std::size_t SelectionCount() const noexcept { return selection_.size(); }
    [[nodiscard]] std::span<const ImageId> Selection() const noexcept { return selection_; }
    [[nodiscard]] ImageId Primary() const noexcept;
    [[nodiscard]] const ImageInfo* PrimaryItem() const noexcept;

    // —— 排序 / 过滤（G-S14 S3：真正重排）——
    void SortBy(SortKey key, bool ascending = true);
    void SetFilter(GalleryFilter filter);
    [[nodiscard]] SortKey Sort() const noexcept { return sortKey_; }
    [[nodiscard]] bool SortAscending() const noexcept { return sortAscending_; }
    [[nodiscard]] const GalleryFilter& Filter() const noexcept { return filter_; }

private:
    void RebuildView();
    [[nodiscard]] bool PassFilter(const ImageInfo& info) const;

    std::vector<ImageInfo> items_;
    std::vector<ImageInfo> view_;
    std::vector<ImageId> selection_;
    SortKey sortKey_ = SortKey::Name;
    bool sortAscending_ = true;
    GalleryFilter filter_;
};

} // namespace shine::gallery
