#pragma once
#include "flow/VideoChain.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace shine::app {

class ChainView : public QWidget {
  public:
    explicit ChainView(QWidget* parent = nullptr);
    void SetChain(flow::VideoChain chain);
    [[nodiscard]] QString Probe() const;

  private:
    void Rebuild();
    flow::VideoChain chain_;
    // 密集行列表（webui .dlist）：行由 Rebuild 逐个建、逐个销毁
    QWidget* list_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
