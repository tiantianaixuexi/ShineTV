#pragma once
// P07-S5–S7：ComfyUI 流程画布。只消费 DTO，不依赖 flow::GraphHost。
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

class QGraphicsProxyWidget;
class QPainter;
class QWidget;

namespace shine::widgets {
class Spinner;
}

namespace shine::kit {

struct FlowPort {
    std::string name;
    std::string type;
    bool input = false;
};

struct FlowCanvasNode {
    std::string id;
    std::string title;
    std::string type;
    double x = 0;
    double y = 0;
    double width = 180;
    double height = 120;
    std::vector<FlowPort> ports;
    std::string state = "todo";
    std::string control_kind;
    std::string control_value;
};

struct FlowCanvasLink {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    std::string type;
};

class FlowCanvas : public QGraphicsView {
    Q_OBJECT
  public:
    using ControlChanged = std::function<void(const std::string&, const std::string&)>;
    using SelectionChanged = std::function<void(const std::vector<std::string>&)>;

    explicit FlowCanvas(QWidget* parent = nullptr);

    void SetGraph(std::vector<FlowCanvasNode> nodes, std::vector<FlowCanvasLink> links);
    void SetNodeState(const std::string& id, const std::string& state);
    void SetControlChangedHandler(ControlChanged handler) { control_changed_ = std::move(handler); }
    void SetSelectionChangedHandler(SelectionChanged handler) { selection_changed_ = std::move(handler); }
    void SelectNode(const std::string& id);
    void ClearSelection();
    void DeleteSelected();
    void DuplicateSelected();
    void SelectAll();
    void FitView();
    void SetZoom(double zoom);
    // 自适应视图时右侧预留的像素（webui FlowCanvas 的 fitInset：出图 / 出片页
    // 都传 380 = 浮动面板 348 + 外缩 16 + 一点余量，否则节点会被压在面板底下）。
    void SetFitInset(double px) { fit_inset_ = px; }
    // 浮动缩放工具条的底边内缩（views.css .canvas-tools.bl = 16 / .bl-up = 118）。
    // 出图页没有胶片条 → 16；出片页底部有 float-strip → 118（设计稿自带这一档）。
    void SetToolsBottomInset(int px) { tools_bottom_ = px; }
    [[nodiscard]] double Zoom() const noexcept { return zoom_; }
    [[nodiscard]] std::size_t NodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t LinkCount() const noexcept { return links_.size(); }
    [[nodiscard]] std::size_t SelectedCount() const noexcept { return selected_.size(); }
    [[nodiscard]] std::vector<FlowCanvasLink> Links() const { return links_; }
    [[nodiscard]] std::vector<FlowCanvasNode> Nodes() const { return nodes_; }
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] bool HasNativeEditor() const noexcept;
    // 浮动工具条样式重刷：换肤时由内部的 ToolsStyleRefresher 回调
    void StyleTools();

  signals:
    void graphChanged();
    void selectionChanged();

  protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* event) override;

    void BuildTools();

  private:
    struct PortPoint {
        QPointF scene;
        bool valid = false;
    };
    [[nodiscard]] PortPoint PortAt(const QPointF& scenePos) const;
    [[nodiscard]] bool CanLink(const FlowCanvasLink& link) const;
    void RebuildItems();
    void NotifySelection();
    [[nodiscard]] std::string NodeIdAt(const QPointF& scenePos) const;

    std::vector<FlowCanvasNode> nodes_;
    void InstallEditor();
    std::vector<FlowCanvasLink> links_;
    std::vector<std::string> selected_;
    std::map<std::string, QGraphicsItem*> node_items_;
    ControlChanged control_changed_;
    SelectionChanged selection_changed_;
    std::string drag_id_;
    QPointF drag_offset_;
    // 背景平移（webui onBgDown/onMove 的 panning）
    bool panning_ = false;
    QPoint pan_anchor_view_;
    PortPoint wire_start_;
    double zoom_ = 1.0;
    double fit_inset_ = 0.0;
    int tools_bottom_ = 16;
    // 浮动缩放工具条（webui .canvas-tools：+ / − / 适应视图 + 运行中转圈）
    QWidget* tools_ = nullptr;
    shine::widgets::Spinner* spin_ = nullptr;
    void LayoutTools();
    void UpdateRunningIndicator();
    QGraphicsProxyWidget* editor_proxy_ = nullptr;
};

} // namespace shine::kit
