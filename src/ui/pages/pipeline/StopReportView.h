#pragma once
// 停止报告：停机原因 / 触发规则 / 预算阈值 / 恢复提示。
//
// 只画**真实存在**的东西：StopPolicy::Evaluate 的判定结果 + Budget 的实际阈值。
// 设计稿里的「停止条件 · S1–S12」清单来自 mock.js，而 src/pipeline/StopPolicy
// 实际只判定 S1–S4 预算、S5 模型前置、S6 Comfy 前置、S7 交叉复核、S8 场景门禁，
// 并没有可供枚举的规则表接口 —— 所以这里不硬凑 12 行，改画「触发规则」与
// 「S1–S4 预算」那一组**真实阈值**（预算用了多少 / 上限多少）。
#include "pipeline/Budget.h"
#include "pipeline/StopPolicy.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace shine::app {

class StopReportView : public QWidget {
  public:
    explicit StopReportView(QWidget* parent = nullptr);
    void SetDecision(pipeline::StopDecision decision);
    // S1–S4 的阈值行用真实预算（StopPolicy::Evaluate 的输入）
    void SetBudget(pipeline::Budget budget);
    // 自带标题开关：装进 SectionCard 时关掉，避免标题出现两次（同 GanttView）
    void SetOwnTitle(bool on);
    [[nodiscard]] QString Probe() const;

  private:
    void Rebuild();

    pipeline::StopDecision decision_;
    pipeline::Budget budget_;
    QLabel* title_ = nullptr;
    QLabel* status_ = nullptr; // 结论行（触发 = danger，未触发 = idle）
    QVBoxLayout* rules_ = nullptr; // S1–S4 预算阈值行
    QLabel* hint_ = nullptr;   // 恢复提示
};

} // namespace shine::app
