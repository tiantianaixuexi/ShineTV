#include "ui/pages/assets/AssetWorkspace.h"
#include "ui/pages/assets/AssetDetailView.h"
#include "ui/pages/assets/ConsistencyView.h"
#include "ui/pages/assets/RefLibraryView.h"
#include "ui/verify/gallery/GalleryWorkspace.h"

#include "core/Async.h"
#include "ui/pages/novel/WorldBoardShared.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/layout/QtLayout.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "util/Encoding.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPointer>
#include <QPixmap>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>
#include <string>
#include <system_error>
#include <algorithm>
#include <array>
#include <utility>

namespace shine::app {
namespace {

constexpr std::array<KindInfo, 4> kAssetEntityKinds = {{
    {"person", "人物", "人物/生物"},
    {"location", "地点", "空间/势力"},
    {"item", "物品", "装备"},
    {"faction", "势力", "空间/势力"},
}};

[[nodiscard]] bool IsMissingAsset(const std::string& message) {
    return message == "该实体尚无视觉资产";
}

[[nodiscard]] QString StatusLabel(const QString& status) {
    if (status == QStringLiteral("PENDING")) return QStringLiteral("待生成");
    if (status == QStringLiteral("PROMPTING") || status == QStringLiteral("GENERATING"))
        return QStringLiteral("生成中");
    if (status == QStringLiteral("CHECKING")) return QStringLiteral("校验中");
    if (status == QStringLiteral("REF_READY")) return QStringLiteral("参考就绪");
    if (status == QStringLiteral("SHEET_READY")) return QStringLiteral("设定集就绪");
    if (status == QStringLiteral("WARDROBE_READY")) return QStringLiteral("服装就绪");
    if (status == QStringLiteral("READY")) return QStringLiteral("可用");
    if (status == QStringLiteral("FAILED")) return QStringLiteral("失败");
    if (status == QStringLiteral("STALE")) return QStringLiteral("需刷新");
    return QStringLiteral("状态未知");
}

[[nodiscard]] const char* StatusTone(const QString& status) {
    if (status == QStringLiteral("PENDING")) return "info";
    if (status == QStringLiteral("PROMPTING") || status == QStringLiteral("GENERATING") ||
        status == QStringLiteral("CHECKING") || status == QStringLiteral("REF_READY"))
        return "accent";
    if (status == QStringLiteral("SHEET_READY") || status == QStringLiteral("WARDROBE_READY") ||
        status == QStringLiteral("READY"))
        return "ok";
    if (status == QStringLiteral("FAILED")) return "danger";
    return "warn";
}

void CopyError(QString* out, const QString& message) {
    if (out != nullptr) {
        *out = message;
    }
}

struct WorkerResult {
    std::string phase;
    std::string detail;
    bool degraded = false;
};

[[nodiscard]] WorkerResult RunAssetInWorker(const std::filesystem::path& dbPath,
                                            shine::novelcore::RowId assetId,
                                            std::optional<shine::novelcore::AssetLayer> layer,
                                            const AssetPolicy& policy) {
    namespace db = shine::db::sqlite;
    namespace novelcore = shine::novelcore;
    db::Database database;
    if (auto opened = database.Open({.path = dbPath, .readOnly = false, .create = false}); !opened) {
        return {.phase = "FAILED", .detail = opened.error().message};
    }

    novelcore::AssetPipelineOptions options;
    options.suspendTimeoutMs = policy.suspendTimeoutMs;
    options.allowDegrade = policy.allowDegrade;
    auto pipeline = layer ? novelcore::RunAssetLayer(database, assetId, *layer, options)
                          : novelcore::RunAssetPipeline(database, assetId, options);
    if (!pipeline) {
        return {.phase = "FAILED", .detail = pipeline.error().message};
    }

    WorkerResult result{.phase = pipeline->status};
    switch (pipeline->outcome) {
        case novelcore::PipelineOutcome::Ready:
            break;
        case novelcore::PipelineOutcome::Suspended:
            result.phase = "PENDING";
            result.detail = pipeline->detail;
            break;
        case novelcore::PipelineOutcome::Degraded:
            result.degraded = true;
            result.detail = pipeline->detail;
            if (result.detail.empty() && !pipeline->notes.empty()) {
                result.detail = pipeline->notes.front();
            }
            break;
        case novelcore::PipelineOutcome::Failed:
            result.phase = "FAILED";
            result.detail = pipeline->detail;
            break;
    }

    if (result.phase == "FAILED") {
        return result;
    }
    for (const novelcore::VisualArtifactRow& artifact : pipeline->artifacts) {
        if (artifact.status != "DONE") {
            continue;
        }
        std::error_code ec;
        const std::filesystem::path path =
            novelcore::ProjectDirOfDb(database.Path()) / util::PathFromUtf8(artifact.rel_path);
        if (!std::filesystem::is_regular_file(path, ec)) {
            return {.phase = "FAILED",
                    .detail = "产物校验失败：" + shine::util::PathToUtf8(artifact.rel_path),
                    .degraded = result.degraded};
        }
    }
    return result;
}

} // namespace

AssetWorkspace::AssetWorkspace(QWidget* parent) : QWidget(parent) {
    BuildUi();
    ResetForClosedBook();
}

void AssetWorkspace::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    // 页面级留白 / 分区间距统一走 layout helper（对齐 webui .vw：20 24 26 / gap 16）
    util::PageMargins(outer);
    util::PageSpacing(outer);

    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(theme::space::kSteps[2]);

