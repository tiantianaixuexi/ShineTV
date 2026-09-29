#pragma once
// ui/kit/qml/QuickHost —— QML 页面在 Widgets 树里的宿主（2026-09-29 架构层）
//
// 定位：**QML 与既有 Widgets 共存**的接缝。QQuickWidget 本身是个 QWidget，
// 所以它能直接放进 QStackedLayout / QSplitter / 任何既有布局里，不需要改外壳。
//
// 三条硬约束（都来自本机的实测，不是推测）：
//
// 1. **软件后端必须在 QApplication 构造前设定**。
//    本机无可交互桌面会话（窗口 IsWindowVisible=false、PrintWindow 纯黑），
//    OpenGL/D3D 场景图起不来。AppEntry::RunApp 在建 QApplication 之前调
//    `QuickHost::ConfigureSceneGraphBackend()` 置 QT_QUICK_BACKEND=software。
//    ⚠️ 事后用 QQuickWindow::setSceneGraphBackend() 改是**不生效**的
//    （环境变量只在引擎初始化时读一次）—— 这个坑很容易踩。
//
// 2. **离屏抓图走 QQuickWidget::grabWidget()，不是 QWidget::grab()**。
//    QWidget::grab() 对 QQuickWidget 只会拿到一个空/黑的 QPixmap：它走的是
//    QWidget 的绘制路径，不触发场景图渲染。QuickHost::GrabToImage() 封装了
//    正确路径，并优先用 grabToImage（异步、能拿到场景图真实内容）。
//
// 3. **grab 前必须让场景图跑至少一帧**。
//    与 ReviewProbe::Pump() 同理（那里是「不 repaint 会抓到空帧」的教训）。
//    QuickHost::Pump() 会额外处理场景图帧。
#include <QImage>
#include <QQuickWidget>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>

class QQmlEngine;
class QQuickItem;

namespace shine::qml {

// 进程启动时调用一次：在 QApplication 构造**之前**强制软件场景图后端。
// 幂等；重复调用无副作用。AppEntry 负责放在正确位置。
void ConfigureSceneGraphBackend();

class QuickHost : public QQuickWidget {
    Q_OBJECT

  public:
    explicit QuickHost(QWidget* parent = nullptr, QQmlEngine* engine = nullptr);
    ~QuickHost() override;

    // 加载一个 .qml 文件。失败返回 false，原因写入 errorString()。
    [[nodiscard]] bool Load(const QString& qml_file);

    // 把 QML 根 item 当作内容铺满自身（尺寸变化时同步）。
    void FillWithRoot();

    // —— 抓图（架构层 3 的核心）——
    //
    // 正确路径：QQuickItem::grabToImage()，异步。回调收到 ready 后 image() 有效。
    // done(qimage, ok) 必定在 GUI 线程被调用一次（成功或失败）。
    // 失败时 qimage 为 null，ok=false，且已把原因写进 errorString()。
    void GrabToImage(std::function<void(const QImage&, bool)> done);

    // 同步版：内部自己转事件循环等 ready。仅供 harness/启动前的单次取证使用，
    // 绝不要在 paintEvent 或 resizeEvent 里调用（会重入）。
    [[nodiscard]] QImage GrabBlocking(int timeout_ms = 3000);

    // 排干 UI 队列 + 事件 + **场景图帧**（harness 的 Pump 在 Widgets 侧，
    // QML 侧这一条是额外的：场景图有独立的 render loop）
    //
    // settle_ms > 0 时额外转事件循环等这么久，让 `Behavior on <color>` 这类
    // 补间动画跑到终态。**抓图前必须给足**（≥ 最长动效时长 320ms），
    // 否则抓到的是动画起点 —— 也就是上一套主题的颜色。
    void Pump(int settle_ms = 0);

  protected:
    void resizeEvent(QResizeEvent* ev) override;

  private:
    std::unique_ptr<QQmlEngine> owned_engine_;
};

} // namespace shine::qml
