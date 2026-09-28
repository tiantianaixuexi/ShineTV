#include "ui/pages/project/ProjectHubView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Surfaces.h"
#include "project/ProjectIndex.h"
#include "project/ProjectTemplate.h"
#include "util/Encoding.h"

#include <QContextMenuEvent>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace shine::app {
namespace {

// UTF-8 转换（文本约定 util/Encoding.h：进出字符串一律 UTF-8）
[[nodiscard]] QString FromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

[[nodiscard]] QString PathText(const std::filesystem::path& p) {
    return FromUtf8(shine::util::PathToUtf8(p));
}

// 项目名首字（自绘封面用；含代理对安全）
[[nodiscard]] QString FirstGlyph(const QString& s) {
    if (s.isEmpty()) {
        return QStringLiteral("?");
    }
    const QChar c0 = s.at(0);
    if (c0.isHighSurrogate() && s.size() >= 2 && s.at(1).isLowSurrogate()) {
        return s.mid(0, 2);
    }
    return QString{c0};
}

// 相对时间：今天 HH:mm / 昨天 HH:mm / N 天前 / 更早日期
[[nodiscard]] QString RelativeTime(std::chrono::system_clock::time_point tp) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms).toLocalTime();
    const int days = dt.date().daysTo(QDate::currentDate());
    if (days <= 0) {
        return QStringLiteral("今天 ") + dt.toString(QStringLiteral("HH:mm"));
    }
    if (days == 1) {
        return QStringLiteral("昨天 ") + dt.toString(QStringLiteral("HH:mm"));
    }
    if (days < 7) {
        return QStringLiteral("%1 天前").arg(days);
    }
    return dt.toString(QStringLiteral("yyyy-MM-dd")); // 更早日期
}

// 卡片第二行：模板中文名 · 相对时间（模板读 project.json，缺省只显示时间）
[[nodiscard]] QString MetaText(const shine::project::RecentEntry& entry) {
    QString tpl;
    if (auto file = shine::project::LoadProjectFile(entry.rootDir); file.has_value()) {
        if (const shine::project::ProjectTemplate* t =
                shine::project::FindTemplate(file->templateId);
            t != nullptr) {
            tpl = FromUtf8(t->name);
        }
    }
    const QString when = RelativeTime(entry.lastOpened);
    return tpl.isEmpty() ? when : (tpl + QStringLiteral(" · ") + when);
}

// 封面占位（16:9）：Token 色渐变 + 首字大字。自绘铺满，任何时刻不闪白；
// 取色只走 shine::widgets::TokenQColor(shine::theme::Current().xxx)，零内联色值。
class CoverMark final : public QWidget {
  public:
    CoverMark(const QString& glyph, int scheme, QWidget* parent)
        : QWidget(parent), glyph_(glyph), scheme_(scheme) {
        setFixedSize(192, 108); // 16:9
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

  protected:
    void paintEvent(QPaintEvent* ev) override {
        QWidget::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::ColorToken& t = theme::Current();
        p.fillRect(rect(), widgets::TokenQColor(t.bgPanel)); // 先铺不透明底色
        QLinearGradient grad(rect().topLeft(), rect().bottomRight());
        const QColor accents[3] = {widgets::TokenQColor(t.accentPrimary),
                                   widgets::TokenQColor(t.accentSecondary),
                                   widgets::TokenQColor(t.accentInfo)};
        grad.setColorAt(0.0, accents[scheme_ % 3]);
        grad.setColorAt(0.55, widgets::TokenQColor(t.bgPanel));
        grad.setColorAt(1.0, widgets::TokenQColor(t.bgElevated));
        p.fillRect(rect(), grad);
        QFont f = font();
        f.setPixelSize(theme::font::kSizes[5]);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(widgets::TokenQColor(t.textPrimary));
        p.drawText(rect(), Qt::AlignCenter, glyph_);
    }

  private:
    QString glyph_;
    int scheme_ = 0;
};

// 项目卡片：Card(Elevated) 自带 hover 抬升；左键=打开，右键=上下文菜单
class ProjectCard final : public widgets::Card {
  public:
    explicit ProjectCard(QWidget* parent)
        : widgets::Card(widgets::Card::Variant::Elevated, parent) {}

    std::function<void(const QPoint&)> on_context;

