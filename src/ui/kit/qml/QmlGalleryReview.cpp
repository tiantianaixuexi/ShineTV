#include "ui/kit/qml/QmlGalleryReview.h"

#include "ui/kit/qml/QuickHost.h"
#include "ui/kit/qml/ThemeBridge.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQmlError>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace shine::qml {
namespace {

struct State {
    std::filesystem::path dir;
    std::vector<std::string> manifest;
};

// QML 资源走 qt_add_resources 打进 qrc，运行时从 :/qt/qml/... 加载。
// 不依赖磁盘路径 —— 打包后的 dist 也能跑，且评审环境与开发环境一致。
constexpr const char* kGalleryQrc = ":/qt/qml/Gallery/Gallery.qml";

// 判断整张图是否只有一种颜色。
bool IsFlatColor(const QImage& img) {
    if (img.isNull() || img.width() < 2 || img.height() < 2) {
        return true;
    }
    const QRgb first = img.pixel(0, 0);
    const int step_x = std::max(1, img.width() / 16);
    const int step_y = std::max(1, img.height() / 16);
    for (int y = 0; y < img.height(); y += step_y) {
        for (int x = 0; x < img.width(); x += step_x) {
            if (img.pixel(x, y) != first) {
                return false;
            }
        }
    }
    return true;
}

void Run(State* state) {
    // ⚠️ **只建一次 QuickHost，五套主题在同一棵 QML 树上切** —— 两层原因：
    //
    // 1. 这才是桥该被验证的行为。所有色值绑定都挂在 ThemeBridge 的
    //    themeChanged 上，切主题时只应重算绑定、不该重建页面。
    //    每套主题重建一次宿主，等于根本没测到「热切换」这条主路径。
    // 2. 重建路径在本机就是崩的：第 2 个 QQuickWidget 走 Load 时访问违例
    //    （0xC0000005）。与其把一个跟本次目标无关的引擎生命周期问题
    //    混进端到端链路，不如先用正确的主路径把链路打通。
    auto* host = new QuickHost();
    host->setFixedSize(1100, 720); // 与 Gallery.qml 里的布局尺寸一致
    host->show();

    const QString qrc = QString::fromLatin1(kGalleryQrc);
    if (!host->Load(qrc)) {
        // ⚠️ 必须把 errors() **全部**打进 manifest，不能只取第一条：
        // QML 的失败常常是多条级联的（一个类型没解析 → 它的每个使用点各报一条），
        // 只看第一条会让人以为是别的问题。
        std::string detail = "load-failed status=" +
                             std::to_string(static_cast<int>(host->status()));
        for (const QQmlError& e : host->errors()) {
            detail += " | " + e.toString().toUtf8().toStdString();
        }
        state->manifest.push_back("qml-gallery 0 FAILED " + detail);
        std::printf("%s\n", state->manifest.back().c_str());
        std::fflush(nullptr);
        std::_Exit(1);
    }
    host->Pump();

    for (const auto id : theme::kAllThemes) {
        const std::string suffix{theme::ThemeFileName(id)};
        std::printf("[qml-review] begin %s\n", suffix.c_str());
        std::fflush(nullptr);
        // 切换主题必须走 ThemeService：它会 ApplyQss + 通知桥层重算颜色绑定。
        // 第一轮之外都**不重新 setSource** —— 走的正是要验证的热切换路径。
        theme::ThemeService::Switch(id, false);
        // 400ms > 最长动效 320ms（slow），保证 Behavior 补间跑到终态再抓
        host->Pump(400);

        // ⚠️ 走场景图 grab，不是 QWidget::grab()（后者对 QQuickWidget 得到空图）
        const QImage shot = host->GrabBlocking(5000);
        const std::filesystem::path path = state->dir / ("qml-gallery-" + suffix + ".png");
        const bool ok = !shot.isNull() && shot.save(QString::fromStdString(util::PathToUtf8(path)),
                                                    "PNG");
        const auto bytes = util::ReadFileBytes(path);
        std::string verdict = ok ? "saved" : "FAILED empty-or-unsaved";
        if (ok) {
            // 本机无桌面会话，「图存下来了」不等于「图里有东西」。
            // 场景图若没真正渲染，出来的是一张纯色/全黑图，PNG 照样有几百字节 ——
            // 必须在 harness 里就把它判成失败，否则会拿一张废图当验收证据。
            if (IsFlatColor(shot)) {
                verdict = "FAILED flat-color(" + std::to_string(shot.width()) + "x" +
                          std::to_string(shot.height()) + " single-color, scene graph did not render)";
            }
        }
        state->manifest.push_back("qml-gallery-" + suffix + " " +
                                  std::to_string(bytes.value_or(std::string{}).size()) + " " + verdict);
        std::printf("[qml-review] end   %s -> %s\n", suffix.c_str(), verdict.c_str());
        std::fflush(nullptr);
    }

    std::string report = "QML gallery review (theme matrix, single host / live theme switch)\n";
    for (const auto& line : state->manifest) {
        report += line + "\n";
    }
    (void)util::WriteFileBytes(state->dir / "shots-manifest.txt", report);
    std::printf("%s", report.c_str());
    std::fflush(nullptr);
    std::_Exit(0);
}

} // namespace

void SaveQmlGalleryReview(const std::filesystem::path& dir) {
    auto* state = new State;
    state->dir = dir;
    util::EnsureDir(dir);
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}

} // namespace shine::qml
