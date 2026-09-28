#include "ui/pages/shell/MainWindow.h"

#include "ui/pages/project/ProjectHubView.h"
#include "ui/pages/project/ProjectWizardDialog.h"
#include "ui/pages/settings/StyleEditorDialog.h"
#include "ui/pages/settings/FirstRunWizard.h"
#include "ui/pages/shell/ActivityRail.h"
#include "ui/pages/shell/BottomDock.h"
#include "ui/pages/shell/Breadcrumb.h"
#include "ui/pages/shell/CommandPalette.h"
#include "ui/pages/shell/RightPanel.h"
#include "ui/pages/novel/NovelWorkspace.h"
#include "ui/pages/assets/AssetWorkspace.h"
#include "ui/pages/imageflow/ImageFlowWorkspace.h"
#include "ui/pages/videoflow/VideoFlowWorkspace.h"
#include "ui/pages/pipeline/PipelineWorkspace.h"
#include "ui/pages/storyboard/StoryboardWorkspace.h"
#include "ui/pages/shell/TopBar.h"
#include "core/Settings.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Navigation.h"
#include "ui/kit/controls/Surfaces.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace shine::app {

namespace {

// 中央文档标签条：双击改名、中键关闭（QTabBar 无 Q_OBJECT 也能加行为，回调上交）
class DocTabBar : public QTabBar {
  public:
    explicit DocTabBar(QWidget* parent = nullptr) : QTabBar(parent) {
        setDocumentMode(true);
        setTabsClosable(true);
        setMovable(true);
        setExpanding(false);
        setDrawBase(false);
    }

    std::function<void(int)> on_middle_close;
    std::function<void(int)> on_rename;

  protected:
    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::MiddleButton) {
            const int i = tabAt(ev->pos());
            if (i >= 0 && on_middle_close) {
                on_middle_close(i);
                return;
            }
        }
        QTabBar::mousePressEvent(ev);
    }
    void mouseDoubleClickEvent(QMouseEvent* ev) override {
        const int i = tabAt(ev->pos());
        if (i >= 0 && on_rename) {
            on_rename(i);
            return;
        }
        QTabBar::mouseDoubleClickEvent(ev);
    }
};

// 工作区索引 ↔ project.json ui.lastWorkspace 的稳定 id
[[nodiscard]] const char* WorkspaceId(int index) {
    switch (index) {
    case 0: return "overview";
    case 1: return "novel";
    case 2: return "assets";
    case 3: return "storyboard";
    case 4: return "image";
    case 5: return "video";
    default: return "overview";
    }
}

[[nodiscard]] int WorkspaceIndexFromId(const std::string& id) {
    for (int i = 0; i < 6; ++i) {
        if (id == WorkspaceId(i)) {
            return i;
        }
    }
    return 0;
}

[[nodiscard]] QString ThemeNameNow() {
    if (theme::CurrentIsCustom()) {
        return QString::fromStdString(theme::CurrentCustomName());
    }
    const std::string_view dn = theme::ThemeDisplayName(theme::CurrentThemeId());
    return QString::fromUtf8(dn.data(), static_cast<int>(dn.size()));
}

[[nodiscard]] bool HasLlmKey() {
    const shine::AppSettings& s = shine::Settings();
    return !s.openaiApiKey.empty() || !s.mimoApiKey.empty() || !s.minimaxApiKey.empty();
}

// P04：小说工作区在活动栏/侧栏的稳定索引（WorkspaceNames：总控/小说/视觉资产/分镜/出图/出片）
[[nodiscard]] int NovelWorkspaceIndex() {
    return shine::app::WorkspaceNames().indexOf(QStringLiteral("小说"));
}

[[nodiscard]] int AssetWorkspaceIndex() {
    return shine::app::WorkspaceNames().indexOf(QStringLiteral("视觉资产"));
}

[[nodiscard]] int StoryboardWorkspaceIndex() {
    return shine::app::WorkspaceNames().indexOf(QStringLiteral("分镜"));
}

[[nodiscard]] int ImageWorkspaceIndex() {
    return shine::app::WorkspaceNames().indexOf(QStringLiteral("出图"));
}

void OpenImageForRef(ImageFlowWorkspace& workspace, const project::ProjectRef& ref) {
    workspace.SetContext(ref.dbPath, ref.rootDir);
}

[[nodiscard]] int VideoWorkspaceIndex() {
    return shine::app::WorkspaceNames().indexOf(QStringLiteral("出片"));
}

void OpenVideoForRef(VideoFlowWorkspace& workspace, const project::ProjectRef& ref) {
    workspace.SetContext(ref.dbPath, ref.rootDir);
}

void OpenStoryboardForRef(StoryboardWorkspace& workspace, const project::ProjectRef& ref) {
    QString err;
    (void)workspace.OpenBook(ref.dbPath, ref.rootDir, &err);
}

