#include "ui/pages/assets/QmlAssetsPage.h"

#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/qml/QuickHost.h"
#include "util/Encoding.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QLayout>
#include <QQmlContext>
#include <QQuickItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantMap>

namespace shine::app {

class AssetPageModel;

namespace {

// ⚠️ **QuickHost 一律不析构**（进程期常驻），照搬 QmlPageReview.cpp 的 LiveHosts()。
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

shine::qml::QuickHost* AcquireHost(const QString& qml_file, shine::app::AssetPageModel* model) {
    auto* host = new shine::qml::QuickHost(); // 无 parent：故意不析构
    host->FillWithRoot();
    HostPool().push_back(host);
    // ⚠️ 顺序是硬要求：先注入 Page，再 Load。
    // Load() 内部会同步创建 QML 根对象并**首次求值所有绑定**，那一刻
    // `Page` 还没进上下文 → 绑定求值成 undefined 且被永久缓存，
    // 之后 setContextProperty 再也救不回来（表现是一屏 "Page is not defined"，
    // 而 QQuickWidget 自己一声不吭）。QmlPageReview 的 assets 页同此纪律。
    host->rootContext()->setContextProperty(QStringLiteral("Page"), model);
    if (!host->Load(qml_file)) {
        QString detail;
        for (const QQmlError& e : host->errors()) {
            detail += e.toString() + QLatin1String(" | ");
        }
        // 载入失败必须立刻可见：QML 页面加载失败是静默的（控件一片空白），
        // 只在日志里留一行会让「页面没东西」和「数据为空」难以区分。
        widgets::Toast::Show(QStringLiteral("QML 页面载入失败：%1").arg(detail.left(200)),
                             widgets::Toast::Tone::Error);
    }
    host->Pump(0);
    return host;
}
} // namespace

QmlAssetsPage::QmlAssetsPage(QWidget* parent) : QWidget(parent) {
    model_ = new AssetPageModel(this);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    content_ = AcquireHost(QStringLiteral(":/qt/qml/Assets.qml"), model_);
    lay->addWidget(content_, 1);

    // 导航与检查器：QQuickWidget 本身就是 QWidget，外壳的 AdoptNav /
    // AddSection 收的都是 QWidget*，所以左栏右栏不必改。
    nav_ = AcquireHost(QStringLiteral(":/qt/qml/AssetNav.qml"), model_);
    inspector_ = AcquireHost(QStringLiteral(":/qt/qml/AssetInspector.qml"), model_);
}

QmlAssetsPage::~QmlAssetsPage() {
    // 三个 QuickHost 故意不 delete（见 HostPool 的注释）。
    content_->setParent(nullptr);
    nav_->setParent(nullptr);
    inspector_->setParent(nullptr);
    content_->hide();
    nav_->hide();
    inspector_->hide();
}

bool QmlAssetsPage::OpenBook(const std::filesystem::path& dbPath,
                             const std::filesystem::path& projectDir, QString* error) {
    const bool ok = model_->OpenBook(dbPath, projectDir, error);
    // 数据变了要让场景图重算一遍绑定：QQuickWidget 不会因为 C++ 端数据变就自动刷新，
    // changed() 只让 QML 绑定重算，宿主还得再跑一帧。
    content_->Pump(0);
    nav_->Pump(0);
    inspector_->Pump(0);
    return ok;
}

void QmlAssetsPage::CloseBook() noexcept {
    model_->CloseBook();
    content_->Pump(0);
    nav_->Pump(0);
    inspector_->Pump(0);
}

bool QmlAssetsPage::RefreshAssets() { return model_->RefreshAssets(); }
bool QmlAssetsPage::SelectEntity(qint64 id) {
    model_->selectEntity(QString::number(id));
    content_->Pump(0);
    nav_->Pump(0);
    inspector_->Pump(0);
    return true;
}
bool QmlAssetsPage::SelectAsset(qint64 id) {
    model_->selectAsset(QString::number(id));
    content_->Pump(0);
    inspector_->Pump(0);
    return true;
}
bool QmlAssetsPage::StartAssetLayer(novelcore::AssetLayer layer) {
    // 直接用 AssetLayerName（枚举自己的名字，就是 visual_artifacts.layer 的真值）。
    // 原来这里维护了一张 keys[] 对照表且把基础身体写成 "body"，与真值 "base_body"
    // 不符 → 经 AssetPageModel::startLayer 查表落空、静默返回 false。
    // 枚举 → 名字是唯一真值，不要在 UI 层再抄一份。
    return model_->startLayer(QString::fromStdString(std::string{novelcore::AssetLayerName(layer)}));
}
bool QmlAssetsPage::StartAssetPipeline() { return model_->startPipeline(); }

bool QmlAssetsPage::WaitVisualsReady(int timeout_ms) {
    if (model_->visualsReady()) {
        return true;
    }
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    bool ready = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [this, &loop, &ready] {
        if (model_->visualsReady()) {
            ready = true;
            loop.quit();
        }
    });
    poll.setInterval(10);
    poll.start();
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    loop.exec();
    poll.stop();
    if (ready) {
        // 数据变了要让场景图再跑一帧，否则抓到的还是回填前的那一帧
        content_->Pump(0);
        inspector_->Pump(0);
    }
    return ready;
}

