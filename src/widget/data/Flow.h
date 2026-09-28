#pragma once
// shine::data —— 流程/时序展示（P02-S6，UI.md §2.2）：Timeline / StageFlow。
#include "widget/controls/WidgetCommon.h"

#include <functional>
#include <vector>

#include <QFrame>
#include <QScrollArea>
#include <QString>
#include <QWidget>

namespace shine::data {

// Timeline —— 垂直 / 水平；事件卡片；当前项高亮
class Timeline : public QFrame {
  public:
    struct Event {
        QString time;
        QString title;
        QString detail;
    };

    explicit Timeline(Qt::Orientation o = Qt::Vertical, QWidget* parent = nullptr);

    void SetEvents(std::vector<Event> events);
    void SetCurrent(int index); // 当前项高亮（左侧 accent 点 + 卡片抬起）
    [[nodiscard]] int Current() const { return current_; }
    void SetOnPick(std::function<void(int)> cb) { on_pick_ = std::move(cb); }

  private:
    void Rebuild();

    Qt::Orientation orientation_;
    std::vector<Event> events_;
    int current_ = -1;
    std::function<void(int)> on_pick_;
    QScrollArea* scroll_ = nullptr;
};

// StageFlow —— 阶段流水线图：节点状态 todo/running/done/failed/skipped（5 态）；
// 连线表示先后；分支表示重试/修复支线
class StageFlow : public QWidget {
  public:
    enum class NodeState { Todo, Running, Done, Failed, Skipped };

    struct Node {
        QString id;
        QString title;
        NodeState state = NodeState::Todo;
        QString branchOf; // 非空 = 该节点是 branchOf 的支线（重试/修复）
    };
    struct Link {
        QString from;
        QString to;
    };

    explicit StageFlow(QWidget* parent = nullptr);

    void SetGraph(std::vector<Node> nodes, std::vector<Link> links);
    void SetNodeState(const QString& id, NodeState s);
    [[nodiscard]] NodeState StateOf(const QString& id) const;
    void SetOnPick(std::function<void(const QString& id)> cb) { on_pick_ = std::move(cb); }
    [[nodiscard]] QString NodeAt(const QPointF& pos) const; // 命中检测（自绘交互）

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    QSize sizeHint() const override;

  private:
    [[nodiscard]] QRectF BoxOf(std::size_t i) const;

    std::vector<Node> nodes_;
    std::vector<Link> links_;
    std::function<void(const QString&)> on_pick_;
};

} // namespace shine::data
