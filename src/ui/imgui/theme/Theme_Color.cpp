// 色值层：0xRRGGBBAA ↔ ImVec4 / ImU32、color-mix 等价运算、十六进制编解码、
// token 字段反射。
//
// 从 Theme.cpp 拆出来的理由：这一族全是**纯函数**（除 TokenFields 的静态表外
// 不碰任何状态），是整个主题层里最容易独立验证、也最容易被误改的一族。
// 混在 Theme.cpp 里时，它和「阴影几何」「ApplyTheme 那 31 行颜色赋值」挨在
// 一起 —— 后者是一次性写全的对照表，任何人动色值都得先在几百行里找到这几十行。

#include "ui/imgui/theme/Theme_Internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace shine::theme {

ImVec4 Rgba(std::uint32_t rgba) {
    return ImVec4(static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f,
                  static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f,
                  static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f,
                  static_cast<float>(rgba & 0xFFu) / 255.0f);
}

std::uint32_t PackRgba(const ImVec4& rgba) {
    const auto channel = [](float v) -> std::uint32_t {
        const float clamped = std::clamp(v, 0.0f, 1.0f);
        return static_cast<std::uint32_t>(std::lround(clamped * 255.0f)) & 0xFFu;
    };
    return (channel(rgba.x) << 24) | (channel(rgba.y) << 16) | (channel(rgba.z) << 8) |
           channel(rgba.w);
}

std::uint32_t MixAlpha(std::uint32_t rgb, float percent) {
    const std::uint32_t alpha =
        static_cast<std::uint32_t>(std::lround(std::clamp(percent, 0.0f, 100.0f) * 2.55f)) & 0xFFu;
    return (rgb & 0xFFFFFF00u) | alpha;
}

std::uint32_t MixAlpha(std::uint32_t rgb, alpha::Step step) {
    return MixAlpha(rgb, alpha::kPercents[static_cast<std::size_t>(step)]);
}

