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

    // 超采样倍率（1 = 原生）。
    //
    // ⚠️ 为什么需要：Qt Quick 的 `Rectangle` 圆角走 QSGRoundedRectNode，是用**固定
    // 细分的多边形**逼近圆弧；软件场景图后端没有 MSAA 去掩盖这些面，于是 3× 放大
    // 能看出亮色块的角被切成斜切多边形。实测 `QQuickItem.antialiasing` 对此**无效**
    // （深空主题出图字节数完全不变，49551 → 49551），`QT_SCALE_FACTOR=1` 也无效
    // （1.25 来自 Windows 桌面 125% 缩放，不是 Qt 的 high-DPI 开关）。
    //
    // 两条路的取舍：
    //   * 换成 Shape + PathQuad —— 确实更平滑（已实测对照），但每个控件多一个几何
    //     节点，运行时成本实打实，而且会动到被页面依赖的 Card/Button/Tag。
    //   * 抓图时超采样 —— 只影响取证路径，**运行时零成本**，且对新写的页面自动生效。
    // 本仓取后者：把宿主放大 N 倍、QML 根 item 保持逻辑尺寸再整体 setScale(N)，
    // 抓完用 SmoothTransformation 缩回逻辑尺寸。等于 2×2 SSAA，圆弧与文字都干净。
    //
    // 用法：构造后 SetSupersample(2)，尺寸要按 2× 设；GrabBlocking 时传 out_size
    // 缩回逻辑尺寸。默认 1 = 产品真实路径，不做超采样。
    void SetSupersample(int factor);
    [[nodiscard]] int supersample() const noexcept { return supersample_; }

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
    //
    // out_size 非空时用 SmoothTransformation 缩放到该尺寸（超采样场景下缩回逻辑
    // 尺寸）；为空则原样返回 supersample 倍的图。
    [[nodiscard]] QImage GrabBlocking(int timeout_ms = 3000, const QSize& out_size = {});

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
    int supersample_ = 1;
};

} // namespace shine::qml
