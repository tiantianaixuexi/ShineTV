#include "ui/pages/imageflow/ImageReviewView.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本页根控件（objectName=reviewView）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:1155–1170：
//   .checklist    gap 6
//   .checklist .ck  p7 10 / r-sm(6) / line-subtle 边 / fill-muted 底 / f12.5
//   .ck.pass .ck-ic ok / .warn warn / .fail danger
// 另外 .art（成图占位）走 kit 的 shineKind="art"，此处只补文字色。
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#reviewView QFrame[ckRole=\"item\"] {\n"
               "  background-color: %1; border: 1px solid %2; border-radius: 6px; }\n"
               "QWidget#reviewView QLabel[ckRole=\"name\"] { color: %3; font-size: 12px; }\n"
               "QWidget#reviewView QLabel[ckRole=\"ic\"] { font-size: 13px; }\n"
               "QWidget#reviewView QLabel[ckRole=\"ic\"][ckState=\"pass\"] { color: %4; }\n"
               "QWidget#reviewView QLabel[ckRole=\"ic\"][ckState=\"warn\"] { color: %5; }\n"
               "QWidget#reviewView QLabel[ckRole=\"ic\"][ckState=\"fail\"] { color: %6; }\n"
               "QWidget#reviewView QLabel[ckRole=\"ic\"][ckState=\"todo\"] { color: %7; }\n")
        .arg(shine::widget::CssRgb(t.fillMuted), shine::widget::CssRgb(t.lineSubtle),
             shine::widget::CssRgb(t.textSecondary), shine::widget::CssRgb(t.statusOk),
             shine::widget::CssRgb(t.statusWarn), shine::widget::CssRgb(t.statusDanger),
             shine::widget::CssRgb(t.textMuted));
}

// 换肤后重挂页面 QSS：ThemeService 是纯静态类，靠 qApp 发的 ThemeChange 事件感知。
class PageStyleRefresher : public QObject {
  public:
    explicit PageStyleRefresher(QWidget* page, QObject* parent) : QObject(parent), page_(page) {
        qApp->installEventFilter(this);
    }
    ~PageStyleRefresher() override { qApp->removeEventFilter(this); }

  protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (ev->type() == QEvent::ThemeChange && watched == qApp) page_->setStyleSheet(PageQss());
        return QObject::eventFilter(watched, ev);
    }

  private:
    QWidget* page_;
};

} // namespace

ImageReviewView::ImageReviewView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("reviewView"));
    setStyleSheet(PageQss());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[3]);

    // webui ReviewPanel：按钮行在最上（评审当前图 / 仅重跑 ⚠ 项）
    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(theme::space::kSteps[1]);
    auto* run = new widgets::Button(QStringLiteral("评审当前图"), widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, this);
    auto* retry = new widgets::Button(QStringLiteral("仅重跑 ⚠ 项"), widgets::Button::Variant::Secondary,
                                      widgets::Button::Size::Sm, this);
    actions->addWidget(run);
    actions->addWidget(retry);
    actions->addStretch(1);
    layout->addLayout(actions);

    // 成图区（webui .art h150）：未评审时显式空态，不画占位假图
    image_ = new QLabel(this);
    image_->setAlignment(Qt::AlignCenter);
    image_->setWordWrap(true);
    image_->setFixedHeight(150);
    widgets::SetKind(image_, "art");
    layout->addWidget(image_);

    // 五项清单（webui .checklist .ck）
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    checklist_ = new QWidget(scroll);
    checklist_lay_ = new QVBoxLayout(checklist_);
    checklist_lay_->setContentsMargins(0, 0, 0, 0);
    checklist_lay_->setSpacing(6); // webui .checklist gap 6
    checklist_lay_->addStretch(1);
    scroll->setWidget(checklist_);
    layout->addWidget(scroll, 1);

    status_ = new QLabel(QStringLiteral("还没评审"), this);
    widgets::SetKind(status_, "statemeta");
    layout->addWidget(status_);
    connect(run, &QPushButton::clicked, this, &ImageReviewView::Run);
    connect(retry, &QPushButton::clicked, this, [this] {
        if (!has_report_) return;
        status_->setText(QStringLiteral("已将失败项加入重跑队列"));
    });
    new PageStyleRefresher(this, this);
    Rebuild();
}

