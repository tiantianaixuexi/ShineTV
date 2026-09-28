#include "ui/pages/imageflow/ImageFlowWorkspace.h"

#include "ui/pages/imageflow/BatchRenderView.h"
#include "ui/pages/imageflow/BindingView.h"
#include "ui/pages/imageflow/ComfyPanel.h"
#include "ui/pages/imageflow/ImageReviewView.h"
#include "ui/pages/imageflow/RenderResultView.h"
#include "db/sqlite/SqliteDb.h"
#include "flow/GraphCompiler.h"
#include "flow/GraphHost.h"
#include "flow/WorkflowIO.h"
#include "flow/FlowValidator.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/File.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace shine::app {
namespace {

[[nodiscard]] QString Text(const std::string& value) { return QString::fromStdString(value); }

[[nodiscard]] std::int64_t StableSeedFor(std::int64_t id) {
    std::uint64_t value = static_cast<std::uint64_t>(id) + 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return static_cast<std::int64_t>(value ^ (value >> 31));
}

[[nodiscard]] std::string NodePortName(const std::string& id, std::size_t index, bool input) {
    for (const auto* node : shine::flow::CanvasNodes()) {
        if (node == nullptr || node->id != id) continue;
        const auto& ports = input ? node->Inputs() : node->Outputs();
        return index < ports.size() ? ports[index].name : std::string{};
    }
    return {};
}

[[nodiscard]] shine::kit::FlowCanvasNode MakeNode(const std::string& id, const std::string& title,
                                                   const std::string& type, double x, double y,
                                                   std::vector<shine::kit::FlowPort> ports,
                                                   const std::string& state = "todo") {
    shine::kit::FlowCanvasNode node;
    node.id = id;
    node.title = title;
    node.type = type;
    node.x = x;
    node.y = y;
    node.ports = std::move(ports);
    node.state = state;
    if (type == "KSampler") {
        node.control_kind = "slider";
        node.control_value = "20";
    }
    return node;
}

} // namespace

ImageFlowWorkspace::ImageFlowWorkspace(QWidget* parent) : QWidget(parent) {
    flow::Init();
    BuildUi();
}

ImageFlowWorkspace::~ImageFlowWorkspace() { flow::Shutdown(); }

