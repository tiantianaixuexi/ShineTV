#include "widget/canvas/FlowCanvas.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsPathItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace shine::kit {
namespace {

class NodeItem final : public QGraphicsObject {
  public:
    NodeItem(const FlowCanvasNode& node, QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent), node_(node) {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        setAcceptHoverEvents(true);
        setZValue(1);
    }

    QRectF boundingRect() const override {
        return QRectF(0, 0, node_.width, node_.height);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = boundingRect().adjusted(1, 1, -1, -1);
        QColor fill = node_.state == "running" ? QColor(55, 83, 112)
                      : node_.state == "done" ? QColor(33, 77, 61)
                      : node_.state == "failed" ? QColor(102, 48, 55)
                                               : QColor(35, 35, 47);
        painter->setBrush(fill);
        painter->setPen(QPen(node_.state == "failed" ? QColor(235, 105, 117)
                                                    : QColor(120, 130, 160), 1.2));
        painter->drawRoundedRect(box, 8, 8);
        painter->setPen(QColor(235, 238, 245));
        QFont title = painter->font();
        title.setBold(true);
        painter->setFont(title);
        painter->drawText(QRectF(12, 8, node_.width - 24, 22), Qt::AlignLeft | Qt::AlignVCenter,
                          QString::fromStdString(node_.title));
        painter->setFont(painter->font());
        painter->setPen(QColor(170, 178, 198));
        painter->drawText(QRectF(12, 30, node_.width - 24, 18), Qt::AlignLeft | Qt::AlignVCenter,
                          QString::fromStdString(node_.type));
        for (std::size_t i = 0; i < node_.ports.size(); ++i) {
            const auto& port = node_.ports[i];
            const double y = 58.0 + static_cast<double>(i) * 17.0;
            const double x = port.input ? 0.0 : node_.width;
            painter->setBrush(port.type == "ANY" || port.type.empty() ? QColor(210, 170, 90)
                                                                         : QColor(90, 170, 210));
            painter->setPen(QPen(QColor(235, 238, 245), 1));
            painter->drawEllipse(QPointF(x, y), 4.5, 4.5);
            painter->setPen(QColor(190, 196, 212));
            const QRectF text(port.input ? 10 : 14, y - 9,
                              port.input ? node_.width - 22 : node_.width - 28, 18);
            painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                              QString::fromStdString(port.name));
        }
    }

    [[nodiscard]] const FlowCanvasNode& Data() const noexcept { return node_; }
    void SetNode(const FlowCanvasNode& node) {
        node_ = node;
        prepareGeometryChange();
        update();
    }

  private:
    FlowCanvasNode node_;
};

[[nodiscard]] QColor LinkColor(const std::string& type) {
    if (type == "MODEL" || type == "CLIP") return QColor(125, 177, 232);
    if (type == "CONDITIONING") return QColor(232, 177, 104);
    if (type == "IMAGE" || type == "LATENT") return QColor(133, 211, 153);
    return QColor(180, 150, 224);
}

[[nodiscard]] bool TypeCompatible(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == "ANY" || to == "ANY" || from == "*" || to == "*") {
        return true;
    }
    if (from == to) return true;
    return from.find(to) != std::string::npos || to.find(from) != std::string::npos;
}

} // namespace

FlowCanvas::FlowCanvas(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::NoDrag);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void FlowCanvas::SetGraph(std::vector<FlowCanvasNode> nodes, std::vector<FlowCanvasLink> links) {
    nodes_ = std::move(nodes);
    links_ = std::move(links);
    selected_.clear();
    RebuildItems();
    FitView();
    emit graphChanged();
    NotifySelection();
}

void FlowCanvas::SetNodeState(const std::string& id, const std::string& state) {
    for (auto& node : nodes_) {
        if (node.id == id) {
            node.state = state;
            if (auto item = node_items_.find(id); item != node_items_.end()) {
                static_cast<NodeItem*>(item->second)->SetNode(node);
            }
        }
    }
}

void FlowCanvas::SelectNode(const std::string& id) {
    if (std::none_of(nodes_.begin(), nodes_.end(), [&](const FlowCanvasNode& node) { return node.id == id; })) return;
    selected_.assign(1, id);
    RebuildItems();
    InstallEditor();
    NotifySelection();
    emit selectionChanged();
}