    bookLabel_ = widgets::SectionTitle(QStringLiteral("视觉资产 · 未打开书库"), header);
    headerLayout->addWidget(bookLabel_);

    const std::array<QString, 5> filterLabels = {
        QStringLiteral("全部"), QStringLiteral("人物"), QStringLiteral("地点"),
        QStringLiteral("物品"), QStringLiteral("势力")};
    const std::array<QString, 5> filterKeys = {
        QString{}, QString::fromStdString(std::string{kAssetEntityKinds[0].key}),
        QString::fromStdString(std::string{kAssetEntityKinds[1].key}),
        QString::fromStdString(std::string{kAssetEntityKinds[2].key}),
        QString::fromStdString(std::string{kAssetEntityKinds[3].key})};
    for (std::size_t i = 0; i < kindButtons_.size(); ++i) {
        kindButtons_[i] = new widgets::Button(filterLabels[i], widgets::Button::Variant::Ghost,
                                              widgets::Button::Size::Sm, header);
        kindButtons_[i]->setCheckable(true);
        kindButtons_[i]->setToolTip(i == 0
                                       ? QStringLiteral("显示全部支持 kind")
                                       : QStringLiteral("按 kind=%1 过滤实体树")
                                             .arg(filterKeys[i]));
        headerLayout->addWidget(kindButtons_[i]);
        connect(kindButtons_[i], &widgets::Button::clicked, this,
                [this, key = filterKeys[i]] { SetKindFilter(key); });
    }

    auto* refresh = new widgets::Button(QStringLiteral("刷新资产"),
                                         widgets::Button::Variant::Secondary,
                                         widgets::Button::Size::Sm, header);
    refresh->setToolTip(QStringLiteral("重新读取当前书的 visual_assets"));
    headerLayout->addWidget(refresh);
    headerLayout->addStretch(1);
    outer->addWidget(header);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    auto* left = new QWidget(splitter);
    left->setMinimumWidth(240);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(theme::space::kSteps[1]);
    leftLayout->addWidget(SectionLabel(left, QStringLiteral("实体树")));
    entityTree_ = new QTreeWidget(left);
    entityTree_->setColumnCount(1);
    entityTree_->setHeaderHidden(true);
    entityTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    widgets::SetKind(entityTree_, "field");
    leftLayout->addWidget(entityTree_, 1);

    // 中栏：资产卡网格（单列滚动 + 卡片网格，不带标题条）
    auto* center = new QWidget(splitter);
    center->setMinimumWidth(560); // 硬下限：低于此值资产卡会被压成两列残条
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(theme::space::kSteps[2], 0, theme::space::kSteps[2], 0);
    centerLayout->setSpacing(theme::space::kSteps[2]);
    assetCount_ = SectionLabel(center, QStringLiteral("资产卡 · 0"));
    auto* scroll = new QScrollArea(center);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    assetsHost_ = new QWidget(scroll);
    assetsGrid_ = new QGridLayout(assetsHost_);
    assetsGrid_->setContentsMargins(0, 0, 0, 0);
    assetsGrid_->setHorizontalSpacing(theme::space::kSteps[2]);
    assetsGrid_->setVerticalSpacing(theme::space::kSteps[2]);
    assetsGrid_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    scroll->setWidget(assetsHost_);
    centerLayout->addWidget(assetCount_);
    centerLayout->addWidget(scroll, 1);

    // 右栏：不再由本页自摆。
    // 上一版这里是第三列（资产详情 / 一致性 / 参考图 / 全局图库），和外壳的右侧检查器
    // 叠在一起，一屏就变成 活动栏 + 页内左 + 页内中 + 页内右 + 外壳右 共五列。
    // 现在页面只留「实体树 | 资产卡」两列，详情交由外壳检查器承载（InspectorBody()）。
    auto* detail_pane = new QWidget(this);
    auto* detail_layout = new QVBoxLayout(detail_pane);
    detail_layout->setContentsMargins(0, 0, 0, 0);
    detail_layout->setSpacing(theme::space::kSteps[1]);
    auto* detail_mode = new widgets::Segmented(
        {QStringLiteral("设定集"), QStringLiteral("一致性"), QStringLiteral("参考图"),
         QStringLiteral("全局图库")},
        detail_pane);
    detail_layout->addWidget(detail_mode);
    detail_stack_ = new QStackedWidget(detail_pane);
    detail_ = new AssetDetailView(detail_stack_);
    detail_->SetOnGenerate([this](novelcore::AssetLayer layer) { StartAssetLayer(layer); });
    detail_->SetOnGenerateAll([this] { StartAssetPipeline(); });
    consistency_ = new ConsistencyView(detail_stack_);
    references_ = new RefLibraryView(detail_stack_);
    global_gallery_ = new GalleryWorkspace(detail_stack_);
    global_gallery_->SetUsageResolver(
        [this](const std::filesystem::path& image_path) {
            return ResolveReferenceUsages(image_path);
        });
    global_gallery_->SetUsageActivator(
        [this](const visual::ReferenceUsage& usage) { return ActivateReferenceUsage(usage); });
    detail_stack_->addWidget(detail_);
    detail_stack_->addWidget(consistency_);
    detail_stack_->addWidget(references_);
    detail_stack_->addWidget(global_gallery_);
    detail_mode->SetOnChanged([this](int index) { detail_stack_->setCurrentIndex(index); });
    detail_layout->addWidget(detail_stack_, 1);
    detail_pane->setMinimumWidth(360);
    inspector_body_ = detail_pane; // 交给外壳检查器（默认收起，Ctrl+I / 顶栏按钮打开）

