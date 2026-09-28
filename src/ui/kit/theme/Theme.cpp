#include "ui/kit/theme/Theme.h"

#include "core/Log.h"
#include "ui/kit/theme/QssBuilder.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <meta>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <yyjson.h>

namespace shine::theme {
namespace {

// 反射字段表（编译期；同 util/Reflect.h 的范式，规则见 docs/30-engineering/coding-rules.md）
template <class T>
consteval auto FieldInfos() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
}

struct ThemeMeta {
    ThemeId id;
    std::string_view ascii;   // 稳定 id（环境变量 / 文件名友好，免 ANSI 编码雷）
    std::string_view file;    // Themes/<file>.json
    std::string_view display; // 显示名
};

// 顺序 == ThemeId 枚举值（深空=0 / 薄暮=1 / 纸墨=2 / 水墨=3 / 极夜=4）。
// ⚠️ 增删主题必须同步改 Token.h 的 enum、这里的数组长度与 ThemeService.cpp 的
//    QSS 缓存数组长度——三者都按下标寻址，长度对不上就是越界。
// 色值逐条抄自 webui/src/styles/tokens.css 的同名 [data-theme] 块；
// 半透明的 line.* / fill.* / shadow.* 按「预合成到 bg.surface」的既有约定写成不透明值，
// shadow.scrim / shadow.1 / shadow.2 / shadow.accent 保留 8 位 RRGGBBAA。
constexpr std::array<ThemeMeta, kAllThemes.size()> kThemes = {{
    {ThemeId::DeepSpace, "deepspace", "深空", "深空（默认）"},
    {ThemeId::Dusk, "dusk", "薄暮", "薄暮"},
    {ThemeId::PaperInk, "paperink", "纸墨", "纸墨"},
    {ThemeId::InkWash, "inkwash", "水墨", "水墨"},
    {ThemeId::PolarNight, "polarnight", "极夜", "极夜"},
}};

std::array<ColorToken, kAllThemes.size()> g_themes{};
std::array<bool, kAllThemes.size()> g_loaded{};
ThemeId g_current = ThemeId::DeepSpace;

// 自定义主题表（P02-S8）：样式编辑器另存；当前可激活其一
std::vector<std::pair<std::string, ColorToken>> g_customs;
long g_customActive = -1; // 下标；-1 = 当前用内置

[[nodiscard]] std::size_t IndexOf(ThemeId id) { return static_cast<std::size_t>(id); }

[[nodiscard]] const ThemeMeta& MetaOf(ThemeId id) { return kThemes[IndexOf(id)]; }

[[nodiscard]] std::string ToHex(std::uint32_t rgba) {
    // 存储布局 = RRGGBBAA（总纲 §2）；不透明时按 #RRGGBB 书写（与主题 JSON 一致）
    if ((rgba & 0xFF) == 0xFF) {
        return fmt::format("#{:06x}", rgba >> 8);
    }
    return fmt::format("#{:08x}", rgba);
}

[[nodiscard]] bool FromHex(std::string_view s, std::uint32_t& out) {
    if (!s.empty() && s.front() == '#') {
        s.remove_prefix(1);
    }
    if (s.size() != 6 && s.size() != 8) {
        return false;
    }
    std::uint32_t v = 0;
    for (const char ch : s) {
        v <<= 4;
        if (ch >= '0' && ch <= '9') {
            v |= static_cast<std::uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            v |= static_cast<std::uint32_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            v |= static_cast<std::uint32_t>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    if (s.size() == 6) {
        v = (v << 8) | 0xFFu; // #RRGGBB → RRGGBBAA 不透明（⚠️ 勿把 alpha 塞高位）
    }
    out = v;
    return true;
}

} // namespace

std::string_view ThemeFileName(ThemeId id) { return MetaOf(id).file; }
std::string_view ThemeDisplayName(ThemeId id) { return MetaOf(id).display; }

bool ThemeIdFromName(std::string_view name, ThemeId& out) {
    for (const ThemeMeta& m : kThemes) {
        if (m.file == name || m.display == name || m.ascii == name) {
            out = m.id;
            return true;
        }
    }
    return false;
}

bool LoadThemesFrom(const std::filesystem::path& dir) {
    bool all = true;
    for (const ThemeMeta& m : kThemes) {
        // 中文文件名必须走 PathFromUtf8（util/Encoding.h：path{std::string} 按 ANSI 解释会错）
        const std::filesystem::path path = dir / util::PathFromUtf8(std::string{m.file} + ".json");
        const std::optional<std::string> bytes = util::ReadFileBytes(path);
        if (!bytes.has_value()) {
            log::Error("主题文件缺失：{}", util::PathToUtf8(path));
            all = false;
            continue;
        }
        ColorToken c{};
        if (!ColorTokenFromJson(*bytes, c)) {
            log::Error("主题文件解析失败（需含全部 {} 个颜色 token）：{}", kColorTokenNames.size(),
                       util::PathToUtf8(path));
            all = false;
            continue;
        }
        g_themes[IndexOf(m.id)] = c;
        g_loaded[IndexOf(m.id)] = true;
    }
    return all;
}

const ColorToken& ThemeColorsOf(ThemeId id) { return g_themes[IndexOf(id)]; }
ThemeId CurrentThemeId() noexcept { return g_current; }
void SetCurrentTheme(ThemeId id) noexcept { g_current = id; }
const ColorToken& Current() {
    if (g_customActive >= 0) {
        return g_customs[static_cast<std::size_t>(g_customActive)].second;
    }
    return g_themes[IndexOf(g_current)];
}

std::array<std::uint32_t, kColorTokenCount> TokenValues(const ColorToken& c) {
    std::array<std::uint32_t, kColorTokenCount> out{};
    std::size_t i = 0;
    template for (constexpr auto m : FieldInfos<ColorToken>()) {
        out[i++] = c.[: m :];
    }
    return out;
}

std::string ColorTokenToJson(const ColorToken& c) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root); // yyjson 硬规则：先 set_root 再序列化
    std::size_t i = 0;
    template for (constexpr auto m : FieldInfos<ColorToken>()) {
        const std::string_view key = kColorTokenNames[i++];
        const std::string hex = ToHex(c.[: m :]);
        (void)yyjson_mut_obj_add(root, yyjson_mut_strncpy(doc, key.data(), key.size()),
                                yyjson_mut_strncpy(doc, hex.c_str(), hex.size()));
    }
    char* text = yyjson_mut_write(doc, 0, nullptr);
    yyjson_mut_doc_free(doc);
    if (text == nullptr) {
        return {};
    }
    std::string out{text};
    std::free(text);
    return out;
}

