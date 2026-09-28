// P04-S8「模型与 Prompt 面板」实现：模型四档分层 + auto 规则灯（评审≠写作才允许）
// + 外置 Prompt 查看 + Key 只显掩码。颜色零内联（check-layers rule 3）。
#include "ui/pages/novel/ModelPromptView.h"
#include "ui/kit/theme/CssColor.h"

#include "core/Settings.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "llm/OpenAIConfig.h"
#include "novel/NovelDirector.h"
#include "novel/NovelPipeline.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace {



// 常见模型候选（`llm::ResolveModel` 接受任意 id；这里给下拉建议 + 自由输入兜底）
[[nodiscard]] std::vector<QString> ModelCandidates() {
    return {QStringLiteral("gpt-4o"),         QStringLiteral("gpt-4o-mini"),
            QStringLiteral("gpt-4.1"),        QStringLiteral("gpt-4.1-mini"),
            QStringLiteral("o3-mini"),        QStringLiteral("mimo-v2.5-pro"),
            QStringLiteral("MiniMax-M3"),     QStringLiteral("claude-sonnet-4"),
            QStringLiteral("（自定义 model id…）")};
}

} // namespace

namespace shine::app {

const std::vector<ModelRoleDef>& ModelRoles() {
    static const std::vector<ModelRoleDef> roles = {
        {"planner", "规划", "中", "T2–T4/T6–T10 章节计划（合并执行）"},
        {"writer", "写作", "高", "T11 正文 + T13 修复稿（高档调用 ≤8）"},
        {"critic", "评审", "高", "T12 章节评审 rubric 8 维判定"},
        {"extractor", "提取", "低", "T14 StateDiff 提取（写状态差分）"},
    };
    return roles;
}

std::string ModelPromptView::PromptOverridePath(const std::filesystem::path& dir,
                                                std::string_view role) {
    if (dir.empty()) {
        return {};
    }
    return util::PathToUtf8(dir / "prompts" / (std::string{role} + ".md"));
}

ModelPromptView::ModelPromptView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                              theme::space::kSteps[2], theme::space::kSteps[2]);
    outer->setSpacing(theme::space::kSteps[2]);

    // —— 顶部：auto 规则灯 + Key 掩码 ——
    auto* head = new QWidget(this);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[1]);
    rule_ = new QLabel(head);
    rule_->setWordWrap(true);
    hl->addWidget(rule_, 1);

    key_mask_ = new QLabel(head);
    widgets::SetTextColor(key_mask_, theme::Current().textSecondary);
    hl->addWidget(new QLabel(QStringLiteral("API Key"), head));
    hl->addWidget(key_mask_);

    key_input_ = new widgets::TextInput(head);
    key_input_->setMinimumWidth(220);
    key_input_->Edit()->setEchoMode(QLineEdit::Password); // 永不明文回显
    key_input_->Edit()->setPlaceholderText(QStringLiteral("粘贴 Key（只写不读回）"));
    key_input_->SetOnChanged([this](const QString& t) {
        if (t.isEmpty()) {
            return;
        }
        SetApiKey(t.toStdString());
        key_input_->Edit()->clear(); // 输入即存，随即清空输入框（明文不驻留 UI）
        RefreshRuleLamp();
    });
    hl->addWidget(key_input_);

    // —— 模型分层 + Prompt 两栏 ——
    auto* split = new QHBoxLayout();
    split->setSpacing(theme::space::kSteps[2]);
    split->addWidget(BuildModels(), 1);
    split->addWidget(BuildPrompt(), 1);

    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    widgets::SetTextColor(hint_, theme::Current().textSecondary);

    outer->addWidget(head);
    outer->addLayout(split, 1);
    outer->addWidget(hint_);
    Reload();
}

ModelPromptView::~ModelPromptView() = default;

QWidget* ModelPromptView::BuildModels() {
    auto* box = new widgets::Card(widgets::Card::Variant::Outlined, this);
    QVBoxLayout* vl = box->BodyLayout();
    vl->addWidget(new QLabel(QStringLiteral("模型分层（规划 / 写作 / 评审 / 提取）"), box));

    const std::vector<ModelRoleDef>& roles = ModelRoles();
    for (const ModelRoleDef& r : roles) {
        auto* row = new QWidget(box);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(theme::space::kSteps[1]);

        auto* name = new QLabel(QStringLiteral("%1（%2）")
                                    .arg(QString::fromUtf8(r.name), QString::fromUtf8(r.tier)),
                                row);
        name->setToolTip(QString::fromUtf8(r.note));
        name->setMinimumWidth(96);
        widgets::SetTextColor(name, theme::Current().textPrimary);

        auto* sel = new widgets::Select(false, false, row);
        sel->setMinimumWidth(240);
        {
            std::vector<widgets::Select::Item> items;
            for (const QString& c : ModelCandidates()) {
                widgets::Select::Item it;
                it.text = c;
                items.push_back(std::move(it));
            }
            sel->SetItems(std::move(items));
        }
        const std::string roleKey{r.role};
        sel->SetOnChanged([this, sel, roleKey] {
            const std::vector<QString> got = sel->Checked();
            if (got.empty()) {
                return;
            }
            const QString picked = got.front();
            if (picked.startsWith(QStringLiteral("（自定义"))) {
                return; // 自由输入走 API（SetRoleModel）
            }
            (void)SetRoleModel(roleKey, picked.toStdString());
        });

        auto* now = new QLabel(row);
        now->setToolTip(QStringLiteral("当前生效模型（`llm::ResolveModel`）"));
        widgets::SetTextColor(now, theme::Current().accentPrimary);

        rl->addWidget(name);
        rl->addWidget(sel);
        rl->addWidget(new QLabel(QStringLiteral("生效"), row));
        rl->addWidget(now, 1);
        vl->addWidget(row);
        model_sel_.push_back(sel);
        model_now_.push_back(now);
    }
    return box;
}

