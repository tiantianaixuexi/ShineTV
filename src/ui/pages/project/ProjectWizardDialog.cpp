#include "ui/pages/project/ProjectWizardDialog.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Surfaces.h"
#include "project/ProjectTemplate.h"
#include "util/Encoding.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "ui/kit/controls/WidgetCommon.h"

namespace shine::app {
namespace {

// —— webui ui.css:1031-1072 .steps 的等价物 ——
// kit 只有下划线式 Tabs，没有「序号圆点 + 连接线」的 Steps 控件；本文件自绘一个局部实现
// （页面私有，不进 kit）。四步：当前步 = accent 实心圆 + accent-fg 序号；
// 已到达步 = accent 描边 + accent 序号；未到达步 = line.strong 描边 + text-muted。
class StepDots final : public QWidget {
  public:
    explicit StepDots(const QStringList& labels, QWidget* parent = nullptr)
        : QWidget(parent), labels_(labels) {
        setFixedHeight(kDot);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setMouseTracking(false);
        setCursor(Qt::PointingHandCursor);
    }

    void SetCurrent(int index) {
        if (index == current_) {
            return;
        }
        current_ = index;
        update();
    }

    void SetReachable(int maxIndex) {
        if (maxIndex == max_reachable_) {
            return;
        }
        max_reachable_ = maxIndex;
        update();
    }

    void SetOnClick(std::function<void(int)> cb) { on_click_ = std::move(cb); }

    [[nodiscard]] int HitStep(const QPoint& p) const {
        int x = 0;
        for (int i = 0; i < labels_.size(); ++i) {
            const int w = StepWidth(i);
            if (p.x() >= x && p.x() < x + w) {
                return i;
            }
            x += w;
        }
        return -1;
    }

    [[nodiscard]] QSize sizeHint() const override {
        int w = 0;
        for (int i = 0; i < labels_.size(); ++i) {
            w += StepWidth(i);
        }
        return {w, kDot};
    }

  protected:
    void paintEvent(QPaintEvent* ev) override {
        QWidget::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const theme::ColorToken& t = theme::Current();

        QFont f = font();
        f.setPixelSize(12); // .steps .stp font-size 12px
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const QFontMetrics fm(f);

        int x = 0;
        for (int i = 0; i < labels_.size(); ++i) {
            const int step_w = StepWidth(i);
            const bool done = i < current_;
            const bool on = i == current_;
            const QColor accent = widgets::TokenQColor(t.accentPrimary);
            const QColor ring = on || done ? accent : widgets::TokenQColor(t.lineStrong);

            // .stp .idx：20×20 圆 + 1.5px 描边 + 11px 序号
            const QRectF dot(x, (kDot - kDotSize) / 2.0, kDotSize, kDotSize);
            p.setBrush(on ? accent : QColor(Qt::transparent));
            p.setPen(QPen(ring, 1.5));
            p.drawEllipse(dot);
            QFont nf = f;
            nf.setPixelSize(11);
            p.setFont(nf);
            p.setPen(on ? widgets::TokenQColor(t.accentPrimaryFg)
                        : (done ? accent : widgets::TokenQColor(t.textMuted)));
            p.drawText(dot, Qt::AlignCenter, QString::number(i + 1));
            p.setFont(f);

            // .stp 文案：当前步 text-primary，其余 text-muted
            p.setPen(on ? widgets::TokenQColor(t.textPrimary) : widgets::TokenQColor(t.textMuted));
            p.drawText(QRect(x + kDot + kStepGap, 0, fm.horizontalAdvance(labels_.at(i)), kDot),
                       Qt::AlignVCenter | Qt::AlignLeft, labels_.at(i));

            // .sline：24×1.5px 连接线
            if (i + 1 < labels_.size()) {
                const int lx = x + kDot + kStepGap + fm.horizontalAdvance(labels_.at(i)) + kLineGap;
                p.setPen(Qt::NoPen);
                p.setBrush(widgets::TokenQColor(t.lineNormal));
                p.drawRect(QRectF(lx, kDot / 2.0 - 0.75, kLine, 1.5));
            }
            x += step_w;
        }
    }

    void mousePressEvent(QMouseEvent* ev) override {
        const int hit = HitStep(ev->position().toPoint());
        if (hit >= 0 && on_click_) {
            on_click_(hit);
        }
        ev->accept();
    }

  private:
    [[nodiscard]] int StepWidth(int i) const {
        if (i < 0 || i >= labels_.size()) {
            return 0;
        }
        QFont f = font();
        f.setPixelSize(12);
        const int label_w = QFontMetrics(f).horizontalAdvance(labels_.at(i));
        int w = kDot + kStepGap + label_w;
        if (i + 1 < labels_.size()) {
            w += kLineGap + kLine;
        }
        return w;
    }

