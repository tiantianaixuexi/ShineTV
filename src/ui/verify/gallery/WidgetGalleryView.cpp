#include "ui/verify/gallery/WidgetGalleryView.h"

#include "ui/kit/data/Flow.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/data/Table.h"
#include "ui/kit/images/Grid.h"
#include "ui/kit/images/Sheet.h"
#include "ui/kit/images/Viewer.h"
#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/ThemeService.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Navigation.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/theme/Theme.h"
#include "ui/layout/QtLayout.h"
#include "util/Encoding.h"

#include <QApplication>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

namespace shine::gallery {
namespace {

using shine::widgets::Badge;
using shine::widgets::Button;
using shine::widgets::Card;
using shine::widgets::Checkbox;
using shine::widgets::EmptyState;
using shine::widgets::ErrorState;
using shine::widgets::Field;
using shine::widgets::IconButton;
using shine::widgets::Kbd;
using shine::widgets::NumberInput;
using shine::widgets::ProgressBar;
using shine::widgets::Radio;
using shine::widgets::SearchBox;
using shine::widgets::Segmented;
using shine::widgets::Select;
using shine::widgets::SetForcedState;
using shine::widgets::SetKind;
using shine::widgets::SetSemibold;
using shine::widgets::SetVariant;
using shine::widgets::Slider;
using shine::widgets::Spinner;
using shine::widgets::Splitter;
using shine::widgets::State;
using shine::widgets::StatusDot;
using shine::widgets::Tabs;
using shine::widgets::Tag;
using shine::widgets::TextArea;
using shine::widgets::TextInput;
using shine::widgets::Toggle;
using shine::widgets::Toolbar;

// 页面骨架：accent 字符图标 + 标题 + 副标题（webui .vw-head / .vw-title / .vw-sub）+ 若干行
QWidget* Page(const QString& title) {
    auto* w = new QWidget();
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(16, 12, 16, 12);
    col->setSpacing(10);
    auto* head = new QWidget(w);
    auto* head_row = new QHBoxLayout(head);
    head_row->setContentsMargins(0, 0, 0, 0);
    head_row->setSpacing(14);
    auto* icon = new QLabel(QStringLiteral("▦"), head);
    SetKind(icon, "vsecicon");
    icon->setFixedWidth(20);
    head_row->addWidget(icon);
    auto* texts = new QWidget(head);
    auto* text_col = new QVBoxLayout(texts);
    text_col->setContentsMargins(0, 0, 0, 0);
    text_col->setSpacing(2);
    auto* t = new QLabel(title, texts);
    SetKind(t, "statetitle");
    SetSemibold(t, true);
    text_col->addWidget(t);
    auto* sub = new QLabel(QStringLiteral("kit 控件 · 与 webui 设计稿逐元素对照"), texts);
    SetKind(sub, "statemeta");
    text_col->addWidget(sub);
    head_row->addWidget(texts, 1);
    col->addWidget(head);
    col->addStretch(1);
    return w;
}

void AddRow(QWidget* page, const QString& caption, QWidget* content) {
    auto* col = qobject_cast<QVBoxLayout*>(page->layout());
    auto* cell = new QWidget(page);
    auto* cc = new QVBoxLayout(cell);
    cc->setContentsMargins(0, 0, 0, 0);
    cc->setSpacing(4);
    auto* cap = new QLabel(caption, cell);
    SetKind(cap, "fieldlabel");
    cc->addWidget(cap);
    cc->addWidget(content);
    col->insertWidget(col->count() - 1, cell);
}

// ============================================================ 画廊栅格（views.css:394-407）
//
// webui 组件画廊是**一张自适应卡片网格**，不是「左导航 + 逐页堆叠」：
//
//   .gal-grid { display: grid;
//               grid-template-columns: repeat(auto-fit, minmax(340px, 1fr));
//               gap: 16px; align-items: start; }
//   .gal-row  { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
//
// 两条规则各自对应下面的 GalGrid / GalRow。Qt 无 Grid 的 auto-fit/minmax，
// 也无 flex-wrap，因此两处都按同一套算式手写（列宽 = 均分剩余空间，够宽就多一列）：
//
//   GalGrid 列数 = max(1, floor((可用宽 + gap) / (minmax 下限 + gap)))
//   GalRow  换行 = 逐项累加宽度，放不下即折行，行内按 align-items:center 垂直居中
//
// gap 16px 取自 webui（= token space.s4），GalRow 的 gap 10px / align-items:center
// 同样逐值对齐设计稿，不是自选值。

// .gal-grid 的 minmax() 下限 340px 与 gap 16px（webui views.css:397-398 原值）
constexpr int kGalMinColW = 340;
constexpr int kGalGap = 16;
// .gal-row 的 gap: 10px + align-items: center（webui views.css:403-404 原值）
constexpr int kGalRowGap = 10;

// .gal-row —— flex 行：横向排布、垂直居中、放不下换行（views.css:401-406）
class GalRow : public QWidget {
  public:
    explicit GalRow(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    void AddItem(QWidget* item) {
        item->setParent(this);
        items_.push_back(item);
        invalidate();
    }

    QSize sizeHint() const override {
        int w = 0;
        int h = 0;
        for (QWidget* item : items_) {
            w += item->sizeHint().width();
            h = std::max(h, item->sizeHint().height());
        }
        if (!items_.empty()) {
            w += kGalRowGap * (static_cast<int>(items_.size()) - 1);
        }
        return {w, h};
    }

    QSize minimumSizeHint() const override {
        if (items_.empty()) {
            return {};
        }
        return items_.front()->minimumSizeHint();
    }

    // 按可用宽算出的高度（折行后所有行高 + 行间距）。
    // 覆写 heightForWidth + hasHeightForWidth，外层 QVBoxLayout 才会按折行结果
    // 给本控件分配高度；否则只用 sizeHint（单行高），折行会被裁掉。
    [[nodiscard]] int heightForWidth(int w) const override { return HeightFor(w); }

    [[nodiscard]] bool hasHeightForWidth() const override { return true; }

    [[nodiscard]] int HeightFor(int availW) const {
        int x = 0;
        int y = 0;
        int line_h = 0;
        for (QWidget* item : items_) {
            const QSize s = item->sizeHint();
            if (x > 0 && x + s.width() > availW) {
                x = 0;
                y += line_h + kGalRowGap;
                line_h = 0;
            }
            x += s.width() + kGalRowGap;
            line_h = std::max(line_h, s.height());
        }
        return items_.empty() ? 0 : y + line_h;
    }

  protected:
    void resizeEvent(QResizeEvent* ev) override {
        QWidget::resizeEvent(ev);
        Place();
    }

  private:
    void invalidate() { updateGeometry(); }

    // 折行排布：行内按 align-items:center 垂直居中，行间距 10px
    void Place() {
        int x = 0;
        int y = 0;
        int line_h = 0;
        for (QWidget* item : items_) {
            const QSize s = item->sizeHint();
            if (x > 0 && x + s.width() > width()) {
                x = 0;
                y += line_h + kGalRowGap;
                line_h = 0;
            }
            item->setGeometry(x, y + (line_h - s.height()) / 2, s.width(), s.height());
            x += s.width() + kGalRowGap;
            line_h = std::max(line_h, s.height());
        }
    }

    std::vector<QWidget*> items_;
};

// .gal-grid —— 自适应卡片网格（views.css:395-400）
// auto-fit minmax(340px,1fr) 的语义 = 「每列至少 340px，剩余空间均分，
// 放得下几列就放几列」；align-items:start = 每张卡片按自身内容高，不被同行拉齐。
class GalGrid : public QWidget {
  public:
    explicit GalGrid(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }

    void AddCard(QWidget* card) {
        card->setParent(this);
        cards_.push_back(card);
        updateGeometry();
    }

