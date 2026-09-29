#include "ui/pages/imageflow/RenderResultView.h"

#include "ui/kit/images/Grid.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本页根控件（objectName=resultView）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:290–320：
//   .dlist .drow  p8 2 + 底部发丝线 + hover fill.hover
//   .art          r-sm(6) + fill-muted（由 kit 的 shineKind="art" 承担）
//   KV 行：k = text-muted 11px / v = text-primary 12.5px
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#resultView *[shineKind=\"drow\"]:hover { background-color: %1; }\n"
               "QWidget#resultView QLabel[kvRole=\"k\"] { color: %2; font-size: 11px; }\n"
               "QWidget#resultView QLabel[kvRole=\"v\"] { color: %3; font-size: 12px; }\n"
               "QWidget#resultView QLabel[rowRole=\"mono\"] { color: %3; font-size: 12px; font-weight: 600; }\n"
               "QWidget#resultView QLabel[rowRole=\"dim\"] { color: %2; font-size: 11px; }\n")
        .arg(shine::widget::CssRgb(t.fillHover), shine::widget::CssRgb(t.textMuted),
             shine::widget::CssRgb(t.textPrimary));
}


// 一行 KV（webui .kv：左键名 muted、右值 primary）
QWidget* MakeKvRow(const QString& key, const QString& value, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(theme::space::kSteps[2]);
    auto* k = new QLabel(key, row);
    k->setProperty("kvRole", QStringLiteral("k"));
    k->setFixedWidth(64);
    auto* v = new widgets::ElidedLabel(value, row);
    v->setProperty("kvRole", QStringLiteral("v"));
    v->SetExpandable(false);
    lay->addWidget(k, 0);
    lay->addWidget(v, 1);
    return row;
}

} // namespace

RenderResultView::RenderResultView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("resultView"));
    setStyleSheet(PageQss());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[3]);

    // webui ResultPanel：art h190 + KV + 两条 dlist 行 + 重跑按钮
    thumb_ = new QLabel(this);
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setWordWrap(true);
    thumb_->setFixedHeight(190); // views.css ResultPanel .art height 190
    widgets::SetKind(thumb_, "art");
    layout->addWidget(thumb_);

    facts_ = new QWidget(this);
    facts_lay_ = new QVBoxLayout(facts_);
    facts_lay_->setContentsMargins(0, 0, 0, 0);
    facts_lay_->setSpacing(0);
    layout->addWidget(facts_);

    assets_ = new QWidget(this);
    assets_lay_ = new QVBoxLayout(assets_);
    assets_lay_->setContentsMargins(0, 0, 0, 0);
    assets_lay_->setSpacing(0);
    layout->addWidget(assets_);

    rerun_ = new widgets::Button(QStringLiteral("重跑本镜出图"), widgets::Button::Variant::Secondary,
                                 widgets::Button::Size::Sm, this);
    rerun_->setToolTip(QStringLiteral("把这个镜头重新排进出图队列"));
    layout->addWidget(rerun_);
    layout->addStretch();

    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(PageQss()); });
    Rebuild();
}

void RenderResultView::SetEntry(const Entry& entry) {
    entry_ = entry;
    Rebuild();
}

void RenderResultView::Rebuild() {
    // 成图：有已解码图就显示；没有就显式空态 —— 不画占位假图
    if (!entry_.image.isNull()) {
        thumb_->setText(QString());
        thumb_->setPixmap(QPixmap::fromImage(entry_.image).scaled(
            320, 190, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        thumb_->setPixmap(QPixmap());
        thumb_->setText(entry_.code.isEmpty() ? QStringLiteral("未选择镜头")
                                              : QStringLiteral("%1 · %2\n尚未出图")
                                                    .arg(entry_.code, entry_.status));
    }

    // KV 区（webui ResultPanel 的 4 行）
    while (facts_lay_->count() > 0) {
        QLayoutItem* item = facts_lay_->takeAt(0);
        if (item == nullptr) break;
        if (QWidget* row = item->widget()) row->deleteLater();
        delete item;
    }
    facts_lay_->addWidget(MakeKvRow(QStringLiteral("镜号"), entry_.code, facts_));
    facts_lay_->addWidget(MakeKvRow(QStringLiteral("所属章节"), entry_.chapter, facts_));
    facts_lay_->addWidget(MakeKvRow(QStringLiteral("场景"), entry_.scene, facts_));
    facts_lay_->addWidget(MakeKvRow(QStringLiteral("出图状态"), entry_.status, facts_));
    facts_lay_->addWidget(MakeKvRow(QStringLiteral("Prompt"), entry_.prompt, facts_));

    // 资产行（webui ResultPanel 的两条 dlist）
    while (assets_lay_->count() > 0) {
        QLayoutItem* item = assets_lay_->takeAt(0);
        if (item == nullptr) break;
        if (QWidget* row = item->widget()) row->deleteLater();
        delete item;
    }
    const bool has_video = !entry_.videoPath.isEmpty();
    {
        auto* row = new QWidget(assets_);
        widgets::SetKind(row, "drow");
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(2, theme::space::kSteps[1], 2, theme::space::kSteps[1]);
        lay->setSpacing(theme::space::kSteps[2]);
        auto* name = new QLabel(QStringLiteral("◈ %1.mp4").arg(entry_.code), row);
        name->setProperty("rowRole", QStringLiteral("mono"));
        auto* note = new QLabel(has_video ? QStringLiteral("待播放") : QStringLiteral("尚未出片"), row);
        note->setProperty("rowRole", QStringLiteral("dim"));
        auto* tag = new widgets::Tag(has_video ? QStringLiteral("已出片") : QStringLiteral("待出片"),
                                     has_video ? "ok" : "idle", false, row);
        if (has_video) row->setToolTip(entry_.videoPath);
        lay->addWidget(name, 1);
        lay->addWidget(note, 0);
        lay->addWidget(tag, 0);
        assets_lay_->addWidget(row);
    }
    {
        auto* row = new QWidget(assets_);
        widgets::SetKind(row, "drow");
        row->setProperty("shineKind", QString{}); // 末行无底边
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(2, theme::space::kSteps[1], 2, theme::space::kSteps[1]);
        lay->setSpacing(theme::space::kSteps[2]);
        auto* name = new QLabel(QStringLiteral("◎ 图评审 · 五项清单"), row);
        name->setProperty("rowRole", QStringLiteral("mono"));
        auto* chev = new QLabel(QStringLiteral("›"), row);
        chev->setProperty("rowRole", QStringLiteral("dim"));
        lay->addWidget(name, 1);
        lay->addWidget(chev, 0);
        assets_lay_->addWidget(row);
    }
}

QString RenderResultView::Probe() const {
    return QStringLiteral("code=%1; status=%2; image=%3; video=%4")
        .arg(entry_.code, entry_.status,
             entry_.image.isNull() ? QStringLiteral("none") : QStringLiteral("ready"),
             entry_.videoPath.isEmpty() ? QStringLiteral("none") : QStringLiteral("ready"));
}

} // namespace shine::app
