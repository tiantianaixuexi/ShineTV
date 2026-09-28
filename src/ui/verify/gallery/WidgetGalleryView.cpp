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
#include "util/Encoding.h"

#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

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

void BuildAll(WidgetGalleryView* self, QListWidget* nav, QStackedWidget* stack) {
    const auto add = [nav, stack](const QString& navText, const char* fileName, QWidget* page) {
        auto* item = new QListWidgetItem(navText, nav);
        item->setData(Qt::UserRole, QString::fromLatin1(fileName));
        stack->addWidget(page);
    };

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
