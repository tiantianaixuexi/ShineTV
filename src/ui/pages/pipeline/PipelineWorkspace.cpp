#include "ui/pages/pipeline/PipelineWorkspace.h"

#include "core/Async.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Surfaces.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/data/Flow.h"
#include "ui/kit/data/Panels.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/layout/QtLayout.h"
#include "ui/pages/pipeline/GanttView.h"
#include "ui/pages/pipeline/LedgerView.h"
#include "ui/pages/pipeline/StopReportView.h"
#include "util/Random.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace shine::app {
namespace {

using shine::data::StatTile;

// views.css:419-430 `.kpi::after`：右上角 90px 正圆 + accent-dim 底 + opacity .7。
// QSS 没有伪元素与 filter/blur，这里用一个子 QFrame 自绘；Qt 会把子控件裁进
// 父矩形，正好等价于设计稿的 `overflow: hidden`。
QString BlobQss() {
    const QColor c = widgets::TokenQColor(theme::Current().accentPrimary);
    return QStringLiteral("QFrame { background-color: rgba(%1,%2,%3,%4);"
                         " border-radius: 999px; border: none; }")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        // --accent-dim 是 accent 的 14%，再乘设计稿的 opacity .7
        .arg(QString::number(0.098, 'f', 3));
}

// views.css:414-456 `.kpi`：label 11.5 w700 muted / value 24 w800 / k-foot 11.5 muted。
// StatTile 已有前两档（kit 已按 ui.css 对齐），第三档（k-foot）在 kit 里是「涨跌提示」位；
// 这里装的是**真实口径的脚注**（高档数 / 预算上限等），不编造环比涨跌。
// kit 里第三行 label 由 QSS 属性驱动，页面只能 findChild 改文案 ——
// 与 StoryboardWorkspace 的 SetTagText 同一手法，不动 kit。
void SetKpiFoot(shine::data::StatTile* tile, const QString& text) {
    if (tile == nullptr) {
        return;
    }
    if (auto* l = tile->findChild<QLabel*>(QStringLiteral("shineStatTrend"))) {
        l->setText(text);
        widgets::SetVariant(l, "flat"); // .k-foot 是 muted 纯文字，不是涨跌色
        widgets::Repolish(l);
    }
}

// views.css:426-428 `.kpi::after` 的几何：90px 正圆，right/top 各 -30px
constexpr int kBlobSize = 90;
constexpr int kBlobInset = 30;

// 装饰圆跟随卡片尺寸重摆（views.css:422-426 绝对定位的等价物）。
// StatTile 在 kit 里且已冻结，所以用事件过滤而不是改它。
class BlobPlacer : public QObject {
  public:
    BlobPlacer(QFrame* host, QWidget* parent) : QObject(parent), host_(host) {
        blob_ = new QFrame(host);
        blob_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        RefreshBlob();
        host_->installEventFilter(this);
        Place();
    }

    void RefreshBlob() {
        if (blob_ != nullptr) {
            blob_->setStyleSheet(BlobQss());
        }
    }

    QFrame* Blob() const { return blob_; }

  protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (watched == host_ && ev->type() == QEvent::Resize && blob_ != nullptr) {
            // right:-30px; top:-30px; width/height:90px
            blob_->setGeometry(host_->width() - kBlobSize + kBlobInset, -kBlobInset, kBlobSize,
                               kBlobSize);
        }
        return QObject::eventFilter(watched, ev);
    }

  private:
    void Place() {
        if (blob_ != nullptr) {
            blob_->setGeometry(host_->width() - kBlobSize + kBlobInset, -kBlobInset, kBlobSize,
                               kBlobSize);
        }
    }

    QWidget* host_ = nullptr;
    QFrame* blob_ = nullptr;
};

// views.css:39 `<Card className={run.active ? 'glow' : ''} bodyClass="col gap-3">`
// —— 这张卡**没有 hoverable**，即不该有 hover 抬升；kit::Card 默认 enter 就抬 2px。
// 局部子类把抬升关掉，几何与配色仍走 kit。
class RunCard : public widgets::Card {
  public:
    using widgets::Card::Card;

