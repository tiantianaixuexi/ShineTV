#include "ui/kit/theme/ThemeService.h"

#include "core/Log.h"
#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Palette.h"
#include "ui/kit/theme/QssBuilder.h"
#include "ui/kit/theme/Token.h"
#include "util/Encoding.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QProperty>
#include <QStandardPaths>
#include <QString>
#include <QVariantAnimation>
#include <QWidget>

namespace shine::theme {
namespace {

std::array<std::string, 4> g_qssCache{};      // 4 张 QSS 预生成常驻（风险表 R6）
std::array<bool, 4> g_qssBuilt{};
bool g_initialized = false;

[[nodiscard]] std::size_t IndexOf(ThemeId id) { return static_cast<std::size_t>(id); }

[[nodiscard]] QString ConfigPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/theme.json");
}

[[nodiscard]] std::filesystem::path CustomThemesDir() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return std::filesystem::path{dir.toStdWString()} / L"themes";
}

// {"theme": "<名字>", "custom": true/false, "reduce": true/false}
// 手写读写足够（单键小文件，内容恒为 UTF-8）；yyjson 不必上
[[nodiscard]] std::string PayloadOf(const std::string& name, bool custom, bool reduce) {
    return std::string{"{\"theme\": \""} + name + "\", \"custom\": " + (custom ? "true" : "false") +
           ", \"reduce\": " + (reduce ? "true" : "false") + "}\n";
}

void LoadPersisted() {
    QFile f{ConfigPath()};
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const std::string text = f.readAll().toStdString();
    const std::string_view key = "\"theme\": \"";
    if (const std::size_t pos = text.find(key); pos != std::string::npos) {
        const std::size_t begin = pos + key.size();
        const std::size_t end = text.find('"', begin);
        if (end != std::string::npos) {
            const std::string name{text.substr(begin, end - begin)};
            ThemeId id{};
            if (ThemeIdFromName(name, id)) {
                SetCurrentTheme(id);
                RevertToBuiltin();
            } else {
                (void)ActivateCustom(name); // 自定义主题：重启后仍可选（P02-S8 判据）
            }
        }
    }
    if (text.find("\"reduce\": true") != std::string::npos) {
        ::shine::motion::SetReduceMotion(true); // 「减少动效」持久化（P02-S8）
    } else if (text.find("\"reduce\": false") != std::string::npos) {
        ::shine::motion::SetReduceMotion(false);
    }
}

} // namespace

// {"theme": "<名字>", "custom": …, "reduce": …} —— 主题选择 + 减少动效一并持久化
void ThemeService::Persist(ThemeId) { PersistCurrent(); }

void ThemeService::PersistCurrent() {
    const bool custom = CurrentIsCustom();
    const std::string name = custom ? CurrentCustomName() : std::string{ThemeFileName(CurrentThemeId())};
    const std::string payload = PayloadOf(name, custom, ::shine::motion::ReduceMotion());
    QFile f{ConfigPath()};
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log::Error("主题选择持久化失败：{}", ConfigPath().toStdString());
        return;
    }
    f.write(payload.data(), static_cast<qint64>(payload.size()));
}

void ThemeService::SetReduceMotionPersisted(bool on) {
    ::shine::motion::SetReduceMotion(on);
    PersistCurrent();
}

const std::string& ThemeService::QssFor(ThemeId id) {
    const std::size_t i = IndexOf(id);
    if (!g_qssBuilt[i]) {
        g_qssCache[i] = QssBuilder::Build(ThemeColorsOf(id));
        g_qssBuilt[i] = true;
    }
    return g_qssCache[i];
}

void ThemeService::Initialize(QApplication& app) {
    if (!g_initialized) {
        // 主题数据若未载入则补载（幂等）；内置 = <exe目录>/themes，自定义 = 应用配置目录/themes
        const std::filesystem::path dir =
            std::filesystem::path{app.applicationDirPath().toStdWString()} / L"themes";
        (void)LoadThemesFrom(dir);
        (void)LoadCustomThemes(CustomThemesDir());
        LoadPersisted();
        g_initialized = true;
    }
    // 应用 QSS（含自定义主题激活态；不播动画）
    if (CurrentIsCustom()) {
        ApplyQss(QssBuilder::Build(::shine::theme::Current()), false);
    } else {
        Switch(CurrentThemeId(), false);
    }
}

ThemeId ThemeService::Current() { return CurrentThemeId(); }

void ThemeService::ApplyQss(const std::string& qss, bool animated) {
    QApplication* app = QApplication::instance() != nullptr ? qApp : nullptr;
    if (app == nullptr) {
        PersistCurrent();
        return;
    }

    // 换肤包夹：更新禁用 → setStyleSheet → 更新恢复（风险表 R6：无闪烁/无残影）
    // 调色板与 QSS 同源同批下发：QSS 没命中的绘制路径也必须跟主题走（见 Palette.h）。
    const QList<QWidget*> tops = QApplication::topLevelWidgets();
    for (QWidget* w : tops) {
        w->setUpdatesEnabled(false);
    }
    app->setPalette(PaletteFor(::shine::theme::Current()));
    app->setStyleSheet(QString::fromStdString(qss));
    for (QWidget* w : tops) {
        w->setUpdatesEnabled(true);
    }

    // 200ms 淡入（时长从 motion token 取；「减少动效」打开时显式服从开关，
    // 保证 §6-4「任何动画不再出现」）
    if (QWidget* win = app->activeWindow(); animated && win != nullptr &&
                                           !::shine::motion::ReduceMotion()) {
        auto* anim = new QVariantAnimation(win);
        anim->setDuration(motion::kDurBaseMs);
        anim->setStartValue(0.85);
        anim->setEndValue(1.0);
        QObject::connect(anim, &QVariantAnimation::valueChanged, win,
                         [win](const QVariant& v) { win->setWindowOpacity(v.toDouble()); });
        QObject::connect(anim, &QVariantAnimation::finished, anim, &QObject::deleteLater);
        anim->start();
    }
}

void ThemeService::Switch(ThemeId id, bool animated) {
    SetCurrentTheme(id);
    RevertToBuiltin();
    ApplyQss(QssFor(id), animated);
    PersistCurrent();
}

void ThemeService::SwitchCustom(const std::string& name, bool animated) {
    if (!ActivateCustom(name)) {
        log::Error("自定义主题不存在：{}", name);
        return;
    }
    ApplyQss(QssBuilder::Build(::shine::theme::Current()), animated);
    PersistCurrent();
}

bool ThemeService::SaveCustom(const std::string& name, const ColorToken& c) {
    return SaveCustomTheme(CustomThemesDir(), name, c);
}

void ThemeService::Preview(const ColorToken& c) {
    // 实时预览：直接下发该 Token 组的 QSS + 调色板，不动当前主题、不持久化
    QApplication* app = QApplication::instance() != nullptr ? qApp : nullptr;
    if (app != nullptr) {
        app->setPalette(PaletteFor(c));
        app->setStyleSheet(QString::fromStdString(QssBuilder::Build(c)));
    }
}

void ThemeService::RevertPreview() {
    QApplication* app = QApplication::instance() != nullptr ? qApp : nullptr;
    if (app != nullptr) {
        app->setPalette(PaletteFor(::shine::theme::Current()));
        app->setStyleSheet(QString::fromStdString(QssBuilder::Build(::shine::theme::Current())));
    }
}

} // namespace shine::theme