QWidget* ModelPromptView::BuildPrompt() {
    auto* box = new widgets::Card(widgets::Card::Variant::Outlined, this);
    QVBoxLayout* vl = box->BodyLayout();

    auto* head = new QWidget(box);
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(theme::space::kSteps[1]);
    auto* cap = new QLabel(QStringLiteral("Prompt（外置查看）"), head);
    widgets::SetTextColor(cap, theme::Current().textPrimary);
    hl->addWidget(cap);
    hl->addStretch(1);
    for (const ModelRoleDef& r : ModelRoles()) {
        auto* b = new widgets::Button(QString::fromUtf8(r.name), widgets::Button::Variant::Ghost,
                                      widgets::Button::Size::Sm, head);
        const std::string roleKey{r.role};
        connect(b, &widgets::Button::clicked, this, [this, roleKey] { ShowRolePrompt(roleKey); });
        hl->addWidget(b);
        role_btns_.push_back(b);
    }
    auto* copyBtn = new widgets::Button(QStringLiteral("复制"), widgets::Button::Variant::Ghost,
                                        widgets::Button::Size::Sm, head);
    connect(copyBtn, &widgets::Button::clicked, this, [this] {
        if (QClipboard* cb = QGuiApplication::clipboard(); cb != nullptr) {
            cb->setText(prompt_view_->toPlainText());
        }
    });
    auto* exportBtn = new widgets::Button(QStringLiteral("另存为覆盖文件"),
                                           widgets::Button::Variant::Secondary,
                                           widgets::Button::Size::Sm, head);
    connect(exportBtn, &widgets::Button::clicked, this, [this] {
        const std::string path = PromptOverridePath(project_dir_, current_role_);
        if (path.empty()) {
            widgets::Toast::Show(QStringLiteral("没设工程根 —— 覆盖文件无处可落"),
                                 widgets::Toast::Tone::Warning);
            return;
        }
        QString err;
        if (!ExportPromptOverride(current_role_, prompt_view_->toPlainText().toStdString(), &err)) {
            widgets::Toast::Show(QStringLiteral("另存失败：%1").arg(err),
                                 widgets::Toast::Tone::Error);
            return;
        }
        widgets::Toast::Show(QStringLiteral("已另存 prompts/%1.md（此后该角色用外置 Prompt）")
                                 .arg(QString::fromStdString(current_role_)),
                             widgets::Toast::Tone::Success);
        Reload();
    });
    hl->addWidget(copyBtn);
    hl->addWidget(exportBtn);
    vl->addWidget(head);

    prompt_view_ = new QPlainTextEdit(box);
    prompt_view_->setReadOnly(true); // 只读查看；改内容请用[另存为覆盖文件]
    prompt_view_->setPlaceholderText(
        QStringLiteral("选一个角色查看其 Prompt（内置 agent::DefaultPrompt，可被工程 prompts/<role>.md 覆盖）"));
    vl->addWidget(prompt_view_, 1);
    return box;
}

void ModelPromptView::SetProjectDir(const std::filesystem::path& dir) {
    project_dir_ = dir;
    Reload();
}

void ModelPromptView::Reload() {
    for (std::size_t i = 0; i < ModelRoles().size(); ++i) {
        const ModelRoleDef& r = ModelRoles()[i];
        const std::string now = llm::ResolveModel(r.role);
        model_now_[i]->setText(QString::fromStdString(now));
    }
    RefreshRuleLamp();
    // Key 只显掩码（`llm::MaskKey`：前后各 3 字符）
    key_mask_->setText(QStringLiteral("当前：%1")
                           .arg(QString::fromStdString(llm::MaskKey(llm::ResolveApiKey()))));
    if (!current_role_.empty()) {
        ShowRolePrompt(current_role_);
    }
}

void ModelPromptView::RefreshRuleLamp() {
    const bool ok = AutoAllowed();
    rule_->setText(ok ? QStringLiteral("✔ auto 可用：评审模型 ≠ 写作模型（`06` §2.7 M5 交叉评审成立）")
                      : QStringLiteral("⛔ auto 禁用：评审模型 = 写作模型 —— 交叉评审形同自评，"
                                       "须把[评审]档改成另一个模型才能开 auto"));
    widgets::SetTextColor(rule_, ok ? theme::Current().statusOk : theme::Current().statusDanger);
}

