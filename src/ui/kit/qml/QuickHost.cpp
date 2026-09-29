#include "ui/kit/qml/QuickHost.h"

#include "ui/kit/qml/ThemeBridge.h"
#include "ui/kit/theme/Theme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QImage>
#include <QJSEngine>  // qmlRegisterSingletonType 的工厂签名用到 QJSEngine*
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QTimer>
#include <QUrl>

#include <cstdlib>

namespace shine::qml {
namespace {

// 工厂定义在 QuickHost 构造函数之前 —— 构造函数里要取它的地址。
QObject* CreateThemeBridge(QQmlEngine*, QJSEngine*) { return &Instance(); }

// 主题层换肤漏斗 → QML 绑定重算。注册在 QuickHost 构造时（第一次建宿主）。
//
// ⚠️ 这一步不做，QML 页面会**永远停在载入那一刻的主题**：ThemeService::Switch
// 只换 QSS 与调色板，不会碰 QML。实测症状很有欺骗性 —— 5 套主题都能出图、
// PNG 大小还各不相同（float-y 动画每帧相位不同），肉眼看文件大小以为"生效了"，
// 打开图才发现标题和配色全是第一套。
void OnThemeChanged() { ThemeBridge::NotifyThemeChanged(); }

} // namespace

void ConfigureSceneGraphBackend() {
    // ⚠️ 必须在 QApplication 构造前调用（见头文件约束 1）。
    // QT_QUICK_BACKEND=software 让场景图走 CPU 光栅化，是本机唯一能出图的路径
    // （实测：QQuickWindow::grabWindow() 与 QQuickItem::grabToImage() 均 PASS）。
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
    }
}

QuickHost::QuickHost(QWidget* parent, QQmlEngine* engine) : QQuickWidget(parent) {
    // 透明底：让宿主能融进既有 Widgets 布局（页面自绘背景）
    setClearColor(Qt::transparent);
    // resizeContentsToView：QML 根 item 跟随宿主尺寸，避免手写 resizeEvent 同步
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    if (engine == nullptr) {
        owned_engine_ = std::make_unique<QQmlEngine>();
        engine = owned_engine_.get();
    }
    // 主题桥注册为 QML 单例，让任何上下文（含 Flow/Loader 之外的子上下文）
    // 都能 `import Shine 1.0` 后直接用。
    //
    // ⚠️⚠️ **模板实参 `<ThemeBridge>` 绝对不能省** —— 这是本文件踩过的最贵的坑。
    // qqml.h 里有两个同名重载：
    //   ① template<typename F>              … F→std::function<QJSValue(QQmlEngine*,QJSEngine*)>
    //   ② template<typename T, typename F>   … F→std::function<QObject *(QQmlEngine*,QJSEngine*)>
    // 不写 <ThemeBridge> 时 T 推导不出来，重载 ② 直接被 SFINAE 排除，只剩 ① 匹配。
    // 而重载 ① 传给引擎的 metaObject 是 nullptr、metaType 是空 QMetaType()
    // （qqml.h:692-705）—— 于是 QML 能解析到单例（不报 ReferenceError），
    // 却拿不到任何元方法，症状是
    //   TypeError: Property 'token' of object Shine/ThemeBridge is not a function
    // 显式给了 <ThemeBridge> 才命中重载 ②，staticMetaObject 与 QmlMetaType 都被填上。
    //
    // 另外两条踩过的路，别走回头路：
    //  1. setContextProperty —— 只在该 engine 的**根上下文**可见，复合类型
    //     （Card/Tag/Button 等独立编译的 .qml）拿不到，实测 `ReferenceError: Theme is not defined`。
    //  2. QML_SINGLETON 宏 —— 那套宏要求引擎自己构造单例（类要有默认构造、由
    //     注册代码 new 出来），与本桥「进程唯一实例、由 ThemeService 主动通知」
    //     的模型冲突，moc 生成的注册代码会盖掉 Q_INVOKABLE 的暴露。
    //
    // **必须按引擎重新注册**（用 qmlTypeId 判重），不能 std::call_once 只注册一次。
    // QQuickWidget 每个实例自带一个内部 QQmlEngine（无法注入），引擎一析构就
    // 连带注销它用到的 C++ 类型注册 —— 于是第 2 个引擎能 import 到 Shine 模块，
    // 却查不到 ThemeBridge 这个类型，直接在 setSource 里访问违例
    // （实测 0xC0000005，崩在第 2 套主题的 Load）。
    //
    // 另一半修复在 ThemeBridge::Instance()：实例**永不析构**。工厂返回的是
    // &Instance()，引擎若把它 delete 掉，函数内 static 会被提前析构，
    // 下一个引擎拿回一个已析构的对象 → 堆损坏（实测 0xC0000374）。
    // 「按引擎重注册」+「实例永不回收」两条必须同时成立，缺一不可。
    if (qmlTypeId("Shine", 1, 0, "ThemeBridge") == -1) {
        qmlRegisterSingletonType<ThemeBridge>("Shine", 1, 0, "ThemeBridge", &CreateThemeBridge);
    }

    // 订阅主题变更：theme 层的换肤漏斗回调到桥，桥发 themeChanged，QML 绑定重算
    theme::SetThemeChangedObserver(&OnThemeChanged);

    // 探测场景图后端：software 后端下 layer.effect 会把 item 整个吞掉，
    // 必须让 QML 侧知道，否则它会把「阴影没画出来」误当成「控件画对了」。
    if (auto* win = quickWindow(); win != nullptr) {
        const QString backend = win->sceneGraphBackend();
        ThemeBridge::SetLayerEffectsAvailable(!backend.isEmpty() &&
                                              backend.compare(QLatin1String("software"),
                                                              Qt::CaseInsensitive) != 0);
    }
}