void OpenAssetForRef(AssetWorkspace& workspace, const project::ProjectRef& ref,
                     const std::string& lastNovel) {
    const auto books = project::ListBooks(ref);
    const project::BookRef* pick = nullptr;
    for (const project::BookRef& book : books) {
        if (!lastNovel.empty() && book.title == lastNovel) {
            pick = &book;
            break;
        }
    }
    if (pick == nullptr && !books.empty()) {
        pick = &books.front();
    }
    QString err;
    (void)workspace.OpenBook(pick != nullptr ? pick->dbPath : ref.dbPath,
                             pick != nullptr ? pick->rootDir : ref.rootDir, &err);
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ShineTV 工作室"));
    resize(1280, 800);

    pages_ = new QStackedWidget(this);
    setCentralWidget(pages_);

    BuildHub();
    BuildWorkshop();
    BuildPaletteCommands();

    // 快捷键（UI.md §2.3）：Ctrl+I 检查器 / Ctrl+J 底栏 / Ctrl+K 命令面板
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+I")), this), &QShortcut::activated,
            this, &MainWindow::ToggleInspector);
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+J")), this), &QShortcut::activated,
            this, &MainWindow::ToggleBottomDock);
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this), &QShortcut::activated,
            this, [this] { palette_->OpenPalette(); });

    RestoreLayout();
    RefreshStatusBar();

    // 上次打开的项目直接回场（P03 验收 #1：关掉再开还在原地）
    bool restored_project = false;
    if (!last_project_root_.empty()) {
        auto ref = svc_.Open(last_project_root_);
        if (ref) {
            EnterProject(*ref);
            restored_project = true;
        }
    }
    if (!restored_project) {
        ShowHubPage();
    }

    // 首启引导（只出现一次）+ 布局损坏提示（回退默认布局 + Toast，不崩）。
    // 验收自动化（截图包/探针/折叠判定）设 SHINE_P03_NOWIZARD 或 SHINE_P03_REVIEW 时跳过——
    // 模态向导会把无人值守链路卡死；firstrun 截图由 P03Review 自带。
    QTimer::singleShot(0, this, [this] {
        if (shine::Settings().firstRun && qEnvironmentVariableIsEmpty("SHINE_P03_NOWIZARD") &&
            qEnvironmentVariableIsEmpty("SHINE_P03_REVIEW")) {
            FirstRunWizard wizard(this, false);
            wizard.exec();
            RefreshStatusBar();
        }
        if (!layout_restored_) {
            shine::widgets::Toast::Show(
                QStringLiteral("布局状态文件损坏，已回退默认布局"), shine::widgets::Toast::Tone::Warning);
        }
    });
}

MainWindow::~MainWindow() = default;

// ────────────────────────────── 两态切换 ──────────────────────────────

void MainWindow::BuildHub() {
    hub_ = new ProjectHubView(&svc_, pages_);
    hub_->SetOnOpenProject([this](const project::ProjectRef& ref) { EnterProject(ref); });
    hub_->SetOnCreateProject([this] {
        ProjectWizardDialog wizard(&svc_, this);
        wizard.SetOnCreated([this](const project::ProjectRef& ref) { EnterProject(ref); });
        wizard.exec();
        hub_->Refresh();
    });
    hub_->SetOnOpenPath([this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("打开项目目录"));
        if (dir.isEmpty()) {
            return;
        }
        OpenProjectPath(shine::util::PathFromUtf8(std::string{dir.toUtf8().constData()}));
    });
    pages_->addWidget(hub_);
}

void MainWindow::ShowHubPage() {
    hub_->Refresh();
    pages_->setCurrentWidget(hub_);
    setWindowTitle(QStringLiteral("ShineTV 工作室"));
}

void MainWindow::ShowWorkshop() {
    pages_->setCurrentWidget(workshop_);
}

void MainWindow::EnterProject(const project::ProjectRef& ref) {
    last_project_root_ = ref.rootDir;
    top_bar_->SetProjectName(QString::fromStdString(ref.name));
    setWindowTitle(QStringLiteral("ShineTV 工作室 — %1").arg(QString::fromStdString(ref.name)));

    // 回到该项目上次的工作区 / 标签状态（project.json ui.*）
    if (const project::ProjectFile* file = svc_.CurrentFile(); file != nullptr) {
        rail_->SetCurrent(WorkspaceIndexFromId(file->lastWorkspace), false);
    }
    UpdateBreadcrumb();
    LoadNovelPages(ref); // P04：小说工作区按当前书（ui.lastNovel）载入 书→卷→章
    if (rail_->Current() == AssetWorkspaceIndex()) {
        EnsureAssetDocTab();
    }
    if (rail_->Current() == StoryboardWorkspaceIndex()) {
        EnsureStoryboardDocTab();
    }
    LoadAssetPages(ref);
    LoadImagePages(ref);
    LoadStoryboardPages(ref);
    LoadVideoPages(ref);
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* pipeline = dynamic_cast<PipelineWorkspace*>(doc_stack_->widget(i)); pipeline != nullptr) {
            pipeline->SetContext(ref.rootDir);
        }
    }
    RefreshStatusBar();
    ShowWorkshop();
    shine::widgets::Toast::Show(QStringLiteral("已打开项目：%1").arg(QString::fromStdString(ref.name)),
                                shine::widgets::Toast::Tone::Success);
}

bool MainWindow::OpenProjectPath(const std::filesystem::path& rootDir) {
    auto ref = svc_.Open(rootDir);
    if (!ref) {
        shine::widgets::Toast::Show(
            QString::fromStdString(ref.error().message), shine::widgets::Toast::Tone::Error);
        return false;
    }
    EnterProject(*ref);
    return true;
}

// ────────────────────────────── P04 文档页装配 ──────────────────────────────

void MainWindow::LoadNovelPages(const project::ProjectRef& ref) {
    const std::string lastNovel =
        svc_.CurrentFile() != nullptr ? svc_.CurrentFile()->lastNovel : std::string{};
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* nw = dynamic_cast<NovelWorkspace*>(doc_stack_->widget(i)); nw != nullptr) {
            nw->LoadFromRef(ref, lastNovel);
        }
    }
}

void MainWindow::LoadAssetPages(const project::ProjectRef& ref) {
    const std::string lastNovel =
        svc_.CurrentFile() != nullptr ? svc_.CurrentFile()->lastNovel : std::string{};
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* assets = dynamic_cast<AssetWorkspace*>(doc_stack_->widget(i)); assets != nullptr) {
            OpenAssetForRef(*assets, ref, lastNovel);
        }
    }
}

