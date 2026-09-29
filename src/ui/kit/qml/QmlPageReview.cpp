#include "ui/kit/qml/QmlPageReview.h"

#include "ui/pages/assets/AssetPageModel.h"

#include "ui/kit/qml/QuickHost.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQmlError>
#include <QScreen>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <QQmlContext>
#include <QQuickItem>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace shine::qml {
namespace {

// 页面注册表。**共享层**：页面作者提供「页面名 + 资源路径 + 画布尺寸」，
// 这里只登记，不要在页面自己的改动里改这个表（并发改会冲突）。
//
// ⚠️ w/h 是**harness 视口**，不是设计稿常量。webui 是响应式 flex 布局
// （`base.css:107 .grow { flex: 1 1 auto; min-width: 0 }`），设计稿里根本没有
// "整页画布宽"这个数；外壳固定的是 `.topbar 46px` / `.statusbar 26px`
// / `.sidepanel 240px` 这类**部件**尺寸。
//
// 所以对齐判据是**部件级几何**（逐条对 CSS 的 px 值），不是整页像素 diff ——
// 拿 harness 视口去要求"整页一模一样"是错的目标。业务页统一 1440x900
// 是为了让各页截图在同一条件下可比；Parity 是控制页，用它自己的 1100x720。
const std::vector<PageEntry> kPages{
    // 设计稿对齐验证页（架构层自检；对照 webui tokens.css + ui.css）
    {"parity", ":/qt/qml/Parity.qml", 1100, 720, false},
    // —— 业务页：与 webui/src/views/*.jsx 一一对应 ——
    {"gallery", ":/qt/qml/Gallery.qml", 1440, 900, false},
    // assets 已接入产品（QmlAssetsPage），数据源是 C++ 注入的 `Page`。
    // 取证时注入一个**未打开书库**的模型，于是出的是空态图 —— 这是真实行为，
    // 有数据的形态由 P05 的产品取证负责，这里不再造假数据。
    {"assets", ":/qt/qml/Assets.qml", 1440, 900, true},
    {"storyboard", ":/qt/qml/Storyboard.qml", 1440, 900, false},
    {"imageflow", ":/qt/qml/ImageFlow.qml", 1440, 900, false},
};

struct State {
    std::filesystem::path dir;
    std::vector<std::string> manifest;
    // 失败计数。⚠️ 以前所有失败（宿主被裁剪 / QML 加载失败 / 空图 / 纯色图）
    // 只写进 manifest 那一行，然后无条件 _Exit(0) —— 恒 0 的退出码等于没验收：
    // harness 按退出码判会吃假绿，按 `overall:` 解析则整行缺失、像「没跑」。
    int failures = 0;
};

// 取证超采样倍率（上限）。见 QuickHost::SetSupersample 的注释：
// 软件场景图后端下 Rectangle 圆角是细分多边形，放大看可见斜切面；
// 超采样 + SmoothTransformation 缩回可让圆弧和文字都干净，且运行时零成本。
//
// ⚠️ **不能写死**：宿主窗口尺寸 = 页宽 × N，而窗口不能超过桌面。本机桌面逻辑尺寸
// 上限约 2304×1440，所以 1440×900 的业务页在 2× 下（2880×1800）**会被系统裁剪**，
// 根 item 于是拿到「比预期小」的布局，出图再缩回就是把错误比例放大 ——
// 文字发虚、间距错位，manifest 却仍报 saved。
// 因此倍率按页算，见 SupersampleFor()；下面的 ClampCheck 兜底。
constexpr int kSupersampleMax = 2;

// 按桌面尺寸算出这一页能用的倍率（1..kSupersampleMax）。
int SupersampleFor(int page_w, int page_h) {
    if (qApp == nullptr) {
        return 1;
    }
    const QScreen* screen = qApp->primaryScreen();
    if (screen == nullptr) {
        return 1;
    }
    // availableGeometry 是逻辑像素，与 QWidget::size() 同单位。
    const QRect avail = screen->availableGeometry();
    if (avail.width() <= 0 || avail.height() <= 0) {
        return 1;
    }
    int n = kSupersampleMax;
    while (n > 1 &&
           (page_w * n > avail.width() || page_h * n > avail.height())) {
        --n;
    }
    return n;
}

// filter 形如 "a,b,c"，命中任意一段即算选中。
// ⚠️ 别只比 substr(0, firstComma) —— 那样 "a,b,c" 永远只认 "a"，
// 后面几页会被静默跳过、manifest 里连一行都没有。
bool MatchesFilter(std::string_view filter, std::string_view name) {
    if (filter.empty()) {
        return true;
    }
    std::size_t pos = 0;
    while (pos <= filter.size()) {
        const std::size_t comma = filter.find(',', pos);
        const std::string_view seg = filter.substr(
            pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        if (seg == name) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return false;
}

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

// 报一条并继续（不中断整批取证）：单页失败不该让其余页的证据也丢。
void ReportFailure(State* state, const std::string& page, const std::string& detail) {
    ++state->failures;
    state->manifest.push_back("qml-" + page + " 0 FAILED " + detail);
    std::printf("[qml-review] FAILED %s : %s\n", page.c_str(), detail.c_str());
    std::fflush(nullptr);
}

// ⚠️⚠️ **宿主必须留到进程结束，绝不能在循环里逐页 delete。**
// （这条注释的旧版本写的是「一页一宿主、拍完即弃」，**那是错的** ——
//   那个写法恰恰就是崩溃路径本身：第 2 页照样要 new 第二个 QuickHost。）
//
// 实测（gdb 调用栈）：storyboard 拍完 → delete host → imageflow 建宿主 →
// 在 QuickHost::Load 的 setSource（QuickHost.cpp:114）里 SIGSEGV，
// 栈上 15 层全在 Qt6Qml 的类型实例化路径：
//   #15 Qt6QuickWidgets → #14..#0 全是 Qt6Qml
// 单页各自单独跑都是 exit 0，**只有同进程连拍第 2 页必崩**。
// 改成把宿主存进 LiveHosts() 不析构后，5 页连拍 25 张图全部 saved、exit 0。
//
// 代价：5 个 QQuickWidget 顶窗同时活着（software 后端，约几百 MB）。
// 本机 2304×1440 桌面下 5 × 1440×900 放得下，且 Run() 末尾是 std::_Exit(0)，
// 本来也不会走析构 —— 内存不构成问题。
//
// 为什么必须留活：本文件的注册修复（见 QuickHost.cpp 构造函数的注释）
// 保证了**每个新引擎**都能重新注册 ThemeBridge 类型，但销毁 QQuickWidget
// 连带销毁其内部 QQmlEngine 后，新引擎的 QML 引擎在这台机器 + 这个
// Qt 6.11.2 构建上会崩在类型实例化里，发生在我们能控制的代码之外。
// 既然引擎一死必崩，就让引擎别死。
std::vector<QuickHost*>& LiveHosts() {
    static std::vector<QuickHost*> v;
    return v;
}

void ShootPage(State* state, const PageEntry& page) {
    const QString res = QString::fromStdString(std::string{page.res});
    auto* host = new QuickHost();
    // 宿主按 N 倍尺寸建，QML 根 item 仍是逻辑尺寸（QuickHost 内部 setScale）。
    // N 按桌面算，见 SupersampleFor。
    const int ss = SupersampleFor(page.w, page.h);
    const QSize want{page.w * ss, page.h * ss};
    host->setFixedSize(want);
    host->SetSupersample(ss);
    host->show();

    // ⚠️ 窗口仍可能被系统裁剪（多屏、缩放策略、setFixedSize 被主题/Qt 改写等）。
    // 一旦裁剪，根 item 拿到的是「比预期小」的布局，再缩回页宽就是
    // **把错误比例的布局放大** —— 文字发虚、间距错位，但 manifest 仍会报 saved。
    // **必须在这里判失败**，不能拿一张比例错误的图当验收证据。
    if (host->size() != want) {
        ReportFailure(state, std::string{page.name},
                      "host-clamped want=" + std::to_string(want.width()) + "x" +
                          std::to_string(want.height()) + " got=" +
                          std::to_string(host->size().width()) + "x" +
                          std::to_string(host->size().height()) +
                          " (超采样倍率超过桌面尺寸，请调低 kSupersampleMax)");
        LiveHosts().push_back(host);
        return;
    }

    // 已接入产品的页面需要 `Page` 上下文属性，否则运行期 ReferenceError。
    // 必须在 Load 之前设：setSource 之后根对象才存在，但绑定求值发生在那之前。
    if (page.needs_page_model) {
        host->rootContext()->setContextProperty(QStringLiteral("Page"), new shine::app::AssetPageModel(host));
    }

    if (!host->Load(res)) {
        // ⚠️ 必须把 errors() **全部**打进 manifest，不能只取第一条：
        // QML 的失败常常是多条级联的（一个类型没解析 → 它的每个使用点各报一条），
        // 只看第一条会让人以为是别的问题。
        std::string detail = "load-failed status=" +
                             std::to_string(static_cast<int>(host->status()));
        for (const QQmlError& e : host->errors()) {
            detail += " | " + e.toString().toUtf8().toStdString();
        }
        ReportFailure(state, std::string{page.name}, detail);
        LiveHosts().push_back(host);
        return;
    }
    // 400ms > 最长动效 320ms，让 Behavior 补间跑到终态再抓（否则抓到上一套主题的颜色）
    host->Pump(400);

    for (const auto id : theme::kAllThemes) {
        const std::string suffix{theme::ThemeFileName(id)};
        std::printf("[qml-review] %s / %s\n", std::string{page.name}.c_str(), suffix.c_str());
        std::fflush(nullptr);
        // 切主题必须走 ThemeService：它会 ApplyQss + 通知桥层重算颜色绑定。
        // 第一轮之外都**不重新 setSource** —— 走的正是要验证的热切换路径。
        theme::ThemeService::Switch(id, false);
        host->Pump(400);

        const QImage shot = host->GrabBlocking(5000, QSize{page.w, page.h});
        const std::string stem = "qml-" + std::string{page.name} + "-" + suffix;
        const std::filesystem::path path = state->dir / (stem + ".png");
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
        state->manifest.push_back(stem + " " + std::to_string(bytes.value_or(std::string{}).size()) +
                                  " " + verdict);
        const bool failed = !ok || verdict.rfind("FAILED", 0) == 0;
        if (failed) {
            ++state->failures;
            std::printf("[qml-review]   %s -> %s\n", stem.c_str(), verdict.c_str());
            std::fflush(nullptr);
        }
    }
    LiveHosts().push_back(host);
}

void Run(State* state, std::string_view filter) {
    for (const PageEntry& page : Pages()) {
        if (!MatchesFilter(filter, page.name)) {
            continue;
        }
        ShootPage(state, page);
    }

    // 一页都没匹配上 filter 是个真错误，不是「跑完了没图」。
    if (state->manifest.empty()) {
        state->failures += 1;
        state->manifest.push_back("no page matched filter (SHINE_QML_PAGES 拼错了?) FAILED");
    }

    const bool ok = state->failures == 0;
    std::string report = "QML page review (theme matrix, one host per page, live theme switch)\n";
    if (!filter.empty()) {
        report += "filter: " + std::string{filter} + "\n";
    }
    for (const auto& line : state->manifest) {
        report += line + "\n";
    }
    report += "failures=" + std::to_string(state->failures) + "\n";
    report += std::string("overall=") + (ok ? "PASS" : "FAIL") + "\n";
    (void)util::WriteFileBytes(state->dir / "shots-manifest.txt", report);
    std::printf("%s", report.c_str());
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
}

} // namespace

const std::vector<PageEntry>& Pages() { return kPages; }

void SaveQmlPageReview(const std::filesystem::path& dir, std::string_view filter) {
    auto* state = new State;
    state->dir = dir;
    util::EnsureDir(dir);
    QTimer::singleShot(0, qApp, [state, filter] { Run(state, filter); });
}

} // namespace shine::qml
