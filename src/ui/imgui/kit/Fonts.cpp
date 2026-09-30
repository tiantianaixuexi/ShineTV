#include "ui/imgui/kit/Fonts.h"

#include "core/Log.h"
#include "ui/imgui/theme/Tokens.h"

// ImFontGlyphRangesBuilder 在 imgui_internal.h 里；GetModuleFileNameW 需要 Win32。
#include <windows.h>

#include <imgui_internal.h>

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
// 只建常用档：每个字号都要乘以整个字形集，全量建会顶到显存上限。
// 未建档由 FontAt() 向上取最近一档（向上取会画大，缺档要尽量补齐）。
// 14.5 / 17 / 26 三档来自 views.css:98 / ui.css:518 / views.css:42，
// 曾经漏建导致这三处字号被向上取档画大，是静默的像素偏差。
const std::vector<float> kUiSizes = {10.5f, 11.0f, 11.5f, 12.0f, 12.5f, 13.0f, 13.5f, 14.0f,
                                      14.5f, 15.0f, 17.0f, 18.0f, 20.0f, 24.0f, 26.0f, 28.0f};
const std::vector<float> kMonoSizes = {10.5f, 11.0f, 11.5f, 12.0f, 12.5f, 13.0f, 14.0f, 18.0f};

// ---- 光栅化密度：字形模糊的根因开关（Dear ImGui 1.92 字体系统）----
//
// 为什么以前是糊的（三条链，每条都独立成立，缺一条就仍然糊）：
//
//  1. `RasterizerDensity` 缺省 1.0f。它才是 1.92 起**唯一**能提高字形光栅化
//     分辨率的旋钮 —— `imgui_draw.cpp` 里
//         rasterizer_density = src->RasterizerDensity * baked->RasterizerDensity
//         scale_for_raster_y = ScaleFactor * baked->Size * rasterizer_density * oversample_v
//         recip_v            = 1 / (oversample_v * rasterizer_density)
//     即密度只进光栅化侧，再由 recip 缩回绘制侧。
//     本工程 `io.DisplayFramebufferScale = (1,1)`（host/Host.cpp），OpenGL3/WGL
//     后端也报不上更高的密度 ⇒ `g.CurrentPixelDensity` 恒为 1 ⇒ 光栅化 1:1。
//
//  2. `PixelSnapH = true` 会把 OversampleH **强制压成 1**，而 **OversampleV 在
//     auto 模式下恒为 1**（imgui_draw.cpp:3524-3525 原文）：
//         *out_oversample_h = (src->OversampleH != 0) ? src->OversampleH
//                                : (raster_size > 36.0f || src->PixelSnapH) ? 1 : 2;
//         *out_oversample_v = (src->OversampleV != 0) ? src->OversampleV : 1;
//     注意第二行**没有** `raster_size > 36.0f` 那个分支 —— 也就是说这一版里
//     纵向超采样永远不会自动开启。CJK 的糊恰恰是纵向笔画糊（小字号一个笔画
//     只摊到 1 个像素高），所以横向超采样救不了，本项目又主动把横向也关了。
//     ⇒ `RasterizerDensity` 不是「更好的旋钮」，是**唯一**能给字高加分辨率的旋钮。
//
//  3. 本工程字号大量是**半点**（10.5 / 11.5 / 12.5 / 13.5 / 14.5，来自设计稿
//     的 CSS 像素）。stb_truetype 按整数高度出位图，1:1 光栅化后再按
//     `scale = size / baked->Size`（imgui_draw.cpp:5791）缩放到半点尺寸 ⇒
//     必然重采样。浏览器能用次像素定位，ImGui 不能。
//
// 修法是把密度抬到 2.0：字形按 2 倍分辨率出位图、绘制时缩回 1/2，由 GPU 做
// 一次高质量双线性缩小 ⇒ 小字号 CJK 从「糊成一团」变成可读。
//
// ⚠️ 为什么**不需要**按 imgui.h:3796 那句警告反向缩放字号：那句针对的是
//    旧式用法（同时把 `io.FontGlobalScale` 设成 1/N）。这里 `size` 与
//    `baked->Size` 都没变（都是 kUiSizes 里的原值），`scale` 恒为 1.0，
//    布局与排版数值**一个都不动** —— 只有位图分辨率变了。
//
// 代价：位图面积约 4 倍。图集显存由下面的 LogFontAtlasIfNeeded 打日志并按
// 64 MB 硬判据校验（超了会 log::Error），不要凭感觉改这个数，改完看日志。
constexpr float kRasterizerDensity = 2.0f;