    QSize sizeHint() const override {
        const int w = std::max(width(), kGalMinColW);
        return {w, ContentHeight(w)};
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

  protected:
    void resizeEvent(QResizeEvent* ev) override {
        QWidget::resizeEvent(ev);
        Place();
    }

  private:
    // auto-fit 的列数算式：放得下几列就放几列（至少一列）
    [[nodiscard]] int ColumnsFor(int availW) const {
        if (cards_.empty()) {
            return 1;
        }
        const int cols = (availW + kGalGap) / (kGalMinColW + kGalGap);
        return std::max(1, std::min(cols, static_cast<int>(cards_.size())));
    }

    // 1fr = 剩余空间均分（扣掉列间距后除以列数）
    [[nodiscard]] int ColumnWidth(int cols) const {
        return std::max(1, (width() - kGalGap * (cols - 1)) / std::max(1, cols));
    }

    [[nodiscard]] int ContentHeight(int availW) const {
        const int cols = ColumnsFor(availW);
        const int col_w = std::max(1, (availW - kGalGap * (cols - 1)) / std::max(1, cols));
        int y = 0;
        for (std::size_t i = 0; i < cards_.size(); i += static_cast<std::size_t>(cols)) {
            int line_h = 0;
            for (std::size_t j = i;
                 j < std::min(cards_.size(), i + static_cast<std::size_t>(cols)); ++j) {
                line_h = std::max(line_h, cards_[j]->sizeHint().height());
            }
            y += line_h + kGalGap;
        }
        return cards_.empty() ? 0 : std::max(0, y - kGalGap);
    }

    void Place() {
        const int cols = ColumnsFor(width());
        const int col_w = ColumnWidth(cols);
        int y = 0;
        for (std::size_t i = 0; i < cards_.size(); i += static_cast<std::size_t>(cols)) {
            const std::size_t end = std::min(cards_.size(), i + static_cast<std::size_t>(cols));
            int line_h = 0;
            for (std::size_t j = i; j < end; ++j) {
                line_h = std::max(line_h, cards_[j]->sizeHint().height());
            }
            for (std::size_t j = i; j < end; ++j) {
                // align-items: start —— 卡片顶部对齐，不拉伸到行高
                cards_[j]->setGeometry(static_cast<int>(j - i) * (col_w + kGalGap), y, col_w,
                                        cards_[j]->sizeHint().height());
            }
            y += line_h + kGalGap;
        }
    }

    std::vector<QWidget*> cards_;
};

// 五态截图登记（GrabAllPages 逐条另存 <name>.png）
std::vector<std::pair<QString, QWidget*>>& ShotRegistry() {
    static std::vector<std::pair<QString, QWidget*>> s;
    return s;
}

void RegisterShot(const QString& name, QWidget* w) { ShotRegistry().emplace_back(name, w); }

// 五态并排行（normal / hover / pressed / disabled / focus）
QWidget* StateRow(const QString& name, const std::function<QWidget*()>& make) {
    auto* w = new QWidget();
    auto* row = new QHBoxLayout(w);
    row->setSpacing(8);
    const std::pair<const char*, State> states[5] = {
        {"常态", State::Normal},   {"悬停", State::Hover},   {"按下", State::Pressed},
        {"禁用", State::Disabled}, {"焦点", State::Focus},
    };
    for (const auto& [caption, st] : states) {
        auto* cell = new QWidget(w);
        auto* cc = new QVBoxLayout(cell);
        cc->setContentsMargins(4, 2, 4, 2);
        cc->setSpacing(4);
        auto* cap = new QLabel(QString::fromUtf8(caption), cell);
        SetKind(cap, "fieldhelp");
        cap->setAlignment(Qt::AlignCenter);
        cc->addWidget(cap, 0, Qt::AlignCenter);
        QWidget* sample = make();
        SetForcedState(sample, st);
        cc->addWidget(sample, 0, Qt::AlignCenter);
        row->addWidget(cell);
    }
    row->addStretch(1);
    RegisterShot(name + QStringLiteral("-五态"), w);
    return w;
}

QWidget* HBox(const std::vector<QWidget*>& items) {
    auto* w = new QWidget();
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    for (QWidget* it : items) {
        row->addWidget(it);
    }
    row->addStretch(1);
    return w;
}

// 静态浮层外壳（Toast / Tooltip / Dialog / Drawer 截图用定格样张）
QFrame* Shell(const char* kind, const char* tone, const QString& text, int w = 260) {
    auto* f = new QFrame();
    SetKind(f, kind);
    if (tone != nullptr && *tone != '\0') {
        f->setProperty("tone", QString::fromLatin1(tone));
    }
    auto* row = new QHBoxLayout(f);
    row->setContentsMargins(10, 8, 10, 8);
    auto* l = new QLabel(text, f);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("background: transparent;"));
    row->addWidget(l);
    f->setFixedWidth(w);
    return f;
}

void BuildAll(WidgetGalleryView* self, QListWidget* nav, QStackedWidget* stack);

} // namespace

WidgetGalleryView::WidgetGalleryView() {
    setWindowTitle(QStringLiteral("ShineTV Studio · 控件画廊"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    nav_ = new QListWidget(this);
    nav_->setFixedWidth(180);
    row->addWidget(nav_);

    pages_ = new QStackedWidget(this);
    row->addWidget(pages_, 1);

    BuildAll(this, nav_, pages_);
    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    if (nav_->count() > 0) {
        nav_->setCurrentRow(0);
    }
}

void WidgetGalleryView::GotoPage(int index) {
    if (index >= 0 && index < pages_->count()) {
        nav_->setCurrentRow(index);
    }
}

int WidgetGalleryView::PageCount() const { return pages_->count(); }

int WidgetGalleryView::GrabAllPages(const std::string& outDirUtf8) {
    const std::filesystem::path dir = shine::util::PathFromUtf8(outDirUtf8);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int ok = 0;
    for (int i = 0; i < pages_->count(); ++i) {
        pages_->setCurrentIndex(i);
        nav_->setCurrentRow(i);
        QApplication::processEvents();
        QApplication::processEvents();
        QWidget* page = pages_->widget(i);
        const QString name = nav_->item(i)->data(Qt::UserRole).toString();
        const std::filesystem::path file = dir / (name.toStdString() + ".png");
        if (page->grab().save(QString::fromStdWString(file.wstring()))) {
            ++ok;
        }
    }
    // 逐控件五态条另存 <控件名>-五态.png（判据「每个控件 5 态截图齐全」）
    for (const auto& [name, strip] : ShotRegistry()) {
        if (strip == nullptr) {
            continue;
        }
        QApplication::processEvents();
        const std::filesystem::path file = dir / (name.toStdString() + ".png");
        if (strip->grab().save(QString::fromStdWString(file.wstring()))) {
            ++ok;
        }
    }
    return ok;
}

namespace {

// 画廊卡片外壳：对应 webui <Card>（ui.css:175-195 的 .card + .card-h + .card-b）。
// 用 kit 的 SectionCard —— 它就是「标题栏 + 内容区」那件（QSS 段逐值对齐 .card-h：
// p12 16 + 底部发丝线 + 13.5px w600），无需另写一套外壳。
shine::widgets::SectionCard* GalCard(const QString& title, QWidget* parent) {
    auto* card = new shine::widgets::SectionCard(title, parent);
    card->SetCollapsible(false); // 设计稿的卡片不可折叠
    return card;
}

// .gal-row 便捷构造：把若干控件摆成一行（放不下自动折行）
GalRow* GalRowOf(std::initializer_list<QWidget*> items, QWidget* parent) {
    auto* row = new GalRow(parent);
    for (QWidget* item : items) {
        row->AddItem(item);
    }
    return row;
}

// 设计稿的说明小字（Gallery.jsx 里反复出现的 <div className="tiny dim">）
QLabel* Note(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    SetKind(label, "statemeta");
    label->setWordWrap(true);
    return label;
}

void BuildAll(WidgetGalleryView* self, QListWidget* nav, QStackedWidget* stack) {
    const auto add = [nav, stack](const QString& navText, const char* fileName, QWidget* page) {
        auto* item = new QListWidgetItem(navText, nav);
        item->setData(Qt::UserRole, QString::fromLatin1(fileName));
        stack->addWidget(page);
    };

    // ================= 设计稿视图（webui Gallery.jsx 1:1） =================
    // 一页看完设计稿陈列的全部控件：.vw-head 页头 + .gal-grid 卡片网格
    // （views.css:394-407）。下面 9 张卡片逐张对应 Gallery.jsx 里同序的 <Card>。
    {
        auto* scroll = new QScrollArea();
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* body = new QWidget(scroll);
        auto* col = new QVBoxLayout(body);
        shine::util::PageMargins(col); // .vw padding: 20px 24px 26px
        shine::util::PageSpacing(col); // .vw gap: 16px

        // —— .vw-head：accent 图标 + 标题 + 副标题 + 右侧 Tag（Gallery.jsx:22-30）——
        auto* head = new QWidget(body);
        auto* head_row = new QHBoxLayout(head);
        head_row->setContentsMargins(0, 0, 0, 0);
        head_row->setSpacing(14);
        auto* head_icon = new QLabel(QStringLiteral("▦"), head);
        SetKind(head_icon, "vsecicon");
        head_icon->setFixedWidth(20);
        head_row->addWidget(head_icon, 0, Qt::AlignVCenter);
        auto* head_texts = new QWidget(head);
        auto* head_col = new QVBoxLayout(head_texts);
        head_col->setContentsMargins(0, 0, 0, 0);
        head_col->setSpacing(2);
        auto* head_title = new QLabel(QStringLiteral("组件画廊"), head_texts);
        SetKind(head_title, "statetitle");
        SetSemibold(head_title, true);
        head_col->addWidget(head_title);
        auto* head_sub = new QLabel(
            QStringLiteral("设计稿组件总览 · 对应 Qt 端 verify/gallery · 36 个组件 / 5 组"),
            head_texts);
        SetKind(head_sub, "statemeta");
        head_col->addWidget(head_sub);
        head_row->addWidget(head_texts, 1);
        auto* head_tag = new Tag(QStringLiteral("Web 设计稿"), "accent", false, head);
        head_row->addWidget(head_tag, 0, Qt::AlignVCenter);
        col->addWidget(head);

        auto* grid = new GalGrid(body);
        col->addWidget(grid);

        // —— ① Button 按钮 · 变体 4 × 尺寸 3（Gallery.jsx:33-54）——
        {
            auto* card = GalCard(QStringLiteral("Button 按钮 · 变体 4 × 尺寸 3"), grid);
            auto* body_lay = card->BodyLayout();
            body_lay->addWidget(GalRowOf({new Button(QStringLiteral("主要"), Button::Variant::Primary),
                                          new Button(QStringLiteral("次要"), Button::Variant::Secondary),
                                          new Button(QStringLiteral("幽灵"), Button::Variant::Ghost),
                                          new Button(QStringLiteral("危险"), Button::Variant::Danger)},
                                         card));
            auto* loading = new Button(QStringLiteral("加载中"), Button::Variant::Primary);
            loading->SetLoading(true);
            auto* disabled = new Button(QStringLiteral("禁用"), Button::Variant::Primary);
            disabled->setEnabled(false);
            body_lay->addWidget(GalRowOf({new Button(QStringLiteral("小号"), Button::Variant::Primary,
                                                     Button::Size::Sm),
                                          new Button(QStringLiteral("中号"), Button::Variant::Primary,
                                                     Button::Size::Md),
                                          new Button(QStringLiteral("大号"), Button::Variant::Primary,
                                                     Button::Size::Lg),
                                          loading, disabled},
                                         card));
            auto* icon_active = new IconButton(QStringLiteral("⚙"), QStringLiteral("设置"));
            icon_active->SetActive(true);
            body_lay->addWidget(GalRowOf({new IconButton(QStringLiteral("↻"),
                                                         QStringLiteral("图标钮 · tooltip"),
                                                         IconButton::Size::Sm),
                                          new IconButton(QStringLiteral("⤓"), QStringLiteral("下载"),
                                                         IconButton::Size::Sm),
                                          icon_active},
                                         card));
            grid->AddCard(card);
        }

        // —— ② Tag / Badge / Kbd / StatusDot（Gallery.jsx:56-79）——
        {
            auto* card = GalCard(QStringLiteral("Tag / Badge / Kbd / StatusDot"), grid);
            auto* body_lay = card->BodyLayout();
            body_lay->addWidget(GalRowOf({new Tag(QStringLiteral("已提交"), "ok"),
                                          new Tag(QStringLiteral("草稿"), "warn"),
                                          new Tag(QStringLiteral("失败"), "danger"),
                                          new Tag(QStringLiteral("运行中"), "busy"),
                                          new Tag(QStringLiteral("参考就绪"), "info"),
                                          new Tag(QStringLiteral("降级 B"), "accent"),
                                          new Tag(QStringLiteral("待出图"))},
                                         card));
            auto* badge_count = new Badge(card);
            badge_count->SetCount(5);
            auto* badge_over = new Badge(card);
            badge_over->SetCount(120);
            auto* badge_dot = new Badge(card);
            badge_dot->SetDot(true);
            body_lay->addWidget(GalRowOf({badge_count, badge_over, badge_dot,
                                          Note(QStringLiteral("状态标记 / 计数 / 点"), card)},
                                         card));
            body_lay->addWidget(GalRowOf({Note(QStringLiteral("快捷键："), card), new Kbd(QStringLiteral("Ctrl"), card),
                                          new Kbd(QStringLiteral("K"), card), new Kbd(QStringLiteral("Ctrl"), card),
                                          new Kbd(QStringLiteral("B"), card), new Kbd(QStringLiteral("Ctrl"), card),
                                          new Kbd(QStringLiteral("Enter"), card)},
                                         card));
            grid->AddCard(card);
        }

        // —— ③ Segmented / Tabs / 进度（Gallery.jsx:81-101）——
        {
            auto* card = GalCard(QStringLiteral("Segmented / Tabs / 进度"), grid);
            auto* body_lay = card->BodyLayout();
            auto* seg = new Segmented(
                {QStringLiteral("列表"), QStringLiteral("看板"), QStringLiteral("日/周/月")}, card);
            seg->SetCurrent(0);
            body_lay->addWidget(seg);
            auto* tabs = new Tabs({QStringLiteral("剧本"), QStringLiteral("分镜"), QStringLiteral("渲染"),
                                   QStringLiteral("设定集")},
                                  card);
            tabs->SetCurrent(1, false); // 设计稿默认停在「分镜」
            body_lay->addWidget(tabs);
            auto* prog = new ProgressBar(card);
            prog->setValue(64);
            prog->SetInlineText(QString());
            prog->setTextVisible(false);
            body_lay->addWidget(prog);
            // 设计稿的 ± 按钮与百分比读数（Gallery.jsx:89-94）
            auto* minus = new Button(QStringLiteral("-"), Button::Variant::Secondary,
                                     Button::Size::Sm, card);
            auto* plus = new Button(QStringLiteral("+"), Button::Variant::Secondary,
                                    Button::Size::Sm, card);
            auto* readout = Note(QStringLiteral("64%"), card);
            const auto step = [prog, readout](int delta) {
                const int next = std::clamp(prog->value() + delta, 0, 100);
                prog->setValue(next);
                readout->setText(QStringLiteral("%1%").arg(next));
            };
            QObject::connect(minus, &Button::clicked, card, [step] { step(-18); });
            QObject::connect(plus, &Button::clicked, card, [step] { step(18); });
            body_lay->addWidget(GalRowOf({minus, plus, readout}, card));
            body_lay->addWidget(GalRowOf({new Spinner(Spinner::Size::Sm, card),
                                          new Spinner(Spinner::Size::Md, card),
                                          new Spinner(Spinner::Size::Lg, card),
                                          Note(QStringLiteral("Spinner / 加载态"), card)},
                                         card));
            grid->AddCard(card);
        }

        // —— ④ Field 表单项 · 五态控件（Gallery.jsx:103-121）——
        {
            auto* card = GalCard(QStringLiteral("Field 表单项 · 五态控件"), grid);
            auto* body_lay = card->BodyLayout();
            auto* name_input = new TextInput(card);
            name_input->SetPlaceholder(QStringLiteral("输入项目名称…"));
            auto* name_field = new Field(QStringLiteral("项目名称"), Field::LabelPos::Top, name_input, card);
            name_field->SetHelp(QStringLiteral("例如：灯语回声"));
            body_lay->addWidget(name_field);

            auto* vendor = new Select(false, false, card);
            vendor->SetItems({{QStringLiteral("小米 MiMo")}, {QStringLiteral("OpenAI 兼容")},
                               {QStringLiteral("自定义端点")}});
            auto* vendor_field = new Field(QStringLiteral("供应商"), Field::LabelPos::Top, vendor, card);

            auto* switches = new QWidget(card);
            auto* switch_col = new QVBoxLayout(switches);
            switch_col->setContentsMargins(0, 18, 0, 0);
            switch_col->setSpacing(8);
            auto* auto_run = new Toggle(switches);
            auto_run->SetChecked(true);
            switch_col->addWidget(GalRowOf({Note(QStringLiteral("自动运行"), switches), auto_run}, switches));
            auto* degrade = new Checkbox(QStringLiteral("允许超期降级"), switches);
            degrade->SetChecked(true);
            switch_col->addWidget(degrade);
            body_lay->addWidget(GalRowOf({vendor_field, switches}, card));

            auto* note_area = new TextArea(500, card);
            note_area->Edit()->setPlaceholderText(QStringLiteral("TextArea · 可拖拽调整高度…"));
            body_lay->addWidget(
                new Field(QStringLiteral("备注"), Field::LabelPos::Top, note_area, card));
            grid->AddCard(card);
        }

        // —— ⑤ DataTable 表格 · 排序 / 筛选 / 选中（Gallery.jsx:123-138）——
        {
            auto* card = GalCard(QStringLiteral("DataTable 表格 · 排序 / 筛选 / 选中"), grid);
            auto* table = new shine::data::DataTable(QStringLiteral("gallerydesign"), card);
            table->SetColumns({{"idx", QStringLiteral("#"), 44},
                               {"code", QStringLiteral("镜号"), 80},
                               {"mood", QStringLiteral("情绪"), 72},
                               {"dur", QStringLiteral("时长"), 64},
                               {"state", QStringLiteral("状态"), 88}});
            table->SetRows({{QStringLiteral("01"), QStringLiteral("S01"), QStringLiteral("静谧"),
                             QStringLiteral("3.5s"), QStringLiteral("完成")},
                            {QStringLiteral("02"), QStringLiteral("S02"), QStringLiteral("温柔"),
                             QStringLiteral("2.8s"), QStringLiteral("完成")},
                            {QStringLiteral("03"), QStringLiteral("S04"), QStringLiteral("怅然"),
                             QStringLiteral("2.2s"), QStringLiteral("生成中")},
                            {QStringLiteral("04"), QStringLiteral("S05"), QStringLiteral("紧张"),
                             QStringLiteral("4.1s"), QStringLiteral("生成中")},
                            {QStringLiteral("05"), QStringLiteral("S06"), QStringLiteral("惊喜"),
                             QStringLiteral("3.0s"), QStringLiteral("待出图")}});
            table->selectRow(2); // 设计稿里高亮的是第 3 行（S04）
            table->setFixedHeight(230);
            card->BodyLayout()->addWidget(table);
            grid->AddCard(card);
        }

        // —— ⑥ StageFlow 阶段流 · 节点四态（Gallery.jsx:140-146）——
        {
            auto* card = GalCard(QStringLiteral("StageFlow 阶段流 · 节点四态"), grid);
            auto* body_lay = card->BodyLayout();
            auto* flow_scroll = new QScrollArea(card);
            flow_scroll->setFrameShape(QFrame::NoFrame);
            flow_scroll->setWidgetResizable(false);
            flow_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            flow_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            auto* flow = new shine::data::StageFlow(flow_scroll);
            flow->SetGraph(
                {
                    {"T1", QStringLiteral("章节初始化"), shine::data::StageFlow::NodeState::Done, QString{}},
                    {"T2", QStringLiteral("本章目标"), shine::data::StageFlow::NodeState::Done, QString{}},
                    {"T3", QStringLiteral("大纲"), shine::data::StageFlow::NodeState::Running, QString{}},
                    {"T11", QStringLiteral("正文写作"), shine::data::StageFlow::NodeState::Todo, QString{}},
                    {"T12", QStringLiteral("章节评审"), shine::data::StageFlow::NodeState::Failed, QString{}},
                    {"T16", QStringLiteral("状态提交"), shine::data::StageFlow::NodeState::Todo, QString{}},
                },
                {});
            // 无支线 → 行高 = snode 30 + 上下留白；84 足够（与 PipelineWorkspace 同值）。
            // 外层滚动壳比内容高 12px，给横向滚动条留位（views.css:814-821 overflow-x:auto）。
            flow->setFixedHeight(84);
            flow_scroll->setWidget(flow);
            flow_scroll->setFixedHeight(96);
            body_lay->addWidget(flow_scroll);
            body_lay->addWidget(Note(
                QStringLiteral("todo / running / done / failed / skipped —— 小说 T1–T17、初始化 I1–I16、"
                               "分镜 V1–V8、连续性 C1–C12 通用。"),
                card));
            auto* kv = new shine::data::KeyValue(card);
            kv->SetPairs({{QStringLiteral("阶段产物"), QStringLiteral("214 个")},
                          {QStringLiteral("LLM 调用"), QStringLiteral("86 次 · 高档 12")},
                          {QStringLiteral("估算成本"), QStringLiteral("¥12.40")}});
            body_lay->addWidget(kv);
            grid->AddCard(card);
        }

        // —— ⑦ Art 占位画 · 程序化生成（Gallery.jsx:148-155）——
        // kit 无 Art 控件：设计稿的 SVG 占位画在此用等尺寸程序化渐变图代替，
        // 尺寸（92×60）与排布与设计稿一致，颜色仍走当前主题 Token。
        {
            auto* card = GalCard(QStringLiteral("Art 占位画 · 程序化生成"), grid);
            const auto art_row = GalRowOf({}, card);
            for (int seed = 0; seed < 6; ++seed) {
                auto* art = new QLabel(art_row);
                art->setFixedSize(92, 60);
                QImage img(92, 60, QImage::Format_RGB32);
                const shine::theme::ColorToken& t = shine::theme::Current();
                const QColor top = shine::widgets::TokenQColor(t.bgPanel);
                const QColor bottom = shine::widgets::TokenQColor(t.accentPrimary);
                for (int y = 0; y < 60; ++y) {
                    for (int x = 0; x < 92; ++x) {
                        img.setPixelColor(x, y, QColor::fromRgbF(
                                                     top.redF() + (bottom.redF() - top.redF()) * y / 59.0,
                                                     top.greenF() + (bottom.greenF() - top.greenF()) * y / 59.0,
                                                     top.blueF() + (bottom.blueF() - top.blueF()) * y / 59.0));
                    }
                }
                art->setPixmap(QPixmap::fromImage(img));
                art_row->AddItem(art);
            }
            card->BodyLayout()->addWidget(art_row);
            card->BodyLayout()->addWidget(
                Note(QStringLiteral("无外部资源 · 程序化渐变占位，种子决定配色与构图。"), card));
            grid->AddCard(card);
        }

        // —— ⑧ 空态 / 反馈（Gallery.jsx:164-177）——
        {
            auto* card = GalCard(QStringLiteral("空态 / 反馈"), grid);
            auto* body_lay = card->BodyLayout();
            auto* empty = new EmptyState(QStringLiteral("🗒"), QStringLiteral("还没有项目"),
                                         QStringLiteral("新建一个项目，开始你的第一部作品"),
                                         QStringLiteral("新建项目"), card);
            body_lay->addWidget(empty);
            auto* info = new Button(QStringLiteral("Info"), Button::Variant::Secondary,
                                    Button::Size::Sm, card);
            auto* success = new Button(QStringLiteral("Success"), Button::Variant::Primary,
                                       Button::Size::Sm, card);
            auto* warning = new Button(QStringLiteral("Warning"), Button::Variant::Secondary,
                                       Button::Size::Sm, card);
            auto* error = new Button(QStringLiteral("Error"), Button::Variant::Danger,
                                     Button::Size::Sm, card);
            QObject::connect(info, &Button::clicked, card,
                    [] { shine::widgets::Toast::Show(QStringLiteral("Info 提示：普通信息反馈")); });
            QObject::connect(success, &Button::clicked, card,
                    [] { shine::widgets::Toast::Show(QStringLiteral("Success：操作已成功完成"),
                                                     shine::widgets::Toast::Tone::Success); });
            QObject::connect(warning, &Button::clicked, card,
                    [] { shine::widgets::Toast::Show(QStringLiteral("Warning：ComfyUI 未连接"),
                                                     shine::widgets::Toast::Tone::Warning); });
            QObject::connect(error, &Button::clicked, card,
                    [] { shine::widgets::Toast::Show(QStringLiteral("Error：KSampler.seed 缺数据"),
                                                     shine::widgets::Toast::Tone::Error); });
            body_lay->addWidget(GalRowOf({info, success, warning, error}, card));
            body_lay->addWidget(
                Note(QStringLiteral("Toast 四色 · 右下角弹出 · 4s 自动消失（error 8s）。"), card));
            grid->AddCard(card);
        }

        col->addStretch(1);
        scroll->setWidget(body);
        add(QStringLiteral("组件画廊（设计稿）"), "GalleryDesign", scroll);
    }

    // ---------------- Button ----------------
    {
        auto* p = Page(QStringLiteral("Button 按钮 · 变体 4 × 尺寸 3 · loading"));
        add(QStringLiteral("Button 按钮"), "Button", p);
        AddRow(p, QStringLiteral("变体（md）"), HBox({
            new Button(QStringLiteral("主操作"), Button::Variant::Primary),
            new Button(QStringLiteral("次操作"), Button::Variant::Secondary),
            new Button(QStringLiteral("幽灵"), Button::Variant::Ghost),
            new Button(QStringLiteral("危险"), Button::Variant::Danger),
        }));
        AddRow(p, QStringLiteral("尺寸 sm / md / lg"), HBox({
            new Button(QStringLiteral("小"), Button::Variant::Primary, Button::Size::Sm),
            new Button(QStringLiteral("中"), Button::Variant::Primary, Button::Size::Md),
            new Button(QStringLiteral("大"), Button::Variant::Primary, Button::Size::Lg),
        }));
        AddRow(p, QStringLiteral("五态（primary）"), StateRow(QStringLiteral("Button"), [] {
            return static_cast<QWidget*>(new Button(QStringLiteral("生成剧本"), Button::Variant::Primary));
        }));
        auto* loading = new Button(QStringLiteral("生成中"), Button::Variant::Primary);
        loading->SetLoading(true);
        AddRow(p, QStringLiteral("loading 态（转圈 + 禁点 + 宽度不跳）"), HBox({loading}));
    }

    // ---------------- IconButton ----------------
    {
        auto* p = Page(QStringLiteral("IconButton 图标钮 · sm/md · tooltip · active"));
        add(QStringLiteral("IconButton 图标钮"), "IconButton", p);
        auto* active = new IconButton(QStringLiteral("▤"), QStringLiteral("剧本面板（活动）"));
        active->SetActive(true);
        AddRow(p, QStringLiteral("sm / md / active"), HBox({
            new IconButton(QStringLiteral("⚙"), QStringLiteral("设置"), IconButton::Size::Sm),
            new IconButton(QStringLiteral("⚙"), QStringLiteral("设置"), IconButton::Size::Md),
            active,
        }));
        AddRow(p, QStringLiteral("五态"), StateRow(QStringLiteral("IconButton"), [] {
            return static_cast<QWidget*>(new IconButton(QStringLiteral("⚙"), QStringLiteral("设置")));
        }));
    }

    // ---------------- Card ----------------
    {
        auto* p = Page(QStringLiteral("Card 卡片 · flat/outlined/elevated + accent · 容器三态"));
        add(QStringLiteral("Card 卡片"), "Card", p);
        auto makeCard = [](Card::Variant v, bool accent, QWidget* inner) {
            auto* c = new Card(v);
            c->SetAccent(accent);
            if (inner != nullptr) {
                c->BodyLayout()->addWidget(inner);
            } else {
                auto* t = new QLabel(QStringLiteral("剧集卡 · 第 12 章改编"));
                SetKind(t, "statetitle");
                c->BodyLayout()->addWidget(t);
                auto* s = new QLabel(QStringLiteral("状态：生成中 · 点击进入详情"));
                SetKind(s, "statesub");
                c->BodyLayout()->addWidget(s);
            }
            c->setMinimumWidth(240);
            return c;
        };
        AddRow(p, QStringLiteral("变体 flat / outlined / elevated / accent"), HBox({
            makeCard(Card::Variant::Flat, false, nullptr),
            makeCard(Card::Variant::Outlined, false, nullptr),
            makeCard(Card::Variant::Elevated, true, nullptr),
        }));
        AddRow(p, QStringLiteral("容器三态：有数据 / 空 / 错误"), HBox({
            makeCard(Card::Variant::Outlined, false, nullptr),
            makeCard(Card::Variant::Outlined, false,
                     new EmptyState(QStringLiteral("🗒"), QStringLiteral("还没有剧集"),
                                    QStringLiteral("先新建一部剧集再开始生成"), QStringLiteral("新建剧集"))),
            makeCard(Card::Variant::Outlined, false,
                     new ErrorState(QStringLiteral("加载失败"), QStringLiteral("网络超时（Error.detail）"))),
        }));
    }

    // ---------------- Tag / Badge / Kbd ----------------
    {
        auto* p = Page(QStringLiteral("Tag / Badge / Kbd"));
        add(QStringLiteral("Tag / Badge / Kbd"), "Tag", p);
        AddRow(p, QStringLiteral("Tag 色调 + 可删"), HBox({
            new Tag(QStringLiteral("默认")),
            new Tag(QStringLiteral("强调"), "accent"),
            new Tag(QStringLiteral("信息"), "info"),
            new Tag(QStringLiteral("成功"), "ok"),
            new Tag(QStringLiteral("警告"), "warn"),
            new Tag(QStringLiteral("失败"), "danger", true),
        }));
        AddRow(p, QStringLiteral("Tag 五态"), StateRow(QStringLiteral("Tag"), [] {
            return static_cast<QWidget*>(new Tag(QStringLiteral("示例"), "accent", true));
        }));
        auto* b0 = new Badge();
        b0->SetCount(5);
        auto* b99 = new Badge();
        b99->SetCount(120);
        auto* bd = new Badge();
        bd->SetDot(true);
        AddRow(p, QStringLiteral("Badge 5 / 120→99+ / 点"), HBox({b0, b99, bd}));
        AddRow(p, QStringLiteral("Kbd"), HBox({
            new Kbd(QStringLiteral("Ctrl+S")), new Kbd(QStringLiteral("Esc")), new Kbd(QStringLiteral("Ctrl+Enter")),
        }));
    }

    // ---------------- Field ----------------
    {
        auto* p = Page(QStringLiteral("Field 表单项 · 标签左/上 · help / error"));
        add(QStringLiteral("Field 表单项"), "Field", p);
        auto* top = new Field(QStringLiteral("剧集名称"), Field::LabelPos::Top, new TextInput());
        top->SetHelp(QStringLiteral("将用于生成封面标题"));
        auto* left = new Field(QStringLiteral("关键词"), Field::LabelPos::Left, new TextInput());
        auto* err = new Field(QStringLiteral("预算"), Field::LabelPos::Top, new TextInput());
        err->SetError(QStringLiteral("预算必须大于 0"));
        AddRow(p, QStringLiteral("标签上 + help / 标签左 / error 态"), HBox({top, left, err}));
    }

    // ---------------- TextInput / TextArea / SearchBox ----------------
    {
        auto* p = Page(QStringLiteral("TextInput / TextArea / SearchBox"));
        add(QStringLiteral("TextInput 输入"), "TextInput", p);
        AddRow(p, QStringLiteral("TextInput 五态（placeholder + 清空钮）"), StateRow(QStringLiteral("TextInput"), [] {
            auto* t = new TextInput();
            t->SetPlaceholder(QStringLiteral("输入剧本名…"));
            t->SetText(QStringLiteral("示例文本"));
            return static_cast<QWidget*>(t);
        }));
        auto* over = new TextArea(20);
        over->SetText(QStringLiteral("这段文案超过了二十个字上限，计数会变红提示"));
        auto* errField = new Field(QStringLiteral("TextArea 计数超限"), Field::LabelPos::Top, over);
        AddRow(p, QStringLiteral("TextArea 字数计数 / 超限 danger"), HBox({
            new TextArea(500), errField,
        }));
        AddRow(p, QStringLiteral("TextArea 五态"), StateRow(QStringLiteral("TextArea"), [] {
            return static_cast<QWidget*>(new TextArea(120));
        }));
        AddRow(p, QStringLiteral("SearchBox 五态（防抖 200ms）"), StateRow(QStringLiteral("SearchBox"), [] {
            auto* s = new SearchBox();
            s->SetPlaceholder(QStringLiteral("搜索角色…"));
            return static_cast<QWidget*>(s);
        }));
    }

    // ---------------- Select ----------------
    {
        auto* p = Page(QStringLiteral("Select · 可搜索/不可搜索 · 分组 · 多选"));
        add(QStringLiteral("Select 下拉"), "Select", p);
        auto* single = new Select(false, false);
        single->SetPlaceholder(QStringLiteral("选择画风…"));
        single->SetItems({{"水墨", "写实系", true}, {"赛博", "写实系", false},
                          {"卡通", "卡通系", false}, {"像素", "卡通系", false}});
        auto* multi = new Select(true, true);
        multi->SetPlaceholder(QStringLiteral("选择标签…"));
        multi->SetItems({{"悬疑", "题材", true}, {"推理", "题材", true}, {"治愈", "题材", false}});
        AddRow(p, QStringLiteral("单选（分组）/ 可搜索多选"), HBox({single, multi}));
        AddRow(p, QStringLiteral("Select 五态"), StateRow(QStringLiteral("Select"), [] {
            auto* s = new Select(false, false);
            s->SetItems({{"水墨", "", true}});
            return static_cast<QWidget*>(s);
        }));
    }

    // ---------------- Slider / NumberInput ----------------
    {
        auto* p = Page(QStringLiteral("Slider / NumberInput"));
        add(QStringLiteral("Slider / NumberInput"), "Slider", p);
        auto* ticks = new Slider(true, 0, 100, QStringLiteral("%"));
        ticks->SetValue(60);
        auto* cont = new Slider(false, 0.0, 5.0, QStringLiteral("x"));
        cont->SetValue(2.5);
        AddRow(p, QStringLiteral("Slider 刻度 / 连续（值 + 单位）"), HBox({ticks, cont}));
        AddRow(p, QStringLiteral("NumberInput 整数 / 浮点（增减 + 范围提示）"), HBox({
            new NumberInput(false, 0, 99),
            new NumberInput(true, 0.0, 10.0),
        }));
        AddRow(p, QStringLiteral("Slider 五态"), StateRow(QStringLiteral("Slider"), [] {
            auto* s = new Slider(true, 0, 100, QStringLiteral("%"));
            s->SetValue(50);
            return static_cast<QWidget*>(s);
        }));
        AddRow(p, QStringLiteral("NumberInput 五态"), StateRow(QStringLiteral("NumberInput"), [] {
            return static_cast<QWidget*>(new NumberInput(false, 0, 99));
        }));
    }

    // ---------------- Toggle / Checkbox / Radio ----------------
    {
        auto* p = Page(QStringLiteral("Toggle / Checkbox(partial) / Radio"));
        add(QStringLiteral("Toggle / Checkbox / Radio"), "Toggle", p);
        auto* ton = new Toggle();
        ton->SetChecked(true);
        AddRow(p, QStringLiteral("Toggle 关 / 开"), HBox({new Toggle(), ton}));
        AddRow(p, QStringLiteral("Toggle 五态"), StateRow(QStringLiteral("Toggle"), [] {
            auto* t = new Toggle();
            t->SetChecked(true);
            return static_cast<QWidget*>(t);
        }));
        auto* partial = new Checkbox(QStringLiteral("全选本章分镜"));
        partial->SetPartial(true);
        auto* on = new Checkbox(QStringLiteral("已确认"));
        on->SetChecked(true);
        AddRow(p, QStringLiteral("Checkbox 关 / partial / 开 · 五态"), HBox({
            new Checkbox(QStringLiteral("未选")), partial, on,
        }));
        AddRow(p, QStringLiteral("Checkbox 五态"), StateRow(QStringLiteral("Checkbox"), [] {
            auto* c = new Checkbox(QStringLiteral("示例"));
            c->SetChecked(true);
            return static_cast<QWidget*>(c);
        }));
        auto* r1 = new Radio(QStringLiteral("横屏 16:9"));
        r1->SetChecked(true);
        AddRow(p, QStringLiteral("Radio 组"), HBox({r1, new Radio(QStringLiteral("竖屏 9:16"))}));
        AddRow(p, QStringLiteral("Radio 五态"), StateRow(QStringLiteral("Radio"), [] {
            auto* r = new Radio(QStringLiteral("示例"));
            r->SetChecked(true);
            return static_cast<QWidget*>(r);
        }));
    }

    // ---------------- ProgressBar / Spinner ----------------
    {
        auto* p = Page(QStringLiteral("ProgressBar / Spinner"));
        add(QStringLiteral("ProgressBar / Spinner"), "ProgressBar", p);
        auto* det = new ProgressBar();
        det->setValue(25);
        det->SetInlineText(QStringLiteral("3/12 章"));
        auto* indet = new ProgressBar();
        indet->SetIndeterminate(true);
        auto* errPb = new ProgressBar();
        errPb->setValue(66);
        errPb->SetState("error");
        errPb->SetInlineText(QStringLiteral("失败 8/12"));
        AddRow(p, QStringLiteral("determinate(内嵌文字) / indeterminate / error"), HBox({det, indet, errPb}));
        // .prog.thin（ui.css:452，4px）与 .prog.run（微光）——离屏抓图看不到动画相位，
        // 但几何与 chunk 底色可验：thin 必须明显比 6px 细。
        auto* thin = new ProgressBar();
        thin->setValue(42);
        thin->SetThin(true);
        thin->setTextVisible(false);
        auto* run = new ProgressBar();
        run->setValue(78);
        run->SetShimmer(true);
        run->setTextVisible(false);
        AddRow(p, QStringLiteral("thin（4px）/ run（微光）"), HBox({thin, run}));
        // .dot 7px 正圆；.dot.run 脉冲（QTimer 自绘，「减少动效」下退化为静态点）
        AddRow(p, QStringLiteral("StatusDot 常态 / run 脉冲"), HBox({
            new StatusDot("ok"), new StatusDot("warn"), new StatusDot("danger"), new StatusDot("busy"),
            [] {
                auto* d = new StatusDot("ok");
                d->SetPulse(true);
                return static_cast<QWidget*>(d);
            }(),
        }));
        AddRow(p, QStringLiteral("Spinner sm / md / lg"), HBox({
            new Spinner(Spinner::Size::Sm), new Spinner(Spinner::Size::Md), new Spinner(Spinner::Size::Lg),
        }));
    }

    // ---------------- EmptyState / ErrorState ----------------
    {
        auto* p = Page(QStringLiteral("EmptyState / ErrorState（容器三态成员）"));
        add(QStringLiteral("EmptyState / ErrorState"), "EmptyState", p);
        AddRow(p, QStringLiteral("EmptyState（必须给出下一步）"), new EmptyState(
            QStringLiteral("🎬"), QStringLiteral("还没有项目"),
            QStringLiteral("新建项目后即可开始分镜与生成"), QStringLiteral("新建项目")));
        auto* es = new ErrorState(QStringLiteral("渲染失败：显存不足"),
                                  QStringLiteral("Error.detail: CUDA out of memory at frame 128 (batch 4)"));
        AddRow(p, QStringLiteral("ErrorState（重试 + 详情折叠）"), es);
    }

    // ---------------- Toast / Tooltip ----------------
    {
        auto* p = Page(QStringLiteral("Toast / Tooltip"));
        add(QStringLiteral("Toast / Tooltip"), "Toast", p);
        AddRow(p, QStringLiteral("Toast 四调（右下角 · 4s/8s · 堆叠 ≤3）"), HBox({
            Shell("toast", "info", QStringLiteral("开始生成第 3 章分镜")),
            Shell("toast", "success", QStringLiteral("分镜生成完成（12/12）")),
            Shell("toast", "warning", QStringLiteral("字数超限，建议精简")),
            Shell("toast", "error", QStringLiteral("渲染失败，请重试")),
        }));
        AddRow(p, QStringLiteral("Tooltip 文本 / 富文本 / 带快捷键（200ms 延迟）"), HBox({
            Shell("tooltip", nullptr, QStringLiteral("普通文本提示")),
            Shell("tooltip", nullptr, QStringLiteral("<b>富文本</b> 提示")),
            HBox({Shell("tooltip", nullptr, QStringLiteral("保存"), 90), new Kbd(QStringLiteral("Ctrl+S"))}),
        }));
    }

    // ---------------- Dialog / Drawer ----------------
    {
        auto* p = Page(QStringLiteral("Dialog / Drawer"));
        add(QStringLiteral("Dialog / Drawer"), "Dialog", p);
        AddRow(p, QStringLiteral("Dialog sm420 / md560 / lg800（Esc 关 · Enter 主按钮）"),
               HBox({Shell("dialog", nullptr, QStringLiteral("sm 420"), 200),
                     Shell("dialog", nullptr, QStringLiteral("md 560"), 260),
                     Shell("dialog", nullptr, QStringLiteral("lg 800"), 340)}));
        AddRow(p, QStringLiteral("Drawer 右侧 380 · 可叠加 · 滑入 motion.base"),
               Shell("drawer", nullptr, QStringLiteral("抽屉内容区（380px）"), 300));
    }

    // ---------------- Tabs / Segmented ----------------
    {
        auto* p = Page(QStringLiteral("Tabs / Segmented"));
        add(QStringLiteral("Tabs / Segmented"), "Tabs", p);
        auto* tabs = new Tabs({QStringLiteral("剧本"), QStringLiteral("分镜"), QStringLiteral("渲染"), QStringLiteral("设定集")});
        AddRow(p, QStringLiteral("Tabs 下划线指示条（滑动 motion.base/emphasized）"), tabs);
        AddRow(p, QStringLiteral("Segmented 2 / 3 / 4 段"), HBox({
            new Segmented({QStringLiteral("列表"), QStringLiteral("看板")}),
            new Segmented({QStringLiteral("日"), QStringLiteral("周"), QStringLiteral("月")}),
            new Segmented({QStringLiteral("全部"), QStringLiteral("进行中"), QStringLiteral("完成"), QStringLiteral("失败")}),
        }));
        AddRow(p, QStringLiteral("Tabs 五态（tab 项）"), StateRow(QStringLiteral("Tabs"), [] {
            auto* b = new QPushButton(QStringLiteral("剧本"));
            SetKind(b, "tab");
            return static_cast<QWidget*>(b);
        }));
        AddRow(p, QStringLiteral("Segmented 五态（segment 项）"), StateRow(QStringLiteral("Segmented"), [] {
            auto* b = new QPushButton(QStringLiteral("日"));
            SetKind(b, "segment");
            return static_cast<QWidget*>(b);
        }));
    }

    // ---------------- Toolbar / Splitter ----------------
    {
        auto* p = Page(QStringLiteral("Toolbar / Splitter"));
        add(QStringLiteral("Toolbar / Splitter"), "Toolbar", p);
        auto* tb = new Toolbar();
        tb->AddGroup({new Button(QStringLiteral("新建"), Button::Variant::Secondary, Button::Size::Sm),
                      new Button(QStringLiteral("打开"), Button::Variant::Secondary, Button::Size::Sm)});
        tb->AddGroup({new IconButton(QStringLiteral("▶"), QStringLiteral("开始生成"), IconButton::Size::Sm),
                      new IconButton(QStringLiteral("⏸"), QStringLiteral("暂停"), IconButton::Size::Sm)});
        tb->AddGroup({new IconButton(QStringLiteral("⤓"), QStringLiteral("导出"), IconButton::Size::Sm),
                      new IconButton(QStringLiteral("⋯"), QStringLiteral("更多操作"), IconButton::Size::Sm)});
        AddRow(p, QStringLiteral("Toolbar 分组 + 分隔线 + 溢出折叠"), tb);

        auto* sp = new Splitter(Qt::Horizontal);
        auto* left = new QLabel(QStringLiteral("面板 A"));
        left->setMinimumWidth(120);
        auto* right = new QLabel(QStringLiteral("面板 B（双击 4px 把手折叠）"));
        right->setMinimumWidth(120);
        sp->addWidget(left);
        sp->addWidget(right);
        sp->setMinimumHeight(90);
        AddRow(p, QStringLiteral("Splitter 4px 把手 + 悬停高亮 + 可折叠"), sp);
    }

    // ---------------- DataTable（P02-S6） ----------------
    {
        auto* p = Page(QStringLiteral("DataTable · 虚拟化 / 排序 / 筛选 / 冻结列 / 操作列 / 框选"));
        add(QStringLiteral("DataTable 表格"), "DataTable", p);
        auto* table = new shine::data::DataTable(QStringLiteral("gallery"));
        table->SetColumns({{"name", QStringLiteral("章"), 180},
                           {"stage", QStringLiteral("阶段"), 110},
                           {"words", QStringLiteral("字数"), 90},
                           {"updated", QStringLiteral("更新"), 120}});
        std::vector<std::vector<QString>> rows;
        for (int i = 0; i < 40; ++i) {
            rows.push_back({QStringLiteral("第 %1 章 · 风起").arg(i + 1),
                            i % 3 == 0 ? QStringLiteral("生成中") : QStringLiteral("完成"),
                            QString::number(900 + i * 31), QStringLiteral("09:41")});
        }
        table->SetRows(rows);
        table->SetActionColumn(QStringLiteral("操作"), [](int) {});
        table->SetFixedColumns(1); // 首列冻结
        table->SetSelectable(shine::data::DataTable::Select::Rubber);
        // 选中第 3 行：左条是 RowChrome 自绘的（QSS 无 inset 阴影），
        // 不选中就截不到，选中态是这张图唯一的判据。hover 底需真实鼠标，离屏抓不到。
        table->selectRow(2);
        table->setMinimumHeight(320);
        AddRow(p, QStringLiteral("40 行样例（同机制虚拟化支撑万行；SHINE_TABLE_BENCH=1 跑 10k 基准）"), table);

        auto* tree = new shine::data::DataTree();
        tree->SetColumns({QStringLiteral("条目"), QStringLiteral("状态")});
        auto* top = tree->AddTop({QStringLiteral("第 3 章 · 风起"), QStringLiteral("生成中")}, true);
        top->appendRow({new QStandardItem(QStringLiteral("3.1 分镜 A")), new QStandardItem(QStringLiteral("完成"))});
        tree->AddTop({QStringLiteral("第 4 章 · 云涌"), QStringLiteral("排队")}, true);
        tree->setMinimumHeight(160);
        AddRow(p, QStringLiteral("DataTree 懒展开 / 拖拽重排 / 多选"), tree);
    }

    // ---------------- StageFlow / Timeline（P02-S6） ----------------
    {
        auto* p = Page(QStringLiteral("StageFlow · 5 节点态 + 分支  /  Timeline"));
        add(QStringLiteral("StageFlow / Timeline"), "StageFlow", p);
        auto* flow = new shine::data::StageFlow();
        flow->SetGraph(
            {
                {"t1", QStringLiteral("剧本解析"), shine::data::StageFlow::NodeState::Done, QString{}},
                {"t2", QStringLiteral("角色设定"), shine::data::StageFlow::NodeState::Done, QString{}},
                {"t3", QStringLiteral("分镜生成"), shine::data::StageFlow::NodeState::Running, QString{}},
                {"t4", QStringLiteral("首帧渲染"), shine::data::StageFlow::NodeState::Failed, QString{}},
                {"t4r", QStringLiteral("重渲修复"), shine::data::StageFlow::NodeState::Todo, QStringLiteral("t4")},
                {"t5", QStringLiteral("成片合成"), shine::data::StageFlow::NodeState::Skipped, QString{}},
            },
            {{"t1", "t2"}, {"t2", "t3"}, {"t3", "t4"}, {"t4", "t4r"}, {"t4r", "t5"}});
        flow->setMinimumHeight(170);
        AddRow(p, QStringLiteral("todo(○) / running(…) / done(✔) / failed(✕) / skipped(→) + 重试支线"), flow);

        auto* tl = new shine::data::Timeline(Qt::Vertical);
        tl->SetEvents({
            {QStringLiteral("09:41"), QStringLiteral("开始生成"), QStringLiteral("T1 分配")},
            {QStringLiteral("09:43"), QStringLiteral("剧本解析完成"), QStringLiteral("12 章")},
            {QStringLiteral("09:52"), QStringLiteral("分镜生成中"), QStringLiteral("7/12")},
        });
        tl->SetCurrent(2);
        tl->setMinimumHeight(220);
        AddRow(p, QStringLiteral("Timeline 事件卡片 + 当前项高亮"), tl);
    }

    // ---------------- Panels（P02-S6） ----------------
    {
        auto* p = Page(QStringLiteral("StatTile / KeyValue / DiffView / JsonTree"));
        add(QStringLiteral("数据面板"), "Panels", p);
        auto* s1 = new shine::data::StatTile();
        s1->SetValue(QStringLiteral("1 284"));
        s1->SetLabel(QStringLiteral("今日生成分镜"));
        s1->SetTrend(12);
        auto* s2 = new shine::data::StatTile();
        s2->SetValue(QStringLiteral("87.5%"));
        s2->SetLabel(QStringLiteral("一次通过率"));
        s2->SetTrend(-3);
        auto* s3 = new shine::data::StatTile();
        s3->SetValue(QStringLiteral("42"));
        s3->SetLabel(QStringLiteral("待渲染"));
        s3->SetTrend(0);
        AddRow(p, QStringLiteral("StatTile 大数字 + 标签 + 趋势"), HBox({s1, s2, s3}));

        auto* kv = new shine::data::KeyValue();
        kv->SetPairs({
            {QStringLiteral("模型"), QStringLiteral("shine-draft-v3")},
            {QStringLiteral("种子"), QStringLiteral("42")},
            {QStringLiteral("提示词"), QStringLiteral("这是一个超过四十个字的长值示例，用来验证折叠展开、双击复制与自动换行都能正常工作的场景")}
        });
        AddRow(p, QStringLiteral("KeyValue 两列 + 值可复制 + 长值折叠"), kv);

        auto* diff = new shine::data::DiffView();
        diff->SetRows({
            {shine::data::DiffView::Kind::Same, QStringLiteral("她推开门，走进风里。"), QStringLiteral("她推开门，走进风里。")},
            {shine::data::DiffView::Kind::Change, QStringLiteral("天色暗了下来。"), QStringLiteral("天色骤然暗了下来。")},
            {shine::data::DiffView::Kind::Del, QStringLiteral("他没有说话。"), QString{}},
            {shine::data::DiffView::Kind::Add, QString{}, QStringLiteral("远处传来第三声钟响。")},
        });
        AddRow(p, QStringLiteral("DiffView 增(+) / 删(-) / 改(~) 三色（status.*）"), diff);

        auto* json = new shine::data::JsonTree();
        json->SetJson(QStringLiteral("{\"shot\": {\"id\": \"S0123\", \"lens\": \"close-up\", \"frames\": [12, 36, 48], \"rendered\": false, \"note\": null}}"));
        json->setMinimumHeight(170);
        AddRow(p, QStringLiteral("JsonTree 可折叠 + 类型着色 + 右键复制路径"), json);
    }

    // ---------------- 图片控件（P02-S7） ----------------
    {
        auto* p = Page(QStringLiteral("ThumbGrid / ImageCard"));
        add(QStringLiteral("缩略图网格"), "ThumbGrid", p);
        auto grad = [](int seed, int w, int h) {
            QImage img(w, h, QImage::Format_RGB32);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    img.setPixelColor(x, y, QColor((x + seed * 31) % 256, (y + seed * 17) % 256,
                                                   (x + y + seed * 7) % 256));
                }
            }
            return img;
        };
        auto* grid = new shine::images::ThumbGrid();
        grid->setModel(new QStandardItemModel(24, 1, grid));
        for (int i = 0; i < 24; ++i) {
            grid->model()->setData(grid->model()->index(i, 0), QStringLiteral("图 %1").arg(i + 1));
        }
        grid->SetThumbProvider([grad](int row, const std::function<void(int, QImage)>& done) {
            done(row, grad(row % 13, 140, 130));
        });
        grid->PrefetchVisible();
        grid->setMinimumHeight(330);
        AddRow(p, QStringLiteral("ThumbGrid 虚拟化网格（2 万张基准 SHINE_THUMB_BENCH=1；占位格永不闪白）"), grid);

        auto* card = new shine::images::ImageCard();
        card->SetThumb(grad(5, 240, 120));
        card->SetTitle(QStringLiteral("第 12 章 · 首帧 S0123"));
        card->SetStatus(QStringLiteral("已渲染"), "ok");
        card->SetActions(QStringLiteral("查看大图"), [] {});
        AddRow(p, QStringLiteral("ImageCard 缩略图 + 标题 + 状态 Tag + 操作"), card);
    }

    {
        auto* p = Page(QStringLiteral("ImageViewer / CropZoom"));
        add(QStringLiteral("看图与放大镜"), "ImageViewer", p);
        QImage tex(800, 600, QImage::Format_RGB32);
        for (int y = 0; y < 600; ++y) {
            for (int x = 0; x < 800; ++x) {
                tex.setPixelColor(x, y, QColor(x % 256, y % 256, (x * y) % 256));
            }
        }
        auto* v = new shine::images::ImageViewer();
        v->SetImage(tex);
        v->setMinimumHeight(340);
        AddRow(p, QStringLiteral("0.1–16× 锚点缩放（滚轮锚=光标）；F 适配 / 1 实际 / ←→ 切图 / Esc（自检 SHINE_VIEWER_SELFTEST=1）"), v);

        auto* cz = new shine::images::CropZoom();
        cz->SetSource(tex);
        cz->Track(QPointF(400, 300));
        AddRow(p, QStringLiteral("CropZoom 3.5× 放大镜（3–4×）"), cz);
    }

    {
        auto* p = Page(QStringLiteral("CompareView 三模式 / SheetGrid 联系表"));
        add(QStringLiteral("对比与联系表"), "CompareSheet", p);
        QImage a(240, 160, QImage::Format_RGB32);
        QImage b(240, 160, QImage::Format_RGB32);
        for (int y = 0; y < 160; ++y) {
            for (int x = 0; x < 240; ++x) {
                a.setPixelColor(x, y, QColor(x % 256, y % 256, 128));
                b.setPixelColor(x, y, x > 120 ? QColor(y % 256, 64, x % 256) : QColor(x % 256, y % 256, 128));
            }
        }
        auto* cmp = new shine::images::CompareView();
        cmp->SetPair(a, b);
        cmp->SetMode(shine::images::CompareView::Mode::Wipe); // 滑动对比
        cmp->setMinimumHeight(180);
        AddRow(p, QStringLiteral("CompareView：并排 / 滑动对比（示出）/ 差异叠加（Mode 切换）"), cmp);

        shine::images::SheetGrid sheet(4, {150, 110});
        for (int i = 0; i < 8; ++i) {
            QImage cell(150, 110, QImage::Format_RGB32);
            for (int y = 0; y < 110; ++y) {
                for (int x = 0; x < 150; ++x) {
                    cell.setPixelColor(x, y, QColor((x + i * 40) % 256, (y + i * 20) % 256, 96));
                }
            }
            sheet.Add(cell, QStringLiteral("镜头 S%1").arg(100 + i));
        }
        const QImage composed = sheet.Compose();
        auto* preview = new QLabel();
        preview->setPixmap(QPixmap::fromImage(composed));
        AddRow(p, QStringLiteral("SheetGrid 联系表 + PNG 导出（ExportPng）"), preview);
        // 截图跑批时顺手把导出件落盘作实证（SHINE_GALLERY_SHOTS 已设 → 导出到同目录）
        const QByteArray shots = qgetenv("SHINE_GALLERY_SHOTS");
        if (!shots.isEmpty()) {
            sheet.ExportPng(std::filesystem::path(shine::util::PathFromUtf8(
                                std::string(shots.constData(), static_cast<std::size_t>(shots.size())))) /
                            "sheet-export.png");
        }
    }
}

// ============================ P02-S9 评审截图包 ============================

} // namespace // 匿名域提前收口：以下 SaveP02Review 需具名导出（UI.md §4 截图包）
namespace {

void Save(const QString& path, QWidget& w) {
    w.show();
    QApplication::processEvents();
    QApplication::processEvents();
    w.repaint(); // 确保子件（QGraphicsView 等）完成首绘再 grab
    QApplication::processEvents();
    w.grab().save(path);
    w.hide();
}

// 确定性程序图（同 Bench）
QImage Grad(int seed, int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.setPixelColor(x, y, QColor((x + seed * 31) % 256, (y + seed * 17) % 256,
                                           (x + y + seed * 7) % 256));
        }
    }
    return img;
}

} // namespace