  protected:
    void enterEvent(QEnterEvent* ev) override { QFrame::enterEvent(ev); }
    void leaveEvent(QEvent* ev) override { QFrame::leaveEvent(ev); }
};

} // namespace

PipelineWorkspace::PipelineWorkspace(QWidget* parent) : QWidget(parent) {
    runner_ = std::make_shared<pipeline::Runner>();
    // T 链（章节阶段机）= 阶段表里 chain == "text" 的那一段（T1–T17）
    for (const auto& def : pipeline::AllStages()) {
        if (def.chain == "text") {
            chain_.push_back(def);
        }
    }
    BuildUi();
}

PipelineWorkspace::~PipelineWorkspace() {
    if (alive_ != nullptr) {
        alive_->store(false); // worker 看到 false 后不再回投，也不再碰 runner_
    }
    if (stop_flag_ != nullptr) {
        stop_flag_->store(true);
    }
}

void PipelineWorkspace::BuildUi() {
    setObjectName(QStringLiteral("pipelineWs"));
    auto* outer = new QVBoxLayout(this);
    // views.css:133-139 `.vw { padding: 20px 24px 26px; gap: 16px }`
    util::PageMargins(outer);
    util::PageSpacing(outer);

    // ── views.css:140-144 `.vw-head { display:flex; align-items:center; gap:14px }` ──
    auto* head = new QWidget(this);
    auto* head_lay = new QHBoxLayout(head);
    head_lay->setContentsMargins(0, 0, 0, 0);
    head_lay->setSpacing(14);
    // Overview.jsx:24 `<Icon name="gauge" width:20 height:20 color:var(--accent) />`
    auto* glyph = new QLabel(QStringLiteral("◔"), head);
    glyph->setFixedSize(20, 20);
    glyph->setAlignment(Qt::AlignCenter);
    widgets::SetTextColor(glyph, theme::Current().accentPrimary);
    head_lay->addWidget(glyph, 0, Qt::AlignVCenter);
    auto* titles = new QWidget(head);
    auto* titles_lay = new QVBoxLayout(titles);
    titles_lay->setContentsMargins(0, 0, 0, 0);
    titles_lay->setSpacing(2);
    auto* title = widgets::SectionTitle(QStringLiteral("全流程总控台"), titles);
    // views.css:145-148 `.vw-title { font-size: 18px; font-weight: 800 }`
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 800;"));
    titles_lay->addWidget(title);
    // views.css:149-152 `.vw-sub { color: text-muted; font-size: 12.5px }`（x.5 就近取整到 12）
    auto* subtitle = new QLabel(QStringLiteral("一句话到成片 · 阶段机 / 预算 / 账本 / 检查点"), titles);
    widgets::SetKind(subtitle, "statemeta");
    subtitle->setStyleSheet(QStringLiteral("font-size: 12px;"));
    titles_lay->addWidget(subtitle);
    head_lay->addWidget(titles, 1);
    next_btn_ = new widgets::Button(QStringLiteral("运行下一阶段"), widgets::Button::Variant::Secondary,
                                    widgets::Button::Size::Sm, head);
    next_btn_->setToolTip(QStringLiteral("只推进一个阶段，便于定位卡住的环节"));
    run_btn_ = new widgets::Button(QStringLiteral("一键全流程"), widgets::Button::Variant::Primary,
                                   widgets::Button::Size::Sm, head);
    run_btn_->setToolTip(QStringLiteral("按阶段机从当前阶段跑到末阶段，落盘阶段产物与账本"));
    head_lay->addWidget(next_btn_);
    head_lay->addWidget(run_btn_);
    outer->addWidget(head);

    // ── 运行状态卡（无标题栏，对应设计稿里的裸 Card）──
    auto* run_card = new RunCard(widgets::Card::Variant::Outlined, this);
    run_card->setObjectName(QStringLiteral("runCard"));
    auto* run_body = run_card->BodyLayout();
    // views.css:193-195 `.card-b { padding: 16px }` + bodyClass "col gap-3"（12px）
    run_body->setContentsMargins(16, 16, 16, 16);
    run_body->setSpacing(theme::space::kSteps[4]);
    auto* run_row = new QWidget(run_card);
    auto* rr = new QHBoxLayout(run_row);
    rr->setContentsMargins(0, 0, 0, 0);
    rr->setSpacing(theme::space::kSteps[3]);
    // Overview.jsx:42-46 三态文案：运行中 accent / 完成 ok / 等待 muted
    status_ = new QLabel(QStringLiteral("等待运行"), run_row);
    widgets::SetKind(status_, "statestatus");
    rr->addWidget(status_, 1);
    // Overview.jsx:49 `<span className="mono tiny dim">{pct}%</span>`
    pct_ = new QLabel(QStringLiteral("0%"), run_row);
    widgets::SetKind(pct_, "statemeta");
    pct_->setProperty("shineWeight", QStringLiteral("mono"));
    widgets::Repolish(pct_);
    rr->addWidget(pct_, 0, Qt::AlignVCenter);
    run_body->addWidget(run_row);

    // views.css:430-443 `.prog`：h6 pill + fill-muted 底 + accent 填充
    progress_ = new QProgressBar(run_card);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    widgets::SetKind(progress_, "progressbar");
    run_body->addWidget(progress_);

    // views.css:814-821 `.stageflow { overflow-x: auto; padding: 4px 2px }`
    // T 链有十几个节点，横向必然超出卡片宽度；设计稿靠 overflow-x 滚动，
    // 这里套 QScrollArea（widgetResizable=false 让 StageFlow 保持自身 sizeHint 宽）。
    auto* flow_scroll = new QScrollArea(run_card);
    flow_scroll->setFrameShape(QFrame::NoFrame);
    flow_scroll->setWidgetResizable(false);
    flow_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    flow_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    stage_flow_ = new shine::data::StageFlow(flow_scroll);
    // webui .stageflow 的行高 = snode 30 + 上下留白；无支线时 84 足够。
    stage_flow_->setFixedHeight(84);
    flow_scroll->setWidget(stage_flow_);
    flow_scroll->setFixedHeight(96); // 84 + 横向滚动条高度（QSS 定的 10px + 余量）
    run_body->addWidget(flow_scroll);
    outer->addWidget(run_card, 0);

    RefreshStageFlow();

    // ── views.css:409-413 `.kpis { display:grid; gap:14px }` ──
    // grid auto-fit + minmax(210px,1fr) 用「等分伸展 + 210px 下限」近似。
    kpi_chapters_ = new StatTile(this);
    kpi_calls_ = new StatTile(this);
    kpi_cost_ = new StatTile(this);
    kpi_shots_ = new StatTile(this);
    kpi_artifacts_ = new StatTile(this);
    kpi_chapters_->SetLabel(QStringLiteral("章节"));
    kpi_calls_->SetLabel(QStringLiteral("LLM 调用"));
    kpi_cost_->SetLabel(QStringLiteral("估算成本"));
    kpi_shots_->SetLabel(QStringLiteral("镜头"));
    kpi_artifacts_->SetLabel(QStringLiteral("阶段产物"));
    auto* kpi_row = new QWidget(this);
    auto* kr = new QHBoxLayout(kpi_row);
    kr->setContentsMargins(0, 0, 0, 0);
    kr->setSpacing(14);
    for (shine::data::StatTile* tile :
         {kpi_chapters_, kpi_calls_, kpi_cost_, kpi_shots_, kpi_artifacts_}) {
        tile->setMinimumWidth(210); // views.css:411 minmax(210px, 1fr)
        kr->addWidget(tile, 1);
    }
    // views.css:419-430 `.kpi::after`：每张 KPI 卡右上角一个 accent 装饰圆。
    // BlobPlacer 挂在卡片上（随卡回收），只把圆本体登记进来供换肤时重刷样式。
    for (shine::data::StatTile* tile :
         {kpi_chapters_, kpi_calls_, kpi_cost_, kpi_shots_, kpi_artifacts_}) {
        auto* placer = new BlobPlacer(tile, tile);
        kpi_blobs_.push_back(placer->Blob());
    }
    outer->addWidget(kpi_row);

    // ── 主体：左列（甘特 + 账本） / 右列（停止条件 + 最近产物 + 运行信息） ──
    // 整块放进滚动区：窄窗口时纵向滚动，而不是把表格压扁。
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* body = new QWidget(scroll);
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    // views.css:153-158 `.grid-3-1 { minmax(0,1fr) 300px; gap:16; align-items:start }`
    grid->setHorizontalSpacing(theme::space::kSteps[5]);
    grid->setVerticalSpacing(theme::space::kSteps[5]);

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
    gantt_card_ = gantt_card;
    gantt_card->SetSubtitle(QStringLiteral("按章 × 阶段链"));
    auto* ledger_card = new widgets::SectionCard(QStringLiteral("账本 · 成本 / 调用 / 降级"), body);
    ledger_card->BodyLayout()->addWidget(ledger_);
    // Overview.jsx:118 的标题是「停止条件 · S1–S12」；src/pipeline/StopPolicy 实际只判
    // S1–S4 预算 / S5 / S6 / S7 / S8，按真实编号写，不沿用设计稿的演示编号。
    auto* stop_card = new widgets::SectionCard(QStringLiteral("停止条件 · S1–S8"), body);
    stop_card->BodyLayout()->addWidget(stop_);
    auto* artifacts_card = new widgets::SectionCard(QStringLiteral("最近产物"), body);
    artifacts_host_ = artifacts_card;
    artifacts_body_ = artifacts_card->BodyLayout();
    auto* info_card = new widgets::SectionCard(QStringLiteral("运行信息"), body);
    info_ = new shine::data::KeyValue(info_card);
    info_card->BodyLayout()->addWidget(info_);

    auto* left_col = new QWidget(body);
    auto* left_col_layout = new QVBoxLayout(left_col);
    left_col_layout->setContentsMargins(0, 0, 0, 0);
    left_col_layout->setSpacing(theme::space::kSteps[4]); // .col gap-3
    left_col_layout->addWidget(gantt_card, 0, Qt::AlignTop);
    left_col_layout->addWidget(ledger_card, 0, Qt::AlignTop);

    auto* right_col = new QWidget(body);
    right_col->setFixedWidth(300); // views.css:155 `300px`
    auto* right_col_layout = new QVBoxLayout(right_col);
    right_col_layout->setContentsMargins(0, 0, 0, 0);
    right_col_layout->setSpacing(theme::space::kSteps[4]); // .col gap-3
    right_col_layout->addWidget(stop_card, 0, Qt::AlignTop);
    right_col_layout->addWidget(artifacts_card, 0, Qt::AlignTop);
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

    connect(next_btn_, &QPushButton::clicked, this, [this] { RunNext(); });
    connect(run_btn_, &QPushButton::clicked, this, [this] { RunAll(); });

    SyncSnapshot();
    RefreshRunButton();
    RefreshRunState();
}

