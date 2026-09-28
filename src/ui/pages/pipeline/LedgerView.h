#pragma once
#include "pipeline/Ledger.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace shine::app {

class LedgerView : public QWidget {
  public:
    explicit LedgerView(QWidget* parent = nullptr);
    void SetLedger(const pipeline::Ledger& ledger, const pipeline::Budget& budget);
    [[nodiscard]] QString Probe() const;

  private:
    int entries_ = 0;
    int calls_ = 0;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace shine::app
