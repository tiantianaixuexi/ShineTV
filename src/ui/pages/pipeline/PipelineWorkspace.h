#pragma once
// 总控台（P09）：一句话到成片的全流程视图。
//
// 结构对齐 webui 设计稿（Overview.jsx + views.css:408-507 / :132-158）：
//   页首 .vw-head（20px accent 字形 + 18px/w800 标题 + 12px muted 副标题 + 按钮，gap 14）
//   → 运行状态卡（结论 / 百分比 / 进度条 / T 链阶段流，横向可滚）
//   → KPI 行 .kpis（章节、LLM 调用、估算成本、镜头、阶段产物；gap 14，卡内右上角装饰圆）
//   → 两列 .grid-3-1（minmax(0,1fr) 300px / gap 16 / 顶端对齐）
//     左 = 全书甘特 + 账本；右 = 停止条件 + 最近产物 + 运行信息
//
// 运行模型（AGENTS.md「UI 线程不做同步 IO」）：
//   `一键全流程` / `运行下一阶段` 把阶段执行放进 worker（阶段产物要落盘），
//   每跑完一个阶段经 async::PostToUi 回投一份**快照**再刷新 UI —— UI 线程
//   在运行期间只读快照，不碰 runner_。停止按阶段粒度生效，因此不需要
//   shine_core 侧新增取消接口（Runner 不在本轮可改范围内）。
//   `LoadMock()` 保留同步语义：verify 的 P09Checks / P09Review / P10Review
//   在调用它之后立刻读 Probe()，改成异步会让断言读到空值。
#include "pipeline/Runner.h"

#include <QWidget>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class QFrame;
class QLabel;
class QProgressBar;
class QVBoxLayout;

namespace shine::data {
class StageFlow;
class StatTile;
class KeyValue;
} // namespace shine::data
namespace shine::widgets {
class Button;
class SectionCard;
} // namespace shine::widgets

namespace shine::app {

class GanttView;
class LedgerView;
class StopReportView;

class PipelineWorkspace : public QWidget {
  public:
    explicit PipelineWorkspace(QWidget* parent = nullptr);
    ~PipelineWorkspace() override;
    void LoadMock();
    void SetContext(std::filesystem::path root);
    void RunNext();
    void RunAll();
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] QString GanttProbe() const;
    [[nodiscard]] QString LedgerProbe() const;
    [[nodiscard]] QString StopProbe() const;

    [[nodiscard]] GanttView* Gantt() const noexcept { return gantt_; }
    [[nodiscard]] LedgerView* Ledger() const noexcept { return ledger_; }
    [[nodiscard]] StopReportView* StopReport() const noexcept { return stop_; }

  protected:
    void changeEvent(QEvent* ev) override; // 换肤后重建取自 token 的装饰圆

  private:
    void BuildUi();
    void SyncSnapshot();      // 空闲时把 runner_ 的状态拷进 UI 侧快照
    void RefreshRunState();  // 运行状态卡 + KPI + 运行信息 + 最近产物（全部读快照）
    void RefreshStageFlow(); // T 链阶段流按已落盘阶段上色
    void RefreshArtifacts(); // 右列「最近产物」：账本里最近几条产物路径
    void RefreshRunButton(); // 运行中 = 停止（danger），空闲 = 一键全流程（primary）
    void RefreshKpi();       // 五个统计块：数值 + k-foot 真实口径
    void SetStatus(const QString& text, std::uint32_t color_token);
    [[nodiscard]] int ChainPercent() const;
    // T 链是否已全部落盘（finished_ 的判据，不看「循环跑完了没有」）
    [[nodiscard]] bool AllChainDone() const;

    // 运行链路（worker 执行，UI 线程只回投快照刷新）
    void StartRun(int steps);
    void RequestStop();
    void OnStageSnapshot(pipeline::StageId stage, bool ok, bool stopped, std::string reason,
                         pipeline::Budget budget, std::vector<pipeline::LedgerEntry> entries);

    std::filesystem::path root_;
    std::shared_ptr<pipeline::Runner> runner_;
    // 生命周期与停止标志走 shared_ptr：worker 任务持有它们，控件析构后
    // worker 仍能安全判断「不要再碰 this / runner_」。
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<bool>> stop_flag_ = std::make_shared<std::atomic<bool>>(false);
    // T 链（章节阶段机）定义 = 阶段表里 chain == "text" 的那一段（T1–T17）。
    // 阶段数与阶段名都由此推导，页面里不写死 17。
    std::vector<pipeline::StageDefinition> chain_;

    GanttView* gantt_ = nullptr;
    LedgerView* ledger_ = nullptr;
    StopReportView* stop_ = nullptr;

    QLabel* status_ = nullptr;      // 运行状态卡里的一行结论
    QLabel* pct_ = nullptr;         // 百分比（mono tiny dim）
    QProgressBar* progress_ = nullptr;
    shine::data::StageFlow* stage_flow_ = nullptr;
    shine::data::KeyValue* info_ = nullptr;  // 运行信息卡
    QVBoxLayout* artifacts_body_ = nullptr;  // 右列「最近产物」列表
    QWidget* artifacts_host_ = nullptr;
    // views.css:419-430 `.kpi::after` 装饰圆（每张 KPI 卡右上角一个）
    std::vector<QFrame*> kpi_blobs_;

    int chapters_shown_ = 0;   // 甘特当前展示的章数（KPI「章节」口径）
    bool running_ = false;     // run.active：驱动按钮态 / 状态文案 / 卡片高亮
    bool finished_ = false;    // run.finished
    pipeline::StageId running_stage_ = pipeline::StageId::T1;
    // UI 侧快照：worker 跑阶段时正在写 runner_，所以界面一律读这份拷贝
    // （运行中由 PostToUi 的快照覆盖，空闲时由 SyncSnapshot 从 runner_ 同步）。
    pipeline::StageId snap_stage_ = pipeline::StageId::T1;
    pipeline::Budget snap_budget_;
    std::vector<pipeline::LedgerEntry> snap_entries_;
    // 已落盘账本的阶段码（T 链），进度百分比与阶段流上色都以此为准，
    // 与甘特 / 账本看到的口径一致。
    std::vector<std::string> finished_codes_;

    // 运行中收到换项目：先记下，跑完再切（worker 正在用 runner_，不能就地 Configure）
    std::filesystem::path pending_root_;
    bool has_pending_root_ = false;

    // KPI 行（统计块）
    shine::data::StatTile* kpi_chapters_ = nullptr;
    shine::data::StatTile* kpi_calls_ = nullptr;
    shine::data::StatTile* kpi_cost_ = nullptr;
    shine::data::StatTile* kpi_shots_ = nullptr;
    shine::data::StatTile* kpi_artifacts_ = nullptr;
    shine::widgets::Button* run_btn_ = nullptr; // 一键全流程 / 停止（同一按钮切文案与色调）
    shine::widgets::Button* next_btn_ = nullptr;
    // 甘特卡标题右侧的「最近 N 章」（Overview.jsx:75 的 extra 位）
    shine::widgets::SectionCard* gantt_card_ = nullptr;
};

} // namespace shine::app