void MainWindow::LoadImagePages(const project::ProjectRef& ref) {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* image = dynamic_cast<ImageFlowWorkspace*>(doc_stack_->widget(i)); image != nullptr) {
            OpenImageForRef(*image, ref);
        }
    }
}

ImageFlowWorkspace* MainWindow::ImageFlowPage() const {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* image = dynamic_cast<ImageFlowWorkspace*>(doc_stack_->widget(i)); image != nullptr) {
            return image;
        }
    }
    return nullptr;
}

void MainWindow::LoadVideoPages(const project::ProjectRef& ref) {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* video = dynamic_cast<VideoFlowWorkspace*>(doc_stack_->widget(i)); video != nullptr) {
            OpenVideoForRef(*video, ref);
        }
    }
}

VideoFlowWorkspace* MainWindow::VideoFlowPage() const {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* video = dynamic_cast<VideoFlowWorkspace*>(doc_stack_->widget(i)); video != nullptr) {
            return video;
        }
    }
    return nullptr;
}

void MainWindow::LoadStoryboardPages(const project::ProjectRef& ref) {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* storyboard = dynamic_cast<StoryboardWorkspace*>(doc_stack_->widget(i));
            storyboard != nullptr) {
            OpenStoryboardForRef(*storyboard, ref);
        }
    }
}

StoryboardWorkspace* MainWindow::StoryboardPage() const {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* storyboard = dynamic_cast<StoryboardWorkspace*>(doc_stack_->widget(i));
            storyboard != nullptr) {
            return storyboard;
        }
    }
    return nullptr;
}

NovelWorkspace* MainWindow::NovelPage() const {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* nw = dynamic_cast<NovelWorkspace*>(doc_stack_->widget(i)); nw != nullptr) {
            return nw;
        }
    }
    return nullptr;
}

AssetWorkspace* MainWindow::AssetPage() const {
    for (int i = 0; i < doc_stack_->count(); ++i) {
        if (auto* assets = dynamic_cast<AssetWorkspace*>(doc_stack_->widget(i)); assets != nullptr) {
            return assets;
        }
    }
    return nullptr;
}

void MainWindow::SwitchWorkspace(int index) {
    rail_->SetCurrent(index, false); // 验收设施：显式接线（下方两步与 SetOnChanged 同路径，幂等）
    UpdateBreadcrumb();
    if (index == NovelWorkspaceIndex()) {
        EnsureNovelDocTab();
    }
    if (index == AssetWorkspaceIndex()) {
        EnsureAssetDocTab();
    }
    if (index == StoryboardWorkspaceIndex()) {
        EnsureStoryboardDocTab();
    }
    if (index == ImageWorkspaceIndex()) {
        EnsureImageDocTab();
    }
    if (index == VideoWorkspaceIndex()) {
        EnsureVideoDocTab();
    }
}

void MainWindow::EnsureNovelDocTab() {
    const QString name = shine::app::WorkspaceNames().value(NovelWorkspaceIndex());
    for (int i = 0; i < doc_tabs_->count(); ++i) {
        if (doc_tabs_->tabText(i) == name) {
            doc_tabs_->setCurrentIndex(i);
            doc_stack_->setCurrentIndex(i);
            UpdateBreadcrumb();
            return;
        }
    }
    AddDocTab(name);
}

void MainWindow::EnsureAssetDocTab() {
    const QString name = shine::app::WorkspaceNames().value(AssetWorkspaceIndex());
    for (int i = 0; i < doc_tabs_->count(); ++i) {
        if (doc_tabs_->tabText(i) == name) {
            doc_tabs_->setCurrentIndex(i);
            doc_stack_->setCurrentIndex(i);
            UpdateBreadcrumb();
            return;
        }
    }
    AddDocTab(name);
}

void MainWindow::EnsureStoryboardDocTab() {
    const QString name = shine::app::WorkspaceNames().value(StoryboardWorkspaceIndex());
    for (int i = 0; i < doc_tabs_->count(); ++i) {
        if (doc_tabs_->tabText(i) == name) {
            doc_tabs_->setCurrentIndex(i);
            doc_stack_->setCurrentIndex(i);
            UpdateBreadcrumb();
            return;
        }
    }
    AddDocTab(name);
}

void MainWindow::EnsureImageDocTab() {
    const QString name = shine::app::WorkspaceNames().value(ImageWorkspaceIndex());
    for (int i = 0; i < doc_tabs_->count(); ++i) {
        if (doc_tabs_->tabText(i) == name) {
            doc_tabs_->setCurrentIndex(i);
            doc_stack_->setCurrentIndex(i);
            UpdateBreadcrumb();
            return;
        }
    }
    AddDocTab(name);
}

void MainWindow::EnsureVideoDocTab() {
    const QString name = shine::app::WorkspaceNames().value(VideoWorkspaceIndex());
    for (int i = 0; i < doc_tabs_->count(); ++i) {
        if (doc_tabs_->tabText(i) == name) {
            doc_tabs_->setCurrentIndex(i);
            doc_stack_->setCurrentIndex(i);
            UpdateBreadcrumb();
            return;
        }
    }
    AddDocTab(name);
}