    static constexpr int kDotSize = 20; // .stp .idx 20×20
    static constexpr int kDot = 20;
    static constexpr int kStepGap = 6;  // .stp gap 6px
    static constexpr int kLineGap = 6;  // .steps gap 6px
    static constexpr int kLine = 24;    // .sline width 24px

    QStringList labels_;
    int current_ = 0;
    int max_reachable_ = 0;
    std::function<void(int)> on_click_;
};

// widgets::Tag 只暴露只读 Text()，改文案走「内部 QLabel + repolish」，
// 与 kit 里 Chip 的 tone 用法同构，不动 kit 本身。
void SetTagText(widgets::Tag* tag, const QString& text) {
    if (tag == nullptr) {
        return;
    }
    if (auto* label = tag->findChild<QLabel*>(); label != nullptr) {
        label->setText(text);
    }
}

// —— ① 步的模板卡（jsx:37-51：整行可点，选中 = accent 描边 + 右侧打勾）——
// kit 的 Radio 只有「圆点 + 文案」，放不下模板的一句话说明，故本页自绘这张行卡。
class TemplateCard final : public QFrame {
  public:
    TemplateCard(const QString& name, const QString& desc, int index, QWidget* parent)
        : QFrame(parent), index_(index), name_(name), desc_(desc) {
        setObjectName(QStringLiteral("tplCard"));
        setCursor(Qt::PointingHandCursor);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(14, 12, 14, 12);
        row->setSpacing(theme::space::kSteps[3]); // 12（jsx padding 14 + gap-3）
        auto* col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
        auto* title = new QLabel(name, this);
        widgets::SetKind(title, "tplname");
        col->addWidget(title);
        auto* sub = new QLabel(desc, this);
        widgets::SetKind(sub, "tpldesc");
        sub->setWordWrap(true);
        col->addWidget(sub);
        row->addLayout(col, 1);
        check_ = new QLabel(QStringLiteral("✓"), this);
        widgets::SetKind(check_, "tplcheck");
        check_->hide(); // 选中才显示
        row->addWidget(check_, 0, Qt::AlignVCenter);
    }

    void SetSelected(bool on) {
        if (selected_ == on) {
            return;
        }
        selected_ = on;
        setProperty("selected", on);
        widgets::Repolish(this);
        if (check_ != nullptr) {
            check_->setVisible(on);
        }
    }

    [[nodiscard]] int Index() const { return index_; }

    void SetOnClick(std::function<void(int)> cb) { on_click_ = std::move(cb); }

  protected:
    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton && on_click_) {
            on_click_(index_);
        }
        ev->accept();
    }

  private:
    int index_ = 0;
    QString name_;
    QString desc_;
    QLabel* check_ = nullptr;
    bool selected_ = false;
    std::function<void(int)> on_click_;
};

// UTF-8 双向转换（文本约定 util/Encoding.h）
[[nodiscard]] std::string Utf8(const QString& s) {
    const QByteArray bytes = s.toUtf8();
    return std::string{bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString FromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

[[nodiscard]] QString PathText(const std::filesystem::path& p) {
    return FromUtf8(shine::util::PathToUtf8(p));
}

// 目录树预览的中间节点（PreviewTree 的相对路径列表 → 缩进树；目录在前、文件在后）
struct TreeNode {
    QString name;
    bool dir = false;
    std::vector<TreeNode> kids;

    [[nodiscard]] TreeNode* FindChild(const QString& n) {
        for (TreeNode& k : kids) {
            if (k.name == n) {
                return &k;
            }
        }
        return nullptr;
    }
};

void EmitTree(const TreeNode& node, QTreeWidgetItem* parent) {
    std::vector<const TreeNode*> kids;
    kids.reserve(node.kids.size());
    for (const TreeNode& k : node.kids) {
        kids.push_back(&k);
    }
    std::stable_sort(kids.begin(), kids.end(),
                     [](const TreeNode* a, const TreeNode* b) { return a->dir && !b->dir; });
    for (const TreeNode* k : kids) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, k->name);
        item->setData(0, Qt::UserRole, k->dir);
        if (k->dir) {
            item->setExpanded(true);
        }
        EmitTree(*k, item);
    }
}

} // namespace

ProjectWizardDialog::ProjectWizardDialog(shine::project::ProjectService* svc, QWidget* parent)
    : QDialog(parent), svc_(svc) {
    setWindowTitle(QStringLiteral("新建项目"));
    setModal(true);
    setObjectName(QStringLiteral("projWizard"));
    resize(600, 560); // jsx:26 modal width 600

    templates_ = svc_ != nullptr ? svc_->Templates() : std::vector<shine::project::ProjectTemplate>{};
    if (templates_.empty()) {
        templates_ = shine::project::AllTemplates(); // 数据定义仍全部来自项目模块
    }

    BuildUi();
    UpdatePathPreview();
    Validate();
}

