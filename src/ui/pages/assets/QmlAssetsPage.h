#pragma once
// ui/pages/assets/QmlAssetsPage —— 资产工作区的 QML 宿主（QML 迁移）
//
// 定位：**替代** AssetWorkspace（Widgets 版已随迁移退役删除）。对外接口刻意保持同名同义，
// 外壳 MainWindow 的接线基本不用改：
//   * NavWidget() / NavHostBox() —— 实体树借给左栏（QQuickWidget 本身是 QWidget，
//     SidePanel::AdoptNav 收的就是 QWidget*，所以左栏不必改）；
//   * InspectorBody() —— 详情借给右栏（RightPanel::AddSection 同理）。
//
// 三个 QuickHost（内容 / 导航 / 检查器）各自持有自己的 QQmlEngine —— QQuickWidget
// 不接受注入引擎。QuickHost 构造时会按引擎重新注册 Shine 单例，所以开几个都行
// （见 QuickHost.cpp 的「按引擎重注册」注释，那条不做第 2 个引擎就会崩）。
//
// 数据一律走 AssetPageModel：QML 侧只 `import Shine 1.0` 拿 ThemeBridge，
// 业务数据由宿主 setContextProperty 注入为 `Page`。
#include "media/GalleryTypes.h"
#include "ui/pages/assets/AssetPageModel.h"

#include <QWidget>

#include <QString>
#include <QStringList>

#include <filesystem>
#include <memory>
#include <vector>

namespace shine::qml {
class QuickHost;
}

namespace shine::app {

class QmlAssetsPage : public QWidget {
    Q_OBJECT

  public:
    explicit QmlAssetsPage(QWidget* parent = nullptr);
    ~QmlAssetsPage() override;

    // —— 与迁移前的 AssetWorkspace 同名同义（外壳 / 取证按这套调用）——
    bool OpenBook(const std::filesystem::path& dbPath, const std::filesystem::path& projectDir,
                  QString* error = nullptr);
    void CloseBook() noexcept;
    bool RefreshAssets();
    bool SelectEntity(qint64 id);
    bool SelectAsset(qint64 id);
    bool StartAssetLayer(novelcore::AssetLayer layer);
    bool StartAssetPipeline();

    // 转事件循环等视觉事实就绪（帧图在 worker 上解码，像素差异异步回填）。
    // 抓图 / 断言前必须调它，否则会拍到「未比对」的中间态。
    // timeout_ms 用尽仍未就绪就直接返回 false（不无限等）。
    bool WaitVisualsReady(int timeout_ms = 2000);
    void SetAssetPolicy(const AssetPolicy& policy) { model_->SetAssetPolicy(policy); }
    [[nodiscard]] const AssetPolicy& Policy() const { return model_->Policy(); }
    std::size_t ImportReferences(const std::vector<std::filesystem::path>& paths);
    void RefreshReferences();
    void ShowDetailPage(int index);
    // 取证钉位：把内容滚动位置显式写死（负值 = 回到顶部并交还控制权）。
    // 用来在**真实视口**下拍折叠线以下的内容 —— 别改成把宿主拉高，那会让
    // 取证图的视口不对应任何真实屏幕。
    void SetScrollY(double y);

    // 参考库导入是异步的（worker 解码 + 写回 refs.json）。等它收敛用，
    // 免得调用方在 UI 线程硬 sleep。超时返回 false。
    bool WaitReferences(int timeout_ms = 10000);
    void SelectReference(const QString& id);
    void SetReferenceMarkers(const QString& markers);
    void BindReferenceToEntity();
    void RemoveReference();

    // ① 导出整版设定集 PNG。target 为空走 QFileDialog 选路径（模态，只能在
    // UI 线程弹）；离屏/验收传显式路径。解码+合成+写盘在 worker。
    void ExportSheet(const QString& target = {});
    bool WaitExport(int timeout_ms = 10000);
    [[nodiscard]] QString ExportProbe() const;

    [[nodiscard]] QString AssetProbe() const { return model_->AssetProbe(); }
    [[nodiscard]] QString StateProbe() const { return model_->StateProbe(); }
    [[nodiscard]] QString DetailProbe() const;
    [[nodiscard]] QString PolicyProbe() const;
    [[nodiscard]] QString ConsistencyProbe() const;
    [[nodiscard]] QString RefProbe() const;
    [[nodiscard]] QString GlobalGalleryProbe() const;
    void SelectGlobalGallerySource(shine::gallery::SourceKind source);
    bool SelectFirstGlobalGallery();

    [[nodiscard]] QStringList ReferenceUsageLabels() const;
    bool ActivateReferenceUsage(int index);

    // 交给外壳的三个容器（都是 QWidget：QQuickWidget 继承自 QWidget）
    [[nodiscard]] QWidget* NavWidget() const;
    [[nodiscard]] QWidget* NavHostBox() const;
    [[nodiscard]] QWidget* InspectorBody() const;

  private:
    [[nodiscard]] shine::qml::QuickHost* MakeHost(const QString& qml_file, QWidget* parent);

    AssetPageModel* model_ = nullptr;
    shine::qml::QuickHost* content_ = nullptr;
    shine::qml::QuickHost* nav_ = nullptr;
    shine::qml::QuickHost* inspector_ = nullptr;
    shine::gallery::SourceKind gallery_source_ = shine::gallery::SourceKind::Local;
    QStringList ref_usage_labels_;
    QString loadError_;
};

} // namespace shine::app
