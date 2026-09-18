#include "gallery/GalleryModel.h"
#include "util/Encoding.h"
#include "util/Strings.h"

#include <algorithm>
#include <utility>

namespace shine::gallery {

const char* SortKeyLabel(SortKey key) noexcept {
    switch (key) {
    case SortKey::Name: return "文件名";
    case SortKey::Size: return "文件大小";
    case SortKey::Modified: return "修改时间";
    case SortKey::Width: return "宽度";
    case SortKey::Height: return "高度";
    case SortKey::Format: return "格式";
    }
    return "文件名";
}

void GalleryModel::SetItems(std::vector<ImageInfo> items) {
    items_ = std::move(items);
    selection_.clear();
    RebuildView();
}

void GalleryModel::Clear() noexcept {
    items_.clear();
    view_.clear();
    selection_.clear();
}

const ImageInfo* GalleryModel::At(std::size_t index) const noexcept {
    return index < view_.size() ? &view_[index] : nullptr;
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
    // 在 View 下标上连选（显示顺序）
    std::size_t a = view_.size();
    std::size_t b = view_.size();
    for (std::size_t i = 0; i < view_.size(); ++i) {
        if (view_[i].id == from) {
            a = i;
        }
        if (view_[i].id == to) {
            b = i;
        }
    }
    if (a >= view_.size() || b >= view_.size()) {
        return;
    }
    if (a > b) {
        std::swap(a, b);
    }
    selection_.clear();
    for (std::size_t i = a; i <= b; ++i) {
        selection_.push_back(view_[i].id);
    }
}

void GalleryModel::ClearSelection() noexcept { selection_.clear(); }

bool GalleryModel::IsSelected(ImageId id) const noexcept {
    return std::ranges::find(selection_, id) != selection_.end();
}

ImageId GalleryModel::Primary() const noexcept { return selection_.empty() ? 0 : selection_.back(); }

const ImageInfo* GalleryModel::PrimaryItem() const noexcept { return Find(Primary()); }

void GalleryModel::SortBy(SortKey key, bool ascending) {
    sortKey_ = key;
    sortAscending_ = ascending;
    RebuildView();
}

void GalleryModel::SetFilter(GalleryFilter filter) {
    filter_ = std::move(filter);
    RebuildView();
}

bool GalleryModel::PassFilter(const ImageInfo& info) const {
    if (!filter_.nameContains.empty()) {
        const std::string name = util::ToLower(util::FileNameToUtf8(info.path));
        if (name.find(util::ToLower(filter_.nameContains)) == std::string::npos) {
            return false;
        }
    }
    if (!filter_.formats.empty()) {
        const std::string fmt = util::ToLower(info.format);
        const bool hit = std::ranges::any_of(filter_.formats, [&](const std::string& f) {
            return util::ToLower(f) == fmt;
        });
        if (!hit) {
            return false;
        }
    }
    return true;
}

void GalleryModel::RebuildView() {
    view_.clear();
    view_.reserve(items_.size());
    for (const ImageInfo& info : items_) {
        if (PassFilter(info)) {
            view_.push_back(info);
        }
    }
    if (view_.size() < 2) {
        return;
    }
    const bool asc = sortAscending_;
    std::ranges::sort(view_, [this, asc](const ImageInfo& a, const ImageInfo& b) {
        int c = 0;
        switch (sortKey_) {
        case SortKey::Name: {
            const std::string na = util::ToLower(util::FileNameToUtf8(a.path));
            const std::string nb = util::ToLower(util::FileNameToUtf8(b.path));
            c = na < nb ? -1 : (na > nb ? 1 : 0);
            break;
        }
        case SortKey::Size:
            c = a.fileSize < b.fileSize ? -1 : (a.fileSize > b.fileSize ? 1 : 0);
            break;
        case SortKey::Modified:
            c = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
            break;
        case SortKey::Width:
            c = a.width < b.width ? -1 : (a.width > b.width ? 1 : 0);
            break;
        case SortKey::Height:
            c = a.height < b.height ? -1 : (a.height > b.height ? 1 : 0);
            break;
        case SortKey::Format: {
            const std::string fa = util::ToLower(a.format);
            const std::string fb = util::ToLower(b.format);
            c = fa < fb ? -1 : (fa > fb ? 1 : 0);
            break;
        }
        }
        return asc ? (c < 0) : (c > 0);
    });
}

} // namespace shine::gallery
