#pragma once
// shine::app —— 面包屑（P03 落点）：项目名 / 工作区 / 当前标签，段可点回。
#include <QFrame>
#include <QStringList>

#include <functional>

namespace shine::app {

class Breadcrumb : public QFrame {
  public:
    explicit Breadcrumb(QWidget* parent = nullptr);

    void SetPath(const QStringList& crumbs);
    void SetOnPick(std::function<void(int)> cb) { on_pick_ = std::move(cb); }

  private:
    void Rebuild();

    QStringList crumbs_;
    std::function<void(int)> on_pick_;
};

} // namespace shine::app
