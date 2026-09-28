#include "pages/settings/FirstRunWizard.h"

#include "core/Settings.h"
#include "widget/theme/Theme.h"
#include "widget/theme/ThemeService.h"
#include "widget/theme/Token.h"
#include "widget/controls/Controls.h"
#include "widget/controls/Inputs.h"
#include "widget/controls/Surfaces.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace shine::app {

namespace {
[[nodiscard]] QString ProviderLabel(const std::string& id) {
    if (id == "openai") return QStringLiteral("OpenAI 兼容");
    if (id == "mimo") return QStringLiteral("小米 MiMo");
    if (id == "minimax") return QStringLiteral("MiniMax");
    return QStringLiteral("自定义端点");
}
} // namespace

FirstRunWizard::FirstRunWizard(QWidget* parent, bool asSettings)
    : QDialog(parent), as_settings_(asSettings) {
    setWindowTitle(as_settings_ ? QStringLiteral("设置") : QStringLiteral("欢迎使用 ShineTV Studio"));
    setModal(true);
    resize(560, 380);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[5], theme::space::kSteps[5],
                            theme::space::kSteps[5], theme::space::kSteps[5]);
    lay->setSpacing(theme::space::kSteps[4]);

    auto* lead = new QLabel(
        as_settings_ ? QStringLiteral("随时可改：主题 / ComfyUI 地址 / LLM Key。")
                     : QStringLiteral("三步就能开工：挑个主题、给 ComfyUI 地址、填 LLM Key。\n"
                                      "都可稍后在「⚙ 设置」里修改。"),
        this);
    lead->setWordWrap(true);
    lay->addWidget(lead);

    stack_ = new QStackedWidget(this);

    // ① 选主题
    {
        auto* page = new QWidget(stack_);
        auto* pl = new QVBoxLayout(page);
        pl->setSpacing(theme::space::kSteps[3]);
        pl->addWidget(new QLabel(QStringLiteral("① 选主题"), page));
        auto* row = new QHBoxLayout();
        for (const theme::ThemeId id : theme::kAllThemes) {
            const std::string_view dn = theme::ThemeDisplayName(id);
            auto* chip = new shine::widgets::Button(QString::fromUtf8(dn.data(), static_cast<int>(dn.size())),
                                                    shine::widgets::Button::Variant::Secondary,
                                                    shine::widgets::Button::Size::Md, page);
            chip->setCheckable(true);
            chip->setChecked(id == theme::CurrentThemeId());
            connect(chip, &shine::widgets::Button::clicked, this, [this, id, chip] {
                theme_pick_->setText(QString::fromUtf8(
                    theme::ThemeDisplayName(id).data(),
                    static_cast<int>(theme::ThemeDisplayName(id).size())));
                theme::ThemeService::Switch(id, false);
                // 同步勾选态（Button 无 Q_OBJECT，不能 qobject_cast —— 用记录的指针表）
                for (shine::widgets::Button* b : theme_chips_) {
                    b->setChecked(b == chip);
                }
            });
            theme_chips_.push_back(chip);
            row->addWidget(chip);
        }
        pl->addLayout(row);
        theme_pick_ = new QLabel(QString::fromUtf8(theme::ThemeDisplayName(theme::CurrentThemeId()).data(),
                                                   static_cast<int>(theme::ThemeDisplayName(theme::CurrentThemeId()).size())),
                                 page);
        pl->addWidget(theme_pick_);
        pl->addStretch();
        stack_->addWidget(page);
    }

    // ② Comfy 地址
    {
        auto* page = new QWidget(stack_);
        auto* pl = new QVBoxLayout(page);
        pl->setSpacing(theme::space::kSteps[3]);
        pl->addWidget(new QLabel(QStringLiteral("② ComfyUI 地址"), page));
        comfy_edit_ = new shine::widgets::TextInput(page);
        comfy_edit_->SetPlaceholder(QStringLiteral("http://127.0.0.1:8188"));
        comfy_edit_->SetText(QString::fromStdString(Settings().comfyBaseUrl));
        auto* field = new shine::widgets::Field(QStringLiteral("地址"), shine::widgets::Field::LabelPos::Top,
                                                comfy_edit_, page);
        field->SetHelp(QStringLiteral("本机 ComfyUI 服务地址；连不上时状态栏会亮黄点。"));
        pl->addWidget(field);
        pl->addStretch();
        stack_->addWidget(page);
    }

    // ③ LLM Key
    {
        auto* page = new QWidget(stack_);
        auto* pl = new QVBoxLayout(page);
        pl->setSpacing(theme::space::kSteps[3]);
        pl->addWidget(new QLabel(QStringLiteral("③ LLM 供应商与 Key"), page));
        provider_ = new shine::widgets::Select(false, false, page);
        std::vector<shine::widgets::Select::Item> items;
        for (const char* id : {"openai", "mimo", "minimax", "custom"}) {
            items.push_back({ProviderLabel(id), {}, Settings().llmProvider == id});
        }
        provider_->SetItems(std::move(items));
        pl->addWidget(new shine::widgets::Field(QStringLiteral("供应商"), shine::widgets::Field::LabelPos::Top,
                                                provider_, page));
        key_edit_ = new shine::widgets::TextInput(page);
        key_edit_->SetPlaceholder(QStringLiteral("sk-…（仅存本机配置，日志不打印）"));
        key_edit_->SetText(QString::fromStdString(Settings().openaiApiKey));
        auto* keyField = new shine::widgets::Field(QStringLiteral("API Key"),
                                                   shine::widgets::Field::LabelPos::Top, key_edit_, page);
        keyField->SetHelp(QStringLiteral("留空 = 稍后再填；开发期可用 mock 供应商。"));
        pl->addWidget(keyField);
        pl->addStretch();
        stack_->addWidget(page);
    }

    lay->addWidget(stack_, 1);

    // 底部：上一步 / 下一步（最后一步「完成」）/ 跳过
    auto* row = new QHBoxLayout();
    back_btn_ = new shine::widgets::Button(QStringLiteral("上一步"),
                                           shine::widgets::Button::Variant::Ghost,
                                           shine::widgets::Button::Size::Md, this);
    connect(back_btn_, &shine::widgets::Button::clicked, this, [this] { ShowStep(step_ - 1); });
    auto* skip = new shine::widgets::Button(
        as_settings_ ? QStringLiteral("取消") : QStringLiteral("跳过"),
        shine::widgets::Button::Variant::Ghost, shine::widgets::Button::Size::Md, this);
    connect(skip, &shine::widgets::Button::clicked, this,
            [this] { as_settings_ ? reject() : Finish(); });
    next_btn_ = new shine::widgets::Button(QStringLiteral("下一步"),
                                           shine::widgets::Button::Variant::Primary,
                                           shine::widgets::Button::Size::Md, this);
    connect(next_btn_, &shine::widgets::Button::clicked, this, [this] {
        if (step_ >= 2) {
            Finish();
            return;
        }
        ShowStep(step_ + 1);
    });
    row->addWidget(back_btn_);
    row->addStretch();
    row->addWidget(skip);
    row->addWidget(next_btn_);
    lay->addLayout(row);

    ShowStep(0);
}

void FirstRunWizard::ShowStep(int step) {
    step_ = step < 0 ? 0 : (step > 2 ? 2 : step);
    stack_->setCurrentIndex(step_);
    back_btn_->setEnabled(step_ > 0);
    next_btn_->setText(step_ >= 2 ? QStringLiteral("完成") : QStringLiteral("下一步"));
}

void FirstRunWizard::GoToStep(int step) {
    ShowStep(step);
}

void FirstRunWizard::Finish() {
    // 落盘：Comfy 地址 / LLM Key / 供应商 + 首启标记（只出现一次）
    Settings().comfyBaseUrl = comfy_edit_->Text().toStdString();
    Settings().openaiApiKey = key_edit_->Text().toStdString();
    if (!as_settings_) {
        Settings().firstRun = false;
    }
    SaveSettings();
    accept();
}

} // namespace shine::app