void PipelineWorkspace::changeEvent(QEvent* ev) {
    QWidget::changeEvent(ev);
    // ThemeService 换肤走 setPalette + setStyleSheet，Qt 分别派发 PaletteChange /
    // StyleChange（ThemeChange 只在样式本身变化时才有），三种都兜住，
    // 否则装饰圆会停在上一个主题的颜色上。
    if (ev->type() == QEvent::ThemeChange || ev->type() == QEvent::PaletteChange ||
        ev->type() == QEvent::StyleChange) {
        // 装饰圆的颜色取自 accent token，换肤后按新 token 重建
        for (auto* blob : kpi_blobs_) {
            if (blob != nullptr) {
                blob->setStyleSheet(BlobQss());
            }
        }
    }
}

bool PipelineWorkspace::AllChainDone() const {
    // finished_ 的判据 = T 链每个阶段都在账本里落过盘，
    // 而不是「worker 的循环跑完了」—— 单步跑完一个阶段不等于全流程完成。
    return !chain_.empty() &&
           std::all_of(chain_.begin(), chain_.end(), [this](const pipeline::StageDefinition& def) {
               return std::find(finished_codes_.begin(), finished_codes_.end(), def.code) !=
                      finished_codes_.end();
           });
}

int PipelineWorkspace::ChainPercent() const {
    if (chain_.empty()) {
        return 0;
    }
    // Overview.jsx:19 `run.finished ? 100 : round((idx+1)/len*100)`
    // 口径换成「已落盘账本的 T 链阶段数」，与甘特 / 账本显示的口径一致。
    if (finished_) {
        return 100;
    }
    const auto done = static_cast<int>(std::min<std::size_t>(finished_codes_.size(), chain_.size()));
    return done * 100 / static_cast<int>(chain_.size());
}

