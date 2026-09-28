#pragma once
// P08-S1：视频节点目录（定义仍来自 /object_info）。
#include "comfy/ComfyNodeDef.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::flow {

struct VideoCatalogEntry {
    std::string class_name;
    std::string display_name;
    std::string category;
};

[[nodiscard]] std::vector<VideoCatalogEntry> BuildVideoCatalog(
    const std::vector<comfy::NodeTypeDef>& definitions);
[[nodiscard]] bool IsVideoNodeType(std::string_view class_name);

} // namespace shine::flow