    splitter->addWidget(left);
    splitter->addWidget(center);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 940});
    // 实体树交给外壳左侧栏（SidePanel::AdoptNav 借走，本页仍持有 entityTree_ 指针）。
    // 所有权留在本页：RebuildEntityTree / ApplyKindFilter 直接刷新 entityTree_，
    // 侧栏只负责摆放。导航不再占页内一列，内容区因此拿到整幅宽度。
    nav_host_ = left;
    nav_box_ = splitter;
    outer->addWidget(splitter, 1);

    connect(entityTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item == nullptr) {
            return;
        }
        if (item->parent() == nullptr) {
            SetKindFilter(item->data(0, Qt::UserRole).toString());
            return;
        }
        SelectEntity(item->data(0, Qt::UserRole).toLongLong());
    });
    connect(refresh, &widgets::Button::clicked, this, [this] { RefreshAssets(); });
}

bool AssetWorkspace::OpenBook(const std::filesystem::path& dbPath,
                              const std::filesystem::path& projectDir, QString* error) {
    // 参数可能正是本对象的成员（错误态「重试」会这样调用），必须先复制再 CloseBook。
    const std::filesystem::path nextDbPath = dbPath;
    const std::filesystem::path nextProjectDir = projectDir;
    CloseBook();
    CopyError(error, {});

    dbPath_ = nextDbPath;
    projectDir_ = nextProjectDir;
    bookName_ = nextProjectDir.empty()
                    ? QStringLiteral("当前书")
                    : QString::fromStdString(util::FileNameToUtf8(nextProjectDir));
    if (bookName_.isEmpty()) {
        bookName_ = QStringLiteral("当前书");
    }
    bookLabel_->setText(QStringLiteral("视觉资产 · %1").arg(bookName_));

    if (nextDbPath.empty()) {
        openError_ = QStringLiteral("没有可打开的小说库路径。");
        CopyError(error, openError_);
        RebuildEntityTree();
        RebuildAssets();
        return false;
    }

    db_ = std::make_unique<db::sqlite::Database>();
    if (auto opened = db_->Open({.path = nextDbPath, .readOnly = true, .create = false}); !opened) {
        openError_ = QStringLiteral("打不开小说库：%1（%2）")
                         .arg(QString::fromStdString(util::PathToUtf8(nextDbPath)),
                              QString::fromStdString(opened.error().message));
        db_.reset();
        CopyError(error, openError_);
        RebuildEntityTree();
        RebuildAssets();
        return false;
    }

    if (!RefreshEntities() || !RefreshAssets()) {
        CopyError(error, openError_);
        return false;
    }
    return true;
}

void AssetWorkspace::CloseBook() noexcept {
    db_.reset();
    ResetForClosedBook();
}

void AssetWorkspace::ResetForClosedBook() noexcept {
    dbPath_.clear();
    projectDir_.clear();
    bookName_.clear();
    openError_.clear();
    kindFilter_.clear();
    selectedEntityId_ = 0;
    entities_.clear();
    selectedAssetId_ = 0;
    assets_.clear();
    ++runSerial_;
    runtime_.clear();
    entityItems_.clear();
    if (bookLabel_ != nullptr) {
        bookLabel_->setText(QStringLiteral("视觉资产 · 未打开书库"));
    }
    RebuildEntityTree();
    RebuildAssets();
    if (detail_ != nullptr) {
        detail_->Clear();
    }
    if (consistency_ != nullptr) {
        consistency_->Clear();
    }
    if (references_ != nullptr) {
        references_->Clear();
    }
}

bool AssetWorkspace::RefreshEntities() {
    entities_.clear();
    if (db_ == nullptr) {
        openError_ = QStringLiteral("尚未打开书库。");
        RebuildEntityTree();
        return false;
    }

    novelcore::NovelGraph graph(*db_);
    for (const KindInfo& kind : kAssetEntityKinds) {
        auto listed = graph.ListEntities(kind.key, {}, 5000);
        if (!listed) {
            entities_.clear();
            openError_ = QStringLiteral("读取%1实体失败：%2")
                             .arg(QString::fromUtf8(kind.label),
                                  QString::fromStdString(listed.error().message));
            RebuildEntityTree();
            return false;
        }
        entities_.insert(entities_.end(), listed->begin(), listed->end());
    }

    if (selectedEntityId_ != 0 &&
        std::none_of(entities_.begin(), entities_.end(), [this](const novelcore::EntityRow& entity) {
            return entity.id == selectedEntityId_;
        })) {
        selectedEntityId_ = 0;
    }
    openError_.clear();
    RebuildEntityTree();
    return true;
}