void PipelineWorkspace::RefreshStageFlow() {
    std::vector<shine::data::StageFlow::Node> nodes;
    for (const auto& def : chain_) {
        shine::data::StageFlow::Node node;
        // 阶段码与阶段名都来自真实阶段表（V 系列不在 T 链里）
        node.id = QStringLiteral("%1 %2").arg(QString::fromStdString(def.code),
                                              QString::fromStdString(def.name));
        const bool done =
            std::find(finished_codes_.begin(), finished_codes_.end(), def.code) != finished_codes_.end();
        if (done) {
            node.state = shine::data::StageFlow::NodeState::Done;
        } else if (running_ && def.id == running_stage_) {
            node.state = shine::data::StageFlow::NodeState::Running;
        } else {
            node.state = shine::data::StageFlow::NodeState::Todo;
        }
        nodes.push_back(std::move(node));
    }
    // 连线按真实先后串起来：第 i 个的出口接第 i+1 个的入口
    std::vector<shine::data::StageFlow::Link> links;
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        links.push_back({nodes[i - 1].id, nodes[i].id});
    }
    stage_flow_->SetGraph(std::move(nodes), std::move(links));
}

void PipelineWorkspace::RefreshKpi() {
    const auto& budget = snap_budget_;
    const auto entries = static_cast<int>(snap_entries_.size());
    kpi_chapters_->SetValue(QString::number(chapters_shown_));
    // views.css:452-456 `.k-foot`：11.5px muted 的真实口径说明，不是编造的环比涨跌
    SetKpiFoot(kpi_chapters_, QStringLiteral("甘特已展开 %1 章").arg(chapters_shown_));
    kpi_calls_->SetValue(QString::number(budget.llm_calls));
    SetKpiFoot(kpi_calls_,
               QStringLiteral("高档 %1 · 上限 %2").arg(budget.high_quality_calls).arg(budget.max_llm_calls));
    kpi_cost_->SetValue(QStringLiteral("¥%1").arg(budget.cost, 0, 'f', 2));
    SetKpiFoot(kpi_cost_, QStringLiteral("上限 ¥%1").arg(budget.max_cost, 0, 'f', 2));
    kpi_shots_->SetValue(QString::number(budget.shots));
    SetKpiFoot(kpi_shots_, QStringLiteral("上限 %1").arg(budget.max_shots));
    kpi_artifacts_->SetValue(QString::number(entries));
    SetKpiFoot(kpi_artifacts_, QStringLiteral("work/ 下每阶段一条产物"));
}