QWidget* MainWindow::MakeDocPage(const QString& title) {
    if (title == QStringLiteral("总控台")) {
        auto* pipeline = new PipelineWorkspace(doc_stack_);
        if (auto ref = svc_.Current()) pipeline->SetContext(ref->rootDir);
        return pipeline;
    }
    if (title == shine::app::WorkspaceNames().value(NovelWorkspaceIndex())) {
        auto* nw = new NovelWorkspace(doc_stack_);
        if (auto ref = svc_.Current()) {
            const std::string lastNovel =
                svc_.CurrentFile() != nullptr ? svc_.CurrentFile()->lastNovel : std::string{};
            nw->LoadFromRef(*ref, lastNovel);
        }
        // 页面不再自摆右栏：把它的属性内容挂到外壳检查器上
        if (QWidget* body = nw->InspectorBody(); body != nullptr) {
            right_->ClearSections();
            right_->AddSection(QStringLiteral("章节属性"), body);
            right_->SetSelection(QStringLiteral("选中一个章节后显示其属性"));
        }
        return nw;
    }
    if (title == shine::app::WorkspaceNames().value(AssetWorkspaceIndex())) {
        auto* assets = new AssetWorkspace(doc_stack_);
        if (auto ref = svc_.Current()) {
            const std::string lastNovel =
                svc_.CurrentFile() != nullptr ? svc_.CurrentFile()->lastNovel : std::string{};
            OpenAssetForRef(*assets, *ref, lastNovel);
        }
        // 页面不再自摆第三列：把它的详情内容挂到外壳检查器上
        if (QWidget* body = assets->InspectorBody(); body != nullptr) {
            right_->ClearSections();
            right_->AddSection(QStringLiteral("资产详情"), body);
            right_->SetSelection(QStringLiteral("选中一个资产后显示其设定集 / 一致性 / 参考图"));
        }
        return assets;
    }
    if (title == shine::app::WorkspaceNames().value(StoryboardWorkspaceIndex())) {
        auto* storyboard = new StoryboardWorkspace(doc_stack_);
        if (auto ref = svc_.Current()) {
            OpenStoryboardForRef(*storyboard, *ref);
        }
        return storyboard;
    }
    if (title == shine::app::WorkspaceNames().value(ImageWorkspaceIndex())) {
        auto* image = new ImageFlowWorkspace(doc_stack_);
        if (auto ref = svc_.Current()) OpenImageForRef(*image, *ref);
        return image;
    }
    if (title == shine::app::WorkspaceNames().value(VideoWorkspaceIndex())) {
        auto* video = new VideoFlowWorkspace(doc_stack_);
        if (auto ref = svc_.Current()) OpenVideoForRef(*video, *ref);
        return video;
    }
    auto* page = new QLabel(QStringLiteral("%1 · 当前工作区（P04–P10 填充）").arg(title), doc_stack_);
    page->setAlignment(Qt::AlignCenter);
    return page;
}

void MainWindow::AddDocTab(const QString& title) {
    const int idx = doc_stack_->addWidget(MakeDocPage(title));
    const int tab = doc_tabs_->addTab(title);
    doc_tabs_->setCurrentIndex(tab);
    doc_stack_->setCurrentIndex(idx);
    UpdateBreadcrumb();
}

void MainWindow::CloseProjectToHub() {
    (void)svc_.Close();
    last_project_root_.clear();
    top_bar_->SetProjectName(QString{});
    ShowHubPage();
}

// ────────────────────────────── 七区装配 ──────────────────────────────

