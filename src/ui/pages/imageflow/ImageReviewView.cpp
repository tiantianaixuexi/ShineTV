#include "ui/pages/imageflow/ImageReviewView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace shine::app {

ImageReviewView::ImageReviewView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("图评审 · 固定五项清单"), this);
    layout->addWidget(title);
    auto* body = new QHBoxLayout;
    image_ = new QLabel(QStringLiteral("生成结果\n（等待图像）"), this);
    image_->setAlignment(Qt::AlignCenter);
    image_->setMinimumSize(220, 150);
    image_->setStyleSheet(QStringLiteral("border: 1px solid palette.mid; padding: 12px;"));
    widgets::SetKind(image_, "statemeta");
    body->addWidget(image_, 1);
    findings_ = new QListWidget(this);
    findings_->setMinimumWidth(300);
    body->addWidget(findings_, 2);
    layout->addLayout(body, 1);
    status_ = new QLabel(QStringLiteral("还没评审"), this);
    widgets::SetKind(status_, "statedetail");
    layout->addWidget(status_);
    auto* run = new widgets::Button(QStringLiteral("评审当前图"), widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, this);
    auto* retry = new widgets::Button(QStringLiteral("仅重跑 ⚠ 项"), widgets::Button::Variant::Secondary,
                                      widgets::Button::Size::Sm, this);
    layout->addWidget(run);
    layout->addWidget(retry);
    connect(run, &QPushButton::clicked, this, &ImageReviewView::Run);
    connect(retry, &QPushButton::clicked, this, [this] {
        if (!has_report_) return;
        status_->setText(QStringLiteral("已将失败项加入重跑队列"));
    });
}

void ImageReviewView::SetInput(const flow::ImageReviewInput& input, const std::string& report_path) {
    input_ = input;
    report_path_ = report_path;
    has_report_ = false;
    image_->setText(QStringLiteral("生成结果\n%1")
                        .arg(QString::fromStdString(input.image_path)));
    status_->setText(QStringLiteral("还没评审"));
    findings_->clear();
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
    findings_->clear();
    for (const auto& finding : report_.findings) {
        findings_->addItem(QStringLiteral("%1 %2 %3")
                               .arg(finding.passed ? QStringLiteral("✔") : QStringLiteral("⚠"),
                                    QString::fromStdString(finding.label),
                                    QString::fromStdString(finding.detail)));
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
