// 主题 JSON 载入层：读 5 份 themes/*.json → 填 g_themes → 算派生色。
//
// 单独成文件的理由：这一族做 IO（读文件）+ 失败上报（log::Error），而
// Theme.cpp 里其余部分全是纯计算。混在一起的后果是「改颜色公式」和
// 「改文件读法」互相影响 review，而且 ParseThemeFile 的失败路径
// （打不开 / 非法 JSON / token 数不足 31）需要连着读三处才能看全。

#include "ui/imgui/theme/Theme_Internal.h"

#include "core/Log.h"

#include <yyjson.h>

#include <fstream>
#include <iterator>
#include <string>

namespace shine::theme {
namespace detail {

bool ParseThemeFile(const std::filesystem::path& path, ThemeId id) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        shine::log::Error("theme: cannot open {}", path.string());
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        shine::log::Error("theme: {} is not valid JSON", path.string());
        return false;
    }

    ThemeRecord record;
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* colors = yyjson_obj_get(root, "colors");
    int hits = 0;
    if (yyjson_is_obj(colors)) {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(colors, &iter);
        yyjson_val* key = nullptr;
        yyjson_val* value = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            value = yyjson_obj_iter_get_val(key);
            const char* raw = yyjson_get_str(value);
            const char* keyText = yyjson_get_str(key);
            if (raw == nullptr || keyText == nullptr) {
                continue;
            }
            std::uint32_t rgba = 0;
            if (!ParseHexColor(raw, rgba)) {
                continue;
            }
            const std::string_view name(keyText, yyjson_get_len(key));
            if (AssignByName(name, rgba, record.colors)) {
                ++hits;
            }
        }
    }
    yyjson_doc_free(doc);

    if (hits != static_cast<int>(kColorTokenCount)) {
        shine::log::Error("theme: {} has {}/{} tokens", path.string(), hits, kColorTokenCount);
        return false;
    }
    record.loaded = true;
    g_themes[IndexOf(id)] = record;
    return true;
}

} // namespace detail

bool LoadThemesFrom(const std::filesystem::path& dir) {
    bool all = true;
    for (ThemeId id : kAllThemes) {
        const std::string_view display = ThemeDisplayName(id);
        const std::filesystem::path file =
            dir / (std::string(display) + ".json"); // theme-ok
        // 逐主题算一遍派生色：kGlassTriples / kGradEnds 都按 ThemeId 寻址。
        if (detail::ParseThemeFile(file, id)) {
            detail::ComputeDerived(detail::g_themes[detail::IndexOf(id)], id);
        } else {
            all = false;
        }
    }
    detail::g_loaded = all;
    return all;
}

} // namespace shine::theme