void MainWindow::BuildWorkshop() {
    workshop_ = new QWidget(pages_);
    auto* lay = new QVBoxLayout(workshop_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    top_bar_ = new TopBar(workshop_);
    top_bar_->SetOnShowHub([this] { ShowHubPage(); });
    top_bar_->SetOnCloseProject([this] { CloseProjectToHub(); });
    top_bar_->SetOnRevealProject([this] {
        if (!last_project_root_.empty()) {
            const QString p = QString::fromStdWString(last_project_root_.wstring());
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
        }
    });
    top_bar_->SetOnOpenPalette([this] { palette_->OpenPalette(); });
    top_bar_->SetOnToggleInspector([this] { ToggleInspector(); });
    top_bar_->SetOnRun([] {
        shine::widgets::Toast::Show(QStringLiteral("全流程运行由 P09 接入（当前为占位按钮）"),
                                    shine::widgets::Toast::Tone::Info);
    });
    top_bar_->SetOnStop([] {
        shine::widgets::Toast::Show(QStringLiteral("停止策略由 P09 接入（当前为占位按钮）"),
                                    shine::widgets::Toast::Tone::Info);
    });
    top_bar_->SetOnSettings([this] {
        FirstRunWizard wizard(this, true);
        wizard.exec();
        RefreshStatusBar();
    });
    top_bar_->SetOnThemeChanged([this] { RefreshStatusBar(); });
    lay->addWidget(top_bar_);

    crumb_ = new Breadcrumb(workshop_);
    crumb_->SetOnPick([this](int i) {
        if (i == 0) {
            ShowHubPage();
        }
    });

    // 中央工作区：面包屑 + 文档标签 + 页面栈（P04–P08 填充）
    auto* center = new QWidget(workshop_);
    auto* cl = new QVBoxLayout(center);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    cl->addWidget(crumb_);

    auto* tabRow = new QWidget(center);
    auto* trl = new QHBoxLayout(tabRow);
    trl->setContentsMargins(theme::space::kSteps[3], 0, theme::space::kSteps[2], 0);
    trl->setSpacing(theme::space::kSteps[1]);
    auto* tabs = new DocTabBar(tabRow);
    doc_tabs_ = tabs;
    auto* addTab = new shine::widgets::IconButton(QStringLiteral("＋"), QStringLiteral("新建标签页"),
                                                  shine::widgets::IconButton::Size::Sm, tabRow);
    trl->addWidget(doc_tabs_, 1);
    trl->addWidget(addTab, 0, Qt::AlignVCenter);
    cl->addWidget(tabRow);

    doc_stack_ = new QStackedWidget(center);
    cl->addWidget(doc_stack_, 1);

    auto add_doc = [this](const QString& title) { AddDocTab(title); };
    add_doc(QStringLiteral("总控台"));
    connect(addTab, &shine::widgets::IconButton::clicked, this,
            [this, add_doc] { add_doc(QStringLiteral("标签 %1").arg(doc_tabs_->count() + 1)); });
    connect(doc_tabs_, &QTabBar::currentChanged, this,
            [this](int i) {
                if (i >= 0 && i < doc_stack_->count()) {
                    doc_stack_->setCurrentIndex(i);
                }
                UpdateBreadcrumb();
            });
    auto close_doc = [this](int i) {
        if (doc_tabs_->count() <= 1) {
            return; // 至少留一页
        }
        if (QWidget* page = doc_stack_->widget(i); page != nullptr) {
            doc_stack_->removeWidget(page);
            page->deleteLater();
        }
        doc_tabs_->removeTab(i);
        UpdateBreadcrumb();
    };
    connect(doc_tabs_, &QTabBar::tabCloseRequested, this, close_doc);
    tabs->on_middle_close = close_doc;
    tabs->on_rename = [this](int i) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("重命名标签"),
                                                   QStringLiteral("新名称："), QLineEdit::Normal,
                                                   doc_tabs_->tabText(i), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            doc_tabs_->setTabText(i, name.trimmed());
            if (QWidget* page = doc_stack_->widget(i); page != nullptr) {
                page->setToolTip(name.trimmed());
            }
            UpdateBreadcrumb();
        }
    };

    // 侧栏（Ctrl+B）+ 活动栏 + 右栏
    rail_ = new ActivityRail(workshop_);
    rail_->SetOnChanged([this](int i) {
        if (i == ImageWorkspaceIndex()) {
            EnsureImageDocTab();
        }
        if (i == VideoWorkspaceIndex()) {
            EnsureVideoDocTab();
        }
        UpdateBreadcrumb();
        if (i == NovelWorkspaceIndex()) {
            EnsureNovelDocTab(); // P04：切到小说工作区 → 确保「小说」页签在
        }
        if (i == AssetWorkspaceIndex()) {
            EnsureAssetDocTab(); // P05：切到视觉资产工作区 → 确保资产页签在
        }
    });
    right_ = new RightPanel(workshop_); // 右侧检查器（默认收起，Ctrl+I / 顶栏按钮开合）

    // 三区骨架：活动栏（固定 56）│ 中央工作区（吃掉全部剩余宽度）│ 右侧检查器（可收起）
    // 上一版是 56/240/900/280 四列常驻且中央区没有最小宽度，左中右一起抢空间，
    // 章节树被压成残条；这里把中央区设为唯一可拉伸项并给硬下限，检查器默认收起。
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    rail_->setFixedWidth(56);
    row->addWidget(rail_);

    hsplit_ = new shine::widgets::Splitter(Qt::Horizontal, workshop_);
    center->setMinimumWidth(kCenterMinW);
    hsplit_->addWidget(center);
    hsplit_->addWidget(right_);
    hsplit_->setStretchFactor(0, 1);   // 只有中央区可拉伸
    hsplit_->setCollapsible(1, true);  // 检查器可收到 0
    row->addWidget(hsplit_, 1);
    lay->addLayout(row, 1);

    bottom_ = new BottomDock(workshop_);
    bottom_->SetOnTabChanged([this](int) { UpdateBreadcrumb(); });
    lay->addWidget(bottom_);

    status_bar_ = new StatusBar(workshop_);
    status_bar_->SetOnPick([this](StatusItem item) { ShowStatusDetail(item); });
    lay->addWidget(status_bar_);

    pages_->addWidget(workshop_);
}

// ────────────────────────────── 折叠（Ctrl+I / Ctrl+J）──────────────────────────────

void MainWindow::ToggleInspector() {
    // 检查器是最后一格：收起 = 宽度归零（sizes{*,0}），展开 = 回到上次宽度
    const QList<int> sizes = hsplit_->sizes();
    const int cur = sizes.size() >= 2 ? sizes[1] : 0;
    const bool collapsed = cur <= 0;
    const int target = collapsed ? (inspector_last_w_ > 0 ? inspector_last_w_ : 320) : 0;
    if (!collapsed) {
        inspector_last_w_ = std::max(280, cur);
    } else {
        right_->show();
    }
    if (inspector_tween_ == nullptr) {
        inspector_tween_ = new shine::motion::Tween(theme::motion::kStandard, this);
    }
    inspector_tween_->Run(cur, target, theme::motion::kDurBaseMs, [this](const QVariant& v) {
        QList<int> s = hsplit_->sizes();
        if (s.size() < 2) {
            return;
        }
        const int val = std::max(0, v.toInt());
        const int delta = val - s[1];
        s[1] = val;
        s[0] = std::max(kCenterMinW, s[0] - delta); // 中央区永不低于下限
        hsplit_->setSizes(s);
    });
    if (shine::motion::ReduceMotion()) {
        inspector_tween_->Settle();
    }
    inspector_visible_ = target > 0;
    top_bar_->SetInspectorActive(inspector_visible_);
}

void MainWindow::ToggleBottomDock() {
    const bool collapsed = bottom_->isHidden() || bottom_->height() <= 0;
    const int cur = collapsed ? 0 : bottom_->height();
    const int target = collapsed ? (bottom_last_h_ > 0 ? bottom_last_h_ : 220) : 0;
    if (collapsed) {
        bottom_->show();
    } else {
        bottom_last_h_ = std::max(120, cur);
    }
    if (bottom_tween_ == nullptr) {
        bottom_tween_ = new shine::motion::Tween(theme::motion::kStandard, this);
    }
    bottom_tween_->Run(cur, target, theme::motion::kDurBaseMs, [this](const QVariant& v) {
        const int val = v.toInt();
        if (val <= 0) {
            // 到 0 就地收起并释放高度约束（不需要 finished 信号，减少一次生命周期管理）
            bottom_->hide();
            bottom_->setMinimumHeight(0);
            bottom_->setMaximumHeight(16777215);
        } else {
            bottom_->setFixedHeight(val);
        }
    });
    if (shine::motion::ReduceMotion()) {
        bottom_tween_->Settle();
    }
    bottom_visible_ = target > 0;
}

