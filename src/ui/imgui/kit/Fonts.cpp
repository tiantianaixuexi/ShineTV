#include "ui/imgui/kit/Fonts.h"

#include "core/Log.h"
#include "ui/imgui/theme/Tokens.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace shine::kit {
namespace {

// ---- 设计稿实测的全部字号（design-spec §1.1）----
// 只建常用档：每个字号都要乘以 2500 个 CJK 字形，全量建会顶到显存上限。
// 未建档由 FontAt() 向上取最近一档。
const std::vector<float> kUiSizes = {10.5f, 11.0f, 11.5f, 12.0f, 12.5f, 13.0f,
                                      13.5f, 14.0f, 15.0f, 18.0f, 20.0f, 24.0f, 28.0f};
const std::vector<float> kMonoSizes = {10.5f, 11.5f, 12.5f, 13.0f, 14.0f, 18.0f};

struct FontPaths {
    std::string ui = "C:/Windows/Fonts/msyh.ttc";
    std::string uiFallback = "C:/Windows/Fonts/simhei.ttf";
    std::string serif = "C:/Windows/Fonts/simsun.ttc";
    std::string mono = "C:/Windows/Fonts/consola.ttf";
    std::string monoAlt = "C:/Windows/Fonts/CascadiaCode.ttf";
};

std::string FirstExisting(const std::vector<std::string>& candidates) {
    for (const std::string& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

std::unordered_map<float, ImFont*> g_fonts;
std::unordered_map<float, ImFont*> g_mono;
std::size_t g_atlasBytes = 0;
int g_textureCount = 0;
bool g_atlasLogged = false;
std::string g_familyName;

// ---------------------------------------------------------------- TTC 解析
struct Reader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] std::uint16_t U16(std::size_t offset) const {
        if (offset + 2 > size) {
            return 0;
        }
        return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
    }
    [[nodiscard]] std::uint32_t U32(std::size_t offset) const {
        if (offset + 4 > size) {
            return 0;
        }
        return (static_cast<std::uint32_t>(data[offset]) << 24) |
               (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
               (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
               static_cast<std::uint32_t>(data[offset + 3]);
    }
    [[nodiscard]] bool Tag(std::size_t offset, const char* tag) const {
        return offset + 4 <= size && std::memcmp(data + offset, tag, 4) == 0;
    }
};

// UTF-16BE → UTF-8（只处理 BMP，name 表足够）
std::string Utf16BeToUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        const auto unit = static_cast<std::uint32_t>((text[i] << 8) | text[i + 1]);
        if (unit < 0x80) {
            out.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            out.push_back(static_cast<char>(0xC0u | (unit >> 6)));
            out.push_back(static_cast<char>(0x80u | (unit & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xE0u | (unit >> 12)));
            out.push_back(static_cast<char>(0x80u | ((unit >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (unit & 0x3Fu)));
        }
    }
    return out;
}

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool MatchesFamily(const std::string& family, const std::string& wanted) {
    if (family.empty()) {
        return false;
    }
    const std::string a = ToLowerAscii(family);
    const std::string b = ToLowerAscii(wanted);
    return a == b || a.rfind(b, 0) == 0; // "Microsoft YaHei UI" 也接受 "Microsoft YaHei"
}

// 取某个 sfnt 偏移处的族名（name 表 nameID=1）。
std::string FamilyNameOf(const Reader& reader, std::size_t fontOffset) {
    const std::size_t tableCount = reader.U16(fontOffset + 4);
    std::size_t nameOffset = 0;
    std::size_t nameLength = 0;
    bool found = false;
    for (std::size_t i = 0; i < tableCount; ++i) {
        const std::size_t record = fontOffset + 12 + i * 16;
        if (reader.Tag(record, "name")) {
            nameOffset = reader.U32(record + 8);
            nameLength = reader.U32(record + 12);
            found = true;
            break;
        }
    }
    if (!found || nameOffset + nameLength > reader.size) {
        return {};
    }

    const std::size_t count = reader.U16(nameOffset + 2);
    const std::size_t storage = nameOffset + reader.U16(nameOffset + 4);
    std::string best;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = nameOffset + 6 + i * 12;
        const std::size_t platformId = reader.U16(record);
        const std::size_t nameId = reader.U16(record + 6);
        const std::size_t length = reader.U16(record + 8);
        const std::size_t offset = reader.U16(record + 10);
        if (nameId != 1) {
            continue;
        }
        const std::size_t start = storage + offset;
        if (start + length > reader.size) {
            continue;
        }
        const std::string_view raw(reinterpret_cast<const char*>(reader.data + start), length);
        std::string decoded;
        if (platformId == 3 || platformId == 0) {
            decoded = Utf16BeToUtf8(raw);
        } else {
            decoded.assign(raw);
        }
        // Windows 平台的记录优先
        if (platformId == 3) {
            return decoded;
        }
        if (best.empty()) {
            best = decoded;
        }
    }
    return best;
}

bool ReadFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return !out.empty();
}

} // namespace

int FindTtcIndex(const std::string& path, const std::string& familyName) {
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        return 0;
    }
    const Reader reader{bytes.data(), bytes.size()};
    if (reader.Tag(0, "ttcf")) {
        const std::size_t fontCount = reader.U32(8);
        for (std::size_t i = 0; i < fontCount && i < 64; ++i) {
            const std::size_t offset = reader.U32(12 + i * 4);
            if (MatchesFamily(FamilyNameOf(reader, offset), familyName)) {
                return static_cast<int>(i);
            }
        }
        return 0; // 没匹配上：退回 0，至少能显示（可能是「Microsoft YaHei」而非 UI 版）
    }
    return 0; // 单字体 sfnt
}

