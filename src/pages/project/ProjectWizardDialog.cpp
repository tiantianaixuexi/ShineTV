#include "pages/project/ProjectWizardDialog.h"

#include "widget/theme/Theme.h"
#include "widget/theme/Token.h"
#include "widget/controls/Surfaces.h"
#include "project/ProjectTemplate.h"
#include "util/Encoding.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
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

namespace shine::app {
namespace {

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
    resize(720, 520);

    templates_ = svc_ != nullptr ? svc_->Templates() : std::vector<shine::project::ProjectTemplate>{};
    if (templates_.empty()) {
        templates_ = shine::project::AllTemplates(); // 数据定义仍全部来自项目模块
    }

    BuildUi();
    UpdateTemplateDesc();
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
    steps_->SetCurrent(step, false);
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
    for (std::size_t i = 0; i < radios_.size(); ++i) {
        radios_[i]->SetChecked(static_cast<int>(i) == sel);
    }
    syncing_ = false;

    UpdateTemplateDesc();
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
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(theme::space::kSteps[6], theme::space::kSteps[5],
                             theme::space::kSteps[6], theme::space::kSteps[5]);
    root->setSpacing(theme::space::kSteps[4]);

    // 步骤指示条（Tabs 只随步高亮；点击只回跳到达过的步）
    steps_ = new widgets::Tabs({QStringLiteral("① 选模板"), QStringLiteral("② 命名与位置"),
                                QStringLiteral("③ 一句话创意"), QStringLiteral("④ 确认")},
                               this);
    root->addWidget(steps_);

    stack_ = new QStackedWidget(this);
    root->addWidget(stack_, 1);

    // —— ① 选模板：三个 Radio（互斥由本容器负责）+ 当前模板描述 ——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setSpacing(theme::space::kSteps[3]);
        for (std::size_t i = 0; i < templates_.size(); ++i) {
            auto* radio = new widgets::Radio(FromUtf8(templates_[i].name), page);
            const int idx = static_cast<int>(i);
            radio->SetOnToggled([this, idx](bool on) {
                if (syncing_ || !on) {
                    return; // 程序化同步 / 取消勾选都不改当前选中项
                }
                template_index_ = idx;
                syncing_ = true;
                for (std::size_t j = 0; j < radios_.size(); ++j) {
                    if (static_cast<int>(j) != idx) {
                        radios_[j]->SetChecked(false);
                    }
                }
                syncing_ = false;
                UpdateTemplateDesc();
                Validate();
            });
            radios_.push_back(radio);
            lay->addWidget(radio);
        }
        desc_ = new QLabel(page);
        widgets::SetKind(desc_, "statesub");
        desc_->setWordWrap(true);
        lay->addWidget(desc_);
        lay->addStretch(1);
        stack_->addWidget(page);
    }

    // —— ② 命名与位置 ——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setSpacing(theme::space::kSteps[3]);

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
        name_field_->SetHelp(QStringLiteral("目录名自动 = 项目名称"));
        lay->addWidget(name_field_);

        auto* loc_row = new QWidget(page);
        auto* loc_lay = new QHBoxLayout(loc_row);
        loc_lay->setContentsMargins(0, 0, 0, 0);
        loc_lay->setSpacing(theme::space::kSteps[2]);
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
        lay->addWidget(loc_field_);

        path_preview_ = new QLabel(QStringLiteral("将创建：…"), page);
        widgets::SetKind(path_preview_, "fieldhelp");
        path_preview_->setTextInteractionFlags(Qt::TextSelectableByMouse); // 完整路径可复制
        lay->addWidget(path_preview_);
        lay->addStretch(1);
        stack_->addWidget(page);
    }

    // —— ③ 一句话创意（可留空）——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setSpacing(theme::space::kSteps[3]);
        premise_edit_ = new widgets::TextArea(200, page);
        connect(premise_edit_->Edit(), &QPlainTextEdit::textChanged, this, [this] {
            if (syncing_) {
                return;
            }
            touched_ = true;
            Validate();
        });
        premise_field_ = new widgets::Field(QStringLiteral("一句话创意"),
                                            widgets::Field::LabelPos::Top, premise_edit_, page);
        premise_field_->SetHelp(QStringLiteral("可留空，稍后在总控台补"));
        lay->addWidget(premise_field_);
        lay->addStretch(1);
        stack_->addWidget(page);
    }

    // —— ④ 确认：KeyValue + 目录树预览 ——
    {
        auto* page = new QWidget(stack_);
        auto* lay = new QVBoxLayout(page);
        lay->setSpacing(theme::space::kSteps[3]);
        summary_ = new data::KeyValue(page);
        lay->addWidget(summary_);

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

    // —— 创建阶段提示（UI.md §3 加载态）+ 底部按钮行 ——
    progress_ = new widgets::ProgressBar(this);
    progress_->hide();
    root->addWidget(progress_);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(theme::space::kSteps[3]);
    actions->addStretch(1);
    cancel_ = new widgets::Button(QStringLiteral("取消"), widgets::Button::Variant::Ghost,
                                  widgets::Button::Size::Md, this);
    prev_ = new widgets::Button(QStringLiteral("上一步"), widgets::Button::Variant::Secondary,
                                widgets::Button::Size::Md, this);
    next_ = new widgets::Button(QStringLiteral("下一步"), widgets::Button::Variant::Primary,
                                widgets::Button::Size::Md, this);
    actions->addWidget(cancel_);
    actions->addWidget(prev_);
    actions->addWidget(next_);
    root->addLayout(actions);

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

    steps_->SetOnChanged([this](int index) {
        if (syncing_) {
            return;
        }
        if (index != step_ && index <= max_step_) {
            GoToStep(index); // 允许点回已完成（到达过）的步
        } else {
            syncing_ = true;
            steps_->SetCurrent(step_, false); // 未到的步：指示条弹回
            syncing_ = false;
        }
    });
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
    // 名称输入后：位置下方 help 显示「目录名自动 = 项目名称」
    loc_field_->SetHelp(name.isEmpty() ? QString{} : QStringLiteral("目录名自动 = 项目名称"));
    const QString loc = loc_edit_->Text().trimmed();
    if (name.isEmpty() || loc.isEmpty()) {
        path_preview_->setText(QStringLiteral("将创建：…"));
        return;
    }
    path_preview_->setText(QStringLiteral("将创建：%1").arg(PathText(ComposeSpec().rootDir)));
}

void ProjectWizardDialog::UpdateTemplateDesc() {
    if (desc_ == nullptr) {
        return;
    }
    if (template_index_ >= 0 && template_index_ < static_cast<int>(templates_.size())) {
        desc_->setText(FromUtf8(templates_[static_cast<std::size_t>(template_index_)].description));
    } else {
        desc_->setText(QString{});
    }
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
    summary_->SetPairs({
        {QStringLiteral("名称"), name.isEmpty() ? QStringLiteral("（未命名）") : name},
        {QStringLiteral("位置"), PathText(spec.rootDir)},
        {QStringLiteral("模板"),
         template_index_ >= 0 && template_index_ < static_cast<int>(templates_.size())
             ? FromUtf8(templates_[static_cast<std::size_t>(template_index_)].name)
             : QStringLiteral("空白项目")},
        {QStringLiteral("创意"),
         premise.isEmpty() ? QStringLiteral("（留空，稍后在总控台补）") : premise},
    });

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