  protected:
    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton) {
            Card::mousePressEvent(ev); // hover / 点击全用 Card 自身行为
        } else {
            ev->accept(); // 右键不触发打开，交给 contextMenuEvent
        }
    }

    void contextMenuEvent(QContextMenuEvent* ev) override {
        if (on_context) {
            on_context(ev->globalPos());
        }
        ev->accept();
    }
};

} // namespace

ProjectHubView::ProjectHubView(shine::project::ProjectService* svc, QWidget* parent)
    : QWidget(parent), svc_(svc) {
    // webui views.css .hub-inner：max-width 1080 居中 + padding 40 32 60。
    // 这里用外层铺满 + 内层定宽居中的两段式，等价于 margin: 0 auto。
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* inner_host = new QWidget(this);
    auto* inner_row = new QHBoxLayout(inner_host);
    inner_row->setContentsMargins(0, 0, 0, 0);
    inner_row->setSpacing(0);

    auto* root = new QVBoxLayout;
    root->setContentsMargins(40, 32, 40, 60);
    root->setSpacing(theme::space::kSteps[4]);

    auto* inner = new QWidget(inner_host);
    inner->setLayout(root);
    inner->setMaximumWidth(1080);
    inner_row->addStretch(1);
    inner_row->addWidget(inner, 10);
    inner_row->addStretch(1);
    shell->addWidget(inner_host, 1);

    // —— 顶行：大标题 + 主/次操作（UI.md §2.1）——
    auto* head = new QHBoxLayout();
    head->setSpacing(theme::space::kSteps[3]);
    auto* title = new QLabel(QStringLiteral("ShineTV Studio"), this);
    widgets::SetKind(title, "dialogtitle");
    widgets::SetSemibold(title, true);
    {
        QFont f = title->font();
        f.setPixelSize(theme::font::kSizes[5]);
        title->setFont(f);
    }
    head->addWidget(title);
    head->addStretch(1);
    new_btn_ = new widgets::Button(QStringLiteral("新建项目"), widgets::Button::Variant::Primary,
                                   widgets::Button::Size::Md, this);
    open_btn_ = new widgets::Button(QStringLiteral("打开…"), widgets::Button::Variant::Secondary,
                                    widgets::Button::Size::Md, this);
    head->addWidget(new_btn_);
    head->addWidget(open_btn_);
    root->addLayout(head);

    // —— 搜索 + 排序 + 状态短语 ——
    auto* tools = new QHBoxLayout();
    tools->setSpacing(theme::space::kSteps[3]);
    search_ = new widgets::SearchBox(this);
    search_->setFixedWidth(280);
    search_->SetPlaceholder(QStringLiteral("搜索项目…"));
    tools->addWidget(search_);
    auto* sort_label = new QLabel(QStringLiteral("排序："), this);
    widgets::SetKind(sort_label, "fieldlabel");
    tools->addWidget(sort_label);
    sort_seg_ = new widgets::Segmented({QStringLiteral("最近打开"), QStringLiteral("名称")}, this);
    tools->addWidget(sort_seg_);
    tools->addStretch(1);
    spinner_ = new widgets::Spinner(widgets::Spinner::Size::Sm, this);
    spinner_->hide();
    tools->addWidget(spinner_);
    status_label_ = new QLabel(this);
    widgets::SetKind(status_label_, "fieldhelp");
    tools->addWidget(status_label_);
    root->addLayout(tools);

    // —— 「最近项目」小标题 + 卡片网格（自适应换行）——
    auto* heading = new QLabel(QStringLiteral("最近项目"), this);
    widgets::SetKind(heading, "fieldlabel");
    widgets::SetSemibold(heading, true);
    root->addWidget(heading);

    grid_host_ = new QWidget(this);
    grid_ = new QGridLayout(grid_host_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(theme::space::kSteps[4]);
    grid_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    root->addWidget(grid_host_, 1);

    // —— 空态 / 错误态 / 无匹配（UI.md §3）——
    empty_ = new widgets::EmptyState(QStringLiteral("📁"), QStringLiteral("还没有项目"),
                                     QStringLiteral("新建一个项目，开始你的第一部作品"),
                                     QStringLiteral("新建项目"), this);
    empty_->SetOnAction([this] {
        if (on_create_) {
            on_create_();
        }
    });
    root->addWidget(empty_, 1);

    error_ = new widgets::ErrorState(
        QStringLiteral("项目索引打不开"),
        QStringLiteral(
            "索引文件损坏或无法解析。点「重试」重建索引（只重建最近列表，不会删除任何项目文件）。"),
        this);
    error_->SetOnRetry([this] { RebuildIndex(); });
    root->addWidget(error_, 1);

    no_match_ = new QLabel(QStringLiteral("没有匹配的项目，换个关键词试试"), this);
    widgets::SetKind(no_match_, "fieldhelp");
    no_match_->setAlignment(Qt::AlignCenter);
    root->addWidget(no_match_, 1);

    // —— 接线 ——
    connect(new_btn_, &widgets::Button::clicked, this, [this] {
        if (on_create_) {
            on_create_();
        }
    });
    connect(open_btn_, &widgets::Button::clicked, this, [this] {
        if (on_open_path_) {
            on_open_path_();
        }
    });
    search_->SetOnSearch([this](const QString& text) {
        filter_ = text;
        BuildCards();
        UpdateState();
    });
    sort_seg_->SetOnChanged([this](int index) {
        sort_ = index;
        SortEntries();
        BuildCards();
        UpdateState();
    });

    Refresh();
}

void ProjectHubView::Refresh() {
    loading_ = true; // 口径：加载期间 StatusText() = 「加载中」
    UpdateState();

    // 索引可读性探测（UI.md §3 项目列表错误态；首次运行文件缺失不算错）
    shine::project::ProjectIndex probe;
    const bool loaded = probe.Load();
    const bool missing = !std::filesystem::exists(probe.IndexFile());
    index_error_ = !loaded && !missing;

    entries_ = svc_ != nullptr ? svc_->Recent() : std::vector<shine::project::RecentEntry>{};
    SortEntries();
    loading_ = false;
    BuildCards();
    UpdateState();
}

void ProjectHubView::SetOnOpenProject(std::function<void(const shine::project::ProjectRef&)> cb) {
    on_open_ = std::move(cb);
}

void ProjectHubView::SetOnCreateProject(std::function<void()> cb) { on_create_ = std::move(cb); }

void ProjectHubView::SetOnOpenPath(std::function<void()> cb) { on_open_path_ = std::move(cb); }

int ProjectHubView::CardCount() const { return static_cast<int>(cards_.size()); }

QString ProjectHubView::StatusText() const {
    if (loading_) {
        return QStringLiteral("加载中");
    }
    if (index_error_) {
        return QStringLiteral("索引损坏");
    }
    if (entries_.empty()) {
        return QStringLiteral("还没有项目");
    }
    return QStringLiteral("共 %1 个项目").arg(CardCount());
}

void ProjectHubView::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    const int avail = grid_host_ != nullptr ? grid_host_->width() : 0;
    if (avail > 0) {
        const int want = std::max(1, (avail + theme::space::kSteps[4]) / (220 + theme::space::kSteps[4]));
        if (want != cols_) {
            BuildCards();
        }
    }
}