void ProjectWizardDialog::SetOnCreated(std::function<void(const shine::project::ProjectRef&)> cb) {
    on_created_ = std::move(cb);
}

int ProjectWizardDialog::CurrentStep() const { return step_; }

void ProjectWizardDialog::GoToStep(int step) {
    if (step < 0 || step > 3) {
        return;
    }
    step_ = step;
    if (step > max_step_) {
        max_step_ = step;
    }
    syncing_ = true;
    // 这两个局部控件类是文件内的匿名类（无 Q_OBJECT），用 static_cast 取回。
    if (auto* dots = static_cast<StepDots*>(steps_); dots != nullptr) {
        dots->SetCurrent(step);
        dots->SetReachable(max_step_);
    }
    syncing_ = false;
    stack_->setCurrentIndex(step);
    if (step == 3) {
        RefreshConfirm();
    }
    Validate();
}

shine::project::ProjectSpec ProjectWizardDialog::Spec() const { return ComposeSpec(); }

void ProjectWizardDialog::SetSpec(const shine::project::ProjectSpec& spec) {
    touched_ = true; // 回填即触发校验（错误文案要当场可见）
    create_error_.clear();

    // rootDir 以项目名结尾 = 完整项目目录 →「位置」取其父；否则把 rootDir 视为「位置」本身。
    // 合成目标目录恒为 位置/项目名称（中文路径经 PathFromUtf8）。
    std::filesystem::path loc = spec.rootDir;
    if (!spec.name.empty() && shine::util::FileNameToUtf8(spec.rootDir) == spec.name) {
        loc = spec.rootDir.parent_path();
    }

    int sel = 0;
    for (std::size_t i = 0; i < templates_.size(); ++i) {
        if (templates_[i].id == spec.templateId) {
            sel = static_cast<int>(i);
        }
    }
    template_index_ = sel;

    syncing_ = true;
    name_edit_->SetText(FromUtf8(spec.name));
    premise_edit_->SetText(FromUtf8(spec.premise));
    loc_edit_->SetText(PathText(loc));
    for (std::size_t i = 0; i < template_cards_.size(); ++i) {
        if (auto* card = static_cast<TemplateCard*>(template_cards_[i]); card != nullptr) {
            card->SetSelected(static_cast<int>(i) == sel);
        }
    }
    syncing_ = false;

    UpdatePathPreview();
    Validate();
}

QString ProjectWizardDialog::StepErrorText() const {
    if (step_ == 3 && !create_error_.isEmpty()) {
        return create_error_;
    }
    return StepProblem(step_);
}

void ProjectWizardDialog::keyPressEvent(QKeyEvent* ev) {
    switch (ev->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            GoNext(); // Enter = 前进 / 创建（文本区的 Enter 换行被控件自身吃掉，不进这里）
            ev->accept();
            return;
        case Qt::Key_Escape:
            RequestCancel();
            ev->accept();
            return;
        default:
            QDialog::keyPressEvent(ev);
            return;
    }
}

// ─────────────────────────────── 布局 ───────────────────────────────