void PipelineWorkspace::SetStatus(const QString& text, std::uint32_t color_token) {
    status_->setText(text);
    widgets::SetTextColor(status_, color_token);
}

void PipelineWorkspace::RefreshRunButton() {
    // Overview.jsx:31-35 `run.active ? 停止(danger) : 一键全流程(primary)`
    if (running_) {
        run_btn_->setText(QStringLiteral("停止"));
        widgets::SetVariant(run_btn_, "danger");
    } else {
        run_btn_->setText(QStringLiteral("一键全流程"));
        widgets::SetVariant(run_btn_, "primary");
    }
    widgets::Repolish(run_btn_);
    // 同一次运行里两个入口不能并发起两个 worker
    next_btn_->setEnabled(!running_);
}

void PipelineWorkspace::SyncSnapshot() {
    // 只在空闲时调用（worker 不在跑，runner_ 不会被并发写）
    snap_stage_ = runner_->CurrentStage();
    snap_budget_ = runner_->Usage();
    snap_entries_ = runner_->LedgerLog().Entries();
}

void PipelineWorkspace::RefreshRunState() {
    const auto percent = ChainPercent();
    progress_->setValue(percent);
    pct_->setText(QStringLiteral("%1%").arg(percent));

    // Overview.jsx:42-46 三态：运行中 accent / 完成 ok / 等待 muted
    if (running_) {
        SetStatus(QStringLiteral("当前阶段 %1 · 运行中")
                      .arg(QString::fromStdString(pipeline::StageCode(running_stage_))),
                  theme::Current().accentPrimary);
    } else if (finished_) {
        SetStatus(QStringLiteral("全流程完成：产物与账本已落盘"), theme::Current().statusOk);
    } else if (finished_codes_.empty()) {
        SetStatus(QStringLiteral("等待运行 · 从 %1 开始")
                      .arg(QString::fromStdString(chain_.empty() ? std::string("T1") : chain_.front().code)),
                  theme::Current().textMuted);
    } else {
        SetStatus(QStringLiteral("已停在 %1 · 未跑完")
                      .arg(QString::fromStdString(pipeline::StageCode(snap_stage_))),
                  theme::Current().textMuted);
    }

    RefreshKpi();
    RefreshStageFlow();
    // Overview.jsx:75 `<span className="tiny dim">最近 6 章</span>`：数字取真实展开章数
    if (gantt_card_ != nullptr) {
        gantt_card_->SetSubtitle(chapters_shown_ > 0
                                     ? QStringLiteral("最近 %1 章").arg(chapters_shown_)
                                     : QStringLiteral("按章 × 阶段链"));
    }

    const double remain = snap_budget_.max_cost - snap_budget_.cost;
    // Overview.jsx:160-167 `.kv`：当前项目 / 数据目录 / 检查点 / 预算余量。
    // 「检查点」那一行设计稿给的是 demo 章节号，这里换成同样来自 Runner 的当前阶段。
    info_->SetPairs({
        {QStringLiteral("当前项目"), root_.empty() ? QStringLiteral("未打开项目")
                                                  : QString::fromStdString(root_.filename().string())},
        {QStringLiteral("数据目录"), root_.empty() ? QStringLiteral("—")
                                                  : QString::fromStdString(root_.string())},
        {QStringLiteral("当前阶段"), QString::fromStdString(pipeline::StageCode(snap_stage_))},
        {QStringLiteral("预算余量"), QStringLiteral("¥%1 / ¥%2")
                                          .arg(remain, 0, 'f', 2)
                                          .arg(snap_budget_.max_cost, 0, 'f', 2)},
    });
    RefreshArtifacts();
}