void ProjectHubView::SortEntries() {
    if (sort_ == 1) { // 名称
        std::stable_sort(entries_.begin(), entries_.end(),
                         [](const shine::project::RecentEntry& a,
                            const shine::project::RecentEntry& b) {
                             return QString::localeAwareCompare(FromUtf8(a.name),
                                                                FromUtf8(b.name)) < 0;
                         });
    } else { // 最近打开
        std::stable_sort(entries_.begin(), entries_.end(),
                         [](const shine::project::RecentEntry& a,
                            const shine::project::RecentEntry& b) {
                             return a.lastOpened > b.lastOpened;
                         });
    }
}

void ProjectHubView::BuildCards() {
    while (QLayoutItem* it = grid_->takeAt(0)) {
        if (QWidget* w = it->widget()) {
            w->hide();
            w->deleteLater(); // 可能在卡片右键回调里重建，延后删除
        }
        delete it;
    }
    cards_.clear();

    const QString key = filter_.trimmed();
    std::vector<shine::project::RecentEntry> shown;
    for (const shine::project::RecentEntry& e : entries_) {
        if (!key.isEmpty() && !FromUtf8(e.name).contains(key, Qt::CaseInsensitive)) {
            continue;
        }
        shown.push_back(e);
    }

    int cols = 4;
    const int avail = grid_host_->width();
    if (avail > 0) {
        cols = std::max(1, (avail + theme::space::kSteps[4]) / (220 + theme::space::kSteps[4]));
    }
    cols_ = cols;

    for (std::size_t i = 0; i < shown.size(); ++i) {
        const shine::project::RecentEntry entry = shown[i]; // 按值带走，进回调
        const QString name = FromUtf8(entry.name);

        auto* card = new ProjectCard(grid_host_);
        card->setFixedSize(220, 172); // 约 220×150；16:9 封面 192×108 装入后的最贴近尺寸
        QVBoxLayout* body = card->BodyLayout();
        body->setContentsMargins(14, 10, 14, 10);
        body->setSpacing(4);

        const int scheme =
            static_cast<int>(std::hash<std::string>{}(entry.id) % static_cast<std::size_t>(3));
        body->addWidget(new CoverMark(FirstGlyph(name), scheme, card));

        auto* name_label = widgets::SectionTitle(name, card);
        name_label->setFixedHeight(20);
        {
            QFont f = name_label->font();
            f.setPixelSize(theme::font::kSizes[2]);
            name_label->setFont(f);
            name_label->setText(QFontMetrics(f).elidedText(name, Qt::ElideRight, 192));
        }
        name_label->setToolTip(name);
        body->addWidget(name_label);

        auto* meta = new QLabel(MetaText(entry), card);
        widgets::SetKind(meta, "statesub");
        meta->setFixedHeight(16);
        {
            QFont f = meta->font();
            f.setPixelSize(theme::font::kSizes[0]);
            meta->setFont(f);
        }
        body->addWidget(meta);

        card->SetOnClick([this, entry] { OpenEntry(entry); });
        card->on_context = [this, entry](const QPoint& gp) { ShowCardMenu(entry, gp); };

        grid_->addWidget(card, static_cast<int>(i) / cols, static_cast<int>(i) % cols);
        cards_.push_back(card);
    }
}

