#include "ui/kit/canvas/FlowCanvas.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
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
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace shine::kit {
namespace {

// webui views.css .fnode 的几何常量：w150 / r10 / head p7 10 / body p7 10 9 / port ⌀9。
inline constexpr double kNodeWidth = 150.0;
inline constexpr double kNodeHeadH = 30.0;
inline constexpr double kPortTop = 40.0;
inline constexpr double kPortPitch = 18.0;
inline constexpr double kNodeRadius = 10.0;

// 自绘控件统一走 theme token（禁止 QColor(55,83,112) 这类字面色值 —— 四套主题
// 里画布节点之前是同一个颜色，切主题时节点根本不变）。
[[nodiscard]] QColor TokenBgPanel() { return widgets::TokenQColor(shine::theme::Current().bgPanel); }
[[nodiscard]] QColor TokenLineNormal() { return widgets::TokenQColor(shine::theme::Current().lineNormal); }
[[nodiscard]] QColor TokenAccent() { return widgets::TokenQColor(shine::theme::Current().accentPrimary); }
[[nodiscard]] QColor TokenAccent2() { return widgets::TokenQColor(shine::theme::Current().accentSecondary); }
[[nodiscard]] QColor TokenInfo() { return widgets::TokenQColor(shine::theme::Current().accentInfo); }
[[nodiscard]] QColor TokenOk() { return widgets::TokenQColor(shine::theme::Current().statusOk); }
[[nodiscard]] QColor TokenBusy() { return widgets::TokenQColor(shine::theme::Current().statusBusy); }
[[nodiscard]] QColor TokenDanger() { return widgets::TokenQColor(shine::theme::Current().statusDanger); }
[[nodiscard]] QColor TokenTextPrimary() { return widgets::TokenQColor(shine::theme::Current().textPrimary); }
[[nodiscard]] QColor TokenTextMuted() { return widgets::TokenQColor(shine::theme::Current().textMuted); }
[[nodiscard]] QColor TokenFillSelected() { return widgets::TokenQColor(shine::theme::Current().fillSelected); }
[[nodiscard]] QColor TokenFillMuted() { return widgets::TokenQColor(shine::theme::Current().fillMuted); }

// webui 的 color-mix(in srgb, C 45%, transparent) 在 QSS/Qt 没有对应声明，
// 这里按同一权重把状态色压到 bgPanel 上（45% = done、50% = fail、18% = run 辉光），
// 与 MainWindow.cpp 的 Blend() 同一口径，只是这里只需要「朝面板底色压」。
[[nodiscard]] QColor Mix(const QColor& color, const QColor& base, double weight) {
    const auto channel = [&](int shift) {
        const double from = static_cast<double>((color.red() >> shift) & 0xFF);
        const double to = static_cast<double>((base.red() >> shift) & 0xFF);
        return static_cast<int>(std::lround(from * weight + to * (1.0 - weight)));
    };
    return QColor(channel(16), channel(8), channel(0));
}

// 节点状态 → 边框色 + 底色 + 辉光色（webui .fnode.done/.run/.fail）。
struct NodeSkin {
    QColor border;
    QColor fill;
    QColor glow; // 3px 外圈（webui box-shadow 0 0 0 3px）
};

[[nodiscard]] NodeSkin SkinFor(const std::string& state) {
    const QColor panel = TokenBgPanel();
    if (state == "running") return {TokenBusy(), panel, Mix(TokenBusy(), panel, 0.18)};
    if (state == "done") return {Mix(TokenOk(), panel, 0.45), panel, QColor()};
    if (state == "failed") return {Mix(TokenDanger(), panel, 0.50), panel, QColor()};
    return {TokenLineNormal(), panel, QColor()};
}

class NodeItem final : public QGraphicsObject {
  public:
    NodeItem(const FlowCanvasNode& node, QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent), node_(node) {
        node_.width = kNodeWidth;
        node_.height = NodeHeightFor(node_);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        setAcceptHoverEvents(true);
        setZValue(1);
        // 画布默认选中框（虚线矩形）与设计稿的 accent 实心描边冲突，直接关掉，
        // 选中态改由 paint() 自己画（views.css .fnode.sel）。
        setFlag(QGraphicsItem::ItemIsFocusable, false);
    }

    void SetSelected(bool on) {
        if (selected_ == on) return;
        selected_ = on;
        update();
    }

    [[nodiscard]] static double NodeHeightFor(const FlowCanvasNode& node) {
        const double ports = static_cast<double>(node.ports.size());
        return std::max(kNodeHeadH + 14.0, kNodeHeadH + ports * kPortPitch + 10.0);
    }

    QRectF boundingRect() const override {
        return QRectF(0, 0, node_.width, node_.height);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = boundingRect().adjusted(1, 1, -1, -1);
        NodeSkin skin = SkinFor(node_.state);
        const bool hovered = option != nullptr && (option->state & QStyle::State_MouseOver);
        // 优先级：选中 > hover > 状态（views.css 里 .fnode.sel / :hover 排在状态态之前）
        if (selected_) {
            skin.border = TokenAccent();
            skin.glow = Mix(TokenAccent(), TokenFillMuted(), 0.34); // ≈ --accent-dim
        } else if (hovered) {
            skin.border = widgets::TokenQColor(shine::theme::Current().lineStrong);
        }

        // views.css .fnode.sel / .run：box-shadow 0 0 0 3px <状态辉光>
        if (skin.glow.isValid()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(skin.glow);
            painter->drawRoundedRect(box.adjusted(-3, -3, 3, 3), kNodeRadius + 3, kNodeRadius + 3);
        }

        // webui .fnode：bg-panel + 1.5px 边 + r-md(10)
        painter->setBrush(skin.fill);
        painter->setPen(QPen(skin.border, selected_ ? 2.0 : 1.5));
        painter->drawRoundedRect(box, kNodeRadius, kNodeRadius);

        // 标题带：accent 图标 + 12px w700 标题 + 底部发丝线（.fhead p7 10）
        const QFont base = painter->font();
        QFont head = base;
        head.setPixelSize(12);
        head.setWeight(QFont::DemiBold);
        painter->setFont(head);
        painter->setPen(TokenAccent());
        painter->drawText(QRectF(10, 0, 14, kNodeHeadH), Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("◆"));
        // 右侧状态记号（.fhead 尾部 check / spin）：done 打勾、failed 打叉；
        // running 的转圈在浮动工具条上（见 FlowCanvas::BuildTools）
        if (node_.state == "done" || node_.state == "failed") {
            painter->setPen(node_.state == "done" ? TokenOk() : TokenDanger());
            painter->drawText(QRectF(node_.width - 24, 0, 14, kNodeHeadH),
                              Qt::AlignRight | Qt::AlignVCenter,
                              node_.state == "done" ? QStringLiteral("✓") : QStringLiteral("✕"));
        }
        painter->setPen(TokenTextPrimary());
        const int title_w = static_cast<int>(node_.width) - 38;
        painter->drawText(QRectF(24, 0, title_w, kNodeHeadH), Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(QString::fromStdString(node_.title),
                                                           Qt::ElideRight, title_w));
        painter->setPen(QPen(TokenFillMuted(), 1));
        painter->drawLine(QPointF(0, kNodeHeadH), QPointF(node_.width, kNodeHeadH));

        // 端口行：11px muted 名称 + 9px 圆点（.port：⌀9 / 2px 描边 / 状态色）
        QFont body = base;
        body.setPixelSize(11);
        painter->setFont(body);
        for (std::size_t i = 0; i < node_.ports.size(); ++i) {
            const auto& port = node_.ports[i];
            const double y = kPortTop + static_cast<double>(i) * kPortPitch;
            const double x = port.input ? 0.0 : node_.width;
            QColor port_color = port.type == "ANY" || port.type.empty()
                                    ? TokenAccent2()
                                    : TokenInfo();
            if (node_.state == "done") port_color = TokenOk();
            if (node_.state == "running") port_color = TokenBusy();
            if (node_.state == "failed") port_color = TokenDanger();
            painter->setPen(QPen(port_color, 2));
            painter->setBrush(TokenBgPanel());
            painter->drawEllipse(QPointF(x, y), 4.5, 4.5);
            painter->setPen(TokenTextMuted());
            const QRectF text(port.input ? 10 : 14, y - 9,
                              port.input ? node_.width - 22 : node_.width - 28, 18);
            painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(QString::fromStdString(port.name),
                                                               Qt::ElideRight,
                                                               static_cast<int>(text.width())));
        }
        painter->setFont(base);
    }

    [[nodiscard]] const FlowCanvasNode& Data() const noexcept { return node_; }
    void SetNode(const FlowCanvasNode& node) {
        node_ = node;
        node_.width = kNodeWidth;
        node_.height = NodeHeightFor(node_);
        prepareGeometryChange();
        update();
    }

  private:
    FlowCanvasNode node_;
    bool selected_ = false;
};

[[nodiscard]] QColor LinkColor(const std::string& type) {
    if (type == "MODEL" || type == "CLIP") return TokenInfo();
    if (type == "CONDITIONING") return TokenAccent2();
    if (type == "IMAGE" || type == "LATENT") return TokenOk();
    return TokenBusy();
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
    setDragMode(QGraphicsView::NoDrag); // 平移自己实现（见 pan_* 成员），节点拖拽优先
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setCursor(Qt::OpenHandCursor);
    BuildTools();
}

// 网格底纹：webui .flow-canvas 的 background 是
// radial-gradient(circle at 1px 1px, line-normal 1px, transparent 0) 0 0 / 22px 22px
// 叠在 fill-muted 上。QSS 没有 background-image，这里自绘。
// 点阵按**视口**坐标画（先复位变换）：CSS 的 background 属于宿主元素，
// 不随画布 zoom 缩放；viewport backgroundBrush 是场景坐标，会跟着缩放，故不用。
void FlowCanvas::drawBackground(QPainter* painter, const QRectF& rect) {
    // painter 的世界变换就是「场景 → 设备」，所以 rect 映射过去即视口可见区
    const QRectF view = painter->transform().mapRect(rect);
    const QTransform saved = painter->transform();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(view, TokenFillMuted());
    constexpr double kGridStep = 22.0; // 设计稿的 22px 点阵
    QColor dot = widgets::TokenQColor(shine::theme::Current().lineNormal);
    painter->setPen(QPen(dot, 1.0));
    const double x0 = std::floor(view.left() / kGridStep) * kGridStep;
    const double y0 = std::floor(view.top() / kGridStep) * kGridStep;
    for (double y = y0; y < view.top() + view.height(); y += kGridStep) {
        for (double x = x0; x < view.left() + view.width(); x += kGridStep) {
            painter->drawPoint(QPointF(x, y));
        }
    }
    painter->setTransform(saved);
}

// 换肤后重刷工具条样式：ThemeService 是纯静态类，靠 qApp 发的 ThemeChange 事件感知
// （与 WidgetCommon.cpp 的阴影重挂同一做法）。
class ToolsStyleRefresher : public QObject {
  public:
    ToolsStyleRefresher(FlowCanvas* canvas, QObject* parent) : QObject(parent), canvas_(canvas) {
        qApp->installEventFilter(this);
    }
    ~ToolsStyleRefresher() override { qApp->removeEventFilter(this); }

  protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (ev->type() == QEvent::ThemeChange && watched == qApp) canvas_->StyleTools();
        return QObject::eventFilter(watched, ev);
    }

  private:
    FlowCanvas* canvas_;
};