void ProjectWizardDialog::BuildUi() {
    // jsx:27-111：modal 由「头（modal-h）+ 体（modal-b）+ 底（modal-f）」三段组成。
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // —— jsx:27-32 / ui.css:955-965 .modal-h：padding 14 18 + 下边框 ——
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("wizHeader"));
    auto* header_row = new QHBoxLayout(header);
    header_row->setContentsMargins(18, 14, 18, 14);
    header_row->setSpacing(theme::space::kSteps[3]); // 10（.modal-h gap）
    // Icon name="sparkles" style={{ color: var(--accent) }}
    auto* glyph = new QLabel(QStringLiteral("✦"), header);
    widgets::SetKind(glyph, "wizglyph");
    header_row->addWidget(glyph, 0, Qt::AlignVCenter);
    // .modal-h .title { font-size: 15px; font-weight: 700 }
    auto* title = new QLabel(QStringLiteral("新建项目"), header);
    widgets::SetKind(title, "dialogtitle");
    widgets::SetSemibold(title, true);
    header_row->addWidget(title, 0, Qt::AlignVCenter);
    header_row->addStretch(1);
    // Steps：jsx:31 <Steps steps={['模板','命名','创意','确认']} current={step} />
    auto* dots = new StepDots({QStringLiteral("模板"), QStringLiteral("命名"),
                               QStringLiteral("创意"), QStringLiteral("确认")}, header);
    dots->SetCurrent(0);
    dots->SetReachable(0);
    dots->SetOnClick([this](int index) {
        if (syncing_) {
            return;
        }
        if (index != step_ && index <= max_step_) {
            GoToStep(index); // 允许点回已完成（到达过）的步
        }
    });
    steps_ = dots;
    header_row->addWidget(dots, 0, Qt::AlignVCenter);
    root->addWidget(header);

    // —— jsx:33-99 / ui.css:966-969 .modal-b：padding 18 + overflow-y auto ——
    auto* body = new QWidget(this);
    auto* body_lay = new QVBoxLayout(body);
    body_lay->setContentsMargins(18, 18, 18, 18);
    body_lay->setSpacing(theme::space::kSteps[3]); // jsx .col.gap-3 = 12

    stack_ = new QStackedWidget(body);
    body_lay->addWidget(stack_, 1);

    // —— jsx:34-52 ① 选模板：三张可点卡片，选中 = accent 描边 + 打勾 ——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(theme::space::kSteps[3]); // .col gap-3
        for (std::size_t i = 0; i < templates_.size(); ++i) {
            const int idx = static_cast<int>(i);
            auto* card = new TemplateCard(FromUtf8(templates_[i].name),
                                          FromUtf8(templates_[i].description), idx, page);
            card->SetOnClick([this](int hit) {
                if (!syncing_) {
                    SelectTemplate(hit);
                }
            });
            card->SetSelected(idx == template_index_);
            template_cards_.push_back(card);
            lay->addWidget(card);
        }
        lay->addStretch(1);
        stack_->addWidget(page);
    }

    // —— jsx:53-70 ② 命名与位置 ——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(theme::space::kSteps[4]); // .col gap-4

        name_edit_ = new widgets::TextInput(page);
        name_edit_->SetPlaceholder(QStringLiteral("例如：灯语回声"));
        name_edit_->SetOnChanged([this](const QString&) {
            if (syncing_) {
                return;
            }
            touched_ = true;
            UpdatePathPreview();
            Validate();
        });
        name_field_ = new widgets::Field(QStringLiteral("项目名称"), widgets::Field::LabelPos::Top,
                                         name_edit_, page);
        lay->addWidget(name_field_);

        auto* loc_row = new QWidget(page);
        auto* loc_lay = new QHBoxLayout(loc_row);
        loc_lay->setContentsMargins(0, 0, 0, 0);
        loc_lay->setSpacing(theme::space::kSteps[2]); // .row gap-2
        loc_edit_ = new widgets::TextInput(loc_row);
        loc_edit_->SetPlaceholder(QStringLiteral("例如：E:\\projects"));
        loc_edit_->SetOnChanged([this](const QString&) {
            if (syncing_) {
                return;
            }
            touched_ = true;
            UpdatePathPreview();
            Validate();
        });
        loc_lay->addWidget(loc_edit_, 1);
        auto* browse = new widgets::Button(QStringLiteral("浏览…"), widgets::Button::Variant::Secondary,
                                           widgets::Button::Size::Md, loc_row);
        connect(browse, &widgets::Button::clicked, this, [this] {
            const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择项目位置"));
            if (dir.isEmpty()) {
                return;
            }
            touched_ = true;
            syncing_ = true;
            // 中文路径安全：QDir 原生分隔符 → UTF-8 串 → PathFromUtf8 再进 std::filesystem::path
            const std::filesystem::path picked = shine::util::PathFromUtf8(
                Utf8(QDir::toNativeSeparators(dir)));
            syncing_ = false;
            loc_edit_->SetText(PathText(picked));
            UpdatePathPreview();
            Validate();
        });
        loc_lay->addWidget(browse);
        loc_field_ = new widgets::Field(QStringLiteral("位置"), widgets::Field::LabelPos::Top,
                                        loc_row, page);
        // jsx:58 Field label="位置" help="项目目录将创建在该路径下"
        loc_field_->SetHelp(QStringLiteral("项目目录将创建在该路径下"));
        lay->addWidget(loc_field_);

        // jsx:64-68 「将创建目录」预览卡（fill-muted 底）
        auto* preview = new widgets::Card(widgets::Card::Variant::Flat, page);
        preview->setObjectName(QStringLiteral("pathPreview"));
        QVBoxLayout* preview_lay = preview->BodyLayout();
        preview_lay->setContentsMargins(14, 10, 14, 10);
        preview_lay->setSpacing(4);
        auto* cap = new QLabel(QStringLiteral("将创建目录"), preview);
        widgets::SetKind(cap, "wizcap");
        preview_lay->addWidget(cap);
        path_preview_ = new QLabel(QStringLiteral("…"), preview);
        widgets::SetKind(path_preview_, "wizmono");
        path_preview_->setWordWrap(true);
        path_preview_->setTextInteractionFlags(Qt::TextSelectableByMouse); // 完整路径可复制
        preview_lay->addWidget(path_preview_);
        lay->addWidget(preview);
        lay->addStretch(1);
        stack_->addWidget(page);
    }

    // —— jsx:71-82 ③ 一句话创意（可留空）——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        premise_edit_ = new widgets::TextArea(200, page);
        connect(premise_edit_->Edit(), &QPlainTextEdit::textChanged, this, [this] {
            if (syncing_) {
                return;
            }
            touched_ = true;
            // jsx:72 label 带字数计数：一句话创意（N/200）
            if (premise_caption_ != nullptr) {
                premise_caption_->setText(QStringLiteral("一句话创意（%1/200）")
                                              .arg(premise_edit_->Text().size()));
            }
            Validate();
        });
        premise_field_ = new widgets::Field(QStringLiteral("一句话创意（0/200）"),
                                            widgets::Field::LabelPos::Top, premise_edit_, page);
        // Field 的标题 QLabel 是内部第一个子控件；取出来单独维护计数文案。
        if (auto* caption = premise_field_->findChild<QLabel*>(); caption != nullptr) {
            premise_caption_ = caption;
        }
        // jsx:72 help：写进 project.json，供 LLM 初始化参考；留空可跳过
        premise_field_->SetHelp(QStringLiteral("将写入 project.json，供 LLM 初始化参考；留空可跳过"));
        lay->addWidget(premise_field_);
        lay->addStretch(1);
        stack_->addWidget(page);
    }

    // —— jsx:83-98 ④ 确认：摘要卡 + 目录树预览 ——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(theme::space::kSteps[3]); // .col gap-3

        // jsx:85-95 card glow：padding 16，名称 17 w800 + 路径 + 模板/创意 Tag
        auto* summary_card = new widgets::Card(widgets::Card::Variant::Outlined, page);
        QVBoxLayout* summary_lay = summary_card->BodyLayout();
        summary_lay->setContentsMargins(16, 16, 16, 16);
        summary_lay->setSpacing(3);
        confirm_name_ = new QLabel(summary_card);
        widgets::SetKind(confirm_name_, "wizname");
        summary_lay->addWidget(confirm_name_);
        confirm_path_ = new QLabel(summary_card);
        widgets::SetKind(confirm_path_, "wizpath");
        confirm_path_->setWordWrap(true);
        confirm_path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        summary_lay->addWidget(confirm_path_);
        auto* tags = new QWidget(summary_card);
        auto* tags_row = new QHBoxLayout(tags);
        tags_row->setContentsMargins(0, 0, 0, 0);
        tags_row->setSpacing(theme::space::kSteps[2]); // .row gap-2
        confirm_tpl_tag_ = new widgets::Tag(QStringLiteral("—"), "accent", false, tags);
        confirm_idea_tag_ = new widgets::Tag(QStringLiteral("—"), "", false, tags);
        tags_row->addWidget(confirm_tpl_tag_);
        tags_row->addWidget(confirm_idea_tag_);
        summary_lay->addWidget(tags);
        lay->addWidget(summary_card);

        auto* tree_label = new QLabel(QStringLiteral("目录树预览"), page);
        widgets::SetKind(tree_label, "fieldlabel");
        lay->addWidget(tree_label);
        tree_ = new QTreeWidget(page);
        tree_->setHeaderHidden(true);
        tree_->setRootIsDecorated(true);
        tree_->setIndentation(theme::space::kSteps[4]);
        tree_->setSelectionMode(QAbstractItemView::NoSelection);
        lay->addWidget(tree_, 1);
        stack_->addWidget(page);
    }
    root->addWidget(body, 1);

    // —— 创建阶段提示（UI.md §3 加载态）——
    progress_ = new widgets::ProgressBar(this);
    progress_->hide();
    root->addWidget(progress_);

    // —— jsx:100-110 / ui.css:970-976 .modal-f：padding 12 18 + 上边框 ——
    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("wizFooter"));
    auto* actions = new QHBoxLayout(footer);
    actions->setContentsMargins(18, 12, 18, 12);
    actions->setSpacing(theme::space::kSteps[3]); // .modal-f gap 8
    // jsx:101 <span className="tiny dim">表单内容不会上传 · 演示数据</span>
    auto* hint = new QLabel(QStringLiteral("表单内容不会上传 · 项目文件只写入本地"), footer);
    widgets::SetKind(hint, "wizcap");
    actions->addWidget(hint, 0, Qt::AlignVCenter);
    actions->addStretch(1);
    cancel_ = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                  widgets::Button::Size::Md, footer);
    prev_ = new widgets::Button(QStringLiteral("上一步"), widgets::Button::Variant::Secondary,
                                widgets::Button::Size::Md, footer);
    next_ = new widgets::Button(QStringLiteral("下一步"), widgets::Button::Variant::Primary,
                                widgets::Button::Size::Md, footer);
    actions->addWidget(cancel_);
    actions->addWidget(prev_);
    actions->addWidget(next_);
    root->addWidget(footer);

    connect(cancel_, &widgets::Button::clicked, this, [this] { RequestCancel(); });
    connect(prev_, &widgets::Button::clicked, this, [this] {
        if (step_ > 0) {
            GoToStep(step_ - 1);
        }
    });
    connect(next_, &widgets::Button::clicked, this, [this] { GoNext(); });

    // Enter = 前进 / 创建：单行输入把回车吞成 returnPressed，从这里转发；
    // 多行文本区的 Enter 保持换行（QPlainTextEdit 自己消费），不会触发前进。
    connect(name_edit_->Edit(), &QLineEdit::returnPressed, this, [this] { GoNext(); });
    connect(loc_edit_->Edit(), &QLineEdit::returnPressed, this, [this] { GoNext(); });

    // 页面局部 QSS：只落在本对话框子树内（#projWizard 前缀），与 kit 全局 QSS 互不干扰。
    setStyleSheet(WizardQss());
    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(WizardQss()); });
}