std::size_t QmlAssetsPage::ImportReferences(const std::vector<std::filesystem::path>& paths) {
    // 真实实现已迁到 AssetPageModel（worker 解码 + 写回 refs.json）。
    // 返回的是**提交数**；实际落库数看 RefProbe() 的 refs= 字段。
    QStringList list;
    list.reserve(static_cast<qsizetype>(paths.size()));
    for (const auto& path : paths) {
        list.push_back(QString::fromStdString(util::PathToUtf8(path)));
    }
    return static_cast<std::size_t>(model_->importReferences(list));
}
void QmlAssetsPage::RefreshReferences() { model_->refreshReferences(); }
void QmlAssetsPage::ShowDetailPage(int index) {
    // QML 根对象暴露 viewIndex（0=总览 1=详情），直接写属性而不是 invokeMethod：
    // 页面根是 Ctl(Rectangle)，invokeMethod 走 QMetaObject 反射找不到 JS 函数。
    if (auto* root = content_->rootObject()) {
        root->setProperty("viewIndex", index);
    }
}

bool QmlAssetsPage::WaitReferences(int timeout_ms) {
    if (model_->waitReferences(timeout_ms)) {
        content_->Pump(0);
        inspector_->Pump(0);
        return true;
    }
    return false;
}
void QmlAssetsPage::SelectReference(const QString& id) { model_->selectReference(id); }
void QmlAssetsPage::SetReferenceMarkers(const QString& markers) {
    model_->setReferenceMarkers(markers);
}
void QmlAssetsPage::BindReferenceToEntity() { model_->bindReferenceToEntity(); }
void QmlAssetsPage::RemoveReference() { model_->removeReference(); }

void QmlAssetsPage::ExportSheet(const QString& target) { model_->exportSheet(target); }
bool QmlAssetsPage::WaitExport(int timeout_ms) {
    if (model_->exportFinished()) {
        return true;
    }
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    bool done = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [this, &loop, &done] {
        if (model_->exportFinished()) {
            done = true;
            loop.quit();
        }
    });
    poll.setInterval(10);
    poll.start();
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    loop.exec();
    poll.stop();
    return done;
}
QString QmlAssetsPage::ExportProbe() const {
    const QVariantMap state = model_->exportState();
    return QStringLiteral("export=ok; path=%1; bytes=%2; layers=%3; busy=%4")
        .arg(state.value(QStringLiteral("lastPath")).toString())
        .arg(state.value(QStringLiteral("lastBytes")).toLongLong())
        .arg(state.value(QStringLiteral("count")).toInt())
        .arg(state.value(QStringLiteral("busy")).toBool() ? QStringLiteral("1")
                                                          : QStringLiteral("0"));
}

QString QmlAssetsPage::DetailProbe() const { return model_->DetailProbe(); }
QString QmlAssetsPage::PolicyProbe() const { return model_->PolicyProbe(); }
QString QmlAssetsPage::ConsistencyProbe() const { return model_->ConsistencyProbe(); }
QString QmlAssetsPage::RefProbe() const { return model_->RefProbe(); }
QString QmlAssetsPage::GlobalGalleryProbe() const {
    return QStringLiteral("gallery=unavailable");
}
void QmlAssetsPage::SelectGlobalGallerySource(gallery::SourceKind source) {
    gallery_source_ = source;
}
bool QmlAssetsPage::SelectFirstGlobalGallery() { return false; }
QStringList QmlAssetsPage::ReferenceUsageLabels() const { return ref_usage_labels_; }
bool QmlAssetsPage::ActivateReferenceUsage(int index) {
    return index >= 0 && index < ref_usage_labels_.size();
}

QWidget* QmlAssetsPage::NavWidget() const { return nav_; }
QWidget* QmlAssetsPage::NavHostBox() const { return nullptr; }
QWidget* QmlAssetsPage::InspectorBody() const { return inspector_; }

} // namespace shine::app