// 浮动缩放工具条（views.css .canvas-tools / .bl：p4 / gap4 / r-md / 左下 16）。
// 出图页在左下（bl=16），出片页底部有胶片条，用 bl-up=118 让开。
void FlowCanvas::BuildTools() {
    auto* tools = new QWidget(viewport());
    tools->setObjectName(QStringLiteral("flowCanvasTools"));
    StyleTools();
    auto* lay = new QVBoxLayout(tools);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    auto* zoom_in = new widgets::IconButton(QStringLiteral("＋"), QStringLiteral("放大"),
                                            widgets::IconButton::Size::Sm, tools);
    auto* zoom_out = new widgets::IconButton(QStringLiteral("－"), QStringLiteral("缩小"),
                                             widgets::IconButton::Size::Sm, tools);
    auto* fit = new widgets::IconButton(QStringLiteral("⊙"), QStringLiteral("适应视图"),
                                        widgets::IconButton::Size::Sm, tools);
    // .canvas-tools 里的运行指示（running 时才出现，webui 用 spin）
    auto* spin = new widgets::Spinner(widgets::Spinner::Size::Sm, tools);
    spin->setFixedWidth(20);
    spin->setVisible(false);
    lay->addWidget(zoom_in, 0, Qt::AlignHCenter);
    lay->addWidget(zoom_out, 0, Qt::AlignHCenter);
    lay->addWidget(fit, 0, Qt::AlignHCenter);
    lay->addWidget(spin, 0, Qt::AlignHCenter);
    tools->show();
    tools_ = tools;
    spin_ = spin;
    new ToolsStyleRefresher(this, this);
    connect(zoom_in, &QPushButton::clicked, this, [this] { SetZoom(zoom_ * 1.2); });
    connect(zoom_out, &QPushButton::clicked, this, [this] { SetZoom(zoom_ / 1.2); });
    connect(fit, &QPushButton::clicked, this, &FlowCanvas::FitView);
    LayoutTools();
}