void AssetWorkspace::RebuildEntityTree() {
    if (entityTree_ == nullptr) {
        return;
    }
    entityTree_->clear();
    entityItems_.clear();

    for (const KindInfo& kind : kAssetEntityKinds) {
        const QString key = QString::fromStdString(std::string{kind.key});
        auto* group = new QTreeWidgetItem(entityTree_);
        group->setData(0, Qt::UserRole, key);
        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        int count = 0;
        for (const novelcore::EntityRow& entity : entities_) {
            if (QString::fromStdString(entity.kind) != key) {
                continue;
            }
            ++count;
            auto* child = new QTreeWidgetItem(group);
            child->setText(0, QString::fromStdString(entity.name));
            child->setData(0, Qt::UserRole, entity.id);
            child->setToolTip(0, QStringLiteral("实体 #%1 · %2\n%3")
                                    .arg(entity.id)
                                    .arg(KindLabelOf(entity.kind),
                                         QString::fromStdString(entity.summary)));
            entityItems_.insert(entity.id, child);
        }
        group->setText(0, QStringLiteral("%1 %2").arg(QString::fromUtf8(kind.label)).arg(count));
        group->setExpanded(true);
    }

    ApplyKindFilter();
    SyncKindButtons();
    if (selectedEntityId_ != 0) {
        if (QTreeWidgetItem* selected = entityItems_.value(selectedEntityId_, nullptr);
            selected != nullptr) {
            entityTree_->setCurrentItem(selected);
        }
    }
}

bool AssetWorkspace::RefreshAssets() {
    assets_.clear();
    if (db_ == nullptr) {
        openError_ = QStringLiteral("尚未打开书库。");
        RebuildAssets();
        return false;
    }

    novelcore::NovelVisual visual(*db_);
    for (const novelcore::EntityRow& entity : entities_) {
        if (selectedEntityId_ != 0 && entity.id != selectedEntityId_) {
            continue;
        }
        if (!kindFilter_.isEmpty() && QString::fromStdString(entity.kind) != kindFilter_) {
            continue;
        }

        auto found = visual.FindAssetByEntity(entity.id);
        if (!found) {
            if (!IsMissingAsset(found.error().message)) {
                assets_.clear();
                openError_ = QStringLiteral("读取实体「%1」的视觉资产失败：%2")
                                 .arg(QString::fromStdString(entity.name),
                                      QString::fromStdString(found.error().message));
                RebuildAssets();
                return false;
            }
            continue;
        }

        AssetEntry entry{.asset = *found, .entity = entity};
        if (auto artifacts = visual.ListArtifacts(entry.asset.id)) {
            entry.degraded = std::any_of(artifacts->begin(), artifacts->end(),
                                         [](const novelcore::VisualArtifactRow& artifact) {
                                             return artifact.degraded;
                                         });
        }
        assets_.push_back(std::move(entry));
    }

    const bool selected_available =
        selectedAssetId_ != 0 &&
        std::any_of(assets_.begin(), assets_.end(), [this](const AssetEntry& entry) {
            return entry.asset.id == selectedAssetId_;
        });
    if (!selected_available) {
        selectedAssetId_ = assets_.empty() ? 0 : assets_.front().asset.id;
    }

    openError_.clear();
    RebuildAssets();
    ShowSelectedAsset();
    return true;
}

