#include "ui/pages/project/ProjectHubView.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Surfaces.h"
#include "project/ProjectIndex.h"
#include "project/ProjectTemplate.h"
#include "util/Encoding.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "ui/kit/controls/WidgetCommon.h"

namespace shine::app {
namespace {

// views.css:6-13 .hub 的几何常量（CSS 像素 → Qt 像素，1:1 不缩放）
constexpr int kInnerMaxWidth = 1080; // .hub-inner max-width
constexpr int kPadTop = 40;          // .hub-inner padding-top
constexpr int kPadX = 32;            // .hub-inner padding 左右
constexpr int kPadBottom = 60;       // .hub-inner padding-bottom
constexpr int kHeroGap = 18;         // .hub-hero gap
constexpr int kHeroMarginBottom = 34;
constexpr int kLogoSize = 52;        // .hub-logo 52×52
constexpr int kLogoRadius = 14;      // .hub-logo border-radius: var(--r-lg)
constexpr int kLogoIcon = 26;        // .hub-logo .icon 26×26
constexpr int kHeroTitleSize = 26;   // .hub-title font-size 26px
constexpr int kToolbarGap = 10;      // .hub-toolbar gap
constexpr int kToolbarMarginBottom = 18;
constexpr int kSearchWidth = 280;    // .hub-toolbar .search width
constexpr int kCountMarginTop = 22;  // .hub-count margin-top
constexpr int kCountMarginBottom = 10;
constexpr int kGridGap = 14;         // .proj-grid gap
constexpr int kCardMinWidth = 240;   // .proj-grid minmax(240px, 1fr)
constexpr int kCoverHeight = 120;    // .proj-card .cover height
constexpr int kCoverTagInset = 10;   // .cover-tag top/left 10px
constexpr int kBodyPadV = 12;        // .pbody padding 12px 14px 13px
constexpr int kBodyPadX = 14;
constexpr int kBodyPadBottom = 13;
constexpr int kFootMarginTop = 11;   // .pfoot margin-top / padding-top
constexpr int kFootGap = 6;          // .pfoot gap
constexpr int kPageFootMarginTop = 36; // .hub-foot margin-top
constexpr int kPageFootGap = 16;       // .hub-foot gap

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

// 卡片副标题：project.json 的 premise（一句话创意）。**缺省就留空**，
// 不用模板名之类的字段顶替 —— 设计稿那一行是可选描述，不是必填统计。
[[nodiscard]] QString PremiseText(const shine::project::RecentEntry& entry) {
    if (auto file = shine::project::LoadProjectFile(entry.rootDir); file.has_value()) {
        return FromUtf8(file->premise).trimmed();
    }
    return {};
}

// 卡片 pmeta：模板中文名 · 相对时间（模板读 project.json，缺省只显示时间）
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

// 封面占位：Token 色渐变 + 首字大字，铺满卡片宽度、固定 kCoverHeight 高。
// 自绘铺满，任何时刻不闪白；取色只走 shine::widgets::TokenQColor(theme::Current().xxx)，
// 零内联色值。hover 时封面轻微放大（views.css:85-87 .proj-card:hover .cover svg）。
class CoverMark final : public QWidget {
  public:
    CoverMark(const QString& glyph, int scheme, QWidget* parent)
        : QWidget(parent), glyph_(glyph), scheme_(scheme) {
        setFixedHeight(kCoverHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    // 悬停缩放（views.css:85-87）：Qt 无 CSS transform，用 QWidget::scale 走几何缩放，
    // 由 ProjectCard 在 enter/leave 时驱动，避免每帧 new 一个 Tween。
    void SetZoomed(bool on) {
        if (zoomed_ == on) {
            return;
        }
        zoomed_ = on;
        update();
    }

  protected:
    void paintEvent(QPaintEvent* ev) override {
        QWidget::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::ColorToken& t = theme::Current();
        p.fillRect(rect(), widgets::TokenQColor(t.bgPanel)); // 先铺不透明底色
        // 悬停时按 1.06 放大裁切（等价 .cover svg transform: scale(1.06)）
        const QRectF target = zoomed_ ? QRectF(rect()).adjusted(-width() * 0.03, -height() * 0.03,
                                                                width() * 0.03, height() * 0.03)
                                      : QRectF(rect());
        p.save();
        p.setClipRect(rect());
        p.translate(target.center());
        p.scale(target.width() / rect().width(), target.height() / rect().height());
        p.translate(-target.center());
        QLinearGradient grad(target.topLeft(), target.bottomRight());
        const QColor accents[3] = {widgets::TokenQColor(t.accentPrimary),
                                   widgets::TokenQColor(t.accentSecondary),
                                   widgets::TokenQColor(t.accentInfo)};
        grad.setColorAt(0.0, accents[scheme_ % 3]);
        grad.setColorAt(0.55, widgets::TokenQColor(t.bgPanel));
        grad.setColorAt(1.0, widgets::TokenQColor(t.bgElevated));
        p.fillRect(target, grad);
        p.restore();
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
    bool zoomed_ = false;
};

// 项目卡片：Card(Elevated) 自带 hover 抬升；左键=打开，右键=上下文菜单。
// 卡片内按钮（打开 / 目录 / ⋯）消费掉点击，避免连带触发整卡打开。
class ProjectCard final : public widgets::Card {
  public:
    explicit ProjectCard(QWidget* parent)
        : widgets::Card(widgets::Card::Variant::Elevated, parent) {}

    std::function<void(const QPoint&)> on_context;
    std::function<void()> on_more; // pfoot 的「⋯」= 复用同一张右键菜单

    void SetCover(CoverMark* cover) { cover_ = cover; }

  protected:
    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton) {
            Card::mousePressEvent(ev); // hover / 点击全用 Card 自身行为
        } else {
            ev->accept(); // 右键不触发打开，交给 contextMenuEvent
        }
    }

    void enterEvent(QEnterEvent* ev) override {
        Card::enterEvent(ev);
        if (cover_ != nullptr) {
            cover_->SetZoomed(true);
        }
    }

    void leaveEvent(QEvent* ev) override {
        Card::leaveEvent(ev);
        if (cover_ != nullptr) {
            cover_->SetZoomed(false);
        }
    }

    void contextMenuEvent(QContextMenuEvent* ev) override {
        if (on_context) {
            on_context(ev->globalPos());
        }
        ev->accept();
    }

  private:
    CoverMark* cover_ = nullptr;
};

// views.css:9-12 .hub 背景：两层 radial-gradient 叠 bg.void。
// QSS 无 radial-gradient，只能自绘。accent-dim = accent 14% alpha；
// 第二层 info 9% alpha。中心/半径按 CSS 百分比换算到控件像素。
void PaintHubBackdrop(QPainter& p, const QRect& r) {
    const theme::ColorToken& t = theme::Current();
    p.fillRect(r, widgets::TokenQColor(t.bgVoid));

    QColor accent = widgets::TokenQColor(t.accentPrimary);
    accent.setAlphaF(0.14); // --accent-dim: rgba(accent, 0.14)
    QColor info = widgets::TokenQColor(t.accentInfo);
    info.setAlphaF(0.09); // color-mix(in srgb, var(--info) 9%, transparent)

    // radial-gradient(900px 420px at 18% -10%, accent-dim, transparent 60%)
    {
        const QPointF center(r.width() * 0.18, -r.height() * 0.10);
        QRadialGradient g(center, 450.0);
        g.setColorAt(0.0, accent);
        QColor clear = accent;
        clear.setAlpha(0);
        g.setColorAt(0.6, clear);
        g.setColorAt(1.0, clear);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawEllipse(center, 450.0, 210.0); // 900×420 尺寸 = 半径的一半
        p.restore();
    }
    // radial-gradient(700px 380px at 95% 0%, info 9%, transparent 60%)
    {
        const QPointF center(r.width() * 0.95, 0.0);
        QRadialGradient g(center, 350.0);
        g.setColorAt(0.0, info);
        QColor clear = info;
        clear.setAlpha(0);
        g.setColorAt(0.6, clear);
        g.setColorAt(1.0, clear);
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawEllipse(center, 350.0, 190.0);
        p.restore();
    }
}

} // namespace

ProjectHubView::ProjectHubView(shine::project::ProjectService* svc, QWidget* parent)
    : QWidget(parent), svc_(svc) {
    setObjectName(QStringLiteral("projectHub"));

    // views.css:6-8 .hub：height 100% + overflow-y auto。背景自绘（paintEvent）。
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    // views.css:14-18 .hub-inner：max-width 1080 居中 + padding 40 / 32 / 60。
    // 两段式：滚动区铺满 → 居中行按视口宽度实时算左右留白（等价 margin: 0 auto）。
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("hubScroll"));
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shell->addWidget(scroll_, 1);