std::size_t ColorTokenFromJsonPartial(std::string_view json, ColorToken& out) {
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        return 0;
    }
    yyjson_val* root = yyjson_doc_get_root(doc); // yyjson 硬规则：一律走 doc_get_root
    if (yyjson_val* colors = yyjson_obj_get(root, "colors"); yyjson_is_obj(colors)) {
        root = colors; // 容许主题文件形状 {"name":…,"colors":{…}}
    }
    std::size_t hit = 0;
    if (yyjson_is_obj(root)) {
        std::size_t i = 0;
        // 只覆盖 JSON 给到的键；out 的其余字段保留调用方预置的值（自定义主题迁移）
        template for (constexpr auto m : FieldInfos<ColorToken>()) {
            const std::string_view key = kColorTokenNames[i++];
            if (yyjson_val* v = yyjson_obj_getn(root, key.data(), key.size());
                v != nullptr && yyjson_is_str(v)) {
                std::uint32_t rgba = 0;
                const std::string_view s{yyjson_get_str(v), yyjson_get_len(v)};
                if (FromHex(s, rgba)) {
                    out.[: m :] = rgba;
                    ++hit;
                }
            }
        }
    }
    yyjson_doc_free(doc);
    return hit;
}

bool ColorTokenFromJson(std::string_view json, ColorToken& out) {
    ColorToken parsed{};
    if (ColorTokenFromJsonPartial(json, parsed) != kColorTokenCount) {
        return false; // 宽容读只宽容「多键」，缺键不算完整主题
    }
    out = parsed;
    return true;
}

