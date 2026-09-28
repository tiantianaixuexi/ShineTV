#pragma once
// shine::app —— 面包屑（P03 落点）：项目名 / 工作区 / 当前标签，段可点回。
#include <QFrame>
#include <QStringList>

#include <functional>

class QLabel;

namespace shine::app {

class Breadcrumb : public QFrame {
  public:
    explicit Breadcrumb(QWidget* parent = nullptr);

    void SetPath(const QStringList& crumbs);
    void SetOnPick(std::function<void(int)> cb) { on_pick_ = std::move(cb); }

  private:
    void Rebuild();

    QStringList crumbs_;
    QLabel* hint_ = nullptr; // 右侧快捷键提示（webui Shell.jsx:138）
    std::function<void(int)> on_pick_;
};

} // namespace shine::app
