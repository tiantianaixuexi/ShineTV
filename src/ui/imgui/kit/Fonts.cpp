#include "ui/imgui/kit/Fonts.h"

#include "core/Log.h"
#include "ui/imgui/theme/Tokens.h"

// 这里只需要公开的 ImGui API。曾经 include 的 <windows.h>（为 GetModuleFileNameW
// 回溯仓库根做字形集扫描）和 <imgui_internal.h>（为 ImFontGlyphRangesBuilder）
// 都随 1.92 的按需字形加载一起删掉了 —— 源码扫描那条路已经不存在。
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
//
// ⚠️ 9.5 / 16.0 两档是后补的，同样是**在用的**字号、同样是缺档：
//    * 9.5 —— `OverviewRightRail.cpp` 的矩阵表头与行首（`FontBoldAt(9.5f)` /
//      `MonoAt(9.5f)`）。缺档时向上取到 **10.5**，即画大 1px（约 10.5%），
//      而它出现在一个 7 列窄格里，累积的横向溢出会挤掉后面的列。
//    * 16.0 —— `Page_Novel.cpp` 的「设定集」标题（`FontBoldAt(16.0f)`）。
//      缺档时向上取到 **17**。
//    判据是「有没有调用点」而不是「CSS 里有没有写过」：CSS 写一堆用不到的字号
//    不值得占图集，但在用的却取不到档就是静默偏差。新增档位前先 grep 调用点。
const std::vector<float> kUiSizes = {9.5f,  10.5f, 11.0f, 11.5f, 12.0f, 12.5f, 13.0f, 13.5f,
                                     14.0f, 14.5f, 15.0f, 16.0f, 17.0f, 18.0f, 20.0f, 24.0f,
                                     26.0f, 28.0f, 34.0f};
const std::vector<float> kMonoSizes = {9.5f, 10.5f, 11.0f, 11.5f, 12.0f, 12.5f, 13.0f, 14.0f, 18.0f};

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
// 代价：位图面积约 4 倍。预算不按总量卡（按需加载下没有固定上限），只看单帧
// 增量与增长速率，见下面 AtlasGuard 那段注释与 LogFontAtlasIfNeeded。
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
// 全局字号缩放（R3）。token 值（设计稿 CSS 像素）永远不动，缩放只在这里乘：
// 图集按 kUiSizes × scale 烘焙，FontAt 系按「请求值 × scale」取档。
// scale=1.0 时与旧路径逐位一致 —— 这是 R3「收编前后像素不变」的依据。
float g_fontScale = 1.0f;
std::size_t g_atlasBytes = 0;
std::size_t g_atlasFrameDelta = 0;
int g_atlasBytesPerPixel = 0; // 实际格式（RGBA32=4；Alpha8=1，但见下面：OpenGL3 后端不支持）
int g_textureCount = 0;
bool g_atlasLogged = false;
std::string g_familyName;
bool g_hasBold = false;

// ---- 图集预算告警 ----
//
// 1.92 的按需字形加载**没有固定上限**：图集随用户实际读到的汉字增长，读多少取
// 决于用户。所以原先那个「总量 > 64MB 就 log::Error」是错的设计：
// ① 把一个正常且可预期的现象标成 Error；② 它量的是 **CPU 侧**暂存缓冲却打印成
// "vram="，让人误以为在拿显存做预算；③ 长阅读下会**每帧**重复刷屏。
//
// 改成三件事：一次性快照（仍然要有数字）、单帧增量告警（图集重排/抖动）、
// 增长速率告警（持续暴涨）。去重按「事件」而不是全局一次性 —— 全局一次性标志的
// 毛病是启动时报一次，之后真的开始暴涨也不吭声。
constexpr std::size_t kMiB = 1024u * 1024u;
// 沿用原来的 64MB 数字，但从 Error 降为 Warn：它是「值得看一眼」而不是「出了故障」。
// 按需加载下超过它只说明用户读了很多字。
constexpr std::size_t kAtlasSoftCapBytes = 64u * kMiB;
// 单帧多出 8MB = 一次大规模重打包（新增字号档 / 触发 repack），值得知道。
constexpr std::size_t kAtlasFrameDeltaWarn = 8u * kMiB;
// 每秒 32MB 持续增长 = 要么在快速翻页，要么有字号在抖。
constexpr std::size_t kAtlasRateWarn = 32u * kMiB;
constexpr int kSampleEveryFrames = 60; // ≈1s @60fps

struct AtlasGuard {
    bool warnedCap = false;
    bool warnedDelta = false;
    bool warnedRate = false;
    std::size_t lastBytes = 0;
    std::size_t sampleBytes = 0;
    int framesSinceSample = 0;
};
AtlasGuard g_guard;

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