    auto* host = new QWidget(scroll_);
    center_row_ = new QHBoxLayout(host);
    center_row_->setContentsMargins(0, 0, 0, 0);
    center_row_->setSpacing(0);

    auto* root = new QVBoxLayout;
    root->setContentsMargins(kPadX, kPadTop, kPadX, kPadBottom);
    root->setSpacing(theme::space::kSteps[4]); // 16：段与段之间的默认节奏

    auto* inner = new QWidget(host);
    inner->setLayout(root);
    inner->setMaximumWidth(kInnerMaxWidth);
    center_row_->addStretch(1);
    center_row_->addWidget(inner, 10);
    center_row_->addStretch(1);
    scroll_->setWidget(host);

    // —— views.css:19-54 .hub-hero ——
    auto* hero = new QWidget(inner);
    auto* hero_row = new QHBoxLayout(hero);
    hero_row->setContentsMargins(0, 0, 0, 0);
    hero_row->setSpacing(kHeroGap);

    // .hub-logo：52×52 / r-lg / grad-accent(120deg accent→info) + shadow-accent
    auto* logo = new QFrame(hero);
    logo->setObjectName(QStringLiteral("hubLogo"));
    logo->setFixedSize(kLogoSize, kLogoSize);
    {
        QFont f = logo->font();
        f.setPixelSize(kLogoIcon);
        f.setWeight(QFont::Bold);
        logo->setFont(f);
        auto* mark = new QLabel(logo);
        // ▶ = webui 的 Icon name="play"（.hub-logo .icon 26×26，色 = accent.fg）
        mark->setText(QStringLiteral("▶"));
        mark->setAlignment(Qt::AlignCenter);
        mark->setAttribute(Qt::WA_TransparentForMouseEvents);
        mark->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;").arg(widget::CssRgb(
                theme::Current().accentPrimaryFg)));
        logo->show();
        mark->adjustSize();
        mark->move((kLogoSize - mark->width()) / 2, (kLogoSize - mark->height()) / 2);
    }
    // 阴影四周占 blur 宽度，父容器必须留够边距才不会裁掉 —— hub-hero 的 gap 18 足够。
    widgets::ApplyShadowOnThemeChange(logo, widgets::ShadowLevel::Accent);
    hero_row->addWidget(logo, 0, Qt::AlignVCenter);

    auto* titles = new QWidget(hero);
    auto* titles_layout = new QVBoxLayout(titles);
    titles_layout->setContentsMargins(0, 0, 0, 0);
    titles_layout->setSpacing(2); // .hub-sub margin-top 2px
    // .hub-title font-size 26px / weight 800 / letter-spacing .01em
    auto* title = widgets::SectionTitle(QStringLiteral("ShineTV Studio"), titles);
    {
        QFont f = title->font();
        f.setPixelSize(kHeroTitleSize);
        f.setWeight(QFont::Weight(800));
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.26); // 26px × 0.01em
        title->setFont(f);
    }
    titles_layout->addWidget(title);
    // .hub-sub：text.secondary
    auto* sub = new QLabel(
        QStringLiteral("把小说设定、章节生产、视觉资产、分镜、出图出片放进同一个工作台"), titles);
    widgets::SetKind(sub, "statesub");
    sub->setWordWrap(true);
    titles_layout->addWidget(sub);
    hero_row->addWidget(titles, 1);

    // .hub-chips：margin-left auto + gap 8
    auto* chips = new QWidget(hero);
    auto* chips_row = new QHBoxLayout(chips);
    chips_row->setContentsMargins(0, 0, 0, 0);
    chips_row->setSpacing(theme::space::kSteps[3]); // 8（.hub-chips gap）
    chips_row->addWidget(new widgets::Tag(QStringLiteral("一句话 → 成片"), "accent", false, chips));
    chips_row->addWidget(new widgets::Tag(QStringLiteral("项目中心制"), "", false, chips));
    chips_row->addWidget(new widgets::Tag(QStringLiteral("本地优先"), "", false, chips));
    hero_row->addWidget(chips, 0, Qt::AlignVCenter);
    root->addWidget(hero);
    root->addSpacing(kHeroMarginBottom - theme::space::kSteps[4]); // hero 的 mb 34（减去默认 spacing）

    // —— views.css:55-63 .hub-toolbar ——
    auto* tools = new QWidget(inner);
    auto* tools_row = new QHBoxLayout(tools);
    tools_row->setContentsMargins(0, 0, 0, 0);
    tools_row->setSpacing(kToolbarGap);
    search_ = new widgets::SearchBox(tools);
    search_->setFixedWidth(kSearchWidth);
    search_->SetPlaceholder(QStringLiteral("搜索项目…"));
    tools_row->addWidget(search_, 0, Qt::AlignVCenter);
    sort_seg_ = new widgets::Segmented({QStringLiteral("最近打开"), QStringLiteral("名称")}, tools);
    tools_row->addWidget(sort_seg_, 0, Qt::AlignVCenter);
    tools_row->addStretch(1);
    spinner_ = new widgets::Spinner(widgets::Spinner::Size::Sm, tools);
    spinner_->hide();
    tools_row->addWidget(spinner_, 0, Qt::AlignVCenter);
    open_btn_ = new widgets::Button(QStringLiteral("打开…"), widgets::Button::Variant::Secondary,
                                    widgets::Button::Size::Md, tools);
    new_btn_ = new widgets::Button(QStringLiteral("新建项目"), widgets::Button::Variant::Primary,
                                   widgets::Button::Size::Md, tools);
    tools_row->addWidget(open_btn_, 0, Qt::AlignVCenter);
    tools_row->addWidget(new_btn_, 0, Qt::AlignVCenter);
    root->addWidget(tools);
    root->addSpacing(kToolbarMarginBottom - theme::space::kSteps[4]);

    // —— views.css:64-71 .hub-count ——
    auto* count_row = new QWidget(inner);
    auto* count_layout = new QHBoxLayout(count_row);
    count_layout->setContentsMargins(0, 0, 0, 0);
    count_layout->setSpacing(theme::space::kSteps[3]);
    count_layout->setContentsMargins(0, kCountMarginTop, 0, 0);
    auto* heading = new QLabel(QStringLiteral("最近项目"), count_row);
    widgets::SetKind(heading, "countlabel");
    count_layout->addWidget(heading, 0, Qt::AlignVCenter);
    status_label_ = new QLabel(count_row);
    widgets::SetKind(status_label_, "countlabel");
    count_layout->addWidget(status_label_, 0, Qt::AlignVCenter);
    count_layout->addStretch(1);
    root->addWidget(count_row);
    root->addSpacing(kCountMarginBottom);

    grid_host_ = new QWidget(inner);
    grid_ = new QGridLayout(grid_host_);
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(kGridGap);
    grid_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    root->addWidget(grid_host_, 1);

    // —— 空态 / 错误态 / 无匹配（views.css:158-163 的 <Card><.empty> 结构）——
    auto* empty_card = new widgets::Card(widgets::Card::Variant::Flat, inner);
    empty_ = new widgets::EmptyState(QStringLiteral("📁"), QStringLiteral("还没有项目"),
                                     QStringLiteral("新建一个项目，开始你的第一部作品"),
                                     QStringLiteral("新建项目"), empty_card);
    empty_->SetOnAction([this] {
        if (on_create_) {
            on_create_();
        }
    });
    empty_card->BodyLayout()->addWidget(empty_);
    root->addWidget(empty_card, 1);

    error_ = new widgets::ErrorState(
        QStringLiteral("项目索引打不开"),
        QStringLiteral(
            "索引文件损坏或无法解析。点「重试」重建索引（只重建最近列表，不会删除任何项目文件）。"),
        this);
    error_->SetOnRetry([this] { RebuildIndex(); });
    root->addWidget(error_, 1);

    no_match_ = new QLabel(QStringLiteral("没有匹配的项目，换个关键词试试"), this);
    widgets::SetKind(no_match_, "countlabel");
    no_match_->setAlignment(Qt::AlignCenter);
    root->addWidget(no_match_, 1);

    // —— views.css:122-130 .hub-foot：居中快捷键提示 ——
    auto* foot = new QWidget(inner);
    auto* foot_row = new QHBoxLayout(foot);
    foot_row->setContentsMargins(0, 0, 0, 0);
    foot_row->setSpacing(kPageFootGap);
    foot_row->addStretch(1);
    auto* hint = new QLabel(QStringLiteral("双击卡片打开 · 右键更多操作"), foot);
    widgets::SetKind(hint, "countlabel");
    foot_row->addWidget(hint, 0, Qt::AlignVCenter);
    foot_row->addStretch(1);
    root->addSpacing(kPageFootMarginTop - theme::space::kSteps[4]);
    root->addWidget(foot);

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

    // 页面局部 QSS：只落本页根控件子树（#projectHub 前缀），与 kit 全局 QSS 互不干扰。
    setStyleSheet(HubQss());
    // 换肤后重挂（页面 QSS 里的具体色值要跟着新主题重算）。
    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(HubQss()); });

    Refresh();
}