// color-mix(in srgb, A N%, B) —— CSS Color 5 的定义是**预乘 alpha**后在 gamma 编码
// 的 sRGB 通道上插值，不是线性光。两端都不透明时它退化成通道直线 lerp；本项目所有
// 调用点的两端都是不透明基色。走 spec 公式而不是直接 lerp，是为了将来传半透明色进来
// 时不会静默算错。
std::uint32_t MixSrgb(std::uint32_t a, std::uint32_t b, float percentOfA) {
    const double t = static_cast<double>(std::clamp(percentOfA, 0.0f, 100.0f)) / 100.0;
    const double alphaA = static_cast<double>(a & 0xFFu);
    const double alphaB = static_cast<double>(b & 0xFFu);
    const double alphaOut = t * alphaA + (1.0 - t) * alphaB;
    if (alphaOut <= 0.0) {
        return 0u;
    }
    const auto channel = [t, alphaA, alphaB, alphaOut, a, b](int shift) -> std::uint32_t {
        const double valueA = static_cast<double>((a >> shift) & 0xFFu);
        const double valueB = static_cast<double>((b >> shift) & 0xFFu);
        const double premultiplied = t * alphaA * valueA + (1.0 - t) * alphaB * valueB;
        const double straight = std::clamp(premultiplied / alphaOut, 0.0, 255.0);
        return static_cast<std::uint32_t>(std::lround(straight)) & 0xFFu;
    };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

// 裸 rgba() 压平：source-over 直通混合（非预乘）。percent 用 CSS 标称值，不是 alpha 字节。
std::uint32_t FlattenOver(std::uint32_t translucent, std::uint32_t opaqueBackdrop, float percent) {
    const double t = static_cast<double>(std::clamp(percent, 0.0f, 100.0f)) / 100.0;
    const auto channel = [t](std::uint32_t top, std::uint32_t bottom, int shift) -> std::uint32_t {
        const double a = static_cast<double>((top >> shift) & 0xFFu);
        const double b = static_cast<double>((bottom >> shift) & 0xFFu);
        return static_cast<std::uint32_t>(std::lround(a * t + b * (1.0 - t))) & 0xFFu;
    };
    return (channel(translucent, opaqueBackdrop, 24) << 24) |
           (channel(translucent, opaqueBackdrop, 16) << 16) |
           (channel(translucent, opaqueBackdrop, 8) << 8) | 0xFFu;
}

ImU32 ToImU32(std::uint32_t rgba) {
    // 唯一一处「theme 存储序 → ImGui 打包序」的转换。走 ImGui 自己的 float4 往返，
    // 不手写位移：位移写错过一次，错的是整个界面而不是一个控件。
    return ImGui::ColorConvertFloat4ToU32(Rgba(rgba));
}

namespace {

// 字段表抽成函数：detail::AssignByName / PersistTheme / 样式编辑器三处共用一份，
// 少一份就少一处漂移风险。顺序 == kColorTokenNames。
const std::array<std::uint32_t ColorToken::*, kColorTokenCount>& TokenFields() {
    static const std::array<std::uint32_t ColorToken::*, kColorTokenCount> fields = {
        &ColorToken::bgVoid, &ColorToken::bgSurface, &ColorToken::bgPanel, &ColorToken::bgElevated,
        &ColorToken::bgOverlay, &ColorToken::lineSubtle, &ColorToken::lineNormal,
        &ColorToken::lineStrong, &ColorToken::textPrimary, &ColorToken::textSecondary,
        &ColorToken::textMuted, &ColorToken::textInverse, &ColorToken::accentPrimary,
        &ColorToken::accentPrimaryHover, &ColorToken::accentPrimaryFg, &ColorToken::accentSecondary,
        &ColorToken::accentInfo, &ColorToken::statusOk, &ColorToken::statusWarn,
        &ColorToken::statusDanger, &ColorToken::statusBusy, &ColorToken::statusIdle,
        &ColorToken::statusPending, &ColorToken::fillHover, &ColorToken::fillSelected,
        &ColorToken::fillMuted, &ColorToken::lineFocus, &ColorToken::shadowScrim,
        &ColorToken::shadow1, &ColorToken::shadow2, &ColorToken::shadowAccent,
    };
    return fields;
}

} // namespace

std::uint32_t TokenValue(const ColorToken& c, std::size_t index) {
    return index < kColorTokenCount ? c.*(TokenFields()[index]) : 0u;
}

std::uint32_t* TokenSlot(ColorToken& c, std::size_t index) {
    return index < kColorTokenCount ? &(c.*(TokenFields()[index])) : nullptr;
}

// ---------------------------------------------------------------- 内部编解码
namespace detail {

// #RRGGBB / #RRGGBBAA → 0xRRGGBBAA（6 位按不透明处理，与 Qt 侧契约一致）
bool ParseHexColor(std::string_view text, std::uint32_t& out) {
    if (text.size() < 7 || text.front() != '#') {
        return false;
    }
    std::uint32_t value = 0;
    std::size_t digits = 0;
    for (std::size_t i = 1; i < text.size() && i <= 8; ++i) {
        const char c = text[i];
        std::uint32_t nibble = 0;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            break;
        }
        value = (value << 4) | nibble;
        ++digits;
    }
    if (digits != 6 && digits != 8) {
        return false;
    }
    out = (digits == 6) ? (value << 8 | 0xFFu) : value;
    return true;
}

// 点分名 → ColorToken 字段。31 项逐个列出：这张表就是「31 项一次写全」的落点，
// 与 kColorTokenNames 同序，新增 token 必须同时加到这里。
bool AssignByName(std::string_view name, std::uint32_t value, ColorToken& out) {
    const std::array<std::uint32_t ColorToken::*, kColorTokenCount> kFields = {
        &ColorToken::bgVoid, &ColorToken::bgSurface, &ColorToken::bgPanel, &ColorToken::bgElevated,
        &ColorToken::bgOverlay, &ColorToken::lineSubtle, &ColorToken::lineNormal,
        &ColorToken::lineStrong, &ColorToken::textPrimary, &ColorToken::textSecondary,
        &ColorToken::textMuted, &ColorToken::textInverse, &ColorToken::accentPrimary,
        &ColorToken::accentPrimaryHover, &ColorToken::accentPrimaryFg, &ColorToken::accentSecondary,
        &ColorToken::accentInfo, &ColorToken::statusOk, &ColorToken::statusWarn,
        &ColorToken::statusDanger, &ColorToken::statusBusy, &ColorToken::statusIdle,
        &ColorToken::statusPending, &ColorToken::fillHover, &ColorToken::fillSelected,
        &ColorToken::fillMuted, &ColorToken::lineFocus, &ColorToken::shadowScrim,
        &ColorToken::shadow1, &ColorToken::shadow2, &ColorToken::shadowAccent,
    };
    for (std::size_t i = 0; i < kColorTokenCount; ++i) {
        if (kColorTokenNames[i] == name) {
            out.*(kFields[i]) = value;
            return true;
        }
    }
    return false;
}

std::string ToHex(std::uint32_t rgba) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%08X", rgba);
    return buffer;
}

} // namespace detail

} // namespace shine::theme