void AssetWorkspace::RebuildAssets() {
    if (assetsGrid_ == nullptr) {
        return;
    }
    ClearLayout(assetsGrid_);
    assetCount_->setText(QStringLiteral("资产卡 · %1").arg(static_cast<int>(assets_.size())));

    if (db_ == nullptr) {
        auto* empty = new widgets::EmptyState(
            QStringLiteral("📚"), QStringLiteral("尚未打开书库"),
            QStringLiteral("工作区会打开当前书；实体和资产准备好后，下一步：从实体生成视觉资产。"),
            QStringLiteral("刷新"), assetsHost_);
        empty->SetOnAction([this] { RefreshAssets(); });
        assetsGrid_->addWidget(empty, 0, 0, 1, 3);
        return;
    }
    if (!openError_.isEmpty()) {
        auto* error = new widgets::ErrorState(QStringLiteral("读取视觉资产失败"), openError_, assetsHost_);
        error->SetOnRetry([this] {
            if (db_ == nullptr) {
                OpenBook(dbPath_, projectDir_);
            } else {
                RefreshAssets();
            }
        });
        assetsGrid_->addWidget(error, 0, 0, 1, 3);
        return;
    }
    if (assets_.empty()) {
        QString title = QStringLiteral("还没有视觉资产");
        QString subtitle;
        if (entities_.empty()) {
            title = QStringLiteral("还没有可绑定的实体");
            subtitle = QStringLiteral(
                "先在设定台创建人物、地点、物品或势力；下一步：从实体生成视觉资产。");
        } else if (selectedEntityId_ == 0) {
            subtitle = QStringLiteral(
                "先在左侧选择实体；下一步：从实体生成视觉资产。P07 未接入前可先导入参考图。");
        } else {
            title = QStringLiteral("这个实体还没有视觉资产");
            subtitle = QStringLiteral(
                "下一步：从实体生成视觉资产。P07 未接入前可先导入参考图。");
        }
        auto* empty = new widgets::EmptyState(QStringLiteral("▧"), title, subtitle,
                                              QStringLiteral("从实体生成视觉资产"), assetsHost_);
        empty->SetOnAction([this] { ShowGenerationNextStep(); });
        assetsGrid_->addWidget(empty, 0, 0, 1, 3);
        return;
    }

    int column = 0;
    int row = 0;
    for (const AssetEntry& entry : assets_) {
        const auto runtimeIt = runtime_.constFind(entry.asset.id);
        const RuntimeState* runtime = runtimeIt == runtime_.cend() ? nullptr : &runtimeIt.value();
        const QString displayStatus = runtime != nullptr && !runtime->phase.isEmpty()
                                          ? runtime->phase
                                          : QString::fromStdString(entry.asset.status);
        const bool degraded = entry.degraded || (runtime != nullptr && runtime->degraded);
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, assetsHost_);
        card->setFixedWidth(230);
        QVBoxLayout* body = card->BodyLayout();

        auto* thumb = new QLabel(card);
        thumb->setFixedHeight(140);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setWordWrap(true);
        widgets::SetKind(thumb, "statedetail");
        const std::filesystem::path relative = util::PathFromUtf8(entry.asset.sheet_rel_path);
        if (relative.empty()) {
            thumb->setText(QStringLiteral("参考图待导入\n（P07 出图接入）"));
        } else {
            const std::filesystem::path imagePath =
                relative.is_absolute() ? relative : projectDir_ / relative;
            QImageReader reader(QString::fromStdString(util::PathToUtf8(imagePath)));
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (image.isNull()) {
                thumb->setText(QStringLiteral("参考图读取失败\n请检查路径或重新导入"));
            } else {
                thumb->setPixmap(QPixmap::fromImage(
                    image.scaled(198, 132, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
        }
        body->addWidget(thumb);

        auto* title = new QLabel(QString::fromStdString(entry.asset.name), card);
        widgets::SetSemibold(title, true);
        title->setWordWrap(true);
        body->addWidget(title);

        auto* statusRow = new QWidget(card);
        auto* statusLayout = new QHBoxLayout(statusRow);
        statusLayout->setContentsMargins(0, 0, 0, 0);
        statusLayout->setSpacing(theme::space::kSteps[1]);
        statusLayout->addWidget(new widgets::Tag(
            StatusLabel(displayStatus), StatusTone(displayStatus), false, statusRow));
        if (degraded) {
            statusLayout->addWidget(
                new widgets::Tag(QStringLiteral("降级:no_reference"), "warn", false, statusRow));
        }
        statusLayout->addStretch(1);
        body->addWidget(statusRow);

        auto* binding = new QLabel(
            QStringLiteral("绑定实体：%1（#%2）")
                .arg(QString::fromStdString(entry.entity.name))
                .arg(entry.entity.id),
            card);
        binding->setWordWrap(true);
        body->addWidget(binding);

        if (!entry.asset.note.empty()) {
            auto* note = new QLabel(QString::fromStdString(entry.asset.note), card);
            note->setWordWrap(true);
            body->addWidget(note);
        }
        if (runtime != nullptr && !runtime->detail.isEmpty()) {
            auto* runtime_note = new QLabel(runtime->detail, card);
            runtime_note->setWordWrap(true);
            widgets::SetKind(runtime_note, "statedetail");
            body->addWidget(runtime_note);
        }

        QString tooltip = QStringLiteral("资产 #%1\n实体：%2（#%3）\n生产状态：%4\n参考图：%5")
                              .arg(entry.asset.id)
                              .arg(QString::fromStdString(entry.asset.name),
                                   QString::fromStdString(entry.entity.name))
                              .arg(entry.entity.id)
                              .arg(displayStatus,
                                   QString::fromStdString(entry.asset.sheet_rel_path));
        if (!entry.asset.note.empty()) {
            tooltip += QStringLiteral("\n备注：%1").arg(QString::fromStdString(entry.asset.note));
        }
        if (runtime != nullptr && !runtime->history.isEmpty()) {
            tooltip += QStringLiteral("\n状态历史：%1")
                           .arg(runtime->history.join(QLatin1String(" → ")));
        }
        card->setToolTip(tooltip);
        card->SetOnClick([this, id = entry.asset.id] { SelectAsset(id); });
        body->addStretch(1);

        assetsGrid_->addWidget(card, row, column);
        column += 1;
        if (column == 3) {
            column = 0;
            row += 1;
        }
    }
}
bool AssetWorkspace::SelectAsset(qint64 id) {
    const auto found = std::find_if(assets_.begin(), assets_.end(),
                                    [id](const AssetEntry& entry) {
                                        return entry.asset.id == id;
                                    });
    if (found == assets_.end()) {
        return false;
    }
    selectedAssetId_ = found->asset.id;
    ShowSelectedAsset();
    return true;
}

void AssetWorkspace::ShowSelectedAsset() {
    if (detail_ == nullptr) {
        return;
    }
    if (db_ == nullptr || selectedAssetId_ == 0) {
        detail_->Clear();
        return;
    }
    const auto found = std::find_if(assets_.begin(), assets_.end(), [this](const AssetEntry& entry) {
        return entry.asset.id == selectedAssetId_;
    });
    if (found == assets_.end()) {
        selectedAssetId_ = 0;
        detail_->Clear();
        return;
    }

    novelcore::NovelVisual visual(*db_);
    QString detail_error;
    if (!detail_->ShowAsset(visual, found->asset, projectDir_, &detail_error)) {
        widgets::Toast::Show(detail_error, widgets::Toast::Tone::Error);
    }
    QString consistency_error;
    if (consistency_ != nullptr &&
        !consistency_->ShowAsset(*db_, visual, found->asset, found->entity.id, projectDir_,
                                  &consistency_error)) {
        widgets::Toast::Show(consistency_error, widgets::Toast::Tone::Warning);
    }
    if (references_ != nullptr) {
        QString ref_error;
        if (!references_->SetProjectRoot(projectDir_, &ref_error)) {
            widgets::Toast::Show(ref_error, widgets::Toast::Tone::Warning);
        }
        references_->SetEntityContext(found->entity.id, QString::fromStdString(found->entity.name),
                                      found->asset.id);
    }
    PushRuntimeToDetail();
}

QString AssetWorkspace::DetailProbe() const {
    return detail_ == nullptr ? QStringLiteral("detail=unavailable") : detail_->DetailProbe();
}

QString AssetWorkspace::ConsistencyProbe() const {
    return consistency_ == nullptr ? QStringLiteral("consistency=unavailable")
                                   : consistency_->ConsistencyProbe();
}

QString AssetWorkspace::RefProbe() const {
    return references_ == nullptr ? QStringLiteral("refs=unavailable") : references_->RefProbe();
}

std::size_t AssetWorkspace::ImportReferences(
    const std::vector<std::filesystem::path>& paths) {
    return references_ == nullptr ? 0 : references_->ImportPaths(paths);
}

void AssetWorkspace::RefreshReferences() {
    if (references_ != nullptr) {
        references_->RefreshReferences();
    }
}

QString AssetWorkspace::GlobalGalleryProbe() const {
    return global_gallery_ == nullptr ? QStringLiteral("gallery=unavailable")
                                      : global_gallery_->GalleryProbe();
}

void AssetWorkspace::SelectGlobalGallerySource(gallery::SourceKind source) {
    if (global_gallery_ != nullptr) {
        global_gallery_->SelectSource(source);
    }
}

bool AssetWorkspace::SelectFirstGlobalGallery() {
    return global_gallery_ != nullptr && global_gallery_->SelectFirst();
}

QStringList AssetWorkspace::ReferenceUsageLabels() const {
    return global_gallery_ == nullptr ? QStringList{} : global_gallery_->UsageLabelsForSelected();
}

bool AssetWorkspace::ActivateReferenceUsage(int index) {
    return global_gallery_ != nullptr && global_gallery_->ActivateUsage(index);
}

void AssetWorkspace::ShowDetailPage(int index) {
    if (detail_stack_ != nullptr && index >= 0 && index < detail_stack_->count()) {
        detail_stack_->setCurrentIndex(index);
    }
}

std::vector<visual::ReferenceUsage> AssetWorkspace::ResolveReferenceUsages(
    const std::filesystem::path& image_path) const {
    if (db_ == nullptr) {
        return {};
    }
    return visual::FindReferenceUsages(*db_, projectDir_, image_path);
}

bool AssetWorkspace::ActivateReferenceUsage(const visual::ReferenceUsage& usage) {
    if (usage.kind == visual::ReferenceUsageKind::Shot) {
        return ShowShotReference(usage.shot_id);
    }
    if (usage.entity_id > 0 && !SelectEntity(usage.entity_id)) {
        return false;
    }
    if (usage.asset_id > 0) {
        (void)SelectAsset(usage.asset_id);
    }
    return usage.entity_id > 0 || usage.asset_id > 0;
}

bool AssetWorkspace::ShowShotReference(std::int64_t shot_id) {
    if (db_ == nullptr || shot_id <= 0) {
        return false;
    }
    novelcore::NovelVisual visual(*db_);
    auto shot = visual.GetShot(shot_id);
    if (!shot) {
        return false;
    }
    auto* drawer = new widgets::Drawer(QStringLiteral("分镜引用 #%1").arg(shot_id), this);
    auto* values = new data::KeyValue(drawer);
    values->SetPairs({
        {QStringLiteral("场景"), QString::number(shot->scene_id)},
        {QStringLiteral("镜序"), QString::number(shot->ord)},
        {QStringLiteral("角色 JSON"), QString::fromStdString(shot->character_ids_json)},
        {QStringLiteral("动作"), QString::fromStdString(shot->action)},
        {QStringLiteral("表演"), QString::fromStdString(shot->expression)},
        {QStringLiteral("情绪"), QString::fromStdString(shot->mood)},
        {QStringLiteral("Prompt"), QString::fromStdString(shot->prompt_text)},
    });
    drawer->BodyLayout()->addWidget(values);
    drawer->Open();
    return true;
}

void AssetWorkspace::SetAssetPolicy(const AssetPolicy& policy) {
    if (detail_ != nullptr) {
        detail_->SetPolicy(policy);
    }
}

QString AssetWorkspace::PolicyProbe() const {
    return detail_ == nullptr ? QStringLiteral("policy=unavailable") : detail_->PolicyProbe();
}

void AssetWorkspace::PushRuntimeToDetail() {
    if (detail_ == nullptr) {
        return;
    }
    const auto found = runtime_.constFind(selectedAssetId_);
    if (found == runtime_.cend()) {
        detail_->SetRuntimeState({}, {}, {}, {}, false, false);
        return;
    }
    const RuntimeState& state = found.value();
    detail_->SetRuntimeState(state.phase, state.detail, state.history, state.layer, state.active,
                             state.degraded);
}

bool AssetWorkspace::StartAssetLayer(novelcore::AssetLayer layer) {
    return StartAssetRun(layer);
}

bool AssetWorkspace::StartAssetPipeline() {
    return StartAssetRun(std::nullopt);
}

bool AssetWorkspace::StartAssetRun(std::optional<novelcore::AssetLayer> layer) {
    if (db_ == nullptr || dbPath_.empty() || selectedAssetId_ == 0) {
        widgets::Toast::Show(QStringLiteral("请先选择一个已有视觉资产的实体。"),
                             widgets::Toast::Tone::Info);
        return false;
    }
    const auto current = runtime_.constFind(selectedAssetId_);
    if (current != runtime_.cend() && current->active) {
        widgets::Toast::Show(QStringLiteral("该资产已有任务在运行；完成后再提交下一次。"),
                             widgets::Toast::Tone::Warning);
        return false;
    }

    RuntimeState state;
    state.run_id = ++runSerial_;
    state.phase = QStringLiteral("GENERATING");
    state.layer = layer ? QString::fromStdString(std::string{
                              novelcore::AssetLayerName(*layer)})
                        : QStringLiteral("all");
    state.detail = QStringLiteral("任务已提交；正在生成形象层并写入产物账。");
    state.history = {state.phase};
    state.active = true;
    runtime_.insert(selectedAssetId_, state);
    RebuildAssets();
    PushRuntimeToDetail();

    QPointer<AssetWorkspace> guard(this);
    const std::filesystem::path db_path = dbPath_;
    const qint64 asset_id = selectedAssetId_;
    const qint64 run_id = state.run_id;
    const AssetPolicy policy = detail_ == nullptr ? AssetPolicy{} : detail_->Policy();
    async::RunOnWorker([guard, db_path, asset_id, run_id, layer, policy] {
        WorkerResult result = RunAssetInWorker(db_path, asset_id, layer, policy);
        async::PostToUi([guard, asset_id, run_id, result = std::move(result)]() mutable {
            if (guard) {
                guard->BeginRunChecking(asset_id, run_id, QString::fromStdString(result.detail),
                                        QString::fromStdString(result.phase), result.degraded);
            }
        });
    });
    return true;
}

void AssetWorkspace::BeginRunChecking(qint64 assetId, qint64 runId, QString detail,
                                      QString finalPhase, bool degraded) {
    auto found = runtime_.find(assetId);
    if (found == runtime_.end() || found->run_id != runId) {
        return;
    }
    const QString worker_detail = detail;
    found->phase = QStringLiteral("CHECKING");
    found->detail = worker_detail.isEmpty()
                        ? QStringLiteral("正在校验落盘文件与父子派生关系。")
                        : worker_detail;
    found->active = true;
    if (!found->history.contains(found->phase)) {
        found->history.push_back(found->phase);
    }
    RebuildAssets();
    PushRuntimeToDetail();

    QTimer::singleShot(
        120, this,
        [this, assetId, runId, finalPhase = std::move(finalPhase),
         worker_detail = std::move(worker_detail), degraded] {
            FinishAssetRun(assetId, runId, std::move(finalPhase), worker_detail, degraded);
        });
}

void AssetWorkspace::FinishAssetRun(qint64 assetId, qint64 runId, QString phase, QString detail,
                                    bool degraded) {
    auto found = runtime_.find(assetId);
    if (found == runtime_.end() || found->run_id != runId) {
        return;
    }
    found->phase = phase;
    found->detail = detail;
    found->active = false;
    found->degraded = degraded;
    if (!phase.isEmpty() && !found->history.contains(phase)) {
        found->history.push_back(phase);
    }
    (void)RefreshAssets();
    if (phase == QStringLiteral("FAILED")) {
        widgets::Toast::Show(QStringLiteral("资产生成失败：%1").arg(detail),
                             widgets::Toast::Tone::Error);
    } else if (degraded) {
        widgets::Toast::Show(QStringLiteral("资产已完成，但使用了降级策略：%1").arg(detail),
                             widgets::Toast::Tone::Warning);
    }
}

QString AssetWorkspace::StateProbe() const {
    const auto found = runtime_.constFind(selectedAssetId_);
    if (found == runtime_.cend()) {
        return QStringLiteral("asset=%1; runtime=none; history=none; active=0; degraded=0")
            .arg(selectedAssetId_);
    }
    const RuntimeState& state = found.value();
    return QStringLiteral("asset=%1; runtime=%2; layer=%3; history=%4; active=%5; degraded=%6; detail=%7")
        .arg(selectedAssetId_)
        .arg(state.phase, state.layer,
             state.history.isEmpty() ? QStringLiteral("none")
                                     : state.history.join(QLatin1Char('>')))
        .arg(state.active ? QStringLiteral("1") : QStringLiteral("0"),
             state.degraded ? QStringLiteral("1") : QStringLiteral("0"),
             state.detail.isEmpty() ? QStringLiteral("none") : state.detail);
}

bool AssetWorkspace::SelectEntity(qint64 id) {
    if (id == 0) {
        selectedEntityId_ = 0;
        if (entityTree_ != nullptr) {
            entityTree_->clearSelection();
            entityTree_->setCurrentItem(nullptr);
        }
        if (db_ != nullptr) {
            RefreshAssets();
        }
        return true;
    }

    const auto found = std::find_if(entities_.begin(), entities_.end(),
                                    [id](const novelcore::EntityRow& entity) {
                                        return entity.id == id;
                                    });
    if (found == entities_.end()) {
        return false;
    }
    if (!kindFilter_.isEmpty() && QString::fromStdString(found->kind) != kindFilter_) {
        kindFilter_.clear();
        ApplyKindFilter();
        SyncKindButtons();
    }
    selectedEntityId_ = id;
    if (entityTree_ != nullptr) {
        if (QTreeWidgetItem* selected = entityItems_.value(id, nullptr); selected != nullptr) {
            entityTree_->setCurrentItem(selected);
            entityTree_->scrollToItem(selected);
        }
    }
    RefreshAssets();
    return true;
}

void AssetWorkspace::ApplyKindFilter() {
    if (selectedEntityId_ != 0 && !kindFilter_.isEmpty() && entityItems_.contains(selectedEntityId_)) {
        QTreeWidgetItem* selected = entityItems_.value(selectedEntityId_, nullptr);
        QTreeWidgetItem* group = selected != nullptr ? selected->parent() : nullptr;
        if (group == nullptr || group->data(0, Qt::UserRole).toString() != kindFilter_) {
            selectedEntityId_ = 0;
        }
    }
    const int groupCount = entityTree_ == nullptr ? 0 : entityTree_->topLevelItemCount();
    for (int i = 0; i < groupCount; ++i) {
        QTreeWidgetItem* group = entityTree_->topLevelItem(i);
        group->setHidden(!kindFilter_.isEmpty() &&
                         group->data(0, Qt::UserRole).toString() != kindFilter_);
    }
    if (selectedEntityId_ == 0 && entityTree_ != nullptr) {
        entityTree_->clearSelection();
    }
}

void AssetWorkspace::SetKindFilter(const QString& kind) {
    kindFilter_ = kind;
    ApplyKindFilter();
    SyncKindButtons();
    if (db_ != nullptr) {
        RefreshAssets();
    }
}

void AssetWorkspace::SyncKindButtons() {
    for (std::size_t i = 0; i < kindButtons_.size(); ++i) {
        const QString key = i == 0
                                ? QString{}
                                : QString::fromStdString(std::string{kAssetEntityKinds[i - 1].key});
        int count = 0;
        for (const novelcore::EntityRow& entity : entities_) {
            if (QString::fromStdString(entity.kind) == key) {
                ++count;
            }
        }
        const QString label = i == 0 ? QStringLiteral("全部") : KindLabelOf(key.toStdString());
        kindButtons_[i]->setText(QStringLiteral("%1 %2").arg(label).arg(count));
        kindButtons_[i]->setChecked(key == kindFilter_);
    }
}

QString AssetWorkspace::AssetProbe() const {
    QString selected = QStringLiteral("全部");
    if (selectedEntityId_ != 0) {
        const auto found = std::find_if(entities_.begin(), entities_.end(),
                                        [this](const novelcore::EntityRow& entity) {
                                            return entity.id == selectedEntityId_;
                                        });
        if (found != entities_.end()) {
            selected = QStringLiteral("%1#%2")
                           .arg(QString::fromStdString(found->name))
                           .arg(found->id);
        } else {
            selected = QStringLiteral("#%1").arg(selectedEntityId_);
        }
    }
    const QString kind = kindFilter_.isEmpty() ? QStringLiteral("全部")
                                              : KindLabelOf(kindFilter_.toStdString());
    QString result = QStringLiteral("book=%1; entities=%2; assets=%3; empty=%4; kind=%5; selected=%6; next=从实体生成视觉资产")
                         .arg(db_ == nullptr ? QStringLiteral("closed") : QStringLiteral("open"))
                         .arg(static_cast<int>(entities_.size()))
                         .arg(static_cast<int>(assets_.size()))
                         .arg(assets_.empty() ? QStringLiteral("1") : QStringLiteral("0"))
                         .arg(kind)
                         .arg(selected);
    if (!assets_.empty()) {
        const AssetEntry& first = assets_.front();
        const auto runtimeIt = runtime_.constFind(first.asset.id);
        const bool degraded =
            first.degraded || (runtimeIt != runtime_.cend() && runtimeIt->degraded);
        const QString runtime = runtimeIt == runtime_.cend() || runtimeIt->phase.isEmpty()
                                    ? QStringLiteral("none")
                                    : runtimeIt->phase;
        result += QStringLiteral("; card=%1|status=%2|entity=%3|degraded=%4|runtime=%5")
                      .arg(QString::fromStdString(first.asset.name),
                           QString::fromStdString(first.asset.status),
                           QString::fromStdString(first.entity.name))
                      .arg(degraded ? QStringLiteral("1") : QStringLiteral("0"), runtime);
    }
    if (selectedAssetId_ != 0) {
        result += QStringLiteral("; selectedAsset=%1").arg(selectedAssetId_);
    }
    if (!openError_.isEmpty()) {
        result += QStringLiteral("; error=%1").arg(QString{openError_}.replace('\n', ' '));
    }
    return result;
}

void AssetWorkspace::ShowGenerationNextStep() const {
    if (selectedEntityId_ == 0) {
        widgets::Toast::Show(QStringLiteral("请先从左侧选择人物、地点、物品或势力实体。"),
                             widgets::Toast::Tone::Info);
        return;
    }
    widgets::Toast::Show(
        QStringLiteral("已选择实体 #%1；P07 尚未接入实际出图。当前可先导入参考图，不会伪造生成结果。")
            .arg(selectedEntityId_),
        widgets::Toast::Tone::Info);
}

} // namespace shine::app