QString ProjectWizardDialog::WizardQss() const {
    // ui.css 里本页独占的几条：modal 底/边/圆角、头尾分隔线、标题字重、步骤条文案、
    // 模板卡选中态、mono 预览行。控件本身（按钮 / 输入 / Tag / Tree）全部走 kit 全局 QSS。
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QDialog#projWizard { background-color: %1; border: 1px solid %2; border-radius: 14px; }\n"
               "QDialog#projWizard QWidget#wizHeader, QDialog#projWizard QWidget#wizFooter {\n"
               "  background: transparent; border: none; }\n"
               "QDialog#projWizard QLabel[shineKind=\"dialogtitle\"] { font-weight: 700; }\n"
               "QDialog#projWizard QLabel[shineKind=\"wizglyph\"] {\n"
               "  background: transparent; color: %3; font-size: 15px; }\n"
               "QDialog#projWizard QLabel[shineKind=\"wizcap\"] {\n"
               "  background: transparent; color: %4; font-size: 11px; }\n"
               "QDialog#projWizard QLabel[shineKind=\"wizmono\"] {\n"
               "  background: transparent; color: %5; font-size: 12px;\n"
               "  font-family: \"Cascadia Code\", \"JetBrains Mono\", Consolas, monospace; }\n"
               "QDialog#projWizard QLabel[shineKind=\"wizname\"] {\n"
               "  background: transparent; color: %6; font-size: 17px; font-weight: 800; }\n"
               "QDialog#projWizard QLabel[shineKind=\"wizpath\"] {\n"
               "  background: transparent; color: %4; font-size: 12px; }\n"
               // ① 模板卡：基线 = 卡片底/边；选中 = accent 描边（jsx:40 的 accent 边框）
               "QDialog#projWizard QFrame#tplCard {\n"
               "  background-color: %7; border: 1px solid %8; border-radius: 10px; }\n"
               "QDialog#projWizard QFrame#tplCard:hover { border-color: %9; }\n"
               "QDialog#projWizard QFrame#tplCard[selected=\"true\"] { border-color: %3; }\n"
               "QDialog#projWizard QLabel[shineKind=\"tplname\"] {\n"
               "  background: transparent; color: %6; font-size: 13px; font-weight: 700; }\n"
               "QDialog#projWizard QLabel[shineKind=\"tpldesc\"] {\n"
               "  background: transparent; color: %4; font-size: 11px; }\n"
               "QDialog#projWizard QLabel[shineKind=\"tplcheck\"] {\n"
               "  background: transparent; color: %3; font-size: 14px; font-weight: 700; }\n")
        .arg(widget::CssRgb(t.bgOverlay), widget::CssRgb(t.lineNormal),
             widget::CssRgb(t.accentPrimary), widget::CssRgb(t.textMuted),
             widget::CssRgb(t.textPrimary), widget::CssRgb(t.textPrimary),
             widget::CssRgb(t.bgPanel), widget::CssRgb(t.lineSubtle),
             widget::CssRgb(t.lineStrong));
}