QuickHost::~QuickHost() = default;

bool QuickHost::Load(const QString& qml_file) {
    const QUrl url = QUrl::fromLocalFile(qml_file);
    if (!url.isValid()) {
        return false;
    }
    // ⚠️ setSource 返回 void（异步加载）。**加载后立刻读 status() 必然不是 Ready**，
    // 直接判 status 会把每次加载都误判成失败（症状：`invalid root object`）。
    // 正确做法是转事件循环等 statusChanged 落到 Ready/Error 终态。
    setSource(url);
    if (status() != QQuickWidget::Status::Ready) {
        QEventLoop loop;
        const QMetaObject::Connection conn =
            connect(this, &QQuickWidget::statusChanged, &loop, [&loop](QQuickWidget::Status s) {
                if (s == QQuickWidget::Status::Ready || s == QQuickWidget::Status::Error) {
                    loop.quit();
                }
            });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        disconnect(conn);
    }
    if (status() != QQuickWidget::Status::Ready) {
        return false;
    }
    // 场景图后端可能到这时才就绪，构造时那次探测拿到的还是空串
    if (auto* win = quickWindow(); win != nullptr) {
        const QString backend = win->sceneGraphBackend();
        ThemeBridge::SetLayerEffectsAvailable(!backend.isEmpty() &&
                                              backend.compare(QLatin1String("software"),
                                                              Qt::CaseInsensitive) != 0);
    }
    FillWithRoot();
    return true;
}

void QuickHost::SetSupersample(int factor) {
    const int f = factor < 1 ? 1 : factor;
    if (supersample_ == f) {
        return;
    }
    supersample_ = f;
    FillWithRoot();
}

void QuickHost::FillWithRoot() {
    // rootObject() 本身就直接是 QQuickItem*（QQuickWidget 的 contentItem 就是它），
    // 不是 QQuickWindow —— 没有 contentItem() 这一层。
    if (auto* root = rootObject()) {
        // 超采样下：根 item 保持**逻辑尺寸**（否则布局会按放大后的尺寸重排），
        // 整体 setScale 放大，渲染目标本身已经是 N 倍 —— 于是每个细节都按 N 倍
        // 分辨率光栅化，抓完再缩回就是 N×N SSAA。
        root->setSize(size() / supersample_);
        root->setScale(static_cast<float>(supersample_));
    }
}