QString ProjectHubView::HubQss() const {
    // views.css 里本页独占的几条规则：hub-logo 渐变底 / hub-count 文案 / 页脚提示。
    // 其余（按钮 / 输入 / 卡片 / Tag / Segmented / EmptyState）全部走 kit 全局 QSS。
    const theme::ColorToken& t = theme::Current();
    const QString accent = widget::CssRgb(t.accentPrimary);
    const QString info = widget::CssRgb(t.accentInfo);
    return QStringLiteral(
        // 滚动区与居中行透明，让 paintEvent 画的 .hub 背景透上来
        "QWidget#projectHub QScrollArea#hubScroll { background: transparent; border: none; }\n"
        "QWidget#projectHub QScrollArea#hubScroll > QWidget > QWidget { background: transparent; }\n"
        // .hub-logo：linear-gradient(120deg, accent → info) + r-lg
        "QWidget#projectHub QFrame#hubLogo {\n"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:1 %2);\n"
        "  border: none; border-radius: %3px; }\n"
        "QWidget#projectHub QFrame#hubLogo QLabel { background: transparent; border: none; }\n"
        // .hub-count / .hub-foot：text-muted 12px w600 letter-spacing .02em
        "QWidget#projectHub QLabel[shineKind=\"countlabel\"] {\n"
        "  color: %4; font-size: 12px; font-weight: 600; background: transparent; }\n"
        // 卡片内的分隔线（.pfoot border-top）与内距由局部布局给，这里只补 hover 描边
        "QWidget#projectHub QFrame[shineKind=\"card\"]:hover { border-color: %1; }\n")
        .arg(accent, info, QString::number(kLogoRadius), widget::CssRgb(t.textMuted));
}