// ────────────────────────────── 状态栏 ──────────────────────────────

void MainWindow::RefreshStatusBar() {
    // Comfy 健康检查由 P07 接入；P03 只报「未连接」+ 可点开详情
    status_bar_->SetComfy(QStringLiteral("Comfy 未连接"), "idle");
    if (HasLlmKey()) {
        status_bar_->SetLlm(QStringLiteral("LLM 就绪"), "ok");
    } else {
        status_bar_->SetLlm(QStringLiteral("LLM 未配置"), "warn");
    }
    status_bar_->SetQueue(0);
    status_bar_->SetProgress(0, 0, QStringLiteral("未运行"));
    status_bar_->SetThemeName(ThemeNameNow());
    status_bar_->SetVersion(QStringLiteral("v" SHINE_VERSION));
}

shine::widgets::Drawer* MainWindow::ShowStatusDetail(StatusItem item) {
    using shine::data::KeyValue;
    switch (item) {
    case StatusItem::Queue:
        if (bottom_->isHidden()) {
            ToggleBottomDock();
        }
        bottom_->SetCurrentTab(0);
        return nullptr;
    case StatusItem::Theme:
        top_bar_->PopupThemeMenu();
        return nullptr;
    case StatusItem::Comfy: {
        auto* drawer = new shine::widgets::Drawer(QStringLiteral("ComfyUI"), this);
        auto* kv = new KeyValue(drawer);
        kv->SetPairs({{QStringLiteral("地址"), QString::fromStdString(shine::Settings().comfyBaseUrl)},
                      {QStringLiteral("状态"), QStringLiteral("未连接（P07 接入健康检查）")},
                      {QStringLiteral("队列"), QStringLiteral("0")}});
        drawer->BodyLayout()->addWidget(kv);
        drawer->Open();
        return drawer;
    }
    case StatusItem::Llm: {
        auto* drawer = new shine::widgets::Drawer(QStringLiteral("LLM"), this);
        auto* kv = new KeyValue(drawer);
        kv->SetPairs({{QStringLiteral("供应商"), QString::fromStdString(shine::Settings().llmProvider)},
                      {QStringLiteral("模型"), QString::fromStdString(shine::Settings().mimoModel)},
                      {QStringLiteral("Key"), HasLlmKey() ? QStringLiteral("已配置（日志不打印）")
                                                          : QStringLiteral("未配置")}});
        drawer->BodyLayout()->addWidget(kv);
        drawer->Open();
        return drawer;
    }
    case StatusItem::Progress: {
        auto* drawer = new shine::widgets::Drawer(QStringLiteral("阶段进度"), this);
        auto* kv = new KeyValue(drawer);
        kv->SetPairs({{QStringLiteral("当前阶段"), QStringLiteral("未运行")},
                      {QStringLiteral("预算/章"), QStringLiteral("%1 次调用")
                                                      .arg(shine::Settings().novelMaxLlmCallsPerChapter)},
                      {QStringLiteral("说明"), QStringLiteral("全流程编排由 P09 接入")}});
        drawer->BodyLayout()->addWidget(kv);
        drawer->Open();
        return drawer;
    }
    case StatusItem::Version: {
        auto* drawer = new shine::widgets::Drawer(QStringLiteral("关于"), this);
        auto* kv = new KeyValue(drawer);
        kv->SetPairs({{QStringLiteral("版本"), QStringLiteral("v" SHINE_VERSION)},
                      {QStringLiteral("项目"), CurrentProjectName()},
                      {QStringLiteral("数据目录"),
                       QString::fromStdWString(LayoutFile().parent_path().wstring())}});
        drawer->BodyLayout()->addWidget(kv);
        drawer->Open();
        return drawer;
    }
    }
    return nullptr;
}

void MainWindow::UpdateBreadcrumb() {
    QStringList crumbs;
    crumbs << (svc_.Current() ? QString::fromStdString(svc_.Current()->name) : QStringLiteral("无项目"));
    crumbs << shine::app::WorkspaceNames().value(rail_ != nullptr ? rail_->Current() : 0);
    crumbs << (doc_tabs_ != nullptr && doc_tabs_->currentIndex() >= 0
                   ? doc_tabs_->tabText(doc_tabs_->currentIndex())
                   : QStringLiteral("—"));
    crumb_->SetPath(crumbs);
}

// ────────────────────────────── 命令面板数据 ──────────────────────────────