void PipelineWorkspace::RefreshArtifacts() {
    if (artifacts_body_ == nullptr || artifacts_host_ == nullptr) {
        return;
    }
    util::ClearLayout(artifacts_body_);
    // Overview.jsx:132-157「最近产物」列真实产物路径，不用演示文件名。
    const auto& entries = snap_entries_;
    if (entries.empty()) {
        auto* empty = new QLabel(QStringLiteral("还没有落盘产物"), artifacts_host_);
        widgets::SetKind(empty, "statemeta");
        empty->setStyleSheet(QStringLiteral("font-size: 12px;"));
        artifacts_body_->addWidget(empty);
        return;
    }
    const auto begin = entries.size() > 4 ? entries.size() - 4 : std::size_t{0};
    for (std::size_t i = entries.size(); i > begin; --i) {
        const auto& entry = entries[i - 1];
        const std::filesystem::path path{entry.output_path};
        // views.css:293-326 `.dlist .drow`：p8 2 + gap 8 + 发丝线分隔
        auto* row = new QFrame(artifacts_host_);
        widgets::SetKind(row, "drow");
        auto* row_lay = new QHBoxLayout(row);
        row_lay->setContentsMargins(2, 8, 2, 8);
        row_lay->setSpacing(8);
        auto* text_col = new QWidget(row);
        auto* text_col_lay = new QVBoxLayout(text_col);
        text_col_lay->setContentsMargins(0, 0, 0, 0);
        text_col_lay->setSpacing(2);
        auto* name = new QLabel(QString::fromStdString(path.filename().string()), text_col);
        widgets::SetKind(name, "statemeta");
        name->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: %1;")
                                .arg(widget::CssRgb(theme::Current().textPrimary)));
        text_col_lay->addWidget(name);
        auto* sub = new QLabel(QString::fromStdString(pipeline::StageCode(entry.stage)), text_col);
        widgets::SetKind(sub, "statemeta"); // .tiny dim
        text_col_lay->addWidget(sub);
        row_lay->addWidget(text_col, 1);
        auto* chevron = new QLabel(QStringLiteral("›"), row);
        widgets::SetKind(chevron, "stateicon"); // muted 12px
        row_lay->addWidget(chevron, 0, Qt::AlignVCenter);
        row->setToolTip(QString::fromStdString(entry.output_path));
        artifacts_body_->addWidget(row);
    }
}