// 工具条上的运行指示：任一节点处于 running 时显示转圈（webui .canvas-tools 里的 spin）
void FlowCanvas::UpdateRunningIndicator() {
    if (spin_ == nullptr) return;
    const bool running = std::any_of(nodes_.begin(), nodes_.end(), [](const FlowCanvasNode& node) {
        return node.state == "running";
    });
    spin_->setVisible(running);
    spin_->SetRunning(running);
}

// 工具条样式：色值全部来自 theme token（.canvas-tools 的 --glass 在 Qt 侧退化为
// 不透明 bg.elevated + line-subtle 细边 + r-md(10)；没有 backdrop-filter）
void FlowCanvas::StyleTools() {
    if (tools_ == nullptr) return;
    const theme::ColorToken& t = theme::Current();
    tools_->setStyleSheet(QStringLiteral("QWidget#flowCanvasTools { background-color: %1; "
                                         "border: 1px solid %2; border-radius: %3px; }")
                              .arg(shine::widget::CssRgb(t.bgElevated),
                                   shine::widget::CssRgb(t.lineSubtle))
                              .arg(theme::radius::kMd));
}

void FlowCanvas::LayoutTools() {
    if (tools_ == nullptr) return;
    tools_->adjustSize();
    tools_->move(16, std::max(16, viewport()->height() - tools_->height() - tools_bottom_));
    tools_->raise();
}

void FlowCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    LayoutTools();
}

void FlowCanvas::SetGraph(std::vector<FlowCanvasNode> nodes, std::vector<FlowCanvasLink> links) {
    nodes_ = std::move(nodes);
    links_ = std::move(links);
    selected_.clear();
    RebuildItems();
    FitView();
    LayoutTools();
    emit graphChanged();
    NotifySelection();
    UpdateRunningIndicator();
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
    UpdateRunningIndicator();
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
    // webui fitInset：出图 / 出片页都传 380（浮动面板 348 + 外缩 16 + 余量）。
    // Qt 侧等价做法是先把可视区右侧扣掉这段，再按剩余宽高算缩放。
    QRectF target = scene()->itemsBoundingRect().adjusted(-24, -24, 24, 24);
    const int avail_w = std::max(200, viewport()->width() - static_cast<int>(fit_inset_) - 48);
    const int avail_h = std::max(160, viewport()->height() - 48);
    const double z = std::min({avail_w / std::max(1.0, target.width()),
                               avail_h / std::max(1.0, target.height()), 1.15});
    SetZoom(z);
    // 水平方向按「扣掉面板后的可视区」居中，垂直方向按全高居中
    const double vis_x = (viewport()->width() - fit_inset_) / 2.0;
    const double vis_y = viewport()->height() / 2.0;
    centerOn(target.center().x() + (vis_x - viewport()->width() / 2.0) / zoom_,
             target.center().y() + (vis_y - viewport()->height() / 2.0) / zoom_);
}

void FlowCanvas::SetZoom(double zoom) {
    // webui FlowCanvas.jsx：setView 里 clamp(0.35, 2)
    zoom_ = std::clamp(zoom, 0.35, 2.0);
    setTransform(QTransform::fromScale(zoom_, zoom_));
}