void FlowCanvas::ClearSelection() {
    selected_.clear();
    RebuildItems();
    InstallEditor();
    NotifySelection();
    emit selectionChanged();
}

void FlowCanvas::DeleteSelected() {
    if (selected_.empty()) return;
    const auto selected = selected_;
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const FlowCanvasNode& node) {
        return std::find(selected.begin(), selected.end(), node.id) != selected.end();
    }), nodes_.end());
    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const FlowCanvasLink& link) {
        return std::find(selected.begin(), selected.end(), link.from_node) != selected.end() ||
               std::find(selected.begin(), selected.end(), link.to_node) != selected.end();
    }), links_.end());
    selected_.clear();
    RebuildItems();
    emit graphChanged();
    NotifySelection();
}

void FlowCanvas::DuplicateSelected() {
    if (selected_.empty()) return;
    const auto original = selected_;
    std::vector<FlowCanvasNode> copies;
    for (const std::string& id : original) {
        const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const FlowCanvasNode& n) {
            return n.id == id;
        });
        if (it == nodes_.end()) continue;
        FlowCanvasNode copy = *it;
        copy.id = id + "-copy-" + std::to_string(copies.size() + 1);
        copy.title += " 副本";
        copy.x += 36;
        copy.y += 36;
        copies.push_back(std::move(copy));
    }
    nodes_.insert(nodes_.end(), copies.begin(), copies.end());
    RebuildItems();
    emit graphChanged();
}

void FlowCanvas::SelectAll() {
    selected_.clear();
    for (const auto& node : nodes_) selected_.push_back(node.id);
    RebuildItems();
    NotifySelection();
    emit selectionChanged();
}

void FlowCanvas::FitView() {
    if (scene()->items().isEmpty()) return;
    fitInView(scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
}

void FlowCanvas::SetZoom(double zoom) {
    zoom_ = std::clamp(zoom, 0.2, 3.0);
    setTransform(QTransform::fromScale(zoom_, zoom_));
}

FlowCanvas::PortPoint FlowCanvas::PortAt(const QPointF& scenePos) const {
    for (const auto& node : nodes_) {
        for (std::size_t i = 0; i < node.ports.size(); ++i) {
            const auto& port = node.ports[i];
            const double x = node.x + (port.input ? 0.0 : node.width);
            const double y = node.y + 58.0 + static_cast<double>(i) * 17.0;
            if (QRectF(QPointF(x, y) - QPointF(8, 8), QSizeF(16, 16)).contains(scenePos)) {
                return {QPointF(x, y), true};
            }
        }
    }
    return {};
}

std::string FlowCanvas::NodeIdAt(const QPointF& scenePos) const {
    for (const auto& node : nodes_) {
        if (QRectF(node.x, node.y, node.width, node.height).contains(scenePos)) return node.id;
    }
    return {};
}

bool FlowCanvas::CanLink(const FlowCanvasLink& link) const {
    if (link.from_node == link.to_node || link.from_node.empty() || link.to_node.empty()) return false;
    const FlowCanvasNode* from = nullptr;
    const FlowCanvasNode* to = nullptr;
    for (const auto& node : nodes_) {
        if (node.id == link.from_node) from = &node;
        if (node.id == link.to_node) to = &node;
    }
    if (from == nullptr || to == nullptr) return false;
    const FlowPort* output = nullptr;
    const FlowPort* input = nullptr;
    for (const auto& port : from->ports) {
        if (!port.input && port.name == link.from_port) output = &port;
    }
    for (const auto& port : to->ports) {
        if (port.input && port.name == link.to_port) input = &port;
    }
    return output != nullptr && input != nullptr && TypeCompatible(output->type, input->type);
}

void FlowCanvas::InstallEditor() {
    if (editor_proxy_ != nullptr) {
        scene()->removeItem(editor_proxy_);
        delete editor_proxy_;
        editor_proxy_ = nullptr;
    }
    if (selected_.size() != 1) return;
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const FlowCanvasNode& node) {
        return node.id == selected_.front();
    });
    if (it == nodes_.end() || it->control_kind.empty()) return;
    QWidget* editor = nullptr;
    if (it->control_kind == "check") {
        auto* check = new QCheckBox;
        check->setChecked(it->control_value == "true");
        connect(check, &QCheckBox::toggled, this, [this, id = it->id](bool value) {
            if (control_changed_) control_changed_(id, value ? "true" : "false");
        });
        editor = check;
    } else if (it->control_kind == "combo") {
        auto* combo = new QComboBox;
        combo->addItems({"euler", "dpmpp_2m", "ddim"});
        combo->setCurrentText(QString::fromStdString(it->control_value));
        connect(combo, &QComboBox::currentTextChanged, this, [this, id = it->id](const QString& value) {
            if (control_changed_) control_changed_(id, value.toStdString());
        });
        editor = combo;
    } else if (it->control_kind == "slider") {
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        slider->setValue(std::max(0, std::min(100, std::atoi(it->control_value.c_str()))));
        connect(slider, &QSlider::valueChanged, this, [this, id = it->id](int value) {
            if (control_changed_) control_changed_(id, std::to_string(value));
        });
        editor = slider;
    } else {
        auto* line = new QLineEdit(QString::fromStdString(it->control_value));
        connect(line, &QLineEdit::textChanged, this, [this, id = it->id](const QString& value) {
            if (control_changed_) control_changed_(id, value.toStdString());
        });
        editor = line;
    }
    editor_proxy_ = new QGraphicsProxyWidget;
    editor_proxy_->setWidget(editor);
    editor_proxy_->setGeometry(QRectF(it->x + 12, it->y + it->height + 6, it->width - 24, 28));
    scene()->addItem(editor_proxy_);
}

