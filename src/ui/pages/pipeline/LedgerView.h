#pragma once
// 账本：阶段 / 输入哈希 / 产物 / 降级（数据来自 pipeline::Ledger + Budget，全为真实值）。
#include "pipeline/Budget.h"
#include "pipeline/Ledger.h"

#include <QWidget>

#include <vector>

class QLabel;
class QTableWidget;

namespace shine::widgets {
class EmptyState;
} // namespace shine::widgets

namespace shine::app {

class LedgerView : public QWidget {
  public:
    explicit LedgerView(QWidget* parent = nullptr);
    void SetLedger(const pipeline::Ledger& ledger, const pipeline::Budget& budget);
    // 运行中从 worker 快照回投（Runner 在 worker 上被写，UI 只能拿副本）
    void SetLedger(const std::vector<pipeline::LedgerEntry>& entries, const pipeline::Budget& budget);
    // 自带标题开关：装进 SectionCard 时关掉，避免标题出现两次（同 GanttView）
    void SetOwnTitle(bool on);
    [[nodiscard]] QString Probe() const;

  private:
    void Render();
    void RefreshEmpty();

    int entries_ = 0;
    int calls_ = 0;
    QLabel* title_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;  // 表格下方的汇总脚注（views.css:112 tiny dim）
    shine::widgets::EmptyState* empty_ = nullptr;
    std::vector<pipeline::LedgerEntry> rows_;
    pipeline::Budget budget_;
};

} // namespace shine::app
