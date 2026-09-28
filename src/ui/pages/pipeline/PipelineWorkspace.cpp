#include "ui/pages/pipeline/PipelineWorkspace.h"

#include "ui/pages/pipeline/GanttView.h"
#include "ui/pages/pipeline/LedgerView.h"
#include "ui/pages/pipeline/StopReportView.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "util/Random.h"

#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace shine::app {

PipelineWorkspace::PipelineWorkspace(QWidget* parent) : QWidget(parent) { BuildUi(); }

void PipelineWorkspace::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("全流程总控台 · 一句话到成片"), this);
    outer->addWidget(title);
    auto* bar = new QWidget(this);
    auto* bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(0, 0, 0, 0);
    auto* next = new widgets::Button(QStringLiteral("运行下一阶段"), widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, bar);
    auto* all = new widgets::Button(QStringLiteral("一键全流程"), widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, bar);
    bar_layout->addWidget(next);
    bar_layout->addWidget(all);
    outer->addWidget(bar);
    status_ = new QLabel(QStringLiteral("等待运行"), this);
    widgets::SetKind(status_, "statedetail");
    outer->addWidget(status_);
    auto* tabs = new QTabWidget(this);
    gantt_ = new GanttView(tabs);
    ledger_ = new LedgerView(tabs);
    stop_ = new StopReportView(tabs);
    tabs->addTab(gantt_, QStringLiteral("甘特图"));
    tabs->addTab(ledger_, QStringLiteral("账本"));
    tabs->addTab(stop_, QStringLiteral("停止报告"));
    outer->addWidget(tabs, 1);
    connect(next, &QPushButton::clicked, this, &PipelineWorkspace::RunNext);
    connect(all, &QPushButton::clicked, this, &PipelineWorkspace::RunAll);
}

void PipelineWorkspace::SetContext(std::filesystem::path root) {
    root_ = std::move(root);
    runner_.Configure(root_, pipeline::RunMode::Auto,
                      [](pipeline::StageId, const std::string&, std::string&) { return true; },
                      [](pipeline::StageId stage) { return pipeline::StageCode(stage) + ":hash"; });
}

void PipelineWorkspace::LoadMock() {
    if (root_.empty()) SetContext(std::filesystem::temp_directory_path() /
                                  ("shinetv-p09-" + shine::util::RandomHex(5)));
    const auto result = runner_.RunAll();
    gantt_->SetChapters(3);
    for (int chapter = 0; chapter < 3; ++chapter) {
        for (const auto& stage : pipeline::AllStages()) gantt_->SetStageState(chapter, stage.id, QStringLiteral("完成"));
    }
    ledger_->SetLedger(runner_.LedgerLog(), runner_.Usage());
    pipeline::StopPolicy policy;
    stop_->SetDecision(policy.Evaluate(runner_.Usage(), true, true, true, true));
    status_->setText(result.ok ? QStringLiteral("全流程完成：产物与账本已落盘")
                               : QStringLiteral("已停止：%1").arg(QString::fromStdString(result.reason)));
}

void PipelineWorkspace::RunNext() {
    const auto result = runner_.RunNext();
    gantt_->SetChapters(3);
    gantt_->SetStageState(0, result.stage, result.ok ? QStringLiteral("完成") : QStringLiteral("停止"));
    ledger_->SetLedger(runner_.LedgerLog(), runner_.Usage());
    status_->setText(QStringLiteral("当前阶段 %1：%2").arg(QString::fromStdString(pipeline::StageCode(result.stage)),
                                                               result.ok ? QStringLiteral("完成") : QString::fromStdString(result.reason)));
}

void PipelineWorkspace::RunAll() { LoadMock(); }

QString PipelineWorkspace::Probe() const {
    return QStringLiteral("runner=%1; gantt=%2; ledger=%3; stop=%4")
        .arg(QString::fromStdString(runner_.Probe()), GanttProbe(), LedgerProbe(), StopProbe());
}

QString PipelineWorkspace::GanttProbe() const { return gantt_->Probe(); }
QString PipelineWorkspace::LedgerProbe() const { return ledger_->Probe(); }
QString PipelineWorkspace::StopProbe() const { return stop_->Probe(); }

} // namespace shine::app