void QuickHost::resizeEvent(QResizeEvent* ev) {
    QQuickWidget::resizeEvent(ev);
    FillWithRoot();
}

void QuickHost::GrabToImage(std::function<void(const QImage&, bool)> done) {
    auto* item = rootObject();
    if (item == nullptr) {
        done(QImage(), false);
        return;
    }
    // 场景图独立于 Widgets 的绘制循环；抓图前先让它跑一帧，
    // 否则拿到的是「还没渲染过」的缓冲（WidgetGrab 侧同款教训）。
    Pump();
    QSharedPointer<QQuickItemGrabResult> result = item->grabToImage();
    if (result == nullptr) {
        // item 路径不可用时退回窗口级抓图（QQuickWindow::grabWindow 走场景图，
        // 与 QWidget::grab() 完全不同路径 —— 后者对 QQuickWidget 只会得到黑图）。
        if (auto* win = quickWindow(); win != nullptr) {
            done(win->grabWindow(), true);
            return;
        }
        done(QImage(), false);
        return;
    }
    // 持有 result 到 ready 触发：它是 QML 侧对象，提前释放会拿到空图
    QObject::connect(result.data(), &QQuickItemGrabResult::ready, result.data(),
                     [result, done = std::move(done)] {
                         const QImage img = result->image();
                         done(img, !img.isNull());
                     });
}

QImage QuickHost::GrabBlocking(int timeout_ms, const QSize& out_size) {
    QImage captured;
    bool finished = false;
    GrabToImage([&captured, &finished](const QImage& img, bool ok) {
        captured = img;
        finished = ok;
    });
    // 自己转事件循环等 ready。仅供 harness 的单次取证使用 ——
    // 绝不在 paintEvent/resizeEvent 里调（会重入，见头文件注释）。
    if (!finished) {
        QEventLoop loop;
        QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
        loop.exec();
    }
    // 缩回逻辑尺寸。必须用 SmoothTransformation：NearestNeighbor 等于白超采样，
    // 反而会把锯齿原样放大成色块。
    if (!captured.isNull() && out_size.isValid() && !out_size.isEmpty() &&
        captured.size() != out_size) {
        captured = captured.scaled(out_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return captured;
}

void QuickHost::Pump(int settle_ms) {
    // Widgets 侧：排干 UI 队列 + 事件 + DeferredDelete（与 ReviewProbe::Pump 同源）
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    // 场景图侧：grabToImage 是异步的，让 render loop 有机会推进。
    // ⚠️ 要用 quickWindow()，不能用 QWidget::window() —— 后者返回 QWidget*，
    // 调 update() 只是重排 Widgets 绘制，根本不驱动场景图。
    if (auto* win = quickWindow(); win != nullptr) {
        win->update();
    }
    QCoreApplication::processEvents();

    // settle_ms > 0 时**转事件循环等这么久**。
    //
    // ⚠️ 这一步是抓图正确性的前提，不是优化。QML 的 `Behavior on color` 在主题
    // 切换后会把新色**动画**过去（120/200/320ms），而 processEvents() 只处理
    // 一瞬间的队列 —— 抓到的是动画起点，也就是**上一套主题的颜色**。
    // 症状极隐蔽：色板（无 Behavior）是对的，控件底色/描边（带 Behavior）全是
    // 旧主题的，看起来像"桥只更新了一部分"。settle 要盖过最长的一档动效时长。
    if (settle_ms > 0) {
        QEventLoop loop;
        QTimer::singleShot(settle_ms, &loop, &QEventLoop::quit);
        loop.exec();
        if (auto* win = quickWindow(); win != nullptr) {
            win->update();
        }
        QCoreApplication::processEvents();
    }
}

} // namespace shine::qml