void ProjectHubView::UpdateState() {
    const bool has_any = !entries_.empty();
    const bool filtered_out = has_any && cards_.empty() && !loading_;
    error_->setVisible(index_error_);
    empty_->setVisible(!index_error_ && !has_any);
    grid_host_->setVisible(!index_error_ && has_any && !filtered_out);
    no_match_->setVisible(!index_error_ && filtered_out);
    spinner_->setVisible(loading_);
    status_label_->setText(StatusText());
}

void ProjectHubView::OpenEntry(const shine::project::RecentEntry& entry) {
    if (svc_ == nullptr) {
        return;
    }
    auto ref = svc_->Open(entry.rootDir);
    if (!ref.has_value()) {
        widgets::Toast::Show(QStringLiteral("打开项目失败：%1").arg(FromUtf8(ref.error().message)),
                             widgets::Toast::Tone::Error); // 卡片保留
        return;
    }
    if (on_open_) {
        on_open_(ref.value());
    }
}

void ProjectHubView::RemoveEntry(const shine::project::RecentEntry& entry) {
    // svc 未提供移除接口：走 ProjectIndex（只移除登记，绝不删项目文件）
    shine::project::ProjectIndex index;
    (void)index.Load();
    (void)index.Remove(entry.id);
    (void)index.Save();
    widgets::Toast::Show(QStringLiteral("已从列表移除「%1」（项目文件未删除）").arg(FromUtf8(entry.name)),
                         widgets::Toast::Tone::Info);
    Refresh();
}

void ProjectHubView::ShowCardMenu(const shine::project::RecentEntry& entry, const QPoint& globalPos) {
    QMenu menu(this);
    menu.addAction(QStringLiteral("打开"), [this, entry] { OpenEntry(entry); });
    menu.addAction(QStringLiteral("在资源管理器中显示"), [entry] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathText(entry.rootDir)));
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("从列表移除（不删文件）"), [this, entry] { RemoveEntry(entry); });
    menu.exec(globalPos);
}

void ProjectHubView::RebuildIndex() {
    shine::project::ProjectIndex index;
    (void)index.Load(); // 坏文件也已回退空索引
    index.Recent().Clear();
    const auto saved = index.Save();
    if (!saved.has_value()) {
        widgets::Toast::Show(QStringLiteral("重建索引失败：%1").arg(FromUtf8(saved.error().message)),
                             widgets::Toast::Tone::Error);
        return;
    }
    widgets::Toast::Show(QStringLiteral("索引已重建（项目文件未改动）"),
                         widgets::Toast::Tone::Success);
    Refresh();
}

} // namespace shine::app
