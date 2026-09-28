#pragma once
// P07-S8：镜头字段到 Comfy 节点参数的绑定编辑器。
#include "flow/FlowBinder.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace shine::app {

class BindingView : public QWidget {
  public:
    explicit BindingView(QWidget* parent = nullptr);
    void SetShotContext(const flow::BindingShotContext& shot);
    void AddDefaultBindings();
    [[nodiscard]] flow::BindingShotContext ShotContext() const noexcept { return shot_; }
    [[nodiscard]] QString Probe() const;

    void ValidateNow();
  private:
    void Rebuild();
    void Validate();

    flow::FlowBinder binder_;
    flow::BindingShotContext shot_;
    // 密集行列表（webui .dlist）：行由 Rebuild 逐个建、逐个销毁
    QWidget* list_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
