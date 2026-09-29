#include "ui/kit/qml/ThemeBridge.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ToneMix.h"

#include <QVariant>
#include <QVariantList>

#include <array>
#include <cstdint>

namespace shine::qml {
namespace {

using theme::ColorToken;

// layer.effect / MultiEffect 是否可用。由 QuickHost 在拿到窗口的场景图后端后写入。
// software 后端下实测会把挂了 layer 的 item **整个吞掉**（Button 的 primary 变体
// 直接不显示），所以这个开关必须是自动探测出来的，不能写死 true。
bool g_layer_effects_ok = false;

// token 点分名 → ColorToken 成员偏移。
// ⚠️ 顺序必须与 Token.h 的 kColorTokenNames 逐位一致（那里是 QSS %N 序号与
// JSON 键序的共同基准）。新增 token 时三处同步：Token.h 字段、kColorTokenNames、
// 本表。漏一处会让该 token 在 QSS 里存在、在 QML 里取不到。
struct TokenEntry {
    const char* name;
    std::size_t offset;
};

constexpr std::array<TokenEntry, theme::kColorTokenCount> kTokens{{
    {"bg.void", offsetof(ColorToken, bgVoid)},
    {"bg.surface", offsetof(ColorToken, bgSurface)},
    {"bg.panel", offsetof(ColorToken, bgPanel)},
    {"bg.elevated", offsetof(ColorToken, bgElevated)},
    {"bg.overlay", offsetof(ColorToken, bgOverlay)},
    {"line.subtle", offsetof(ColorToken, lineSubtle)},
    {"line.normal", offsetof(ColorToken, lineNormal)},
    {"line.strong", offsetof(ColorToken, lineStrong)},
    {"text.primary", offsetof(ColorToken, textPrimary)},
    {"text.secondary", offsetof(ColorToken, textSecondary)},
    {"text.muted", offsetof(ColorToken, textMuted)},
    {"text.inverse", offsetof(ColorToken, textInverse)},
    {"accent.primary", offsetof(ColorToken, accentPrimary)},
    {"accent.primary.hover", offsetof(ColorToken, accentPrimaryHover)},
    {"accent.primary.fg", offsetof(ColorToken, accentPrimaryFg)},
    {"accent.secondary", offsetof(ColorToken, accentSecondary)},
    {"accent.info", offsetof(ColorToken, accentInfo)},
    {"status.ok", offsetof(ColorToken, statusOk)},
    {"status.warn", offsetof(ColorToken, statusWarn)},
    {"status.danger", offsetof(ColorToken, statusDanger)},
    {"status.busy", offsetof(ColorToken, statusBusy)},
    {"status.idle", offsetof(ColorToken, statusIdle)},
    {"status.pending", offsetof(ColorToken, statusPending)},
    {"fill.hover", offsetof(ColorToken, fillHover)},
    {"fill.selected", offsetof(ColorToken, fillSelected)},
    {"fill.muted", offsetof(ColorToken, fillMuted)},
    {"line.focus", offsetof(ColorToken, lineFocus)},
    {"shadow.scrim", offsetof(ColorToken, shadowScrim)},
    {"shadow.1", offsetof(ColorToken, shadow1)},
    {"shadow.2", offsetof(ColorToken, shadow2)},
    {"shadow.accent", offsetof(ColorToken, shadowAccent)},
}};

// 与 ToneMix.h 的下标同序：accent/info/ok/warn/danger/busy/pending
constexpr std::array<const char*, 7> kToneNames{"accent", "info", "ok", "warn",
                                               "danger", "busy", "pending"};

// RRGGBBAA（Token.h 的存储布局）→ QColor
QColor ToQColor(std::uint32_t rgba) {
    return QColor(static_cast<int>((rgba >> 24) & 0xFF), static_cast<int>((rgba >> 16) & 0xFF),
                  static_cast<int>((rgba >> 8) & 0xFF), static_cast<int>(rgba & 0xFF));
}

std::uint32_t FieldOf(const ColorToken& c, std::size_t offset) {
    return *reinterpret_cast<const std::uint32_t*>(
        reinterpret_cast<const std::byte*>(&c) + offset); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

} // namespace

std::uint32_t ToneValue(std::size_t tone_index) noexcept {
    static constexpr std::array<std::size_t, 7> kOffsets{
        offsetof(ColorToken, accentPrimary), offsetof(ColorToken, accentInfo),
        offsetof(ColorToken, statusOk),      offsetof(ColorToken, statusWarn),
        offsetof(ColorToken, statusDanger),  offsetof(ColorToken, statusBusy),
        offsetof(ColorToken, statusPending)};
    if (tone_index >= kOffsets.size()) {
        return 0;
    }
    return FieldOf(theme::Current(), kOffsets[tone_index]);
}

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent) {}

QVariantMap ThemeBridge::colors() const {
    const ColorToken& c = theme::Current();
    QVariantMap m;
    for (const TokenEntry& e : kTokens) {
        m.insert(QString::fromLatin1(e.name), QVariant::fromValue(ToQColor(FieldOf(c, e.offset))));
    }
    return m;
}

QVariantMap ThemeBridge::toneBackgrounds() const {
    QVariantMap m;
    for (std::size_t i = 0; i < kToneNames.size(); ++i) {
        m.insert(QString::fromLatin1(kToneNames[i]),
                 QVariant::fromValue(ToQColor(theme::MixOver(
                     ToneValue(i), theme::Current().bgSurface, theme::kToneBgMixRatio))));
    }
    return m;
}

QVariantMap ThemeBridge::toneEdges() const {
    QVariantMap m;
    for (std::size_t i = 0; i < kToneNames.size(); ++i) {
        m.insert(QString::fromLatin1(kToneNames[i]),
                 QVariant::fromValue(ToQColor(theme::MixOver(
                     ToneValue(i), theme::Current().bgSurface, theme::kToneEdgeMixRatio))));
    }
    return m;
}

QVariantMap ThemeBridge::radii() const {
    return QVariantMap{{QStringLiteral("xs"), theme::radius::kXs},
                       {QStringLiteral("sm"), theme::radius::kSm},
                       {QStringLiteral("md"), theme::radius::kMd},
                       {QStringLiteral("lg"), theme::radius::kLg},
                       {QStringLiteral("xl"), theme::radius::kXl},
                       {QStringLiteral("pill"), theme::radius::kPill}};
}

QVariantMap ThemeBridge::spaces() const {
    // 键用 CSS 的刻度名（"1"…="7"）而不是下标：QML 侧 `spaces["3"]` 自带语义，
    // 下标会让人在 4/8/12 与 0/2/4/8 两套编号之间猜（Token.h 的 kSteps 下标 1
    // 是 Qt 侧历史刻度，设计稿没有对应）。
    QVariantMap m;
    for (int css = 1; css <= 7; ++css) {
        m.insert(QString::number(css), theme::space::kSteps[static_cast<std::size_t>(css) + 1]);
    }
    return m;
}

QVariantMap ThemeBridge::durations() const {
    return QVariantMap{{QStringLiteral("fast"), theme::motion::kDurFastMs},
                       {QStringLiteral("base"), theme::motion::kDurBaseMs},
                       {QStringLiteral("slow"), theme::motion::kDurSlowMs}};
}

int ThemeBridge::baseFontPx() const { return theme::font::kBase; }

QString ThemeBridge::fontFamily() const {
    return QString::fromStdString(std::string{theme::CurrentFontFamily()});
}

bool ThemeBridge::reduceMotion() const { return motion::ReduceMotion(); }

void ThemeBridge::SetLayerEffectsAvailable(bool ok) {
    if (g_layer_effects_ok == ok) {
        return;
    }
    g_layer_effects_ok = ok;
    Q_EMIT Instance().themeChanged();
}

bool ThemeBridge::layerEffectsAvailable() const { return g_layer_effects_ok; }

QString ThemeBridge::themeName() const {
    if (theme::CurrentIsCustom()) {
        return QString::fromStdString(theme::CurrentCustomName());
    }
    return QString::fromStdString(std::string{theme::ThemeDisplayName(theme::CurrentThemeId())});
}

void ThemeBridge::NotifyThemeChanged() {
    // Instance() 是函数内静态，首次调用即构造；这里取地址发信号。
    // 刻意不缓存裸指针到全局 —— 静态局部由程序退出时析构，缓存会在
    // 「先发信号后析构」的顺序问题里留下悬垂引用。
    Q_EMIT Instance().themeChanged();
}

ThemeBridge& Instance() {
    // ⚠️ 刻意 new 且**永不回收**，不能用函数内 static。
    // QML 引擎在自身析构时会清掉它持有的单例注册与实例；工厂返回的是
    // &Instance()，于是「函数内 static 被 delete 掉」，第二个 QuickHost 建新引擎时
    // 工厂把一个已析构的对象交回去 → 堆损坏（实测：第 1 套主题出图正常，
    // 第 2 套主题直接 0xC0000374）。
    // 进程期单例本来就不需要回收，这里是有意泄漏，不是疏忽。
    static ThemeBridge* const instance = new ThemeBridge;
    return *instance;
}

} // namespace shine::qml