struct FontPaths {
    std::string ui = "C:/Windows/Fonts/msyh.ttc";
    std::string uiFallback = "C:/Windows/Fonts/simhei.ttf";
    std::string uiBold = "C:/Windows/Fonts/msyhbd.ttc";
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
std::unordered_map<float, ImFont*> g_bold;
std::size_t g_atlasBytes = 0;
int g_textureCount = 0;
bool g_atlasLogged = false;
std::string g_familyName;
bool g_hasBold = false;

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

// ---- 业务字形集（P2.5 硬判据：不能有豆腐块）----
//
// GetGlyphRangesChineseSimplifiedCommon() 只有 2500 常用字，而本项目的
// 业务名（"章节"/"提示词"/"一致性校验"/"降级策略"…）大半**不在其中**——
// 图集里没有的字形会被画成 '?'，而且不报任何错。
//
// 这里扫源码里出现的中文字符，合并进常用字集。扫的是 UTF-8 字节里
// >= 0xE0 的三字节序列（GBK 注释里的汉字同样会落进来，多烘焙一些无妨，
// 相对于 2500 字只是零头）。字形集最终按 codepoint 去重排序。
//
// 为什么不走动态字形：ImGui 1.93 的动态路径要在每帧后重建图集并重传
// 纹理，而取证通道要的是「同一帧内所有字形都已在图集里」的确定性。
// 静态集合 + 源码扫描能同时满足这两个要求。
const std::vector<std::string>& GlyphScanRoots() {
    static const std::vector<std::string> roots = [] {
        std::vector<std::string> found;
        std::error_code ec;
        // 扫的是整个 src/ 下有业务汉字的目录。
        const std::vector<const char*> kSubs = {"ui/imgui", "novel", "pipeline", "visual",
                                                "media",  "project", "flow", "core", "comfy",
                                                "paint",  "mcp",     "llm"};

        // ⚠️ 顺序很重要：**先从 exe 路径回溯**，再用 CWD。
        //    程序常被从 build/ 或任意目录启动，CWD 未必是仓库根 —— 只认 CWD 时
        //    扫描会一个文件都找不到，字形集退回 2500 常用字，节点副行里的
        //    「雨夜」「转身」这类词就变 '?'（实测踩过）。
        const auto tryRoot = [&](const std::filesystem::path& repoRoot) {
            std::vector<std::string> hit;
            for (const char* sub : kSubs) {
                const std::filesystem::path dir = repoRoot / "src" / sub;
                if (std::filesystem::exists(dir, ec)) {
                    hit.emplace_back(dir.string());
                }
            }
            return hit;
        };

        wchar_t buffer[MAX_PATH]{};
        if (const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH); length > 0) {
            std::filesystem::path dir = std::filesystem::path(buffer).parent_path();
            for (int up = 0; up < 5; ++up) {
                found = tryRoot(dir);
                if (!found.empty()) {
                    break;
                }
                const std::filesystem::path parent = dir.parent_path();
                if (parent == dir) {
                    break;
                }
                dir = parent;
            }
        }
        if (found.empty()) {
            found = tryRoot(std::filesystem::current_path(ec));
        }
        return found;
    }();
    return roots;
}

