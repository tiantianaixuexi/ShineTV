#include "ui/imgui/kit/Icon_Registry.h"

#include "ui/imgui/kit/Icon_Path.h"

#include <array>
#include <string>
#include <unordered_map>

namespace shine::kit {
namespace {

// Icon.jsx:3-48 的 46 条 d 路径，逐字照抄（24 网格 / 1.6 描边）。
// 末尾两个是本实现补的：minus（真正的减号，JSX 把 x 当减号用）与
// flow（Gallery.jsx:140 传了未定义名字被兜底吞掉的 bug）。
const std::array<std::pair<std::string_view, const char*>, 48> kIconPaths{{
    {"gauge", "M12 13.5a1.8 1.8 0 1 0 0-3.6 1.8 1.8 0 0 0 0 3.6Zm1.6-2.7 3.4-3.4M4 20h16M5.5 17a8 8 0 1 1 13 0"},
    {"book", "M5 4.5A1.5 1.5 0 0 1 6.5 3H19v15.5H6.5A1.5 1.5 0 0 0 5 20V4.5ZM5 18.5A1.5 1.5 0 0 1 6.5 17H19M8.5 7h7M8.5 10h4"},
    {"masks", "M4.5 5.5c2.3-1.2 4.7-1.2 7 0v6.2c0 2.8-1.6 4.8-3.5 4.8S4.5 14.5 4.5 11.7V5.5ZM11.5 5.5c2.3-1.2 4.7-1.2 7 0v6.2c0 2.8-1.6 4.8-3.5 4.8M7 9h2.2M14 9h2.2M7.2 12.2c.9.7 1.8.7 2.7 0M14.2 12.2c.9.7 1.8.7 2.7 0"},
    {"clapper", "M4 9h16v9a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 18V9Zm0 0-.6-2.9a1.5 1.5 0 0 1 1.2-1.8l11-1.9a1.5 1.5 0 0 1 1.7 1.1L18 6.5 4 9Zm4.6-4.4L7.3 8m6-3.6L12 8m6-3.2L16.8 8M9 13h6"},
    {"image", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11Zm0 8.2 4.2-4.2 5 5m1.6-1.6 1.7-1.7L20 14.7M15 9.5h.01"},
    {"film", "M4 5.5A1.5 1.5 0 0 1 5.5 4h13A1.5 1.5 0 0 1 20 5.5v13a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 18.5v-13ZM4 9h16M4 15h16M8 4v16m8-16v16"},
    {"play", "M8 5.8v12.4a.6.6 0 0 0 .92.5l9.6-6.2a.6.6 0 0 0 0-1L8.92 5.3a.6.6 0 0 0-.92.5Z"},
    {"stop", "M7 7h10v10H7z"},
    {"search", "M15.8 15.8 20 20m-3-9.5a6.5 6.5 0 1 1-13 0 6.5 6.5 0 0 1 13 0Z"},
    {"settings", "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm7.4-3a7.4 7.4 0 0 0-.1-1.2l2-1.5-2-3.4-2.3 1a7.4 7.4 0 0 0-2.1-1.3L14.5 3h-5l-.4 2.6a7.4 7.4 0 0 0-2 1.2l-2.4-1-2 3.5 2 1.5a7.4 7.4 0 0 0 0 2.4l-2 1.5 2 3.4 2.3-1a7.4 7.4 0 0 0 2.1 1.3l.4 2.6h5l.4-2.6a7.4 7.4 0 0 0 2-1.2l2.4 1 2-3.5-2-1.5c.1-.4.1-.8.1-1.2Z"},
    {"plus", "M12 5v14M5 12h14"},
    {"minus", "M5 12h14"},
    {"x", "M6 6l12 12M18 6 6 18"},
    {"chevron", "M9 5.5 15.5 12 9 18.5"},
    {"chevdown", "M5.5 9 12 15.5 18.5 9"},
    {"check", "M4.5 12.5 10 18 19.5 6.5"},
    {"alert", "M12 8.5V13m0 3.2h.01M10.3 4.2 2.8 17a1.6 1.6 0 0 0 1.4 2.4h15.6a1.6 1.6 0 0 0 1.4-2.4L13.7 4.2a1.6 1.6 0 0 0-2.8 0Z"},
    {"info", "M12 11v5m0-8.5h.01M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0Z"},
    {"layers", "m12 3 9 5-9 5-9-5 9-5Zm9 9-9 5-9-5m18 4.5-9 5-9-5"},
    {"text", "M5 6.5V5h14v1.5M12 5v14M9 19h6"},
    {"chip", "M8 8h8v8H8V8Zm-2.5-2.5h13v13h-13v-13ZM9 3v2.5M15 3v2.5M9 18.5V21M15 18.5V21M3 9h2.5M3 15h2.5M18.5 9H21M18.5 15H21"},
    {"wave", "M3 12h2l2-6 3 12 3-9 2 5 2-2h4"},
    {"aperture", "M12 15.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Zm8.5-3.5a8.5 8.5 0 1 1-17 0 8.5 8.5 0 0 1 17 0ZM12 12h.01M12 3.5l4 5M20.5 12l-6.4.9M16.9 19.9l-3.8-5.2M7 19.9l2.4-6M3.5 12l6.4.9M7.1 4.1l3.8 5.2"},
    {"encode", "M5 15V9m3.5 8V8.5M12 19V6m3.5 10.5v-6M19 15v-3"},
    {"grid", "M4 4h7v7H4V4Zm9 0h7v7h-7V4ZM4 13h7v7H4v-7Zm9 0h7v7h-7v-7Z"},
    {"refresh", "M19 12a7 7 0 1 1-2-4.9M19 4v4h-4"},
    {"folder", "M3.5 7A1.5 1.5 0 0 1 5 5.5h4l2 2.5h8A1.5 1.5 0 0 1 20.5 9.5v8A1.5 1.5 0 0 1 19 19H5a1.5 1.5 0 0 1-1.5-1.5V7Z"},
    {"palette", "M12 21a9 9 0 1 1 9-9c0 2-1.5 3-3 3h-2a2 2 0 0 0-1.5 3.3c.4.5.4 1.2-.2 1.5-.7.2-1.5.2-2.3.2ZM7.5 11h.01M10 7.8h.01M14.5 7.5h.01"},
    {"terminal", "m5 8 3.5 3.5L5 15m7 1.5h7M4 4.5h16A1.5 1.5 0 0 1 21.5 6v12a1.5 1.5 0 0 1-1.5 1.5H4A1.5 1.5 0 0 1 2.5 18V6A1.5 1.5 0 0 1 4 4.5Z"},
    {"list", "M9 6h11M9 12h11M9 18h11M4 6h.01M4 12h.01M4 18h.01"},
    {"sparkles", "M12 4.5 13.8 9l4.7 1.8-4.7 1.8L12 17l-1.8-4.4L5.5 10.8 10.2 9 12 4.5ZM19 15l.9 2.1L22 18l-2.1.9L19 21l-.9-2.1L16 18l2.1-.9L19 15ZM5.5 3l.7 1.8L8 5.5l-1.8.7L5.5 8l-.7-1.8L3 5.5l1.8-.7L5.5 3Z"},
    {"wand", "m15 6 3 3M9 18 20 7l-3-3L6 15l-1.5 4.5L9 18ZM6.5 9.5 8 8m3.5 8.5L13 15"},
    {"dots", "M6 12h.01M12 12h.01M18 12h.01"},
    {"download", "M12 4v10m0 0 4-4m-4 4-4-4M5 19h14"},
    {"upload", "M12 14V4m0 0L8 8m4-4 4 4M5 19h14"},
    {"eye", "M12 5.5c5 0 8.5 4.2 9.5 6.5-1 2.3-4.5 6.5-9.5 6.5S3.5 14.3 2.5 12c1-2.3 4.5-6.5 9.5-6.5Zm0 9a2.5 2.5 0 1 0 0-5 2.5 2.5 0 0 0 0 5Z"},
    {"compare", "M12 3v18M8 7 4.5 12 8 17M16 7l3.5 5L16 17"},
    {"link", "M9.5 14.5 14.5 9.5M8 11 5.8 13.2a3.8 3.8 0 0 0 5.4 5.4L13.4 16m2.6-3 2.2-2.2a3.8 3.8 0 0 0-5.4-5.4L10.6 8"},
    {"zap", "M13 3 5 13.5h5.5L11 21l8-10.5h-5.5L13 3Z"},
    {"panel", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM14.5 5v14"},
    {"panelL", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM9.5 5v14"},
    {"dock", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM4 14.5h16"},
    {"moon", "M20 13.5A8 8 0 0 1 10.5 4 8 8 0 1 0 20 13.5Z"},
    {"target", "M12 13.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3Zm0 4.5a6 6 0 1 0 0-12 6 6 0 0 0 0 12Zm0-16.5v3m0 17v-3M3 12h3m12 0h3"},
    {"clock", "M12 7v5l3 2m6-2a9 9 0 1 1-18 0 9 9 0 0 1 18 0Z"},
    {"users", "M9 11.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Zm-6 8c.6-3.4 3-5.5 6-5.5s5.4 2.1 6 5.5m1-13.9a3.5 3.5 0 0 1 0 6.8m1.7 2.4c2 .7 3.5 2.4 4 4.7"},
    {"bolt2", "M8 3h8l-1 6h4L8.5 21l1.5-8H5L8 3Z"},
    {"flow", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM7.5 9.5h4M15 14.5h-4m2.5-2.5L12 9.5m0 5 2.5 2.5"},
}};

class IconRegistry {
public:
    static const IconRegistry& Instance() {
        static const IconRegistry registry;
        return registry;
    }

    const IconGlyph* Find(std::string_view name) const {
        if (const auto it = byName_.find(std::string(name)); it != byName_.end()) {
            return &glyphs_[it->second];
        }
        return &glyphs_[fallback_];
    }

    const std::vector<std::string_view>& Names() const { return names_; }

private:
    IconRegistry() {
        glyphs_.resize(kIconPaths.size());
        names_.reserve(kIconPaths.size());
        for (std::size_t i = 0; i < kIconPaths.size(); ++i) {
            ParseIconPath(kIconPaths[i].second, glyphs_[i]);
            names_.push_back(kIconPaths[i].first);
            byName_.emplace(std::string(kIconPaths[i].first), i);
            if (kIconPaths[i].first == "info") {
                fallback_ = i;
            }
        }
    }

    std::vector<IconGlyph> glyphs_;
    std::vector<std::string_view> names_;
    std::unordered_map<std::string, std::size_t> byName_;
    std::size_t fallback_ = 0; // info
};

} // namespace

const IconGlyph* GetIcon(std::string_view name) { return IconRegistry::Instance().Find(name); }

const std::vector<std::string_view>& IconNames() { return IconRegistry::Instance().Names(); }

} // namespace shine::kit