void ProjectWizardDialog::SelectTemplate(int index) {
    if (index < 0 || index >= static_cast<int>(template_cards_.size())) {
        return;
    }
    template_index_ = index;
    for (std::size_t i = 0; i < template_cards_.size(); ++i) {
        if (auto* card = static_cast<TemplateCard*>(template_cards_[i]); card != nullptr) {
            card->SetSelected(static_cast<int>(i) == index);
        }
    }
    Validate();
}

// ───────────────────────── 校验 / 预览 / 按钮态 ─────────────────────────

shine::project::ProjectSpec ProjectWizardDialog::ComposeSpec() const {
    shine::project::ProjectSpec spec;
    spec.name = Utf8(name_edit_->Text().trimmed());
    spec.premise = Utf8(premise_edit_->Text().trimmed());
    spec.templateId = (template_index_ >= 0 &&
                       template_index_ < static_cast<int>(templates_.size()))
                          ? templates_[static_cast<std::size_t>(template_index_)].id
                          : std::string{"blank"};
    // 目标目录 = 位置 / 项目名称（UTF-8 → path，中文路径安全）
    std::filesystem::path root =
        shine::util::PathFromUtf8(Utf8(QDir::toNativeSeparators(loc_edit_->Text().trimmed())));
    if (!spec.name.empty()) {
        root /= shine::util::PathFromUtf8(spec.name);
    }
    spec.rootDir = root;
    return spec;
}