void PipelineWorkspace::SetContext(std::filesystem::path root) {
    root_ = std::move(root);
    if (running_) {
        // worker 正在用 runner_，换项目等这轮跑完再 Configure
        pending_root_ = root_;
        has_pending_root_ = true;
        return;
    }
    runner_->Configure(root_, pipeline::RunMode::Auto,
                       [](pipeline::StageId, const std::string&, std::string&) { return true; },
                       [](pipeline::StageId stage) { return pipeline::StageCode(stage) + ":hash"; });
    finished_ = false;
    finished_codes_.clear();
    running_stage_ = runner_->CurrentStage();
    SyncSnapshot();
    RefreshRunState();
}

void PipelineWorkspace::LoadMock() {
    // verify 的 P09Checks / P09Review / P10Review 在调用后立刻读 Probe()，必须同步返回。
    if (root_.empty()) {
        SetContext(std::filesystem::temp_directory_path() /
                   ("shinetv-p09-" + shine::util::RandomHex(5)));
    }
    const auto result = runner_->RunAll();
    chapters_shown_ = 3;
    gantt_->SetChapters(chapters_shown_);
    for (int chapter = 0; chapter < chapters_shown_; ++chapter) {
        for (const auto& def : pipeline::AllStages()) {
            gantt_->SetStageState(chapter, def.id, QString::fromStdString(result.ok ? "完成" : "停止"));
        }
    }
    ledger_->SetLedger(runner_->LedgerLog(), runner_->Usage());
    pipeline::StopPolicy policy;
    stop_->SetDecision(policy.Evaluate(runner_->Usage(), true, true, true, true));
    stop_->SetBudget(runner_->Usage());
    // 进度口径与账本一致：已落盘账本的阶段
    finished_codes_.clear();
    for (const auto& entry : runner_->LedgerLog().Entries()) {
        finished_codes_.push_back(pipeline::StageCode(entry.stage));
    }
    running_ = false;
    finished_ = result.ok && AllChainDone();
    SyncSnapshot(); // worker 已结束，把 runner_ 的最终状态拷进 UI 快照
    RefreshRunButton();
    RefreshRunState();
}

void PipelineWorkspace::RunNext() {
    if (running_) {
        return;
    }
    StartRun(1);
}

void PipelineWorkspace::RunAll() {
    if (running_) {
        RequestStop();
        return;
    }
    StartRun(-1); // -1 = 跑到末阶段
}

void PipelineWorkspace::RequestStop() {
    if (!running_) {
        return;
    }
    // 按阶段粒度生效：worker 跑完当前阶段就收手
    stop_flag_->store(true);
    SetStatus(QStringLiteral("正在停止 · 当前阶段 %1 收尾")
                  .arg(QString::fromStdString(pipeline::StageCode(running_stage_))),
              theme::Current().statusWarn);
}