void ImageReviewView::SetInput(const flow::ImageReviewInput& input, const std::string& report_path) {
    input_ = input;
    report_path_ = report_path;
    has_report_ = false;
    report_ = {};
    // flow::ImageReviewInput 只带路径（图解码在 worker 侧完成），本视图显示路径 +
    // 显式空态文案，不在 UI 线程读盘、也不画占位假图。
    image_->setPixmap(QPixmap());
    image_->setText(QStringLiteral("生成结果\n%1\n（尚未解码，点击「评审当前图」加载）")
                        .arg(QString::fromStdString(input_.image_path)));
    status_->setText(QStringLiteral("还没评审"));
    Rebuild();
}

void ImageReviewView::Run() {
    if (!reviewer_) {
        status_->setText(QStringLiteral("评审模型不可用：请配置视觉模型或显式切换 mock"));
        return;
    }
    report_ = flow::RunImageReview(input_, reviewer_, report_path_);
    has_report_ = true;
    Rebuild();
}

void ImageReviewView::Rebuild() {
    while (checklist_lay_->count() > 1) {
        QLayoutItem* item = checklist_lay_->takeAt(0);
        if (item == nullptr) break;
        if (QWidget* row = item->widget()) row->deleteLater();
        delete item;
    }
    // 未评审时先列五项固定清单（键与 flow::ImageReview 的固定项一致），
    // 有报告后按报告条目覆盖；没有报告就以待评审态展示骨架。
    struct Item { QString name; };
    QList<Item> items;
    if (has_report_) {
        for (const auto& finding : report_.findings) {
            items.append({QString::fromStdString(finding.label)});
        }
    } else {
        items = {Item{QStringLiteral("脸部")}, Item{QStringLiteral("手部")}, Item{QStringLiteral("构图")},
                 Item{QStringLiteral("一致性")}, Item{QStringLiteral("文字")}};
    }
    for (int i = 0; i < items.size(); ++i) {
        bool pass = false;
        bool warn = false;
        QString detail;
        if (has_report_ && i < static_cast<int>(report_.findings.size())) {
            const auto& finding = report_.findings[static_cast<std::size_t>(i)];
            pass = finding.passed;
            warn = !finding.passed && finding.detail.find("偏") != std::string::npos;
            detail = QString::fromStdString(finding.detail);
        }
        const char* state = has_report_ ? (pass ? "pass" : (warn ? "warn" : "fail")) : "todo";
        const char* ic = has_report_ ? (pass ? "✓" : (warn ? "⚠" : "✕")) : "◷";
        const char* tag_tone = has_report_ ? (pass ? "ok" : (warn ? "warn" : "danger")) : "idle";
        const QString tag_text = has_report_ ? (pass ? QStringLiteral("过") : (warn ? QStringLiteral("⚠") : QStringLiteral("✕")))
                                             : QStringLiteral("待评审");

        auto* row = new QFrame(checklist_);
        row->setProperty("ckRole", QStringLiteral("item"));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(10, 7, 10, 7); // webui .ck p7 10
        line->setSpacing(theme::space::kSteps[2]);
        auto* icon = new QLabel(QString::fromUtf8(ic), row);
        icon->setProperty("ckRole", QStringLiteral("ic"));
        icon->setProperty("ckState", QString::fromLatin1(state));
        icon->setFixedWidth(16);
        icon->setAlignment(Qt::AlignCenter);
        auto* name = new widgets::ElidedLabel(items[i].name, row);
        name->setProperty("ckRole", QStringLiteral("name"));
        name->SetExpandable(false);
        name->setToolTip(detail.isEmpty() ? items[i].name : QStringLiteral("%1 · %2").arg(items[i].name, detail));
        auto* tag = new widgets::Tag(tag_text, tag_tone, false, row);
        line->addWidget(icon, 0);
        line->addWidget(name, 1);
        line->addWidget(tag, 0);
        checklist_lay_->insertWidget(checklist_lay_->count() - 1, row);
    }
    if (!has_report_) {
        status_->setText(report_path_.empty() ? QStringLiteral("还没评审")
                                              : QStringLiteral("还没评审 · 报告将写入 %1")
                                                    .arg(QString::fromStdString(report_path_)));
        return;
    }
    status_->setText(QStringLiteral("%1 · 报告：%2")
                         .arg(QString::fromStdString(report_.Describe()),
                              QString::fromStdString(report_.report_path)));
}

QString ImageReviewView::Probe() const {
    return QStringLiteral("has_report=%1; ok=%2; findings=%3; path=%4")
        .arg(has_report_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(report_.ok ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(report_.findings.size())
        .arg(QString::fromStdString(report_.report_path));
}

} // namespace shine::app
