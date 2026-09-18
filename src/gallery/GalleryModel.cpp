#include "gallery/GalleryModel.h"

#include <algorithm>
#include <utility>

namespace shine::gallery {

const char* SortKeyLabel(SortKey key) noexcept {
    switch (key) {
    case SortKey::Name:
        return "文件名";
    case SortKey::Size:
        return "文件大小";
    case SortKey::Modified:
        return "修改时间";
    case SortKey::Width:
        return "宽度";
    case SortKey::Height:
        return "高度";
    }
    return "文件名";
}

void GalleryModel::SetItems(std::vector<ImageInfo> items) {
    items_ = std::move(items);
    selection_.clear(); // 条目换了 → 旧 id 不再有意义
}

void GalleryModel::Clear() noexcept {
    items_.clear();
    selection_.clear();
}

const ImageInfo* GalleryModel::At(std::size_t index) const noexcept {
    return index < items_.size() ? &items_[index] : nullptr;
}

const ImageInfo* GalleryModel::Find(ImageId id) const noexcept {
    const std::size_t index = IndexOf(id);
    return index < items_.size() ? &items_[index] : nullptr;
}

std::size_t GalleryModel::IndexOf(ImageId id) const noexcept {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == id) {
            return i;
        }
    }
    return items_.size();
}

void GalleryModel::SelectOnly(ImageId id) {
    if (Find(id) == nullptr) {
        return;
    }
    selection_.clear();
    selection_.push_back(id);
}

void GalleryModel::ToggleSelect(ImageId id) {
    if (Find(id) == nullptr) {
        return;
    }
    const auto it = std::ranges::find(selection_, id);
    if (it == selection_.end()) {
        selection_.push_back(id);
    } else {
        selection_.erase(it);
    }
}

void GalleryModel::SelectRange(ImageId from, ImageId to) {
    const std::size_t a = IndexOf(from);
    const std::size_t b = IndexOf(to);
    if (a >= items_.size() || b >= items_.size()) {
        return; // 两端都必须在表里（锚点条目可能已被新扫描换掉）
    }
    const std::size_t first = std::min(a, b);
    const std::size_t last = std::max(a, b);
    selection_.clear();
    for (std::size_t i = first; i <= last; ++i) {
        selection_.push_back(items_[i].id);
    }
}

void GalleryModel::ClearSelection() noexcept { selection_.clear(); }

bool GalleryModel::IsSelected(ImageId id) const noexcept {
    return std::ranges::find(selection_, id) != selection_.end();
}

ImageId GalleryModel::Primary() const noexcept { return selection_.empty() ? 0 : selection_.back(); }

const ImageInfo* GalleryModel::PrimaryItem() const noexcept { return Find(Primary()); }

void GalleryModel::SortBy(SortKey key, bool ascending) noexcept {
    // G-S5 只记录（UI 能显示当前档位）；G-S14 S3 在这里落 `std::sort` + `Items()` 的顺序就是显示顺序。
    sortKey_ = key;
    sortAscending_ = ascending;
}

void GalleryModel::SetFilter(GalleryFilter filter) { filter_ = std::move(filter); }

} // namespace shine::gallery