void FlowCanvas::RebuildItems() {
    editor_proxy_ = nullptr;
    scene()->clear();
    node_items_.clear();
    for (const auto& node : nodes_) {
        auto* item = new NodeItem(node);
        item->setPos(node.x, node.y);
        item->setData(0, QString::fromStdString(node.id));
        scene()->addItem(item);
        node_items_.emplace(node.id, item);
        if (std::find(selected_.begin(), selected_.end(), node.id) != selected_.end()) item->setSelected(true);
    }
    for (const auto& link : links_) {
        const auto from = std::find_if(nodes_.begin(), nodes_.end(), [&](const FlowCanvasNode& n) {
            return n.id == link.from_node;
        });
        const auto to = std::find_if(nodes_.begin(), nodes_.end(), [&](const FlowCanvasNode& n) {
            return n.id == link.to_node;
        });
        if (from == nodes_.end() || to == nodes_.end()) continue;
        QPointF p1;
        QPointF p2;
        for (std::size_t i = 0; i < from->ports.size(); ++i) {
            if (!from->ports[i].input && from->ports[i].name == link.from_port) {
                p1 = QPointF(from->x + from->width, from->y + 58 + i * 17);
            }
        }
        for (std::size_t i = 0; i < to->ports.size(); ++i) {
            if (to->ports[i].input && to->ports[i].name == link.to_port) {
                p2 = QPointF(to->x, to->y + 58 + i * 17);
            }
        }
        QPainterPath path(p1);
        const double dx = std::max(40.0, std::abs(p2.x() - p1.x()) * 0.45);
        path.cubicTo(p1 + QPointF(dx, 0), p2 - QPointF(dx, 0), p2);
        auto* wire = new QGraphicsPathItem(path);
        wire->setPen(QPen(LinkColor(link.type), 2.0));
        wire->setZValue(0);
        scene()->addItem(wire);
    }
}

void FlowCanvas::NotifySelection() {
    if (selection_changed_) selection_changed_(selected_);
}

void FlowCanvas::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const QPointF anchor = mapToScene(event->position().toPoint());
    const double next = std::clamp(zoom_ * factor, 0.2, 3.0);
    const double ratio = next / zoom_;
    setTransform(QTransform::fromScale(next, next));
    const QPointF delta = anchor - mapToScene(event->position().toPoint());
    translate(delta.x() * ratio, delta.y() * ratio);
    zoom_ = next;
    event->accept();
}

void FlowCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const QPointF pos = mapToScene(event->pos());
    const PortPoint port = PortAt(pos);
    if (port.valid) {
        for (const auto& node : nodes_) {
            for (std::size_t i = 0; i < node.ports.size(); ++i) {
                const double x = node.x + (node.ports[i].input ? 0 : node.width);
                const double y = node.y + 58 + i * 17;
                if (QRectF(QPointF(x, y) - QPointF(8, 8), QSizeF(16, 16)).contains(pos) &&
                    !node.ports[i].input) {
                    wire_start_ = {QPointF(x, y), true};
                    return;
                }
            }
        }
    }
    const std::string id = NodeIdAt(pos);
    if (id.empty()) {
        ClearSelection();
        return;
    }
    if (!event->modifiers().testFlag(Qt::ShiftModifier)) selected_.clear();
    if (std::find(selected_.begin(), selected_.end(), id) == selected_.end()) selected_.push_back(id);
    drag_id_ = id;
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const FlowCanvasNode& n) { return n.id == id; });
    if (it != nodes_.end()) drag_offset_ = pos - QPointF(it->x, it->y);
    RebuildItems();
    NotifySelection();
    emit selectionChanged();
}

void FlowCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (wire_start_.valid) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    if (drag_id_.empty()) return;
    const QPointF pos = mapToScene(event->pos()) - drag_offset_;
    for (auto& node : nodes_) {
        if (node.id == drag_id_) {
            node.x = std::round(pos.x() / 8.0) * 8.0;
            node.y = std::round(pos.y() / 8.0) * 8.0;
            if (auto item = node_items_.find(drag_id_); item != node_items_.end()) {
                item->second->setPos(node.x, node.y);
            }
        }
    }
    RebuildItems();
}

void FlowCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (wire_start_.valid && event->button() == Qt::LeftButton) {
        const QPointF pos = mapToScene(event->pos());
        const PortPoint target = PortAt(pos);
        if (target.valid) {
            for (const auto& node : nodes_) {
                for (std::size_t i = 0; i < node.ports.size(); ++i) {
                    const double x = node.x + (node.ports[i].input ? 0 : node.width);
                    const double y = node.y + 58 + i * 17;
                    if (node.ports[i].input &&
                        QRectF(QPointF(x, y) - QPointF(8, 8), QSizeF(16, 16)).contains(pos)) {
                        FlowCanvasLink link;
                        for (const auto& source : nodes_) {
                            for (std::size_t j = 0; j < source.ports.size(); ++j) {
                                const double sx = source.x + (source.ports[j].input ? 0 : source.width);
                                const double sy = source.y + 58 + j * 17;
                                if (!source.ports[j].input &&
                                    QRectF(QPointF(sx, sy) - QPointF(8, 8), QSizeF(16, 16)).contains(wire_start_.scene)) {
                                    link.from_node = source.id;
                                    link.from_port = source.ports[j].name;
                                    link.type = source.ports[j].type;
                                }
                            }
                        }
                        link.to_node = node.id;
                        link.to_port = node.ports[i].name;
                        if (CanLink(link)) {
                            const bool duplicate = std::any_of(links_.begin(), links_.end(), [&](const FlowCanvasLink& l) {
                                return l.from_node == link.from_node && l.from_port == link.from_port &&
                                       l.to_node == link.to_node && l.to_port == link.to_port;
                            });
                            if (!duplicate) links_.push_back(std::move(link));
                        }
                    }
                }
            }
        }
        wire_start_ = {};

        RebuildItems();
        emit graphChanged();
    }
    drag_id_.clear();
    QGraphicsView::mouseReleaseEvent(event);
}

bool FlowCanvas::HasNativeEditor() const noexcept {
    return editor_proxy_ != nullptr && editor_proxy_->widget() != nullptr;
}

void FlowCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        DeleteSelected();
        return;
    }
    if (event->key() == Qt::Key_A && event->modifiers().testFlag(Qt::ControlModifier)) {
        SelectAll();
        return;
    }
    if (event->key() == Qt::Key_D && event->modifiers().testFlag(Qt::ControlModifier)) {
        DuplicateSelected();
        return;
    }
    if (event->key() == Qt::Key_0 && event->modifiers().testFlag(Qt::ControlModifier)) {
        FitView();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

QString FlowCanvas::Probe() const {
    return QStringLiteral("nodes=%1; links=%2; selected=%3; zoom=%4")
        .arg(static_cast<int>(nodes_.size()))
        .arg(static_cast<int>(links_.size()))
        .arg(static_cast<int>(selected_.size()))
        .arg(zoom_, 0, 'f', 2);
}

} // namespace shine::kit
