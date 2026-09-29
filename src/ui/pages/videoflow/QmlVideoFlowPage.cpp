#include "ui/pages/videoflow/QmlVideoFlowPage.h"

#include "ui/kit/controls/Surfaces.h"   // widgets::Toast
#include "ui/kit/qml/QuickHost.h"
#include "util/Encoding.h"

#include <QQmlContext>
#include <QQuickItem>
#include <QRect>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <vector>

namespace shine::app {

class VideoFlowPageModel;

namespace {

// ⚠️ **QuickHost 一律不析构**（进程期常驻），照搬 QmlAssetsPage.cpp 的 HostPool()。
//
// 依据是那边踩出来的实测，不是猜测：销毁 QQuickWidget 会连带销毁它内部的
// QQmlEngine，而**之后再新建的引擎会崩在 Qt6Qml 的类型实例化路径里**
// （SIGSEGV，栈上 15 层全在 Qt6QuickWidgets→Qt6Qml）。单页单独跑都 exit 0，
// 只有「销毁旧的 + 建新的」同进程连做才崩。QuickHost 构造时的「按引擎重新
// 注册 ThemeBridge」只解决了类型查不到，解决不了引擎析构后的堆状态。
//
// 本机是 Qt 6.11.2 的 software 后端；换硬件/GPU 后端后可以重新验证这条约束
// 并考虑改回「跟随页面析构」。
std::vector<shine::qml::QuickHost*>& HostPool() {
    static std::vector<shine::qml::QuickHost*> pool;
    return pool;
}

} // namespace

QmlVideoFlowPage::QmlVideoFlowPage(QWidget* parent) : QWidget(parent) {
    // ⚠️ model_ **故意不给页面做 parent**：QuickHost 常驻不析构（见 HostPool），
    //    它的 QQmlContext 里存着 `Page` 这个 QObject*。模型若随页面析构，
    //    那个 context property 就变成悬垂指针（关掉再开一个出片页后，
    //    旧引擎仍在、指向已释放的对象）。跟 QuickHost 一起活到进程结束，
    //    代价是每关一次页签漏一个几百字节的小对象 —— 换掉一整类悬垂引用。
    model_ = new VideoFlowPageModel();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* host = new shine::qml::QuickHost(); // 无 parent：故意不析构
    host->FillWithRoot();
    HostPool().push_back(host);
    content_ = host;
    // ⚠️ 顺序是硬要求：先注入 Page，再 Load。
    // Load() 内部会同步创建 QML 根对象并**首次求值所有绑定**，那一刻
    // `Page` 还没进上下文 → 绑定求值成 undefined 且被永久缓存，
    // 之后 setContextProperty 再也救不回来（表现是一屏 "Page is not defined"，
    // 而 QQuickWidget 自己一声不吭）。QmlAssetsPage 同一纪律。
    content_->rootContext()->setContextProperty(QStringLiteral("Page"), model_);
    if (!content_->Load(QStringLiteral(":/qt/qml/VideoFlow.qml"))) {
        QString detail;
        for (const QQmlError& e : content_->errors()) {
            detail += e.toString() + QLatin1String(" | ");
        }
        // 载入失败必须立刻可见：QML 页面加载失败是静默的（控件一片空白），
        // 只在日志里留一行会让「页面没东西」和「数据为空」难以区分。
        widgets::Toast::Show(QStringLiteral("QML 页面载入失败：%1").arg(detail.left(200)),
                             widgets::Toast::Tone::Error);
    }
    lay->addWidget(content_, 1);
    PumpFrame();
}

QmlVideoFlowPage::~QmlVideoFlowPage() {
    // QuickHost 故意不 delete（见 HostPool 的注释）。
    content_->setParent(nullptr);
    content_->hide();
}

void QmlVideoFlowPage::PumpFrame() { content_->Pump(0); }

void QmlVideoFlowPage::SetContext(std::filesystem::path dbPath, std::filesystem::path projectDir) {
    model_->SetContext(std::move(dbPath), std::move(projectDir));
    PumpFrame();
}

void QmlVideoFlowPage::LoadMock() {
    model_->loadMock();
    PumpFrame();
}

void QmlVideoFlowPage::Validate() {
    model_->validate();
    PumpFrame();
}

void QmlVideoFlowPage::TogglePanel() {
    model_->togglePanel();
    PumpFrame();
}

void QmlVideoFlowPage::SetTab(const QString& key) {
    model_->setTab(key);
    PumpFrame();
}

void QmlVideoFlowPage::SelectNode(const QString& id) {
    model_->selectNode(id);
    PumpFrame();
}

void QmlVideoFlowPage::EnqueueAll() {
    model_->enqueueAll();
    PumpFrame();
}

void QmlVideoFlowPage::ShowRunning() {
    model_->showRunning();
    PumpFrame();
}

void QmlVideoFlowPage::SetChain(flow::VideoChain chain) {
    model_->SetChain(std::move(chain));
    PumpFrame();
}

void QmlVideoFlowPage::SetShots(VideoShotList shots) {
    model_->SetShots(std::move(shots));
    PumpFrame();
}

void QmlVideoFlowPage::SetVideos(VideoShotList videos) {
    model_->SetVideos(std::move(videos));
    PumpFrame();
}

void QmlVideoFlowPage::SetFilmCells(std::vector<VideoFilmCellFact> cells) {
    model_->SetFilmCells(std::move(cells));
    PumpFrame();
}

void QmlVideoFlowPage::SetFirstFrame(std::int64_t shotId, const std::filesystem::path& path) {
    model_->setFirstFrame(static_cast<int>(shotId), QString::fromStdString(util::PathToUtf8(path)));
    PumpFrame();
}

void QmlVideoFlowPage::PickFilmCell(int index) {
    model_->pickFilmCell(index);
    PumpFrame();
}

bool QmlVideoFlowPage::ExportScene(const std::filesystem::path& dir) {
    const bool ok =
        model_->exportSceneTo(QString::fromStdString(util::PathToUtf8(dir)));
    PumpFrame();
    return ok;
}

QString QmlVideoFlowPage::Probe() const { return model_->Probe(); }
QString QmlVideoFlowPage::ChainProbe() const { return model_->ChainProbe(); }
QString QmlVideoFlowPage::TaskProbe() const { return model_->TaskProbe(); }
QString QmlVideoFlowPage::FinalProbe() const { return model_->FinalProbe(); }
QString QmlVideoFlowPage::FilmProbe() const { return model_->FilmProbe(); }
QString QmlVideoFlowPage::GraphProbe() const { return model_->GraphProbe(); }
int QmlVideoFlowPage::NodeCount() const { return model_->NodeCount(); }
int QmlVideoFlowPage::LinkCount() const { return model_->LinkCount(); }
QString QmlVideoFlowPage::ActiveTab() const { return model_->ActiveTab(); }

QImage QmlVideoFlowPage::GrabRegion(const QString& region) {
    // 走 QuickHost::GrabBlocking（场景图路径），不是 QWidget::grab() ——
    // 原因见 ReviewProbe.h 的注释：QQuickWidget 的画面不在 Widgets 绘制链上。
    QImage shot = content_->GrabBlocking(3000, content_->size());
    if (shot.isNull() || region.isEmpty()) {
        return shot;
    }
    QObject* root = content_->rootObject();
    if (root == nullptr) {
        return shot;
    }
    const QRect rect = root->property(("region" + region).toUtf8().constData()).toRect();
    // 几何是 QML 自己报的（页面坐标系），这里只做一次边界收敛：
    // QML 还没量出尺寸时 rect 是空的，那就不裁（宁可给整页，也不给一张 0×0 的图）。
    if (rect.isEmpty()) {
        return shot;
    }
    return shot.copy(rect.intersected(QRect(QPoint(0, 0), shot.size())));
}

} // namespace shine::app