void ImageFlowWorkspace::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 画布满铺整页（webui .canvas-page：absolute inset 0）；浮动工具栏与浮动面板
    // 压在画布之上（.float-toolbar / .float-panel，各自带 16px 外缩）。
    // Qt 的等价做法：三者放进同一个 grid cell，靠对齐标志决定各自的位置 ——
    // 上一版把面板排成了画布下方的独立横条（canvas 与 split_row 各占一半高度），
    // 这正是"结构搬过来了但一眼不像"的典型。
    auto* stage = new QWidget(this);
    auto* stage_lay = new QGridLayout(stage);
    stage_lay->setContentsMargins(0, 0, 0, 0);
    stage_lay->setSpacing(0);

    canvas_ = new shine::kit::FlowCanvas(stage);
    canvas_->setMinimumWidth(360);
    stage_lay->addWidget(canvas_, 0, 0);

    // 左上：浮动工具栏（accent 标记 + 标题 + 导入/导出/校验/批量出图 + 状态）
    auto* tool_host = new QWidget(stage);
    tool_host->setObjectName(QStringLiteral("floatHost"));
    auto* tool_host_lay = new QVBoxLayout(tool_host);
    tool_host_lay->setContentsMargins(16, 16, 16, 16);
    tool_host_lay->setSpacing(0);
    auto* toolbar = new QWidget(tool_host);
    toolbar->setObjectName(QStringLiteral("floatToolbar"));
    auto* tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(12, 8, 12, 8);
    tb->setSpacing(8);
    auto* flow_mark = new QLabel(QStringLiteral("◈"), toolbar); // 与 kit 字符图标约定一致
    widgets::SetKind(flow_mark, "stateicon");
    auto* flow_title = widgets::SectionTitle(QStringLiteral("出图流程 · 分镜图_v3"), toolbar);
    auto* sep = new QWidget(toolbar);
    sep->setObjectName(QStringLiteral("floatSep"));
    sep->setFixedWidth(1);
    sep->setFixedHeight(18);
    auto* import = new widgets::Button(QStringLiteral("导入"), widgets::Button::Variant::Ghost,
                                       widgets::Button::Size::Sm, toolbar);
    auto* export_button = new widgets::Button(QStringLiteral("导出"), widgets::Button::Variant::Ghost,
                                              widgets::Button::Size::Sm, toolbar);
    auto* validate = new widgets::Button(QStringLiteral("提交前校验"), widgets::Button::Variant::Secondary,
                                         widgets::Button::Size::Sm, toolbar);
    auto* run = new widgets::Button(QStringLiteral("批量出图"), widgets::Button::Variant::Primary,
                                    widgets::Button::Size::Sm, toolbar);
    status_ = new QLabel(QStringLiteral("等待导入工作流"), toolbar);
    widgets::SetKind(status_, "statemeta");
    tb->addWidget(flow_mark);
    tb->addWidget(flow_title);
    tb->addWidget(sep);
    tb->addWidget(import);
    tb->addWidget(export_button);
    tb->addWidget(validate);
    tb->addWidget(run);
    tb->addSpacing(4);
    tb->addWidget(status_);
    tool_host_lay->addWidget(toolbar, 0, Qt::AlignLeft | Qt::AlignTop);
    stage_lay->addWidget(tool_host, 0, 0, Qt::AlignLeft | Qt::AlignTop);

    // ── 右侧浮动参数面板（webui float-panel：top16 right16 bottom16 w348）──
    panel_ = new QWidget(stage);
    panel_->setObjectName(QStringLiteral("floatPanel"));
    panel_->setFixedWidth(348);
    auto* panel_lay = new QVBoxLayout(panel_);
    panel_lay->setContentsMargins(14, 12, 14, 12);
    panel_lay->setSpacing(10);

    auto* panel_head = new QWidget(panel_);
    auto* ph = new QHBoxLayout(panel_head);
    ph->setContentsMargins(0, 0, 0, 0);
    ph->setSpacing(8);
    auto* panel_title = widgets::SectionTitle(QStringLiteral("镜头参数"), panel_head);
    fold_btn_ = new QPushButton(QStringLiteral("▾"), panel_head);
    fold_btn_->setToolTip(QStringLiteral("折叠 / 展开面板（画布拿回整幅宽度）"));
    widgets::SetKind(fold_btn_, "iconbutton");
    widgets::SetSizeAttr(fold_btn_, "sm");
    ph->addWidget(panel_title, 1);
    ph->addWidget(fold_btn_, 0, Qt::AlignVCenter);
    panel_lay->addWidget(panel_head);

    // 页签：绑定 · 批量出图 · 图评审 · 结果（与 webui 顺序一致）
    // 用 QTabWidget 承载内容栈：kit::Tabs 只是无内容的指示条，
    // 浮动面板需要「标签 + 页面」一体，所以这里保留 QTabWidget。
    panel_stack_ = new QTabWidget(panel_);
    binding_ = new BindingView(panel_stack_);
    batch_ = new BatchRenderView(panel_stack_);
    review_ = new ImageReviewView(panel_stack_);
    result_ = new RenderResultView(panel_stack_);
    panel_stack_->addTab(binding_, QStringLiteral("绑定"));
    panel_stack_->addTab(batch_, QStringLiteral("批量出图"));
    panel_stack_->addTab(review_, QStringLiteral("图评审"));
    panel_stack_->addTab(result_, QStringLiteral("结果"));
    panel_lay->addWidget(panel_stack_, 1);

    // 面板底部常驻 ComfyUI 健康条（webui fp-f）：连接状态常驻可见，
    // 不再单独占页面底部一整行。
    comfy_ = new ComfyPanel(panel_);
    panel_lay->addWidget(comfy_);

    auto* panel_host = new QWidget(stage);
    panel_host->setObjectName(QStringLiteral("floatHost"));
    panel_host_ = panel_host;
    auto* panel_host_lay = new QVBoxLayout(panel_host);
    panel_host_lay->setContentsMargins(0, 16, 16, 16);
    panel_host_lay->setSpacing(0);
    panel_host_lay->addWidget(panel_);
    stage_lay->addWidget(panel_host, 0, 0, Qt::AlignRight);

    outer->addWidget(stage, 1);

    connect(import, &QPushButton::clicked, this, [this] {
        SetStatus(QStringLiteral("请使用 ImportApiJson() 导入工作流文本（远程入口已预留）"));
    });
    connect(export_button, &QPushButton::clicked, this, [this] {
        const auto path = project_dir_ / "work" / "flow.json";
        SetStatus(ExportApiJson(Text(path.string())) ? QStringLiteral("已导出 flow.json")
                                                       : QStringLiteral("导出失败"));
    });
    connect(validate, &QPushButton::clicked, this, &ImageFlowWorkspace::Validate);
    connect(run, &QPushButton::clicked, this, &ImageFlowWorkspace::RunMock);
    connect(fold_btn_, &QPushButton::clicked, this, &ImageFlowWorkspace::TogglePanel);
}

void ImageFlowWorkspace::TogglePanel() {
    panel_folded_ = !panel_folded_;
    if (panel_host_ != nullptr) {
        panel_host_->setVisible(!panel_folded_);
    }
    fold_btn_->setText(panel_folded_ ? QStringLiteral("▸") : QStringLiteral("▾"));
    fold_btn_->setToolTip(panel_folded_ ? QStringLiteral("展开面板")
                                         : QStringLiteral("折叠 / 展开面板（画布拿回整幅宽度）"));
}