ProjectWizardDialog::FieldProblem ProjectWizardDialog::NameLocProblem() const {
    const QString name = name_edit_->Text().trimmed();
    const QString loc = loc_edit_->Text().trimmed();
    if (name.isEmpty()) {
        return {QStringLiteral("项目名称不能为空，请输入名称后再继续"), true};
    }
    static const QString kBad = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : name) {
        if (kBad.contains(c)) {
            return {QStringLiteral("项目名称不能包含 \\ / : * ? \" < > | 这些字符，请换个名字"),
                    true};
        }
    }
    if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
        return {QStringLiteral("项目名称不可用，请换个名字"), true};
    }
    if (loc.isEmpty()) {
        return {QStringLiteral("请选择项目位置（可用「浏览…」选目录）"), false};
    }

    const shine::project::ProjectSpec spec = ComposeSpec();
    std::error_code ec;
    const std::filesystem::path root = spec.rootDir;
    const std::filesystem::path parent = root.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, ec)) {
        return {QStringLiteral("项目位置不存在，请重新选择"), false};
    }
    if (std::filesystem::is_directory(root, ec)) {
        if (std::filesystem::exists(shine::project::ProjectFilePath(root), ec)) {
            return {QStringLiteral("该目录已包含 ShineTV 项目（project.json），请换个名字"), true};
        }
        if (!std::filesystem::is_empty(root, ec)) {
            return {QStringLiteral("目标目录已存在且非空，请换个名字或换个位置"), true};
        }
    } else if (std::filesystem::exists(root, ec)) {
        return {QStringLiteral("目标路径已存在但不是目录，请换个名字"), true};
    }

    // 合成目标路径（位置/项目名称）交给项目服务终检；中文原因直接进 Field error
    const auto res = shine::project::ValidateSpec(spec);
    if (!res.has_value()) {
        const bool on_name = res.error().code == "conflict" || res.error().code == "invalid";
        QString msg = FromUtf8(res.error().message);
        if (on_name && !msg.contains(QStringLiteral("换个名字"))) {
            msg += QStringLiteral("，请换个名字");
        }
        return {msg, on_name};
    }
    return {};
}

QString ProjectWizardDialog::PremiseProblem() const {
    if (premise_edit_->IsOverLimit()) {
        return QStringLiteral("一句话创意最多 200 字，请精简一下");
    }
    return {};
}

QString ProjectWizardDialog::StepProblem(int step) const {
    switch (step) {
        case 1:
            return NameLocProblem().text;
        case 2:
            return PremiseProblem();
        case 3: {
            const QString naming = NameLocProblem().text;
            return naming.isEmpty() ? PremiseProblem() : naming;
        }
        default:
            return {}; // 步 0 选模板无校验
    }
}

void ProjectWizardDialog::Validate() {
    const FieldProblem naming = NameLocProblem();
    const QString premise = PremiseProblem();
    const bool naming_bad = touched_ && !naming.text.isEmpty();
    name_field_->SetError(naming_bad && naming.on_name ? naming.text : QString{});
    name_edit_->SetError(naming_bad && naming.on_name);
    loc_field_->SetError(naming_bad && !naming.on_name ? naming.text : QString{});
    loc_edit_->SetError(naming_bad && !naming.on_name);
    premise_field_->SetError(touched_ && !premise.isEmpty() ? premise : QString{});

    UpdatePathPreview();

    prev_->setEnabled(step_ > 0);
    next_->setText(step_ == 3 ? QStringLiteral("创建") : QStringLiteral("下一步"));
    next_->setEnabled(StepProblem(step_).isEmpty()); // 有错禁用「下一步」
}

void ProjectWizardDialog::UpdatePathPreview() {
    const QString name = name_edit_->Text().trimmed();
    // 名称输入后：位置下方 help 补「目录名自动 = 项目名称」（叠加设计稿那句常驻说明）
    loc_field_->SetHelp(name.isEmpty()
                            ? QStringLiteral("项目目录将创建在该路径下")
                            : QStringLiteral("项目目录将创建在该路径下 · 目录名自动 = 项目名称"));
    const QString loc = loc_edit_->Text().trimmed();
    if (name.isEmpty() || loc.isEmpty()) {
        path_preview_->setText(QStringLiteral("<位置>\\<项目名>"));
        return;
    }
    // jsx:66 mono：{path}\{name or <项目名>}
    path_preview_->setText(PathText(ComposeSpec().rootDir));
}

bool ProjectWizardDialog::HasContent() const {
    return !name_edit_->Text().trimmed().isEmpty() || !loc_edit_->Text().trimmed().isEmpty() ||
           !premise_edit_->Text().trimmed().isEmpty();
}