// ---------------------------------------------------------------- 字形 em 盒校正（R6）
//
// 为什么要有这个系数：ImGui 的「字号」是把 hhea.ascent − descent 这个**跨度**映射成
// 那么多像素（stbtt_ScaleForPixelHeight 的定义），而 CSS 的 font-size 是 **em 盒**。
// msyh 的 A−D ≈ 1.32em ⇒ 画出来的字只有请求值的 0.7577 —— 全仓文字比设计稿小 24%
// （第十一轮实测：请求 24px 的 CJK 墨迹 18px/字；Chrome 同段 CSS 24px）。
//
// 修法 = `ImFontConfig::ExtraSizeScale`：`imgui_draw.cpp:4747` 把它乘进
// `ScaleFactor`，而字形位图与 AdvanceX 都按 `ScaleFactor × baked->Size` 算（:4801）
// ⇒ 字形与步进变成 E 倍；行高度量（Ascent/Descent，:4780）会除回 E 所以**不变**
// ⇒ 基线体系不动，只有墨迹变大。
//
// ⚠️ 进度.md 第十一轮记的候选公式「upem/(A−D) = 0.7577」**方向反了**（那是
//    缩小）：正确值是 **(A−D)/upem ≈ 1.32**，本轮已按源码核对并实测订正。
// ⚠️ 逐字面不同（msyh / 宋体 / Cascadia 各自的 hhea 跨度不一样）⇒ 从字体文件
//    现读，不写死：head 表的 unitsPerEm 与 hhea 表的 ascender/descender。
[[nodiscard]] float FaceExtraSizeScale(const std::string& path, int fontNo) {
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        return 1.0f;
    }
    const Reader reader{bytes.data(), bytes.size()};
    std::size_t fontOffset = 0;
    if (reader.Tag(0, "ttcf")) {
        const std::size_t fontCount = reader.U32(8);
        if (fontNo < 0 || static_cast<std::size_t>(fontNo) >= fontCount) {
            return 1.0f;
        }
        fontOffset = reader.U32(12 + static_cast<std::size_t>(fontNo) * 4);
    }
    std::size_t headOffset = 0;
    std::size_t hheaOffset = 0;
    const std::size_t tableCount = reader.U16(fontOffset + 4);
    for (std::size_t i = 0; i < tableCount; ++i) {
        const std::size_t record = fontOffset + 12 + i * 16;
        if (reader.Tag(record, "head")) {
            headOffset = reader.U32(record + 8);
        } else if (reader.Tag(record, "hhea")) {
            hheaOffset = reader.U32(record + 8);
        }
    }
    if (headOffset == 0 || hheaOffset == 0) {
        return 1.0f;
    }
    const int upem = reader.U16(headOffset + 18);          // head.unitsPerEm
    const int ascent = static_cast<std::int16_t>(reader.U16(hheaOffset + 4));
    const int descent = static_cast<std::int16_t>(reader.U16(hheaOffset + 6));
    const int span = ascent - descent;
    if (upem <= 0 || span <= 0) {
        return 1.0f;
    }
    // 健全性钳制：正常字体的 A−D 在 0.9..2.0em 之间；超出说明解析错了，宁可不校正。
    const float ratio = static_cast<float>(span) / static_cast<float>(upem);
    if (ratio < 0.9f || ratio > 2.0f) {
        return 1.0f;
    }
    return ratio;
}

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
    // ⚠️ 这里量的是 **CPU 侧**的像素暂存缓冲（ImTextureData::Pixels 是
    //    `unsigned char*`），**不是显存** —— 1.92 之前这里打的是 "vram="，
    //    标签是错的，害得人以为在拿显存预算。GPU 侧只留一个 TexID。
    // 用 GetSizeInBytes() 而不是 Width*Height*4：它读 BytesPerPixel，而
    // TexDesiredFormat 已经是 Alpha8（1 字节/像素），硬编码 4 会虚报 4 倍。
    g_atlasBytes = 0;
    g_atlasBytesPerPixel = 0;
    for (int i = 0; i < atlas->TexList.Size; ++i) {
        g_atlasBytes += static_cast<std::size_t>(atlas->TexList[i]->GetSizeInBytes());
        g_atlasBytesPerPixel = atlas->TexList[i]->BytesPerPixel;
    }
    g_atlasFrameDelta = g_atlasBytes > g_guard.lastBytes ? g_atlasBytes - g_guard.lastBytes : 0;
    g_guard.lastBytes = g_atlasBytes;
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

    // ---- 字形集：刻意不设 ImFontConfig::GlyphRanges ----
    //
    // 1.92 之前必须喂它，否则图集只烘那一份字形、其余全是豆腐块 —— 早期版本
    // 扫 281 个源文件抽汉字再合并 2500 常用字，为的就是这个。
    // 1.92 起那整套是 **legacy 路径**，源码链路闭合在 imgui_draw.cpp：
    //     ImGui_ImplOpenGL3_Init:1097   io.BackendFlags |= RendererHasTextures
    //     :2772 ImFontAtlasBuildUpdateRendererHasTexturesFromContext
    //                                -> atlas->RendererHasTextures = true
    //     :3514 if (atlas->RendererHasTextures == false)
    //                                ImFontAtlasBuildLegacyPreloadAllGlyphRanges(atlas);
    //     :3568 src->GlyphRanges 全工程**只在这一处**被读
    // 本工程用 OpenGL3 后端 ⇒ 那条分支永不执行 ⇒ GlyphRanges 存下来后没人读。
    // 真正在起作用的是**按需加载**：小说正文里任意汉字，只要 msyh.ttc 里有就
    // 会被自动烘焙。所以「字形集够不够」这个问题根本不存在。
    //
    // 代价从「豆腐块」换成了「图集随阅读量增长」—— 见 LogFontAtlasIfNeeded 的
    // 增长告警。ImGui 自己会多纹理打包（TexList 是数组），变大不会崩。
    //
    // ⚠️ 换回 legacy 后端（不声明 RendererHasTextures）时这段必须重新接上，
    //    否则界面全是 '?'。那不是回归，是换了一种加载协议。
    //
    // 字体只要 alpha，1 字节/像素足够：1.92 给了这个开关（默认 RGBA32）。
    // CPU 侧暂存缓冲直接省 4 倍，等于把图集预算放大 4 倍。
    // ⚠️ 图集格式：**不能**设成 ImTextureFormat_Alpha8。
    //
    // core 侧是支持的（imgui_draw.cpp:4132 `new_tex->Create(atlas->TexDesiredFormat, …)`），
    // 但 **imgui_impl_opengl3 后端把格式硬编码成 RGBA**：
    //     :712  glTexImage2D(…, GL_RGBA, …, GL_RGBA, GL_UNSIGNED_BYTE, pixels)
    //     :733  glTexSubImage2D(…, GL_RGBA, …)
    //     :739  const int src_pitch = r.w * tex->BytesPerPixel;   ← 按 1B/px 算行跨度
    // 最后一行按 Alpha8 的 1 字节/像素算跨度，却仍然用 GL_RGBA 上传（OpenGL 期望
    // 每行 4 字节）⇒ 读越界 / 上传垃圾。实测：设了 Alpha8 之后图集根本烘不出来，
    // `atlas->TexList` 一直是空，`LogFontAtlasIfNeeded` 永远等不到第一帧。
    //
    // 所以「字体只要 alpha，省 4 倍」这个想法在**这个后端上不成立**。等上游
    // OpenGL3 后端支持了 ImTextureFormat_Alpha8 再开；开之前这里只能是 RGBA32。
    // 下面的统计用 GetSizeInBytes()（读 BytesPerPixel）而不是硬编码 *4，
    // 这样将来换格式时统计口径自动跟着变，不会又虚报 4 倍。
    atlas->Clear();

    g_fonts.clear();
    const float uiExtra = FaceExtraSizeScale(activePath, activeFontNo);
    for (const float size : kUiSizes) {
        ImFontConfig config;
        config.FontNo = activeFontNo;
        // 全局缩放档：档位本身乘 scale（scale=1.0 时就是原值，表不变）。
        config.SizePixels = size * g_fontScale;
        // R6：em 盒校正 —— 字形与步进 ×(A−D)/upem，行盒不变（见函数注释）。
        config.ExtraSizeScale = uiExtra;
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
        const float boldExtra = FaceExtraSizeScale(boldPath, boldNo);
        for (const float size : kUiSizes) {
            ImFontConfig config;
            config.FontNo = boldNo;
            config.SizePixels = size * g_fontScale;
            config.ExtraSizeScale = boldExtra;
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
        const float monoExtra = FaceExtraSizeScale(monoPath, 0);
        for (const float size : kMonoSizes) {
            ImFontConfig config;
            config.SizePixels = size * g_fontScale;
            config.ExtraSizeScale = monoExtra; // 等宽字面自己的 hhea 跨度
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
            fallback.ExtraSizeScale = uiExtra; // 中文回落是 msyh，用它自己的跨度
            fallback.PixelSnapH = true;
            fallback.RasterizerDensity = kRasterizerDensity; // 必须与主源一致，否则同一 ImFont 内两种密度的位图互相错位
            fallback.MergeMode = true; // 追加进上面那个 ImFont，不新建
            atlas->AddFontFromFileTTF(uiPath.c_str(), config.SizePixels, &fallback);
            g_mono[size] = monoFont;
        }
    }
    if (g_mono.empty()) {
        g_mono = g_fonts; // 退回首字体，保证 KV 的等宽对齐不塌
    }

    for (ImFont* font : atlas->Fonts) {
        font->CurrentRasterizerDensity = io.DisplayFramebufferScale.x;
    }

    // ⚠️ **不要**在这里调 ImFontAtlas::Build()：1.92+ 的后端声明
    // ImGuiBackendFlags_RendererHasTextures，由它在第一帧按需烘焙并上传纹理。
    // 提前 Build() 会打 "Called ImFontAtlas::Build() before ImGuiBackendFlags_
    // RendererHasTextures got set!" 并每帧重复报。显存统计改在首帧后做。
    g_atlasLogged = false;
    return true;
}

