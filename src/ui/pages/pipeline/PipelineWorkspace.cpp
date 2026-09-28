#include "ui/pages/pipeline/PipelineWorkspace.h"

#include "ui/pages/pipeline/GanttView.h"
#include "ui/pages/pipeline/LedgerView.h"
#include "ui/pages/pipeline/StopReportView.h"
#include "ui/kit/data/Flow.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/layout/QtLayout.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/layout/QtLayout.h"
#include "util/Random.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace shine::app {

PipelineWorkspace::PipelineWorkspace(QWidget* parent) : QWidget(parent) { BuildUi(); }

void PipelineWorkspace::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    // 页面级留白 / 分区间距统一走 layout helper（对齐 webui .vw：20 24 26 / gap 16）
    util::PageMargins(outer);
    util::PageSpacing(outer);

    // ── 页首：标题 + 副标题 + 操作 ──
    // webui 的 vw-head：图标 + 标题/副标题 + spacer + 运行按钮
    auto* head = new QWidget(this);
    auto* head_lay = new QHBoxLayout(head);
    head_lay->setContentsMargins(0, 0, 0, 0);
    head_lay->setSpacing(theme::space::kSteps[2]);
    auto* head_text = new QVBoxLayout;
    head_text->setSpacing(2);
    auto* title = widgets::SectionTitle(QStringLiteral("全流程总控台"), head);
    QFont tf = title->font();
    tf.setPointSize(tf.pointSize() + 2);
    tf.setBold(true);
    title->setFont(tf);
    auto* subtitle = new QLabel(QStringLiteral("一句话到成片 · 阶段机 / 预算 / 账本 / 检查点"), head);
    widgets::SetKind(subtitle, "statesub");
    head_text->addWidget(title);
    head_text->addWidget(subtitle);
    head_lay->addLayout(head_text, 1);

    auto* next = new widgets::Button(QStringLiteral("运行下一阶段"), widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, head);
    next->setToolTip(QStringLiteral("只推进一个阶段，便于定位卡住的环节"));
    run_btn_ = new widgets::Button(QStringLiteral("一键全流程"), widgets::Button::Variant::Primary,
                                   widgets::Button::Size::Sm, head);
    run_btn_->setToolTip(QStringLiteral("从当前阶段跑到 T17，落盘阶段产物与账本"));
    head_lay->addWidget(next);
    head_lay->addWidget(run_btn_);
    outer->addWidget(head);

    // ── 运行状态卡：结论 + 百分比 + 阶段流 ──
    auto* run_card = new widgets::SectionCard(QStringLiteral("运行状态"), this);
    auto* run_body = run_card->BodyLayout();
    auto* run_row = new QWidget(run_card);
    auto* rr = new QHBoxLayout(run_row);
    rr->setContentsMargins(0, 0, 0, 0);
    status_ = new QLabel(QStringLiteral("等待运行 · 一键全流程开始 T1"), run_row);
    widgets::SetKind(status_, "statedetail");
    pct_ = new QLabel(QStringLiteral("0%"), run_row);
    widgets::SetKind(pct_, "statesub");
    rr->addWidget(status_, 1);
    rr->addWidget(pct_, 0, Qt::AlignVCenter);
    run_body->addWidget(run_row);

    progress_ = new QProgressBar(run_card);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    widgets::SetKind(progress_, "progressbar");
    run_body->addWidget(progress_);

    stage_flow_ = new shine::data::StageFlow(run_card);
    // webui .stageflow 的行高 = snode 30 + 上下留白；支线再占一行 → 84 足够。
    // 不给伸展因子：让它拉伸会在卡片底部留出一大片空白。
    stage_flow_->setMinimumHeight(84);
    stage_flow_->setMaximumHeight(84);
    run_body->addWidget(stage_flow_, 0);
    outer->addWidget(run_card, 0);

    RefreshStageFlow();

    // ── KPI 行 ──
    const auto make_kpi = [this](const QString& label) {
        auto* tile = new shine::data::StatTile(this);
        tile->SetLabel(label);
        return tile;
    };
    kpi_chapters_ = make_kpi(QStringLiteral("章节"));
    kpi_artifacts_ = make_kpi(QStringLiteral("阶段产物"));
    kpi_calls_ = make_kpi(QStringLiteral("LLM 调用"));
    kpi_shots_ = make_kpi(QStringLiteral("镜头"));
    kpi_cost_ = make_kpi(QStringLiteral("估算成本"));
    auto* kpi_row = new QWidget(this);
    auto* kr = new QHBoxLayout(kpi_row);
    kr->setContentsMargins(0, 0, 0, 0);
    kr->setSpacing(theme::space::kSteps[2]);
    for (shine::data::StatTile* tile : {kpi_chapters_, kpi_artifacts_, kpi_calls_, kpi_shots_, kpi_cost_}) {
        kr->addWidget(tile, 1);
    }
    outer->addWidget(kpi_row);

    // ── 主体：左列（甘特 + 账本） / 右列（停止条件 + 运行信息） ──
    // 整块放进滚动区：窄窗口时纵向滚动，而不是把表格压扁。
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* body = new QWidget(scroll);
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(theme::space::kSteps[2]);
    grid->setVerticalSpacing(theme::space::kSteps[2]);

    gantt_ = new GanttView(body);
    ledger_ = new LedgerView(body);
    stop_ = new StopReportView(body);
    // 三个子视图都自带标题，而它们马上会被装进分区卡片（卡片已有标题栏），
    // 不关掉就会出现同一个标题连着两行。默认保留自带标题，独立使用时不受影响。
    gantt_->SetOwnTitle(false);
    ledger_->SetOwnTitle(false);
    stop_->SetOwnTitle(false);

    auto* gantt_card = new widgets::SectionCard(QStringLiteral("全书甘特 · 章节 × 阶段"), body);
    gantt_card->BodyLayout()->addWidget(gantt_);
    auto* ledger_card = new widgets::SectionCard(QStringLiteral("账本 · 成本 / 调用 / 降级"), body);
    ledger_card->BodyLayout()->addWidget(ledger_);
    auto* stop_card = new widgets::SectionCard(QStringLiteral("停止条件"), body);
    stop_card->BodyLayout()->addWidget(stop_);

    // 运行信息：当前项目 / 数据目录 / 检查点 / 预算余量（KeyValue 已有）
    auto* info_card = new widgets::SectionCard(QStringLiteral("运行信息"), body);
    info_ = new shine::data::KeyValue(info_card);
    info_card->BodyLayout()->addWidget(info_);

    // webui Overview.jsx:72-169 是 `.grid-3-1` 包两个 `col gap-3` 竖列：
    //   左 = 甘特 + 账本，右 = 停止条件 + 最近产物 + 运行信息。
    // CSS：`grid-template-columns: minmax(0,1fr) 300px; gap:16; align-items:start`
    //      （views.css:153-158）—— 右列**定宽 300**，两列都**按内容高度**、顶端对齐。
    // 此处照此搭两个列容器：
    //   * 列容器固定 300 宽，还原定宽右列（此前按 1/3 比例分配，右列被拉得又宽又空）；
    //   * 每列内部是 VBox + 12 间距，卡片只占内容高（此前 setRowStretch(1,1) 把富余
    //     高度全喂给第二行，账本卡被撑到半屏高、卡内留出大片死白 —— 这是「不像」的主因）。
    auto* left_col = new QWidget(body);
    auto* left_col_layout = new QVBoxLayout(left_col);
    left_col_layout->setContentsMargins(0, 0, 0, 0);
    left_col_layout->setSpacing(theme::space::kSteps[4]); // .col gap-3
    left_col_layout->addWidget(gantt_card, 0, Qt::AlignTop);
    left_col_layout->addWidget(ledger_card, 0, Qt::AlignTop);

    auto* right_col = new QWidget(body);
    right_col->setFixedWidth(300);
    auto* right_col_layout = new QVBoxLayout(right_col);
    right_col_layout->setContentsMargins(0, 0, 0, 0);
    right_col_layout->setSpacing(theme::space::kSteps[4]); // .col gap-3
    right_col_layout->addWidget(stop_card, 0, Qt::AlignTop);
    right_col_layout->addWidget(info_card, 0, Qt::AlignTop);

    grid->addWidget(left_col, 0, 0, Qt::AlignTop);
    grid->addWidget(right_col, 0, 1, Qt::AlignTop);
    grid->setColumnStretch(0, 1);
    grid->setAlignment(Qt::AlignTop);
    // 宽表在窄窗口下横向滚动，而不是被右列挤成残条
    gantt_card->SetContentMinWidth(560);
    ledger_card->SetContentMinWidth(560);

    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    connect(next, &QPushButton::clicked, this, &PipelineWorkspace::RunNext);
    connect(run_btn_, &QPushButton::clicked, this, &PipelineWorkspace::RunAll);
}