bool SelfTestRoundTrip(std::string* report) {
    bool ok = true;
    std::string text = fmt::format("当前主题: {}（持久化回读验证用）\n", ThemeDisplayName(CurrentThemeId()));
    for (const ThemeId id : kAllThemes) {
        if (!g_loaded[IndexOf(id)]) {
            ok = false;
            text += fmt::format("FAIL: 主题未载入 {}\n", ThemeFileName(id));
            continue;
        }
        const ColorToken& c = ThemeColorsOf(id);
        ColorToken back{};
        const std::string json = ColorTokenToJson(c);
        const bool rt = ColorTokenFromJson(json, back) && back == c;
        ok = ok && rt;
        std::string qssDetail;
        const bool qssOk = QssBuilder::SelfCheck(c, &qssDetail);
        ok = ok && qssOk;
        text += fmt::format("{}: tokens={} round-trip={} qss={}\n", ThemeFileName(id),
                            kColorTokenNames.size(), rt ? "OK" : "FAIL", qssOk ? "OK" : "FAIL");
        text += "  " + qssDetail + "\n";
    }
    if (report != nullptr) {
        *report = text;
    }
    return ok;
}

// ============================ 自定义主题（P02-S8）============================

std::size_t CustomThemeCount() { return g_customs.size(); }

std::string CustomThemeName(std::size_t i) {
    return i < g_customs.size() ? g_customs[i].first : std::string{};
}

const ColorToken& CustomThemeColors(std::size_t i) { return g_customs[i].second; }

bool LoadCustomThemes(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) {
        return true; // 还没存过自定义主题
    }
    bool all = true;
    // 每个 <名字>.json 一份自定义主题（宽容读：缺键的直接跳过）
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string name = util::PathToUtf8(entry.path().stem());
            const std::optional<std::string> bytes = util::ReadFileBytes(entry.path());
            if (!bytes.has_value()) {
                all = false;
                continue;
            }
            // 缺键迁移（方案 01 加了 status.pending / fill.* / line.focus / shadow.scrim，
            // 6 个键）：老自定义主题只有 22 个键，宽容读会整体判不完整而**整个主题加载失败**。
            // 这里以当前内置主题为底，只覆盖文件里给到的键 —— 用户原有的 22 个值全部保留。
            const ThemeId baseId = g_loaded[IndexOf(g_current)] ? g_current : ThemeId::DeepSpace;
            ColorToken c = ThemeColorsOf(baseId);
            const std::size_t hit = ColorTokenFromJsonPartial(*bytes, c);
            if (hit == 0) {
                log::Error("自定义主题解析失败（无有效颜色 token）：{}", util::PathToUtf8(entry.path()));
                all = false;
                continue;
            }
            if (hit != kColorTokenCount) {
                log::Warn("自定义主题 {} 缺 {} 个 token，已用内置主题 {} 补全（存回后即为完整主题）",
                          util::PathToUtf8(entry.path()), kColorTokenCount - hit,
                          ThemeDisplayName(baseId));
            }
            bool dup = false;
            for (auto& [n, token] : g_customs) {
                if (n == name) {
                    token = c; // 重扫 = 刷新（幂等）
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                g_customs.emplace_back(name, c);
            }
        }
    } catch (...) {
        all = false;
    }
    return all;
}

bool SaveCustomTheme(const std::filesystem::path& dir, const std::string& name,
                     const ColorToken& c) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / util::PathFromUtf8(name + ".json");
    const std::string json = ColorTokenToJson(c);
    if (json.empty() || !util::WriteFileBytes(path, json)) {
        log::Error("自定义主题写入失败：{}", util::PathToUtf8(path));
        return false;
    }
    for (auto& [n, token] : g_customs) {
        if (n == name) {
            token = c;
            return true;
        }
    }
    g_customs.emplace_back(name, c);
    return true;
}

bool ActivateCustom(const std::string& name) {
    for (std::size_t i = 0; i < g_customs.size(); ++i) {
        if (g_customs[i].first == name) {
            g_customActive = static_cast<long>(i);
            return true;
        }
    }
    return false;
}

bool CurrentIsCustom() noexcept { return g_customActive >= 0; }

std::string CurrentCustomName() {
    return g_customActive >= 0 ? g_customs[static_cast<std::size_t>(g_customActive)].first
                               : std::string{};
}

void RevertToBuiltin() { g_customActive = -1; }

} // namespace shine::theme