void MainWindow::BuildPaletteCommands() {
    palette_ = new CommandPalette(this);
    std::vector<CommandItem> items;

    auto cmd = [&items](const QString& title, const QString& shortcut, std::function<void()> action) {
        items.push_back({CommandItem::Kind::Command, title, shortcut, {}, std::move(action)});
    };
    auto pageItem = [&items](const QString& title, std::function<void()> action) {
        items.push_back({CommandItem::Kind::Page, title, {}, {}, std::move(action)});
    };

    cmd(QStringLiteral("新建项目"), QStringLiteral("Ctrl+N"), [this] {
        ProjectWizardDialog wizard(&svc_, this);
        wizard.SetOnCreated([this](const project::ProjectRef& ref) { EnterProject(ref); });
        wizard.exec();
        hub_->Refresh();
    });
    cmd(QStringLiteral("打开项目…"), {}, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("打开项目目录"));
        if (!dir.isEmpty()) {
            OpenProjectPath(shine::util::PathFromUtf8(std::string{dir.toUtf8().constData()}));
        }
    });
    cmd(QStringLiteral("项目列表"), {}, [this] { ShowHubPage(); });
    cmd(QStringLiteral("关闭项目"), {}, [this] { CloseProjectToHub(); });
    cmd(QStringLiteral("运行当前流程"), QStringLiteral("Ctrl+Enter"), [] {
        shine::widgets::Toast::Show(QStringLiteral("全流程运行由 P09 接入（当前为占位命令）"),
                                    shine::widgets::Toast::Tone::Info);
    });
    cmd(QStringLiteral("新建章节"), {}, [] {
        shine::widgets::Toast::Show(QStringLiteral("章节操作由 P04 接入（当前为占位命令）"),
                                    shine::widgets::Toast::Tone::Info);
    });
    cmd(QStringLiteral("切换检查器"), QStringLiteral("Ctrl+I"), [this] { ToggleInspector(); });
    cmd(QStringLiteral("切换底栏"), QStringLiteral("Ctrl+J"), [this] { ToggleBottomDock(); });
    cmd(QStringLiteral("样式编辑器…"), {}, [this] {
        auto* dlg = new StyleEditorDialog();
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    cmd(QStringLiteral("减少动效：开 / 关"), {}, [] {
        theme::ThemeService::SetReduceMotionPersisted(!shine::motion::ReduceMotion());
    });
    for (const theme::ThemeId id : theme::kAllThemes) {
        const std::string_view dn = theme::ThemeDisplayName(id);
        const QString name = QString::fromUtf8(dn.data(), static_cast<int>(dn.size()));
        cmd(QStringLiteral("主题：%1").arg(name), {}, [this, id] {
            theme::ThemeService::Switch(id);
            RefreshStatusBar();
        });
    }

    const QStringList& names = shine::app::WorkspaceNames();
    for (int i = 0; i < names.size(); ++i) {
        pageItem(QStringLiteral("前往：%1").arg(names[i]), [this, i] {
            rail_->SetCurrent(i);
            ShowWorkshop();
        });
    }
    const QStringList dockTabs = {QStringLiteral("任务队列"), QStringLiteral("日志"),
                                  QStringLiteral("产物"), QStringLiteral("校验报告")};
    for (int i = 0; i < dockTabs.size(); ++i) {
        pageItem(QStringLiteral("前往：%1").arg(dockTabs[i]), [this, i] {
            if (bottom_->isHidden()) {
                ToggleBottomDock();
            }
            bottom_->SetCurrentTab(i);
        });
    }

    palette_->SetCommands(std::move(items));
    palette_->SetEntityProvider([this](const QString&) {
        std::vector<CommandItem> entities;
        if (const std::optional<project::ProjectRef> ref = svc_.Current()) {
            entities.push_back({CommandItem::Kind::Entity,
                                QStringLiteral("项目 · %1").arg(QString::fromStdString(ref->name)),
                                {},
                                QString::fromStdWString(ref->rootDir.wstring()),
                                [this] { ShowWorkshop(); }});
        }
        for (const project::RecentEntry& e : svc_.Recent()) {
            const std::filesystem::path root = e.rootDir;
            entities.push_back({CommandItem::Kind::Entity,
                                QStringLiteral("最近 · %1").arg(QString::fromStdString(e.name)),
                                {},
                                QString::fromStdWString(root.wstring()),
                                [this, root] { OpenProjectPath(root); }});
        }
        return entities;
    });
}

QString MainWindow::CurrentProjectName() const {
    if (const project::ProjectFile* file = svc_.CurrentFile(); file != nullptr) {
        return QString::fromStdString(file->name);
    }
    return {};
}

// ────────────────────────────── 布局持久化 ──────────────────────────────

std::filesystem::path MainWindow::LayoutFile() const {
    const QString appdata = qEnvironmentVariable("APPDATA");
    std::filesystem::path dir;
    if (!appdata.isEmpty()) {
        dir = std::filesystem::path{appdata.toStdWString()} / L"ShineTVStudio";
    } else {
        dir = std::filesystem::current_path();
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / L"layout.dat";
}

void MainWindow::SaveLayout() {
    // ui 状态写回项目文件（ui.lastWorkspace）——关窗与验收探针共用同一落点，
    // 否则无人值守 _Exit 的跑法会漏写，下次进项目被默认值覆盖（实测踩过）。
    if (project::ProjectFile* file = svc_.CurrentFile(); file != nullptr) {
        file->lastWorkspace = WorkspaceId(rail_->Current());
        if (const std::optional<project::ProjectRef> ref = svc_.Current()) {
            (void)svc_.Save(*ref);
        }
    }
    QJsonObject o;
    o[QStringLiteral("magic")] = QStringLiteral("shinetv-layout-1");
    o[QStringLiteral("geometry")] = QString::fromLatin1(saveGeometry().toBase64());
    o[QStringLiteral("windowState")] = QString::fromLatin1(saveState().toBase64());
    o[QStringLiteral("inspectorVisible")] = inspector_visible_;
    o[QStringLiteral("bottomVisible")] = bottom_visible_;
    o[QStringLiteral("inspectorLastW")] = inspector_last_w_;
    o[QStringLiteral("bottomLastH")] = bottom_last_h_;
    QJsonArray hs;
    for (const int s : hsplit_->sizes()) {
        hs.append(s);
    }
    o[QStringLiteral("hSizes")] = hs;
    o[QStringLiteral("workspace")] = rail_->Current();
    o[QStringLiteral("bottomTab")] = bottom_->CurrentTab();
    QJsonArray docs;
    for (int i = 0; i < doc_tabs_->count(); ++i) {
        docs.append(doc_tabs_->tabText(i));
    }
    o[QStringLiteral("docTabs")] = docs;
    o[QStringLiteral("docTab")] = doc_tabs_->currentIndex();
    o[QStringLiteral("lastProjectRoot")] =
        QString::fromStdString(shine::util::PathToUtf8(last_project_root_));

    const std::filesystem::path file = LayoutFile();
    (void)shine::util::WriteFileBytes(file,
                                      QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString());
}

void MainWindow::RestoreLayout() {
    layout_restored_ = true;
    const auto bytes = shine::util::ReadFileBytes(LayoutFile());
    if (!bytes) {
        return; // 首次启动：默认布局，不算损坏
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(*bytes), &err);
    const QJsonObject o = doc.object();
    if (err.error != QJsonParseError::NoError || !doc.isObject() ||
        o[QStringLiteral("magic")].toString() != QStringLiteral("shinetv-layout-1")) {
        layout_restored_ = false; // 被手改坏 → 回退默认布局（风险表对策）
        return;
    }
    restoreGeometry(QByteArray::fromBase64(o[QStringLiteral("geometry")].toString().toLatin1()));
    restoreState(QByteArray::fromBase64(o[QStringLiteral("windowState")].toString().toLatin1()));
    // 旧版 layout.dat 写的是四列 {56,240,900,280}（sideVisible/sideLastW），
    // 读不到新键时按「检查器收起」处理并提示一次，避免把旧的 240 当检查器宽度恢复。
    const bool has_new = o.contains(QStringLiteral("inspectorVisible"));
    inspector_visible_ = has_new ? o[QStringLiteral("inspectorVisible")].toBool(false) : false;
    bottom_visible_ = o[QStringLiteral("bottomVisible")].toBool(true);
    inspector_last_w_ = o[QStringLiteral("inspectorLastW")].toInt(320);
    bottom_last_h_ = o[QStringLiteral("bottomLastH")].toInt(220);

    QList<int> sizes;
    for (const QJsonValue& v : o[QStringLiteral("hSizes")].toArray()) {
        sizes.append(v.toInt());
    }
    if (has_new && sizes.size() >= 2) {
        hsplit_->setSizes(sizes);
    } else {
        // 首次 / 旧版：检查器收起，中央区吃掉除活动栏外的全部宽度
        hsplit_->setSizes({std::max(kCenterMinW, hsplit_->width() - 56), 0});
    }
    right_->setVisible(inspector_visible_);
    top_bar_->SetInspectorActive(inspector_visible_);
    bottom_->setVisible(bottom_visible_);
    rail_->SetCurrent(o[QStringLiteral("workspace")].toInt(0), false);
    bottom_->SetCurrentTab(o[QStringLiteral("bottomTab")].toInt(0));

    // 工作区标签状态（标题逐字还原，双击改名也保得住）
    while (doc_tabs_->count() > 0) {
        doc_tabs_->removeTab(0); // QTabBar 没有 clear()（QListWidget 才有）——逐个摘
    }
    while (doc_stack_->count() > 0) {
        QWidget* w = doc_stack_->widget(0);
        doc_stack_->removeWidget(w);
        w->deleteLater();
    }
    QStringList titles;
    for (const QJsonValue& v : o[QStringLiteral("docTabs")].toArray()) {
        titles << v.toString();
    }
    if (titles.isEmpty()) {
        titles << QStringLiteral("总控台");
    }
    for (const QString& t : titles) {
        doc_stack_->addWidget(MakeDocPage(t));
        doc_tabs_->addTab(t);
    }
    const int docTab = o[QStringLiteral("docTab")].toInt(0);
    doc_tabs_->setCurrentIndex(docTab >= 0 && docTab < doc_tabs_->count() ? docTab : 0);
    doc_stack_->setCurrentIndex(doc_tabs_->currentIndex());

    last_project_root_ =
        shine::util::PathFromUtf8(o[QStringLiteral("lastProjectRoot")].toString().toStdString());
    UpdateBreadcrumb();
}

QString MainWindow::LayoutProbe() const {
    QStringList sizes;
    for (const int s : hsplit_->sizes()) {
        sizes << QString::number(s);
    }
    QStringList out;
    out << QStringLiteral("project=%1").arg(CurrentProjectName());
    out << QStringLiteral("window=%1x%2").arg(width()).arg(height());
    out << QStringLiteral("workspace=%1").arg(rail_->Current());
    out << QStringLiteral("bottomTab=%1").arg(bottom_->CurrentTab());
    out << QStringLiteral("inspector=%1").arg(hsplit_->sizes().value(1, 0) > 0 ? 1 : 0);
    out << QStringLiteral("bottom=%1").arg(bottom_->isHidden() ? 0 : 1);
    out << QStringLiteral("hSizes=%1").arg(sizes.join(QLatin1Char(',')));
    out << QStringLiteral("docTabs=%1").arg(doc_tabs_->count());
    out << QStringLiteral("docTab=%1").arg(doc_tabs_->currentIndex());
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

void MainWindow::SetTestLayout() {
    // 固定的非默认布局（验收：重启后逐项还原，全默认值比不出「还原」）
    resize(1400, 880);
    rail_->SetCurrent(2, false);
    right_->setVisible(true);
    inspector_visible_ = true;
    inspector_last_w_ = 300;
    hsplit_->setSizes({std::max(kCenterMinW, 1400 - 56 - 300), 300});
    bottom_->setVisible(true);
    bottom_->SetCurrentTab(1);
    if (QWidget* page = doc_stack_->widget(0); page != nullptr) {
        doc_tabs_->setTabText(0, QStringLiteral("分镜草稿"));
    }
    UpdateBreadcrumb();
}

void MainWindow::closeEvent(QCloseEvent* ev) {
    SaveLayout(); // 含 ui 状态写回项目文件
    ev->accept();
}

} // namespace shine::app
