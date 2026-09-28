#include "ui/pages/shell/StatusBar.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>

namespace shine::app {

// 状态点：小圆点自绘（tone → status.* token，零字面色）
class StatusBar::Dot : public QWidget {
  public:
    explicit Dot(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(9, 9); }
    void SetTone(const char* tone) {
        const theme::ColorToken& t = theme::Current();
        std::uint32_t c = t.statusIdle;
        if (tone != nullptr && tone[0] != '\0') {
            const std::string_view s{tone};
            if (s == "ok") c = t.statusOk;
            else if (s == "warn") c = t.statusWarn;
            else if (s == "danger") c = t.statusDanger;
            else if (s == "busy") c = t.statusBusy;
        }
        color_ = shine::widgets::TokenQColor(c);
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        p.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }

  private:
    QColor color_;
};

namespace {
[[nodiscard]] QString QueueLabel(int n) {
    return QStringLiteral("队列 %1").arg(n);
}
} // namespace

StatusBar::StatusBar(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("statusBar"));
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[1],
                            theme::space::kSteps[3], theme::space::kSteps[1]);
    lay->setSpacing(theme::space::kSteps[3]);

    comfy_dot_ = new Dot(this);
    comfy_dot_->SetTone("idle");
    llm_dot_ = new Dot(this);
    llm_dot_->SetTone("idle");
    comfy_btn_ = MakeItem(QStringLiteral("Comfy 未连接"), StatusItem::Comfy);
    llm_btn_ = MakeItem(QStringLiteral("LLM 未配置"), StatusItem::Llm);
    queue_btn_ = MakeItem(QueueLabel(0), StatusItem::Queue);
    progress_btn_ = MakeItem(QStringLiteral("未运行"), StatusItem::Progress);
    progress_ = new shine::widgets::ProgressBar(this);
    progress_->setFixedSize(96, 6);
    progress_->setTextVisible(false);
    progress_->setVisible(false);
    theme_btn_ = MakeItem(QStringLiteral("深空"), StatusItem::Theme);
    version_btn_ = MakeItem(QStringLiteral("v0.0.0"), StatusItem::Version);

    lay->addWidget(comfy_dot_, 0, Qt::AlignVCenter);
    lay->addWidget(comfy_btn_);
    lay->addWidget(llm_dot_, 0, Qt::AlignVCenter);
    lay->addWidget(llm_btn_);
    lay->addWidget(queue_btn_);
    lay->addWidget(progress_);
    lay->addWidget(progress_btn_);
    lay->addStretch();
    lay->addWidget(theme_btn_);
    lay->addWidget(version_btn_);
}

QPushButton* StatusBar::MakeItem(const QString& text, StatusItem item) {
    auto* btn = new QPushButton(text, this);
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setObjectName(QStringLiteral("statusItem"));
    connect(btn, &QPushButton::clicked, this, [this, item] {
        if (on_pick_) {
            on_pick_(item);
        }
    });
    return btn;
}

void StatusBar::SetComfy(const QString& text, const char* tone) {
    comfy_btn_->setText(text);
    comfy_dot_->SetTone(tone);
}

void StatusBar::SetLlm(const QString& text, const char* tone) {
    llm_btn_->setText(text);
    llm_dot_->SetTone(tone);
}

void StatusBar::SetQueue(int n) {
    queue_btn_->setText(QueueLabel(n < 0 ? 0 : n));
}

void StatusBar::SetProgress(int current, int total, const QString& text) {
    progress_btn_->setText(text.isEmpty() ? QStringLiteral("未运行") : text);
    if (total > 0) {
        progress_->setVisible(true);
        progress_->setRange(0, total);
        progress_->setValue(current);
    } else {
        progress_->setVisible(false);
    }
}

void StatusBar::SetThemeName(const QString& name) {
    theme_btn_->setText(name);
}

void StatusBar::SetVersion(const QString& version) {
    version_btn_->setText(version);
}

QString StatusBar::ComfyText() const {
    return comfy_btn_->text();
}

QString StatusBar::LlmText() const {
    return llm_btn_->text();
}

int StatusBar::QueueCount() const {
    const QString t = queue_btn_->text();
    const int sp = t.indexOf(' ');
    bool ok = false;
    const int n = (sp >= 0 ? t.mid(sp + 1) : t).toInt(&ok);
    return ok ? n : 0;
}

QString StatusBar::ThemeName() const {
    return theme_btn_->text();
}

} // namespace shine::app