FlowCanvas::PortPoint FlowCanvas::PortAt(const QPointF& scenePos) const {
    for (const auto& node : nodes_) {
        for (std::size_t i = 0; i < node.ports.size(); ++i) {
            const auto& port = node.ports[i];
            const double x = node.x + (port.input ? 0.0 : node.width);
            const double y = node.y + kPortTop + static_cast<double>(i) * kPortPitch;
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
        // 选中态由 NodeItem 自己画（accent 实心边 + 3px 辉光），
        // 不走 QGraphicsView 默认选中框 —— 那一圈虚线矩形设计稿里没有。
        item->SetSelected(std::find(selected_.begin(), selected_.end(), node.id) != selected_.end());
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
                p1 = QPointF(from->x + from->width, from->y + kPortTop + i * kPortPitch);
            }
        }
        for (std::size_t i = 0; i < to->ports.size(); ++i) {
            if (to->ports[i].input && to->ports[i].name == link.to_port) {
                p2 = QPointF(to->x, to->y + kPortTop + i * kPortPitch);
            }
        }
        QPainterPath path(p1);
        const double dx = std::max(36.0, std::abs(p2.x() - p1.x()) * 0.55);
        path.cubicTo(p1 + QPointF(dx, 0), p2 - QPointF(dx, 0), p2);
        // webui 连线是两层：底层 line-normal 5px @0.4（柔化底衬），
        // 上层 1.8px 的类型色 / 灰线。两端都不是 todo 时上层走 6 6 虚线
        // （CSS 的 flow-dash 动画在 Qt 侧没有 keyframes，只能给静态虚线）。
        const bool active = from->state != "todo" && to->state != "todo";
        auto* halo = new QGraphicsPathItem(path);
        QColor halo_color = widgets::TokenQColor(shine::theme::Current().lineNormal);
        halo_color.setAlphaF(0.4 * halo_color.alphaF());
        QPen halo_pen(halo_color, 5.0);
        halo_pen.setCapStyle(Qt::RoundCap);
        halo->setPen(halo_pen);
        halo->setZValue(0);
        scene()->addItem(halo);
        auto* wire = new QGraphicsPathItem(path);
        QPen pen(active ? LinkColor(link.type) : widgets::TokenQColor(shine::theme::Current().lineStrong),
                 1.8);
        pen.setCapStyle(Qt::RoundCap);
        if (active) {
            QList<qreal> dashes{6.0, 6.0};
            pen.setDashPattern(dashes);
            pen.setDashOffset(-6.0); // 静态斜纹的相位偏移，视觉上接近流动中的一帧
        }
        wire->setPen(pen);
        wire->setZValue(1);
        scene()->addItem(wire);
    }
}

void FlowCanvas::NotifySelection() {
    if (selection_changed_) selection_changed_(selected_);
}

void FlowCanvas::wheelEvent(QWheelEvent* event) {
    // webui：exp(-deltaY * 0.0014) 的指数缩放 + 锚点缩放
    const double factor = std::exp(-event->angleDelta().y() * 0.0014);
    const QPointF anchor = mapToScene(event->position().toPoint());
    const double next = std::clamp(zoom_ * factor, 0.35, 2.0);
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
                const double y = node.y + kPortTop + i * kPortPitch;
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
        // 空白处按下 = 平移画布（webui onBgDown → panning → cursor grabbing）
        panning_ = true;
        pan_anchor_view_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
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
    if (panning_) {
        const QPoint delta = event->pos() - pan_anchor_view_;
        pan_anchor_view_ = event->pos();
        translate(-delta.x() / zoom_, -delta.y() / zoom_);
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
                    const double y = node.y + kPortTop + i * kPortPitch;
                    if (node.ports[i].input &&
                        QRectF(QPointF(x, y) - QPointF(8, 8), QSizeF(16, 16)).contains(pos)) {
                        FlowCanvasLink link;
                        for (const auto& source : nodes_) {
                            for (std::size_t j = 0; j < source.ports.size(); ++j) {
                                const double sx = source.x + (source.ports[j].input ? 0 : source.width);
                                const double sy = source.y + kPortTop + j * kPortPitch;
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
    if (panning_) {
        panning_ = false;
        viewport()->setCursor(Qt::OpenHandCursor);
    }
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
