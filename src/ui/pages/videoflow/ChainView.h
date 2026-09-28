#pragma once
#include "flow/VideoChain.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace shine::app {

class ChainView : public QWidget {
  public:
    explicit ChainView(QWidget* parent = nullptr);
    void SetChain(flow::VideoChain chain);
    [[nodiscard]] QString Probe() const;

  private:
    void Rebuild();
    flow::VideoChain chain_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
