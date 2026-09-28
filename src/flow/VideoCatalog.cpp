#include "flow/VideoCatalog.h"

#include <algorithm>

namespace shine::flow {
namespace {

bool LooksLikeVideo(const std::string& name) {
    return name.find("Video") != std::string::npos || name.find("H3") != std::string::npos ||
           name.find("Animate") != std::string::npos || name.find("Upscale") != std::string::npos ||
           name.find("Encode") != std::string::npos || name.find("RIFE") != std::string::npos;
}

} // namespace

std::vector<VideoCatalogEntry> BuildVideoCatalog(const std::vector<comfy::NodeTypeDef>& definitions) {
    std::vector<VideoCatalogEntry> out;
    for (const auto& definition : definitions) {
        if (!IsVideoNodeType(definition.className)) continue;
        out.push_back({definition.className,
                       definition.displayName.empty() ? definition.className : definition.displayName,
                       definition.category.empty() ? "视频" : definition.category});
    }
    std::ranges::sort(out, [](const auto& left, const auto& right) {
        return left.class_name < right.class_name;
    });
    return out;
}

bool IsVideoNodeType(std::string_view class_name) { return LooksLikeVideo(std::string{class_name}); }

} // namespace shine::flow