void ImageFlowWorkspace::SwitchPanelTab(int index) {
    if (panel_stack_ != nullptr && index >= 0 && index < panel_stack_->count()) {
        panel_stack_->setCurrentIndex(index);
    }
}

void ImageFlowWorkspace::SetContext(std::filesystem::path db_path, std::filesystem::path project_dir) {
    db_path_ = std::move(db_path);
    project_dir_ = std::move(project_dir);
    LoadFromStoryboard();
}

void ImageFlowWorkspace::SetBaseUrl(const QString& url) { comfy_->SetBaseUrl(url); }

void ImageFlowWorkspace::LoadMock() {
    std::vector<shine::kit::FlowCanvasNode> nodes;
    nodes.push_back(MakeNode("1", "LoadImage · 参考图", "LoadImage", 20, 40,
                             {{"image", "IMAGE", true}, {"IMAGE", "IMAGE", false}}, "done"));
    nodes.push_back(MakeNode("2", "CLIPText · 正向", "CLIPText", 20, 220,
                             {{"text", "STRING", true}, {"CONDITIONING", "CONDITIONING", false}}, "done"));
    nodes.push_back(MakeNode("3", "CLIPText · 负向", "CLIPText", 20, 400,
                             {{"text", "STRING", true}, {"CONDITIONING", "CONDITIONING", false}}, "done"));
    nodes.push_back(MakeNode("4", "KSampler", "KSampler", 300, 170,
                             {{"positive", "CONDITIONING", true}, {"negative", "CONDITIONING", true},
                              {"latent", "LATENT", true}, {"LATENT", "LATENT", false}}, "running"));
    nodes.push_back(MakeNode("5", "VAEDecode", "VAEDecode", 570, 170,
                             {{"samples", "LATENT", true}, {"IMAGE", "IMAGE", false}}, "todo"));
    nodes.push_back(MakeNode("6", "SaveImage", "SaveImage", 820, 170,
                             {{"images", "IMAGE", true}}, "failed"));
    std::vector<shine::kit::FlowCanvasLink> links{
        {"2", "CONDITIONING", "4", "positive", "CONDITIONING"},
        {"3", "CONDITIONING", "4", "negative", "CONDITIONING"},
        {"4", "LATENT", "5", "samples", "LATENT"},
        {"5", "IMAGE", "6", "images", "IMAGE"},
    };
    canvas_->SetGraph(std::move(nodes), std::move(links));
    canvas_->SelectNode("4");
    flow::BindingShotContext shot{101, "雨夜灯塔，苏黎半侧身回头", "watermark", "frame-101.png", 12873491};
    binding_->SetShotContext(shot);
    binding_->AddDefaultBindings();
    batch_->SetShots({{101, QStringLiteral("S01")}, {102, QStringLiteral("S02")},
                      {103, QStringLiteral("S03")}});
    batch_->EnqueueAll();
    batch_->RunMockBatch();
    review_->SetInput({101, "visual/generated/S01.png", {"assets/refs/suli.png"}},
                      "build/_shots/P07/generation_checks.json");
    review_->SetReviewer([](const flow::ImageReviewInput& input, flow::ImageReviewError& error) {
        (void)error;
        flow::ImageReviewReport report;
        report.model = "mock-vision";
        report.findings = {
            {"face", "脸部", true, "五官比例正常", {}},
            {"hands", "手部", false, "右手手指偏多", {}},
            {"composition", "构图", true, "近景视线符合镜头", {}},
            {"consistency", "一致性", true, "服装与设定集基线一致", {}},
            {"text", "文字", true, "画面无乱码文字", {}},
        };
        report.ok = false;
        (void)input;
        return std::optional<flow::ImageReviewReport>{std::move(report)};
    });
    review_->Run();
    // 结果页签：演示镜头「已出图、未出片」。成图路径指向项目产物，
    // 真实解码走 media 层；演示数据不塞假图，缺失时就显示空态。
    RenderResultView::Entry entry;
    entry.code = QStringLiteral("S05");
    entry.chapter = QStringLiteral("第 3 章 · 风起");
    entry.scene = QStringLiteral("12 场 · 长街雨幕");
    entry.status = QStringLiteral("评审未过");
    entry.prompt = QStringLiteral("v7（重生成 +1）");
    entry.imagePath = QStringLiteral("visual/generated/S05.png");
    entry.videoPath.clear();
    result_->SetEntry(entry);
    SetStatus(QStringLiteral("演示图：节点四态、绑定、批量降级与图评审均已加载"));
}

