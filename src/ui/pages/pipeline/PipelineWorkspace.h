#pragma once
// 总控台（P09）：一句话到成片的全流程视图。
//
// 结构对齐 webui 设计稿（webui/src/views/Overview.jsx）：
//   页首（标题 + 副标题 + 运行按钮）
//   → 运行状态卡（当前阶段 / 百分比进度 / T1–T17 阶段流）
//   → KPI 行（章节、产物、LLM 调用、镜头、成本）
//   → 两列分区网格：左 = 全书甘特 + 账本；右 = 停止条件 + 运行信息
//
// 三个子视图（GanttView / LedgerView / StopReportView）保持原有公开接口：
// 验收探针 P09Checks、P09Review、P10Review 直接依赖 Probe() 与对象指针，
// 这次只改摆放位置与外层结构，不动它们的行为。
#include "pipeline/Runner.h"

#include <QWidget>

#include <filesystem>

class QLabel;
class QProgressBar;

namespace shine::data {
class StageFlow;
class StatTile;
class KeyValue;
} // namespace shine::data
namespace shine::widgets {
class Button;
} // namespace shine::widgets

namespace shine::app {

class GanttView;
class LedgerView;
class StopReportView;

class PipelineWorkspace : public QWidget {
  public:
    explicit PipelineWorkspace(QWidget* parent = nullptr);
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

  private:
    void BuildUi();
    void RefreshRunState();   // 运行状态卡 + KPI：阶段流、进度、五个统计块
    void RefreshStageFlow();  // T1–T17 阶段流按 runner 当前阶段上色
    [[nodiscard]] int CurrentChapterIndex() const;

    std::filesystem::path root_;
    pipeline::Runner runner_;
    GanttView* gantt_ = nullptr;
    LedgerView* ledger_ = nullptr;
    StopReportView* stop_ = nullptr;

    QLabel* status_ = nullptr;      // 运行状态卡里的一行结论
    QLabel* pct_ = nullptr;         // 百分比
    QProgressBar* progress_ = nullptr;
    shine::data::StageFlow* stage_flow_ = nullptr;
    shine::data::KeyValue* info_ = nullptr;  // 运行信息卡
    int chapters_shown_ = 0;        // 甘特当前展示的章数（KPI「章节」口径）

    // KPI 行（统计块）
    shine::data::StatTile* kpi_chapters_ = nullptr;
    shine::data::StatTile* kpi_artifacts_ = nullptr;
    shine::data::StatTile* kpi_calls_ = nullptr;
    shine::data::StatTile* kpi_shots_ = nullptr;
    shine::data::StatTile* kpi_cost_ = nullptr;
    shine::widgets::Button* run_btn_ = nullptr; // 一键全流程 / 停止（同一按钮切文案）
};

} // namespace shine::app