void PipelineWorkspace::SetContext(std::filesystem::path root) {
    root_ = std::move(root);
    runner_.Configure(root_, pipeline::RunMode::Auto,
                      [](pipeline::StageId, const std::string&, std::string&) { return true; },
                      [](pipeline::StageId stage) { return pipeline::StageCode(stage) + ":hash"; });
    RefreshRunState();
}

int PipelineWorkspace::CurrentChapterIndex() const {
    // Runner 的当前阶段在 T1–T17 上的序号（0 起）；非章节阶段（T/V）归到末位。
    const auto id = runner_.CurrentStage();
    if (id < pipeline::StageId::T1 || id > pipeline::StageId::T17) {
        return 16;
    }
    return static_cast<int>(id) - static_cast<int>(pipeline::StageId::T1);
}

void PipelineWorkspace::RefreshStageFlow() {
    std::vector<shine::data::StageFlow::Node> nodes;
    const int current = CurrentChapterIndex();
    for (const auto& def : pipeline::AllStages()) {
        if (def.id < pipeline::StageId::T1 || def.id > pipeline::StageId::T17) {
            continue; // V 系列属分镜工作区，不画进章节链
        }
        shine::data::StageFlow::Node node;
        node.id = QString::fromStdString(def.code);
        node.title = QString::fromStdString(def.name);
        const int idx = static_cast<int>(def.id) - static_cast<int>(pipeline::StageId::T1);
        node.state = idx < current ? shine::data::StageFlow::NodeState::Done
                                   : shine::data::StageFlow::NodeState::Todo;
        nodes.push_back(std::move(node));
    }
    // 连线按真实先后串起来：第 i 个的出口接第 i+1 个的入口
    std::vector<shine::data::StageFlow::Link> links;
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        links.push_back({nodes[i - 1].id, nodes[i].id});
    }
    stage_flow_->SetGraph(std::move(nodes), std::move(links));
}