void LogFontAtlasIfNeeded() {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas->TexList.Size == 0) {
        return; // 后端还没烘焙，下一帧再问
    }
    TickAtlasStats();

    // ---- 一次性快照：仍然要有数字，但把「是什么」说准 ----
    if (!g_atlasLogged) {
        g_atlasLogged = true;
        shine::log::Info(
            "font atlas: {} sizes={} textures={} cpu-bytes={:.2f} MB ({} B/px, CPU 侧暂存；"
            "按需加载，无固定上限)",
            g_familyName, g_fonts.size(), g_textureCount,
            static_cast<double>(g_atlasBytes) / static_cast<double>(kMiB), g_atlasBytesPerPixel);
    }

    // ---- 单帧增量：一帧多出 8MB = 一次大规模重打包 ----
    if (g_atlasFrameDelta > kAtlasFrameDeltaWarn) {
        if (!g_guard.warnedDelta) {
            g_guard.warnedDelta = true;
            shine::log::Warn("font atlas 单帧增长 {:.2f} MB（{} -> {}，{} 张纹理）：发生了一次"
                             "大规模重打包。若持续出现，说明有字号在反复请求新档位。",
                             static_cast<double>(g_atlasFrameDelta) / static_cast<double>(kMiB),
                             g_guard.lastBytes / kMiB, g_atlasBytes / kMiB, g_textureCount);
        }
    } else if (g_guard.warnedDelta) {
        g_guard.warnedDelta = false; // 恢复了，下次真的再涨还会报
    }

    // ---- 增长速率：按需加载下唯一真正值得盯的量 ----
    if (++g_guard.framesSinceSample < kSampleEveryFrames) {
        return;
    }
    g_guard.framesSinceSample = 0;
    const std::size_t grew =
        g_atlasBytes > g_guard.sampleBytes ? g_atlasBytes - g_guard.sampleBytes : 0;
    g_guard.sampleBytes = g_atlasBytes;
    if (grew > kAtlasRateWarn) {
        if (!g_guard.warnedRate) {
            g_guard.warnedRate = true;
            shine::log::Warn("font atlas 每秒增长 {:.2f} MB，当前 {:.2f} MB / {} 张纹理：正在大量"
                             "烘焙新字形。正常翻页也会有；持续不降才需要查是否有字号在抖。",
                             static_cast<double>(grew) / static_cast<double>(kMiB),
                             static_cast<double>(g_atlasBytes) / static_cast<double>(kMiB),
                             g_textureCount);
        }
    } else if (g_guard.warnedRate) {
        g_guard.warnedRate = false;
    }

    // ---- 软上限：超过只警告，且只在该「次」超过期间报一次 ----
    if (g_atlasBytes > kAtlasSoftCapBytes) {
        if (!g_guard.warnedCap) {
            g_guard.warnedCap = true;
            shine::log::Warn("font atlas CPU 侧暂存 {:.2f} MB 已超过软上限 {} MB。按需加载下这不是"
                             "故障，只说明用户读了很多字；ImGui 会自动多纹理打包（TexList 是数组），"
                             "不会崩。真要省就减少预烘焙字号档（kUiSizes / kMonoSizes）。",
                             static_cast<double>(g_atlasBytes) / static_cast<double>(kMiB),
                             kAtlasSoftCapBytes / kMiB);
        }
    } else if (g_guard.warnedCap) {
        g_guard.warnedCap = false;
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

// 请求值乘全局缩放档后再查表：token 是设计稿像素，缩放只在这一个口子上发生
// （208 个调用点一个都不用动 —— 这就是「字号可调整」结构上成立的条件）。
ImFont* FontAt(float pixelSize) { return LookupNearest(g_fonts, pixelSize * g_fontScale); }
ImFont* MonoAt(float pixelSize) { return LookupNearest(g_mono, pixelSize * g_fontScale); }
ImFont* FontBoldAt(float pixelSize) { return LookupNearest(g_bold, pixelSize * g_fontScale); }
bool HasBoldFace() { return g_hasBold; }

void SetUiFontScale(float scale) { g_fontScale = scale > 0.0f ? scale : 1.0f; }
float UiFontScale() { return g_fontScale; }

ImFont* BaseFont() { return FontAt(theme::font::kBase); }

} // namespace shine::kit