void TickAtlasStats() {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    g_textureCount = atlas->TexList.Size;
    g_atlasBytes = 0;
    for (int i = 0; i < atlas->TexList.Size; ++i) {
        const ImTextureData* texture = atlas->TexList[i];
        g_atlasBytes += static_cast<std::size_t>(texture->Width) *
                        static_cast<std::size_t>(texture->Height) * 4u;
    }
}

bool BuildFontAtlas(bool serif) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;

    const FontPaths paths;
    const bool wantSerif = serif;
    const std::string uiPath = FirstExisting({paths.ui, paths.uiFallback});
    if (uiPath.empty()) {
        shine::log::Error("no CJK-capable font found (msyh.ttc / simhei.ttf) — UI will show tofu");
        atlas->AddFontDefault();
        g_atlasLogged = true;
        return false;
    }
    // --font-ui 族序里中文主力是 "Microsoft YaHei UI"；它在 msyh.ttc 里不一定是 0 号，
    // 所以按族名查表定位，查不到就退回 0（Microsoft YaHei）。
    const int uiFontNo = FindTtcIndex(uiPath, "Microsoft YaHei UI");

    std::string activePath = uiPath;
    int activeFontNo = uiFontNo;
    if (wantSerif && !paths.serif.empty() && std::filesystem::exists(paths.serif)) {
        activePath = paths.serif;
        activeFontNo = FindTtcIndex(paths.serif, "SimSun");
        if (activeFontNo == 0) {
            activeFontNo = FindTtcIndex(paths.serif, "NSimSun");
        }
    }
    g_familyName = activePath + "#" + std::to_string(activeFontNo);

    atlas->Clear();

    // GetGlyphRangesChineseSimplifiedCommon() 覆盖 ASCII + 常用汉字 2500 + 标点。
    // ⚠️ 不能用 ChineseFull()：21000 字 × 13 档字号，图集会顶到几百 MB。
    const ImWchar* cjkRanges = atlas->GetGlyphRangesChineseSimplifiedCommon();

    g_fonts.clear();
    for (const float size : kUiSizes) {
        ImFontConfig config;
        config.FontNo = activeFontNo;
        config.SizePixels = size * io.DisplayFramebufferScale.x;
        config.GlyphRanges = cjkRanges; // 静态数组，生命周期覆盖字体，1:1 限定可光栅化的字形集
        config.PixelSnapH = true;
        ImFont* font = atlas->AddFontFromFileTTF(activePath.c_str(), config.SizePixels, &config);
        if (font != nullptr) {
            g_fonts[size] = font;
        }
    }
    if (g_fonts.empty()) {
        atlas->AddFontDefault();
    }
    // 正文基准 13px 是 ui.css 里声明最多的控件字号。
    io.FontDefault = FontAt(theme::font::kBase);

    // 等宽：--font-mono 的 Cascadia Code / JetBrains Mono / Consolas，只取拉丁。
    const std::string monoPath = FirstExisting({paths.monoAlt, paths.mono});
    g_mono.clear();
    if (!monoPath.empty()) {
        for (const float size : kMonoSizes) {
            ImFontConfig config;
            config.SizePixels = size * io.DisplayFramebufferScale.x;
            config.PixelSnapH = true;
            ImFont* font = atlas->AddFontFromFileTTF(monoPath.c_str(), config.SizePixels, &config);
            if (font != nullptr) {
                g_mono[size] = font;
            }
        }
    }
    if (g_mono.empty()) {
        g_mono = g_fonts; // 退回首字体，保证 KV 的等宽对齐不塌
    }

    // ⚠️ **不要**在这里调 ImFontAtlas::Build()：1.92+ 的后端声明
    // ImGuiBackendFlags_RendererHasTextures，由它在第一帧按需烘焙并上传纹理。
    // 提前 Build() 会打 "Called ImFontAtlas::Build() before ImGuiBackendFlags_
    // RendererHasTextures got set!" 并每帧重复报。显存统计改在首帧后做。
    g_atlasLogged = false;
    return true;
}