void SaveP02Review(const std::string& dirUtf8) {
    const QString dir = QString::fromStdString(dirUtf8) + QStringLiteral("/");
    QDir().mkpath(QString::fromStdString(dirUtf8));
    using shine::widgets::Button;
    using shine::widgets::Card;
    using shine::widgets::EmptyState;
    using shine::widgets::ErrorState;
    using shine::widgets::SetForcedState;
    using shine::widgets::State;

    // —— gallery-button-states.png：按钮 5 态 × 4 变体特写 ——
    {
        QWidget host;
        auto* lay = new QVBoxLayout(&host);
        lay->addWidget(new QLabel(QStringLiteral("按钮 · 5 态 × 4 变体（常态/悬停/按下/禁用/焦点）"), &host));
        const std::array<std::pair<const char*, Button::Variant>, 4> variants = {{
            {"primary", Button::Variant::Primary},
            {"secondary", Button::Variant::Secondary},
            {"ghost", Button::Variant::Ghost},
            {"danger", Button::Variant::Danger},
        }};
        const std::array<State, 5> states = {State::Normal, State::Hover, State::Pressed,
                                             State::Disabled, State::Focus};
        for (const auto& [vname, variant] : variants) {
            auto* rowLabel = new QLabel(QString::fromLatin1(vname), &host);
            lay->addWidget(rowLabel);
            auto* row = new QHBoxLayout();
            for (const State st : states) {
                auto* b = new Button(QStringLiteral("生成剧本"), variant, Button::Size::Md, &host);
                SetForcedState(b, st);
                row->addWidget(b);
            }
            lay->addLayout(row);
        }
        host.resize(620, 420);
        Save(dir + QStringLiteral("gallery-button-states.png"), host);
    }

    // —— gallery-container-states.png：容器 3 态（正常/空/错误）——
    {
        QWidget host;
        auto* lay = new QVBoxLayout(&host);
        lay->addWidget(new QLabel(QStringLiteral("容器 · 3 态（有数据 / 空 / 错误）"), &host));
        auto* row = new QHBoxLayout();
        auto* normal = new Card(Card::Variant::Outlined, &host);
        auto* t = new QLabel(QStringLiteral("剧集卡 · 第 12 章改编"), normal);
        shine::widgets::SetKind(t, "statetitle");
        normal->BodyLayout()->addWidget(t);
        auto* s = new QLabel(QStringLiteral("状态：生成中 · 点击进入详情"), normal);
        shine::widgets::SetKind(s, "statesub");
        normal->BodyLayout()->addWidget(s);
        row->addWidget(normal);
        row->addWidget(new EmptyState(QStringLiteral("🗒"), QStringLiteral("还没有剧集"),
                                      QStringLiteral("先新建一部剧集再开始生成"),
                                      QStringLiteral("新建剧集"), &host));
        row->addWidget(new ErrorState(QStringLiteral("加载失败"),
                                      QStringLiteral("网络超时（Error.detail）"), &host));
        lay->addLayout(row);
        host.resize(900, 300);
        Save(dir + QStringLiteral("gallery-container-states.png"), host);
    }

    // —— gallery-datatable.png：表格（选中 + 排序 + 固定列）——
    {
        QWidget host;
        auto* lay = new QVBoxLayout(&host);
        auto* table = new shine::data::DataTable(QStringLiteral("p02review"), &host);
        table->SetColumns({{"name", QStringLiteral("章"), 200},
                           {"stage", QStringLiteral("阶段"), 110},
                           {"words", QStringLiteral("字数"), 90},
                           {"updated", QStringLiteral("更新"), 120}});
        std::vector<std::vector<QString>> rows;
        for (int i = 0; i < 18; ++i) {
            rows.push_back({QStringLiteral("第 %1 章 · 风起").arg(i + 1),
                            i % 3 == 0 ? QStringLiteral("生成中") : QStringLiteral("完成"),
                            QString::number(900 + i * 31), QStringLiteral("09:41")});
        }
        table->SetRows(rows);
        table->SetFixedColumns(1);                    // 固定列
        table->sortByColumn(1, Qt::AscendingOrder);   // 排序（表头指示）
        table->selectRow(3);                          // 选中
        table->setMinimumHeight(360);
        lay->addWidget(table);
        host.resize(760, 400);
        Save(dir + QStringLiteral("gallery-datatable.png"), host);
    }

    // —— gallery-thumbgrid.png：缩略图网格（加载中 + 失败卡 全部入画）——
    {
        QWidget host;
        auto* lay = new QVBoxLayout(&host);
        auto* grid = new shine::images::ThumbGrid(&host);
        grid->SetCellSize(152, 176);
        grid->setModel(new QStandardItemModel(9, 1, grid)); // 3×3：已载 / 加载中 / 失败 各一行
        for (int i = 0; i < 9; ++i) {
            grid->model()->setData(grid->model()->index(i, 0), QStringLiteral("图 %1").arg(i + 1));
        }
        grid->SetThumbProvider([](int row, const std::function<void(int, QImage)>& done) {
            if (row >= 6) {
                done(row, QImage{}); // 空图 = 失败卡（⚠ 重试提示）
            } else if (row < 3) {
                done(row, Grad(row % 13, 140, 130)); // 已加载
            }
            // 3..5 不回图 = 加载中占位（…）
        });
        grid->PrefetchVisible();
        grid->setMinimumHeight(580);
        lay->addWidget(grid);
        host.resize(620, 620);
        Save(dir + QStringLiteral("gallery-thumbgrid.png"), host);
    }

    // —— gallery-imageviewer.png：查看器（放大后 2.5×）——
    {
        QWidget host;
        auto* lay = new QVBoxLayout(&host);
        auto* v = new shine::images::ImageViewer(&host);
        lay->addWidget(v);
        host.resize(760, 420);
        host.show(); // 先 show：隐藏态 viewport 尺寸为 0，缩放/滚动数学会把图推出视野
        QApplication::processEvents();
        v->SetImage(Grad(3, 800, 600));
        v->ZoomBy(2.5, QPoint(300, 220)); // 放大后
        QApplication::processEvents();
        Save(dir + QStringLiteral("gallery-imageviewer.png"), host);
    }

    // —— gallery-<主题>-full.png：控件画廊全览 × 4 主题 ——
    for (const shine::theme::ThemeId id : shine::theme::kAllThemes) {
        shine::theme::ThemeService::Switch(id, false);
        QApplication::processEvents();
        WidgetGalleryView gv;
        gv.resize(1180, 820);
        const QString name = QString::fromUtf8(shine::theme::ThemeFileName(id).data(),
                                               static_cast<int>(shine::theme::ThemeFileName(id).size()));
        Save(dir + QStringLiteral("gallery-%1-full.png").arg(name), gv);
    }

    // —— theme-switch-before/after.png：主题切换前后 ——
    {
        shine::theme::ThemeService::Switch(shine::theme::ThemeId::DeepSpace, false);
        QApplication::processEvents();
        WidgetGalleryView gv;
        gv.resize(1180, 820);
        gv.show();
        QApplication::processEvents();
        gv.grab().save(dir + QStringLiteral("theme-switch-before.png"));
        shine::theme::ThemeService::Switch(shine::theme::ThemeId::Dusk, false);
        QApplication::processEvents();
        gv.grab().save(dir + QStringLiteral("theme-switch-after.png"));
        gv.hide();
        shine::theme::ThemeService::Switch(shine::theme::ThemeId::DeepSpace, false);
        QApplication::processEvents();
    }

    // —— gallery-motion-reduced.png：「减少动效」打开后的画廊 ——
    {
        shine::motion::SetReduceMotion(true);
        QApplication::processEvents();
        WidgetGalleryView gv;
        gv.resize(1180, 820);
        Save(dir + QStringLiteral("gallery-motion-reduced.png"), gv);
        shine::motion::SetReduceMotion(false);
    }
}

} // namespace shine::gallery