bool ModelPromptView::AutoAllowed() const {
    return shine::novel::CrossReviewEffective(); // `06` §2.7 M5：critic 模型 ≠ writer 模型
}

QString ModelPromptView::AutoRuleText() const {
    return QStringLiteral("auto=%1 writer=%2 critic=%3")
        .arg(AutoAllowed() ? QStringLiteral("allow") : QStringLiteral("block"))
        .arg(QString::fromStdString(llm::ResolveModel("writer")),
             QString::fromStdString(llm::ResolveModel("critic")));
}

std::string ModelPromptView::SetRoleModel(std::string_view role, std::string_view modelId) {
    AppSettings& s = Settings();
    std::string* slot = nullptr;
    if (role == "planner") {
        slot = &s.openaiModelPlanner;
    } else if (role == "writer") {
        slot = &s.openaiModelWriter;
    } else if (role == "critic") {
        slot = &s.openaiModelCritic;
    } else if (role == "extractor" || role == "extract") {
        slot = &s.openaiModelExtractor;
    }
    if (slot != nullptr) {
        *slot = std::string{modelId};
        SaveSettings();
    }
    Reload();
    return llm::ResolveModel(role);
}

void ModelPromptView::SetApiKey(std::string_view key) {
    AppSettings& s = Settings();
    const std::string p = s.llmProvider;
    if (p == "mimo") {
        s.mimoApiKey = std::string{key};
    } else if (p == "minimax") {
        s.minimaxApiKey = std::string{key};
    } else {
        s.openaiApiKey = std::string{key}; // openai / custom 共用
    }
    SaveSettings();
}

bool ModelPromptView::ExportPromptOverride(std::string_view role, std::string_view text,
                                           QString* err) {
    const std::string path = PromptOverridePath(project_dir_, role);
    if (path.empty()) {
        if (err != nullptr) {
            *err = QStringLiteral("没设工程根");
        }
        return false;
    }
    if (!util::WriteFileEnsuredDir(std::filesystem::path{path}, std::string{text})) {
        if (err != nullptr) {
            *err = QStringLiteral("写文件失败：%1")
                       .arg(QString::fromStdString(util::PathToUtf8(path)));
        }
        return false;
    }
    return true;
}

QString ModelPromptView::EffectivePrompt(std::string_view role) const {
    const std::string path = PromptOverridePath(project_dir_, role);
    if (!path.empty()) {
        if (auto bytes = util::ReadFileBytes(path); bytes) {
            return QString::fromStdString(*bytes);
        }
    }
    return QString::fromStdString(agent::DefaultPrompt(role));
}

void ModelPromptView::ShowRolePrompt(std::string_view role) {
    current_role_ = std::string{role};
    prompt_view_->setPlainText(EffectivePrompt(role));
    const std::string path = PromptOverridePath(project_dir_, role);
    hint_->setText(path.empty()
                       ? QStringLiteral("内置 Prompt（`agent::DefaultPrompt`）—— 设工程根后可另存外置覆盖")
                       : (util::ReadFileBytes(path)
                              ? QStringLiteral("外置覆盖：%1")
                                    .arg(QString::fromStdString(util::PathToUtf8(path)))
                              : QStringLiteral("内置 Prompt（%1 不存在则用内置）")
                                    .arg(QString::fromStdString(util::PathToUtf8(path)))));
}

// ————————————————————————————————————————————— 探针

QString ModelPromptView::ModelProbe() const {
    QString out = QStringLiteral("rule %1\n").arg(AutoRuleText());
    for (const ModelRoleDef& r : ModelRoles()) {
        out += QStringLiteral("role %1|%2|tier=%3|model=%4\n")
                   .arg(QString::fromUtf8(r.role), QString::fromUtf8(r.name), QString::fromUtf8(r.tier))
                   .arg(QString::fromStdString(llm::ResolveModel(r.role)));
    }
    // 掩码（**绝不出明文**）：`llm::MaskKey` 前后各 3 字符
    out += QStringLiteral("key-mask=%1\n")
               .arg(QString::fromStdString(llm::MaskKey(llm::ResolveApiKey())));
    return out;
}

QString ModelPromptView::PromptProbe() const {
    QString out;
    for (const ModelRoleDef& r : ModelRoles()) {
        const QString text = EffectivePrompt(r.role);
        const std::string path = PromptOverridePath(project_dir_, r.role);
        const bool external = !path.empty() && util::ReadFileBytes(path).has_value();
        out += QStringLiteral("prompt %1|chars=%2|source=%3\n")
                   .arg(QString::fromUtf8(r.role))
                   .arg(text.size())
                   .arg(external ? QStringLiteral("external") : QStringLiteral("builtin"));
    }
    return out;
}

} // namespace shine::app