void PipelineWorkspace::StartRun(int steps) {
    if (running_) {
        return;
    }
    if (root_.empty()) {
        SetContext(std::filesystem::temp_directory_path() /
                   ("shinetv-run-" + shine::util::RandomHex(5)));
    }
    running_ = true;
    finished_ = false;
    stop_flag_->store(false);
    running_stage_ = snap_stage_;
    // 甘特上先点亮当前阶段：设计稿的 `.gcell.run`（进行中）就是这一格
    gantt_->SetStageState(0, running_stage_, QStringLiteral("进行"));
    RefreshRunButton();
    RefreshStageFlow();
    RefreshRunState();

    const auto alive = alive_;
    const auto stop_flag = stop_flag_;
    const auto runner = runner_;
    const int total = steps < 0 ? static_cast<int>(pipeline::AllStages().size()) : steps;
    async::RunOnWorker([this, alive, stop_flag, runner, total] {
        bool failed = false;
        bool user_stop = false;
        std::string reason;
        for (int i = 0; i < total; ++i) {
            if (!alive->load() || stop_flag->load()) {
                user_stop = !failed;
                break;
            }
            const auto result = runner->RunNext();
            // 快照：worker 写 runner_，UI 只拿到副本，避免并发读
            const auto budget = runner->Usage();
            const auto entries = runner->LedgerLog().Entries();
            async::PostToUi([this, alive, stage = result.stage, ok = result.ok, stopped = result.stopped,
                             stage_reason = result.reason, budget, entries] {
                if (!alive->load()) {
                    return;
                }
                OnStageSnapshot(stage, ok, stopped, stage_reason, budget, entries);
            });
            if (!result.ok) {
                failed = true;
                reason = result.reason;
                break;
            }
            if (result.executed == 0 && result.reused == 0) {
                break; // 已越过末阶段
            }
        }
        async::PostToUi([this, alive, failed, user_stop, reason] {
            if (!alive->load()) {
                return;
            }
            running_ = false;
            finished_ = !failed && !user_stop && AllChainDone();
            SyncSnapshot(); // worker 收工，此刻读 runner_ 才是安全的
            RefreshRunButton();
            if (user_stop) {
                SetStatus(QStringLiteral("已停止 · 停在 %1")
                              .arg(QString::fromStdString(pipeline::StageCode(snap_stage_))),
                          theme::Current().statusWarn);
            } else if (failed) {
                SetStatus(QStringLiteral("已停止：%1").arg(QString::fromStdString(reason)),
                          theme::Current().statusDanger);
            }
            RefreshRunState();
            if (has_pending_root_) {
                const auto next = pending_root_;
                pending_root_.clear();
                has_pending_root_ = false;
                SetContext(next);
            }
        });
    });
}

void PipelineWorkspace::OnStageSnapshot(pipeline::StageId stage, bool ok, bool stopped,
                                        std::string reason, pipeline::Budget budget,
                                        std::vector<pipeline::LedgerEntry> entries) {
    running_stage_ = stage;
    snap_stage_ = stage;
    snap_budget_ = budget;
    snap_entries_ = entries;
    if (ok && !stopped) {
        const auto code = pipeline::StageCode(stage);
        if (std::find(finished_codes_.begin(), finished_codes_.end(), code) == finished_codes_.end()) {
            finished_codes_.push_back(code);
        }
        gantt_->SetStageState(0, stage, QStringLiteral("完成"));
    } else if (stopped || !ok) {
        gantt_->SetStageState(0, stage, QStringLiteral("停止"));
    }
    ledger_->SetLedger(snap_entries_, snap_budget_);
    stop_->SetBudget(snap_budget_);
    RefreshRunState();
    if (!ok && !reason.empty()) {
        SetStatus(QStringLiteral("已停止：%1").arg(QString::fromStdString(reason)),
                  theme::Current().statusDanger);
    }
}

QString PipelineWorkspace::Probe() const {
    return QStringLiteral("runner=%1; gantt=%2; ledger=%3; stop=%4")
        .arg(QString::fromStdString(runner_->Probe()), GanttProbe(), LedgerProbe(), StopProbe());
}

QString PipelineWorkspace::GanttProbe() const { return gantt_->Probe(); }
QString PipelineWorkspace::LedgerProbe() const { return ledger_->Probe(); }
QString PipelineWorkspace::StopProbe() const { return stop_->Probe(); }

} // namespace shine::app