// 把一个 UTF-8 片段里的中文 codepoint 记进 builder
void AddUtf8Cjk(std::string_view text, ImFontGlyphRangesBuilder& builder) {
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x80) {
            builder.AddChar(static_cast<ImWchar>(byte));
            ++i;
        } else if ((byte & 0xE0) == 0xC0 && i + 1 < text.size()) {
            builder.AddChar(static_cast<ImWchar>(((byte & 0x1Fu) << 6) |
                                                  (static_cast<unsigned char>(text[i + 1]) & 0x3Fu)));
            i += 2;
        } else if ((byte & 0xF0) == 0xE0 && i + 2 < text.size()) {
            const auto cp = static_cast<ImWchar>(
                ((byte & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                (static_cast<unsigned char>(text[i + 2]) & 0x3Fu));
            // 只收 CJK 统一表意文字（U+4E00..U+9FFF）+ 扩展 A + 兼容表意
            if ((cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
                (cp >= 0xF900 && cp <= 0xFAFF)) {
                builder.AddChar(cp);
            }
            i += 3;
        } else if ((byte & 0xF8) == 0xF0 && i + 3 < text.size()) {
            i += 4; // 四字节序列（扩展 B+），本项目用不到，跳过
        } else {
            ++i;
        }
    }
}

// 常用 2500 字 ∪ 源码里出现的业务汉字。返回的数组由下面那个 static 持有到
// 进程结束 —— 1.92+ 里 ImFontAtlas 并不接管这块内存，指针的所有权仍在我们。
const ImWchar* BuildGlyphRanges(ImFontAtlas* atlas) {
    ImFontGlyphRangesBuilder builder;
    // 基线：ImGui 自带的 2500 常用字 + 拉丁 + 标点
    for (const ImWchar* range = atlas->GetGlyphRangesChineseSimplifiedCommon(); range != nullptr &&
                                range[0] != 0;) {
        for (ImWchar c = range[0]; c <= range[1] && c != 0xFFFF; ++c) {
            builder.AddChar(c);
        }
        if (range[1] == 0xFFFF) {
            break;
        }
        range += 2;
    }
    // 叠加：源码扫描
    std::error_code ec;
    for (const std::string& root : GlyphScanRoots()) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".json") {
                continue;
            }
            std::ifstream stream(entry.path(), std::ios::binary);
            if (!stream) {
                continue;
            }
            const std::string content((std::istreambuf_iterator<char>(stream)),
                                      std::istreambuf_iterator<char>());
            AddUtf8Cjk(content, builder);
        }
    }
    // 兜底基线：界面固定用到的标点 / 符号 / 箭头。
    //
    // ⚠️ 源码扫描（AddUtf8Cjk）**只收** 0x3400-0x9FFF、0xF900-0xFAFF 三段汉字，
    //    所以 `›`（U+203A 单角引号）、`→`、`×`、`±`、`…` 这类**永远进不来**。
    //    症状不是报错，是界面上某个符号变豆腐块：面包屑的 `›` 分隔符实测就是
    //    画成了缺字形（Fonts.h 的硬规则是「无豆腐块」，靠扫描兜不住，只能显式列）。
    //    这里按 codepoint 逐个登记，新增符号时改这一处即可。
    static constexpr ImWchar kUiSymbols[] = {
        0x00B7,  // ·
        0x00D7,  // ×
        0x00F7,  // ÷
        0x00B1,  // ±
        0x2013,  // –
        0x2014,  // —
        0x2018, 0x2019,  // ' '
        0x201C, 0x201D,  // " "
        0x2022,  // •
        0x2026,  // …
        0x2039,  // ‹
        0x203A,  // ›
        0x2190, 0x2191, 0x2192, 0x2193,  // ← ↑ → ↓
        0x2212,  // −
        0x25CF,  // ●
        0x25CB,  // ○
        0x2605,  // ★
        0x2606,  // ☆
    };
    for (const ImWchar c : kUiSymbols) {
        builder.AddChar(c);
    }
    // ⚠️ 范围数组的**生命周期必须覆盖字体**：1.92+ 的 ImFontConfig::GlyphRanges
    //    文档原文是 "*LEGACY* THE ARRAY DATA NEEDS TO PERSIST AS LONG AS THE FONT
    //    IS ALIVE"，图集在**后续帧**按需烘焙时才回读这个指针。
    //    早先这里写的是局部 `ImVector<ImWchar> ranges;` 然后 `return ranges.Data` ——
    //    函数一返回内存就还给分配器，指针悬垂。1.92 之前那样写没事，是因为
    //    AddFontFromFileTTF 会在当场把范围拷走；1.92+ 不再当场拷。
    //    症状不会报错：静态存储后读到的仍是同一批字节（分配器没复用那块内存），
    //    所以"看起来一直是对的"，直到某次分配把它盖掉。
    static ImVector<ImWchar> ranges;
    ranges.clear();
    builder.BuildRanges(&ranges);
    // 补 NUL 结尾（BuildRanges 已保证，这里只是防御）
    if (ranges.empty() || ranges.back() != 0) {
        ranges.push_back(0);
    }
    // 字形集是 P2.5 的硬判据，扫不到根目录就必须吵：静默退回 2500 常用字时，
    // 界面只表现为个别字变 '?'，不报错也不崩。
    shine::log::Info("glyph ranges: {} roots, {} codepoints", GlyphScanRoots().size(),
                     static_cast<std::size_t>(ranges.Size > 0 ? ranges.Size - 1 : 0));
    return ranges.Data;
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

    // 字形集 = 常用 2500 字 ∪ 源码里出现的业务汉字（见 BuildGlyphRanges 的说明）。
    // ⚠️ 范围数组的生命周期归 ImFontAtlas 所有：AddFontFromFileTTF 会在
    //    Build 时把字形烘焙进去，之后不再回读这个指针。
    const ImWchar* cjkRanges = BuildGlyphRanges(atlas);

    g_fonts.clear();
    for (const float size : kUiSizes) {
        ImFontConfig config;
        config.FontNo = activeFontNo;
        config.SizePixels = size * io.DisplayFramebufferScale.x;
        config.GlyphRanges = cjkRanges; // 静态数组，生命周期覆盖字体，1:1 限定可光栅化的字形集
        config.PixelSnapH = true;       // 对齐 AdvanceX，KV 列不错位；代价是 oversample 被压成 1，靠 kRasterizerDensity 补
        config.RasterizerDensity = kRasterizerDensity;
        ImFont* font = atlas->AddFontFromFileTTF(activePath.c_str(), config.SizePixels, &config);
        if (font != nullptr) {
            g_fonts[size] = font;
        }
    }
    if (g_fonts.empty()) {
        atlas->AddFontDefault();
    }

    // 粗体面：设计稿的 600/700/800 全靠它。只建同样档位，字形集与常规一致。
    g_bold.clear();
    g_hasBold = false;
    const std::string boldPath = FirstExisting({paths.uiBold});
    if (!boldPath.empty()) {
        const int boldNo = FindTtcIndex(boldPath, "Microsoft YaHei UI Bold");
        for (const float size : kUiSizes) {
            ImFontConfig config;
            config.FontNo = boldNo;
            config.SizePixels = size * io.DisplayFramebufferScale.x;
            config.GlyphRanges = cjkRanges;
            config.PixelSnapH = true;
            config.RasterizerDensity = kRasterizerDensity;
            if (ImFont* font =
                    atlas->AddFontFromFileTTF(boldPath.c_str(), config.SizePixels, &config);
                font != nullptr) {
                g_bold[size] = font;
            }
        }
        g_hasBold = !g_bold.empty();
    }
    if (!g_hasBold) {
        g_bold = g_fonts; // 退回常规字重：只偏细，不出豆腐块
    }

    // 正文基准 13px 是 ui.css 里声明最多的控件字号。
    io.FontDefault = FontAt(theme::font::kBase);

    // 等宽：--font-mono 的 Cascadia Code / JetBrains Mono / Consolas。
    //
    // ⚠️ 这些字体**只有拉丁字形**，中文一律缺。而本项目的等宽位置并不只放哈希：
    // 节点副行（"第 3 章 雨夜"）、产物路径、剧本片段都是中文 —— 只挂等宽字体时
    // 它们全变 '?'（截图里节点标题正常、副行全是问号就是这个原因）。
    //
    // 修法是给每个等宽字体接一条**雅黑回落链**：MergeMode 往同一个 ImFont 里追加，
    // ImGui 按 codepoint 逐个查，第一个没命中的（中文）落到第二个字体的字形上。
    // 拉丁仍走 Cascadia 的等距字形（KV 对齐不塌），中文走雅黑（可读）。
    const std::string monoPath = FirstExisting({paths.monoAlt, paths.mono});
    g_mono.clear();
    if (!monoPath.empty() && uiFontNo >= 0) {
        for (const float size : kMonoSizes) {
            ImFontConfig config;
            config.SizePixels = size * io.DisplayFramebufferScale.x;
            config.PixelSnapH = true;
            config.RasterizerDensity = kRasterizerDensity;
            ImFont* monoFont =
                atlas->AddFontFromFileTTF(monoPath.c_str(), config.SizePixels, &config);
            if (monoFont == nullptr) {
                continue;
            }
            ImFontConfig fallback;
            fallback.FontNo = uiFontNo;
            fallback.SizePixels = config.SizePixels;
            fallback.PixelSnapH = true;
            fallback.RasterizerDensity = kRasterizerDensity; // 必须与主源一致，否则同一 ImFont 内两种密度的位图互相错位
            fallback.GlyphRanges = cjkRanges;
            fallback.MergeMode = true; // 追加进上面那个 ImFont，不新建
            atlas->AddFontFromFileTTF(uiPath.c_str(), config.SizePixels, &fallback);
            g_mono[size] = monoFont;
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
ImFont* FontBoldAt(float pixelSize) { return LookupNearest(g_bold, pixelSize); }
bool HasBoldFace() { return g_hasBold; }

ImFont* BaseFont() { return FontAt(13.0f); }

} // namespace shine::kit