void ImageFlowWorkspace::LoadFromStoryboard() {
    const auto shots = ReadShots();
    std::vector<std::pair<std::int64_t, QString>> labels;
    for (const auto& shot : shots) labels.emplace_back(shot.shot_id, Text("S" + std::to_string(shot.shot_id)));
    batch_->SetShots(std::move(labels));
    if (!shots.empty()) {
        binding_->SetShotContext(shots.front());
        SetStatus(QStringLiteral("已从分镜读取 %1 个镜头；等待导入流程").arg(shots.size()));
    } else {
        canvas_->SetGraph({}, {});
        SetStatus(QStringLiteral("当前项目没有已提交镜头；可导入工作流或运行演示"));
    }
}

std::vector<flow::BindingShotContext> ImageFlowWorkspace::ReadShots() const {
    std::vector<flow::BindingShotContext> result;
    if (db_path_.empty()) return result;
    shine::db::sqlite::Database db;
    if (auto opened = db.Open({.path = db_path_, .readOnly = true, .create = false}); !opened) return result;
    shine::novelcore::NovelVisual visual(db);
    auto chapters = shine::novelcore::NovelGraph(db).ListChapters(2000);
    if (!chapters) return result;
    for (const auto& chapter : *chapters) {
        auto shots = visual.ListShotsByChapter(chapter.id);
        if (!shots) continue;
        for (const auto& shot : *shots) {
            result.push_back({shot.id, shot.prompt_text, shot.negative_text, {}, StableSeedFor(shot.id)});
        }
    }
    return result;
}

void ImageFlowWorkspace::RefreshGraphFromHost() {
    std::vector<shine::kit::FlowCanvasNode> nodes;
    for (const auto* node : flow::CanvasNodes()) {
        if (node == nullptr) continue;
        std::vector<shine::kit::FlowPort> ports;
        for (const auto& port : node->Inputs()) ports.push_back({port.name, port.allowedTypes.empty() ? "ANY" : port.allowedTypes.front(), true});
        for (const auto& port : node->Outputs()) ports.push_back({port.name, port.allowedTypes.empty() ? "ANY" : port.allowedTypes.front(), false});
        nodes.push_back(MakeNode(node->id, node->Title(), node->ClassType(), node->pos.x, node->pos.y,
                                 std::move(ports)));
    }
    std::vector<shine::kit::FlowCanvasLink> links;
    for (const auto& link : flow::EnumerateLinks()) {
        links.push_back({link.fromNodeId, NodePortName(link.fromNodeId, link.fromSocketIndex, false),
                         link.toNodeId, NodePortName(link.toNodeId, link.toSocketIndex, true), link.type});
    }
    canvas_->SetGraph(std::move(nodes), std::move(links));
}

void ImageFlowWorkspace::ImportApiJson(const QString& text) {
    const auto report = flow::ImportApiJson(text.toStdString());
    RefreshGraphFromHost();
    SetStatus(report.ok ? QStringLiteral("导入完成：%1 节点 / %2 连线").arg(report.nodes).arg(report.links)
                       : QStringLiteral("导入失败"));
}

bool ImageFlowWorkspace::ExportApiJson(const QString& path) {
    return flow::ExportApiJson(path.toStdString());
}

void ImageFlowWorkspace::Validate() {
    const auto compiled = flow::CompileToApiJson();
    if (!compiled.ok) {
        SetStatus(QStringLiteral("编译失败：%1").arg(QString::fromStdString(
            compiled.errors.empty() ? "未知错误" : compiled.errors.front().message)));
        return;
    }
    flow::GenerationValidationInput input;
    input.api_json = compiled.apiJson;
    input.shot = {0, {}, {}, {}, 0};
    input.object_info_ready = false;
    const auto result = flow::ValidateForSubmit(input, nullptr);
    SetStatus(QString::fromStdString(result.Describe()));
}

void ImageFlowWorkspace::RunMock() { LoadMock(); }

void ImageFlowWorkspace::SetStatus(const QString& text) { status_->setText(text); }

QString ImageFlowWorkspace::Probe() const {
    return QStringLiteral("graph=%1; binding=%2; batch=%3; review=%4; comfy=%5; result=%6")
        .arg(GraphProbe(), BindingProbe(), BatchProbe(), ReviewProbe(), ComfyProbe(), ResultProbe());
}

QString ImageFlowWorkspace::ResultProbe() const { return result_ != nullptr ? result_->Probe() : QString{}; }

QString ImageFlowWorkspace::GraphProbe() const { return canvas_->Probe(); }
QString ImageFlowWorkspace::ReviewProbe() const { return review_->Probe(); }
QString ImageFlowWorkspace::BatchProbe() const { return batch_->Probe(); }
QString ImageFlowWorkspace::BindingProbe() const { return binding_->Probe(); }
QString ImageFlowWorkspace::ComfyProbe() const { return comfy_->Probe(); }

} // namespace shine::app