// ─────────────────────────── 前进 / 创建 / 取消 ───────────────────────────

void ProjectWizardDialog::GoNext() {
    touched_ = true;
    Validate();
    if (!StepProblem(step_).isEmpty()) {
        return; // 有错不前进（UI.md §2.2）
    }
    if (step_ < 3) {
        GoToStep(step_ + 1);
    } else {
        DoCreate();
    }
}

void ProjectWizardDialog::DoCreate() {
    if (svc_ == nullptr) {
        widgets::Toast::Show(QStringLiteral("项目服务不可用，无法创建项目"),
                             widgets::Toast::Tone::Error);
        return;
    }
    if (next_->IsLoading()) {
        return;
    }
    create_error_.clear();
    next_->SetLoading(true); // 转圈 + 禁点 + 保持宽度
    progress_->SetIndeterminate(true);
    progress_->SetInlineText(QStringLiteral("建目录 / 建库 / 写模板…"));
    progress_->show();
    // 让 loading 态先上屏再进阻塞创建（排除用户输入，防重入）
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const shine::project::ProjectSpec spec = ComposeSpec();
    auto created = svc_->Create(spec);

    next_->SetLoading(false);
    progress_->hide();
    progress_->SetIndeterminate(false);
    progress_->SetInlineText(QString{});

    if (!created.has_value()) {
        const QString msg = FromUtf8(created.error().message);
        create_error_ = msg;
        widgets::Toast::Show(QStringLiteral("创建失败：%1").arg(msg), widgets::Toast::Tone::Error);
        touched_ = true;
        // 回到出错步：命名/位置问题回步 1，创意超限回步 2，其余留在确认步
        if (!NameLocProblem().text.isEmpty()) {
            GoToStep(1);
        } else if (!PremiseProblem().isEmpty()) {
            GoToStep(2);
        } else {
            Validate();
        }
        return;
    }

    widgets::Toast::Show(QStringLiteral("项目「%1」已创建").arg(name_edit_->Text().trimmed()),
                         widgets::Toast::Tone::Success);
    const shine::project::ProjectRef ref = created.value();
    accept();
    if (on_created_) {
        on_created_(ref);
    }
}

void ProjectWizardDialog::RefreshConfirm() {
    const shine::project::ProjectSpec spec = ComposeSpec();
    const QString name = FromUtf8(spec.name);
    const QString premise = FromUtf8(spec.premise);
    // jsx:88-89 名称（未命名兜底）+ 完整路径
    confirm_name_->setText(name.isEmpty() ? QStringLiteral("未命名项目") : name);
    confirm_path_->setText(PathText(spec.rootDir));

    // jsx:90-93 Tag × 2：模板名（accent）+ 有无一句话创意
    const QString tpl_name = (template_index_ >= 0 &&
                              template_index_ < static_cast<int>(templates_.size()))
                                 ? FromUtf8(templates_[static_cast<std::size_t>(template_index_)].name)
                                 : QStringLiteral("空白项目");
    SetTagText(confirm_tpl_tag_, tpl_name);
    SetTagText(confirm_idea_tag_, premise.isEmpty() ? QStringLiteral("无一句话创意")
                                                    : QStringLiteral("含一句话创意"));

    // 目录树预览：PreviewTree 相对路径（目录带尾部 /）→ 缩进树；项目根节点用项目名
    tree_->clear();
    auto* root_item = new QTreeWidgetItem(tree_);
    root_item->setText(0, name.isEmpty() ? QStringLiteral("未命名项目") : name);
    root_item->setExpanded(true);

    TreeNode root_node;
    root_node.name = name;
    root_node.dir = true;
    for (const std::string& raw : shine::project::PreviewTree(spec.templateId)) {
        const QString rel = FromUtf8(raw);
        const bool is_dir = rel.endsWith(QLatin1Char('/'));
        const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }
        TreeNode* cur = &root_node;
        for (int i = 0; i < parts.size(); ++i) {
            TreeNode* child = cur->FindChild(parts.at(i));
            if (child == nullptr) {
                TreeNode node;
                node.name = parts.at(i);
                node.dir = (i == parts.size() - 1) ? is_dir : true;
                cur->kids.push_back(std::move(node));
                child = &cur->kids.back();
            }
            cur = child;
        }
    }
    EmitTree(root_node, root_item);
}

void ProjectWizardDialog::RequestCancel() {
    if (HasContent()) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("放弃新建项目？"),
                        QStringLiteral("表单里还有内容，关闭后不会保存。确定放弃吗？"),
                        QMessageBox::NoButton, this);
        QPushButton* give_up = box.addButton(QStringLiteral("放弃"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("继续填写"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != give_up) {
            return;
        }
    }
    QDialog::reject();
}

} // namespace shine::app