void ProjectHubView::paintEvent(QPaintEvent* ev) {
    QWidget::paintEvent(ev);
    QPainter p(this);
    PaintHubBackdrop(p, rect());
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
    return QStringLiteral("· 共 %1 个项目").arg(CardCount());
}

int ProjectHubView::AvailableCardWidth() const {
    // .hub-inner 是 max-width 1080 的定宽居中块：视口再宽也不会超过它。
    const int inner = std::min(width(), kInnerMaxWidth);
    return std::max(0, inner - kPadX * 2);
}

int ProjectHubView::ColumnCount() const {
    // views.css:74 repeat(auto-fill, minmax(240px, 1fr))：列宽下限 240，间距 14。
    const int avail = AvailableCardWidth();
    if (avail <= 0) {
        return 0; // 宽度未知，交给下一次 resize 再算
    }
    return std::max(1, (avail + kGridGap) / (kCardMinWidth + kGridGap));
}

void ProjectHubView::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    // views.css:14-18 margin: 0 auto：视口比 max-width 宽时，把多出来的均分到左右。
    if (center_row_ != nullptr) {
        const int viewport = scroll_ != nullptr ? scroll_->viewport()->width() : width();
        const int slack = std::max(0, viewport - kInnerMaxWidth);
        center_row_->setContentsMargins(slack / 2, 0, slack / 2, 0);
    }
    if (ColumnCount() != cols_) {
        BuildCards();
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
        // webui 同时匹配 name 与 desc；Qt 侧对应「名称 + 一句话创意」
        if (!key.isEmpty() && !FromUtf8(e.name).contains(key, Qt::CaseInsensitive) &&
            !PremiseText(e).contains(key, Qt::CaseInsensitive)) {
            continue;
        }
        shown.push_back(e);
    }

    cols_ = ColumnCount();
    if (cols_ <= 0) {
        cols_ = 1; // 首帧宽度未知：先按单列建，等 resizeEvent 再重排
    }
    // 1fr：每列等分剩余宽度（省略号宽度按实际列宽算，不是 240 下限）
    const int card_width =
        (AvailableCardWidth() - kGridGap * (cols_ - 1)) / std::max(1, cols_);
    for (int c = 0; c < cols_; ++c) {
        grid_->setColumnMinimumWidth(c, kCardMinWidth);
        grid_->setColumnStretch(c, 1);
    }

    for (std::size_t i = 0; i < shown.size(); ++i) {
        const shine::project::RecentEntry entry = shown[i]; // 按值带走，进回调
        const QString name = FromUtf8(entry.name);

        auto* card = new ProjectCard(grid_host_);
        card->setMinimumWidth(kCardMinWidth);
        QVBoxLayout* body = card->BodyLayout();
        // views.css:94-96 .pbody { padding: 12px 14px 13px }
        body->setContentsMargins(kBodyPadX, kBodyPadV, kBodyPadX, kBodyPadBottom);
        body->setSpacing(0);

        const int scheme =
            static_cast<int>(std::hash<std::string>{}(entry.id) % static_cast<std::size_t>(3));
        auto* cover = new CoverMark(FirstGlyph(name), scheme, card);
        body->addWidget(cover);
        card->SetCover(cover);

        // views.css:88-93 .cover-tag：绝对定位 top/left 10px 的 accent Tag
        // （backdrop-filter 无对应能力，按既定契约用不透明底）。
        QString tpl_short;
        if (auto file = shine::project::LoadProjectFile(entry.rootDir); file.has_value()) {
            if (const shine::project::ProjectTemplate* t =
                    shine::project::FindTemplate(file->templateId);
                t != nullptr) {
                tpl_short = FromUtf8(t->name);
            }
        }
        if (!tpl_short.isEmpty()) {
            auto* tag = new widgets::Tag(tpl_short, "accent", false, cover);
            tag->setAttribute(Qt::WA_TransparentForMouseEvents);
            cover->show(); // move() 前必须先 show()
            tag->adjustSize();
            tag->move(kCoverTagInset, kCoverTagInset);
        }

        // views.css:97-100 .pname { font-size: 14.5px; font-weight: 700 }
        auto* name_label = new QLabel(name, card);
        widgets::SetKind(name_label, "statetitle");
        widgets::SetSemibold(name_label, true);
        name_label->setFixedHeight(20);
        {
            QFont f = name_label->font();
            f.setPixelSize(theme::font::kSizes[2]); // 14.5px 就近取整到 14px
            f.setWeight(QFont::Bold);               // 700
            name_label->setFont(f);
        }
        name_label->setToolTip(name);
        body->addWidget(name_label);

        // webui .tiny dim ellipsis（margin-top 3）：project.json 的 premise，没有就不占位
        const QString premise = PremiseText(entry);
        if (!premise.isEmpty()) {
            auto* desc = new QLabel(card);
            widgets::SetKind(desc, "statesub");
            QFont f = desc->font();
            f.setPixelSize(theme::font::kSizes[0]); // 12px
            desc->setFont(f);
            const int desc_w = std::max(40, card_width - kBodyPadX * 2);
            desc->setText(QFontMetrics(f).elidedText(premise, Qt::ElideRight, desc_w));
            desc->setToolTip(premise);
            desc->setFixedHeight(16);
            body->addSpacing(3); // webui 的 margin-top 3
            body->addWidget(desc);
        }

        // views.css:101-108 .pmeta：flex gap 8 / margin-top 6 / text-muted / 12px
        body->addSpacing(6);
        auto* meta = new QLabel(MetaText(entry), card);
        widgets::SetKind(meta, "statemeta"); // text-muted
        {
            QFont f = meta->font();
            f.setPixelSize(theme::font::kSizes[0]); // 12px
            meta->setFont(f);
        }
        meta->setFixedHeight(16);
        body->addWidget(meta);

        // views.css:109-116 .pfoot：margin-top 11 / padding-top 11 / 上边框 line-subtle
        body->addSpacing(kFootMarginTop);
        auto* line = new QFrame(card);
        line->setObjectName(QStringLiteral("pfootline"));
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("QFrame#pfootline { background-color: %1; border: none; }")
                                .arg(widget::CssRgb(theme::Current().lineSubtle)));
        body->addWidget(line);

        auto* foot = new QWidget(card);
        auto* foot_row = new QHBoxLayout(foot);
        foot_row->setContentsMargins(0, kFootMarginTop, 0, 0);
        foot_row->setSpacing(kFootGap);
        auto* open_card = new widgets::Button(QStringLiteral("打开"),
                                              widgets::Button::Variant::Primary,
                                              widgets::Button::Size::Sm, foot);
        auto* reveal = new widgets::Button(QStringLiteral("目录"),
                                           widgets::Button::Variant::Ghost,
                                           widgets::Button::Size::Sm, foot);
        reveal->setToolTip(QStringLiteral("在资源管理器中显示"));
        auto* more = new widgets::Button(QStringLiteral("⋯"), widgets::Button::Variant::Ghost,
                                         widgets::Button::Size::Sm, foot);
        more->setToolTip(QStringLiteral("更多操作"));
        more->setFixedWidth(28);
        foot_row->addWidget(open_card, 0, Qt::AlignVCenter);
        foot_row->addWidget(reveal, 0, Qt::AlignVCenter);
        foot_row->addWidget(more, 0, Qt::AlignVCenter);
        foot_row->addStretch(1);
        body->addWidget(foot);

        connect(open_card, &widgets::Button::clicked, this, [this, entry] { OpenEntry(entry); });
        connect(reveal, &widgets::Button::clicked, this, [entry] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(PathText(entry.rootDir)));
        });
        connect(more, &widgets::Button::clicked, this, [this, entry, more] {
            ShowCardMenu(entry, more->mapToGlobal(QPoint(0, more->height())));
        });

        card->SetOnClick([this, entry] { OpenEntry(entry); });
        card->on_context = [this, entry](const QPoint& gp) { ShowCardMenu(entry, gp); };

        grid_->addWidget(card, static_cast<int>(i) / cols_, static_cast<int>(i) % cols_);
        cards_.push_back(card);
    }
}

void ProjectHubView::UpdateState() {
    const bool has_any = !entries_.empty();
    const bool filtered_out = has_any && cards_.empty() && !loading_;
    error_->setVisible(index_error_);
    empty_->parentWidget()->setVisible(!index_error_ && !has_any); // EmptyState 包在 Card 里
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