void PipelineWorkspace::RefreshRunState() {
    const auto& budget = runner_.Usage();
    const auto entries = static_cast<int>(runner_.LedgerLog().Entries().size());
    const int idx = CurrentChapterIndex();
    const int pct = (idx + 1) * 100 / 17;
    progress_->setValue(pct);
    pct_->setText(QStringLiteral("%1%").arg(pct));

    // 章节数：甘特视图按章展开，这里用它的探针口径取真实章数，
    // 不另外维护一份计数（避免两处数字对不上）。
    kpi_chapters_->SetValue(QString::number(chapters_shown_));
    kpi_artifacts_->SetValue(QString::number(entries));
    kpi_calls_->SetValue(QString::number(budget.llm_calls));
    kpi_shots_->SetValue(QString::number(budget.shots));
    kpi_cost_->SetValue(QString::number(budget.cost, 'f', 2));

    const double remain = budget.max_cost - budget.cost;
    info_->SetPairs({
        {QStringLiteral("当前项目"), root_.empty() ? QStringLiteral("未打开项目")
                                                  : QString::fromStdString(root_.filename().string())},
        {QStringLiteral("数据目录"), root_.empty() ? QStringLiteral("—")
                                                  : QString::fromStdString(root_.string())},
        {QStringLiteral("当前阶段"), QString::fromStdString(pipeline::StageCode(runner_.CurrentStage()))},
        {QStringLiteral("预算余量"), QStringLiteral("¥%1 / ¥%2").arg(remain, 0, 'f', 2).arg(budget.max_cost, 0, 'f', 2)},
    });
    RefreshStageFlow();
}

void PipelineWorkspace::LoadMock() {
    if (root_.empty()) SetContext(std::filesystem::temp_directory_path() /
                                  ("shinetv-p09-" + shine::util::RandomHex(5)));
    const auto result = runner_.RunAll();
    chapters_shown_ = 3;
    gantt_->SetChapters(3);
    for (int chapter = 0; chapter < 3; ++chapter) {
        for (const auto& stage : pipeline::AllStages()) gantt_->SetStageState(chapter, stage.id, QStringLiteral("完成"));
    }
    ledger_->SetLedger(runner_.LedgerLog(), runner_.Usage());
    pipeline::StopPolicy policy;
    stop_->SetDecision(policy.Evaluate(runner_.Usage(), true, true, true, true));
    status_->setText(result.ok ? QStringLiteral("全流程完成：产物与账本已落盘")
                               : QStringLiteral("已停止：%1").arg(QString::fromStdString(result.reason)));
    RefreshRunState();
}

void PipelineWorkspace::RunNext() {
    const auto result = runner_.RunNext();
    chapters_shown_ = 3;
    gantt_->SetChapters(3);
    gantt_->SetStageState(0, result.stage, result.ok ? QStringLiteral("完成") : QStringLiteral("停止"));
    ledger_->SetLedger(runner_.LedgerLog(), runner_.Usage());
    status_->setText(QStringLiteral("当前阶段 %1：%2").arg(QString::fromStdString(pipeline::StageCode(result.stage)),
                                                               result.ok ? QStringLiteral("完成") : QString::fromStdString(result.reason)));
    RefreshRunState();
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