void LogFontAtlasIfNeeded() {
    if (g_atlasLogged) {
        return;
    }
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas->TexList.Size == 0) {
        return; // 后端还没烘焙，下一帧再问
    }
    g_atlasLogged = true;
    TickAtlasStats();
    shine::log::Info("font atlas: {} sizes={} textures={} vram={:.1f} MB (hard limit 64 MB)",
                     g_familyName, g_fonts.size(), g_textureCount,
                     static_cast<double>(g_atlasBytes) / (1024.0 * 1024.0));
    if (g_atlasBytes > 64u * 1024u * 1024u) {
        shine::log::Error("font atlas exceeds the 64 MB budget: {} bytes", g_atlasBytes);
    }
}

std::size_t FontAtlasBytes() { return g_atlasBytes; }
int FontAtlasTextureCount() { return g_textureCount; }

namespace {

ImFont* LookupNearest(const std::unordered_map<float, ImFont*>& table, float pixelSize) {
    if (table.empty()) {
        return nullptr;
    }
    if (const auto exact = table.find(pixelSize); exact != table.end()) {
        return exact->second;
    }
    // 向上取最近一档：小于请求值的字号一定存在，取它的下一档，
    // 避免文字被裁掉（ImGui 的 CalcTextSizeA 不会裁，但绘制会）。
    float bestSize = -1.0f;
    for (const auto& [size, font] : table) {
        (void)font;
        if (size >= pixelSize && (bestSize < 0.0f || size < bestSize)) {
            bestSize = size;
        }
    }
    if (bestSize >= 0.0f) {
        return table.at(bestSize);
    }
    return table.begin()->second;
}

} // namespace

ImFont* FontAt(float pixelSize) { return LookupNearest(g_fonts, pixelSize); }
ImFont* MonoAt(float pixelSize) { return LookupNearest(g_mono, pixelSize); }

ImFont* BaseFont() { return FontAt(13.0f); }

} // namespace shine::kit
