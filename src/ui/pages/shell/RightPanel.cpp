#include "ui/pages/shell/RightPanel.h"

#include "ui/kit/data/Panels.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace shine::app {

namespace {

// 可折叠段：点头部展开/收起（QToolBox 不在 kit 样式覆盖面内，这里用 kit 件自拼）
class Section : public QFrame {
  public:
    Section(const QString& title, QWidget* body, QWidget* parent = nullptr) : QFrame(parent) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(theme::space::kSteps[1]);
        header_ = new shine::widgets::Button(QStringLiteral("▾ %1").arg(title),
                                             shine::widgets::Button::Variant::Ghost,
                                             shine::widgets::Button::Size::Sm, this);
        connect(header_, &shine::widgets::Button::clicked, this, [this, title] {
            body_->setVisible(!body_->isVisible());
            header_->setText((body_->isVisible() ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) + title);
        });
        body_ = body;
        lay->addWidget(header_);
        lay->addWidget(body_);
    }

  private:
    shine::widgets::Button* header_ = nullptr;
    QWidget* body_ = nullptr;
};

} // namespace

RightPanel::RightPanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("rightPanel"));
    setMinimumWidth(200);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[3],
                            theme::space::kSteps[3], theme::space::kSteps[3]);
    lay->setSpacing(theme::space::kSteps[3]);

    // 属性
    auto* props = new shine::data::KeyValue(this);
    props->SetPairs({{QStringLiteral("项目"), QStringLiteral("（未选中）")},
                     {QStringLiteral("工作区"), QStringLiteral("总控")},
                     {QStringLiteral("标签页"), QStringLiteral("—")}});
    lay->addWidget(new Section(QStringLiteral("属性"), props, this));

    // 预览
    auto* previewBody = new QWidget(this);
    auto* pl = new QVBoxLayout(previewBody);
    pl->setContentsMargins(0, 0, 0, 0);
    auto* preview = new QLabel(QStringLiteral("预览区（P05/P07 接入：\n资产基线图 / 出图结果）"), previewBody);
    preview->setWordWrap(true);
    preview->setMinimumHeight(96);
    preview->setAlignment(Qt::AlignCenter);
    pl->addWidget(preview);
    lay->addWidget(new Section(QStringLiteral("预览"), previewBody, this));

    // 生成参数
    auto* params = new shine::data::KeyValue(this);
    params->SetPairs({{QStringLiteral("后端"), QStringLiteral("mock（P07 接 ComfyUI）")},
                      {QStringLiteral("步数"), QStringLiteral("20")},
                      {QStringLiteral("预算/章"), QStringLiteral("40 次调用")}});
    lay->addWidget(new Section(QStringLiteral("生成参数"), params, this));

    lay->addStretch();
}

} // namespace shine::app
