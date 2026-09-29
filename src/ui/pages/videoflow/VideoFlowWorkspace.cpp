#include "ui/pages/videoflow/VideoFlowWorkspace.h"

#include "ui/pages/videoflow/FilmStrip.h"
#include "ui/pages/videoflow/ChainView.h"
#include "ui/pages/videoflow/FinalCutView.h"
#include "ui/pages/videoflow/VideoTaskView.h"
#include "flow/FlowValidator.h"
#include "flow/VideoCatalog.h"
#include "flow/VideoChain.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在页面根控件（objectName=vidFlowWs）上，选择器一律以 #vidFlowWs 打头，
// 因此与 kit 全局 QSS、以及其它页面目录完全隔离（不改 QssBuilder.cpp）。
// 逐条对应 webui/src/styles/views.css:195–262：
//   .float-panel .fp-h / .fp-b / .fp-f 的三段内距与两道发丝线
//   .float-strip 的 r-lg(14) + line-subtle 边（胶片格本身见 FilmStrip 的局部 QSS）
// 面板本体的底 / 边 / 圆角 / 阴影由 QssBuilder 的 QWidget#floatPanel 承担。
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#vidFlowWs QWidget#fpHead {\n"
               "  background: transparent; border: none;\n"
               "  border-bottom: 1px solid %1; }\n"
               "QWidget#vidFlowWs QWidget#fpBody { background: transparent; border: none; }\n"
               "QWidget#vidFlowWs QWidget#fpFoot {\n"
               "  background: transparent; border: none;\n"
               "  border-top: 1px solid %1; }\n"
               "QWidget#vidFlowWs QWidget#fpPanelTitle {\n"
               "  background: transparent; border: none; font-size: 13px; font-weight: 700; }\n")
        .arg(shine::widget::CssRgb(t.lineSubtle));
}

// 换肤后重挂页面 QSS 由 widgets::RefreshOnThemeChange 统一负责（见构造处调用）。
// 不要改回「在 qApp 上过滤 QEvent::ThemeChange」——ApplyQss 从不发那个事件。

} // namespace

VideoFlowWorkspace::VideoFlowWorkspace(QWidget* parent) : QWidget(parent) { BuildUi(); }

void VideoFlowWorkspace::BuildUi() {
    // 页面根控件：页面专属 QSS 的挂载点（选择器前缀 #vidFlowWs）
    setObjectName(QStringLiteral("vidFlowWs"));
    setStyleSheet(PageQss());
    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(PageQss()); });
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 画布满铺整页（webui .canvas-page）；浮动工具栏（左上）、浮动参数面板（右侧）、
    // 连播胶片条（底部）都压在画布之上。三者与画布放进同一个 grid cell，
    // 靠对齐标志定位 —— 与 CSS 的 absolute + inset 等价。
    auto* stage = new QWidget(this);
    auto* stage_lay = new QGridLayout(stage);
    stage_lay->setContentsMargins(0, 0, 0, 0);
    stage_lay->setSpacing(0);

    canvas_ = new shine::kit::FlowCanvas(stage);
    canvas_->setMinimumWidth(360);
    // webui VideoFlow.jsx：<FlowCanvas ... fill fitInset={380} />
    canvas_->SetFitInset(380);
    // 底部有 float-strip，缩放工具条按 .canvas-tools.bl-up 让到 118
    canvas_->SetToolsBottomInset(118);
    stage_lay->addWidget(canvas_, 0, 0);

    // 左上：浮动工具栏
    auto* tool_host = new QWidget(stage);
    tool_host->setObjectName(QStringLiteral("floatHost"));
    auto* tool_host_lay = new QVBoxLayout(tool_host);
    tool_host_lay->setContentsMargins(16, 16, 16, 16);
    tool_host_lay->setSpacing(0);
    auto* toolbar = new QWidget(tool_host);
    toolbar->setObjectName(QStringLiteral("floatToolbar"));
    // webui views.css:195 .float-toolbar box-shadow: var(--shadow-1)
    widgets::ApplyShadow(toolbar, widgets::ShadowLevel::Sm);
    auto* tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(12, 8, 12, 8);
    tb->setSpacing(8);
    auto* flow_mark = new QLabel(QStringLiteral("◈"), toolbar); // 与 kit 字符图标约定一致
    widgets::SetKind(flow_mark, "stateicon");
    auto* flow_title = widgets::SectionTitle(QStringLiteral("出片流程 · H3 视频"), toolbar);
    auto* sep = new QWidget(toolbar);
    sep->setObjectName(QStringLiteral("floatSep"));
    sep->setFixedWidth(1);
    sep->setFixedHeight(18);
    auto* validate = new widgets::Button(QStringLiteral("提交前参数校验"), widgets::Button::Variant::Secondary,
                                         widgets::Button::Size::Sm, toolbar);
    status_ = new QLabel(QStringLiteral("等待导入视频工作流"), toolbar);
    widgets::SetKind(status_, "statemeta");
    tb->addWidget(flow_mark);
    tb->addWidget(flow_title);
    tb->addWidget(sep);
    tb->addWidget(validate);
    tb->addSpacing(4);
    tb->addWidget(status_);
    tool_host_lay->addWidget(toolbar, 0, Qt::AlignLeft | Qt::AlignTop);
    stage_lay->addWidget(tool_host, 0, 0, Qt::AlignLeft | Qt::AlignTop);

    // ── 右侧浮动面板（可折叠）：首尾帧链 · 视频任务 · 成片 ──
    panel_ = new QWidget(stage);
    panel_->setObjectName(QStringLiteral("floatPanel"));
    // webui views.css:216 .float-panel box-shadow: var(--shadow-2)
    widgets::ApplyShadow(panel_, widgets::ShadowLevel::Lg);
    panel_->setFixedWidth(348);
    // 面板自身不加内距：内距分三段给（fp-h p12 14 / fp-b p8 12 14 / fp-f p10 14）
    auto* panel_lay = new QVBoxLayout(panel_);
    panel_lay->setContentsMargins(0, 0, 0, 0);
    panel_lay->setSpacing(0);

    // fp-h：accent 列表图标 + 标题 + 折叠按钮
    auto* panel_head = new QWidget(panel_);
    panel_head->setObjectName(QStringLiteral("fpHead"));
    auto* ph = new QHBoxLayout(panel_head);
    ph->setContentsMargins(14, 12, 14, 12); // views.css .float-panel .fp-h p12 14
    ph->setSpacing(9);
    auto* head_mark = new QLabel(QStringLiteral("▤"), panel_head);
    widgets::SetKind(head_mark, "stateicon");
    widgets::SetTextColor(head_mark, theme::Current().accentPrimary);
    auto* panel_title = new QLabel(QStringLiteral("出片参数"), panel_head);
    panel_title->setObjectName(QStringLiteral("fpPanelTitle"));
    fold_btn_ = new QPushButton(QStringLiteral("▾"), panel_head);
    fold_btn_->setToolTip(QStringLiteral("折叠 / 展开面板（画布拿回整幅宽度）"));
    widgets::SetKind(fold_btn_, "iconbutton");
    widgets::SetSizeAttr(fold_btn_, "sm");
    ph->addWidget(head_mark, 0, Qt::AlignVCenter);
    ph->addWidget(panel_title, 1);
    ph->addWidget(fold_btn_, 0, Qt::AlignVCenter);
    panel_lay->addWidget(panel_head);

    // fp-b：页签内容（首尾帧链 · 视频任务 · 成片，与 webui 顺序一致）
    // ⚠️ 取舍：设计稿这一段是自定义 Segmented 胶囊分段，QTabWidget 是下划线页签。
    // 换成 Segmented 会打破 verify/review/P08Review.cpp 的 findChild<QTabWidget*>()
    // 反查与切页探针，故本轮**刻意保留** QTabWidget。
    auto* panel_body = new QWidget(panel_);
    panel_body->setObjectName(QStringLiteral("fpBody"));
    auto* body_lay = new QVBoxLayout(panel_body);
    body_lay->setContentsMargins(12, 8, 12, 14); // views.css .fp-b p8 12 14
    body_lay->setSpacing(10);
    panel_stack_ = new QTabWidget(panel_body);
    chain_ = new ChainView(panel_stack_);
    tasks_ = new VideoTaskView(panel_stack_);
    final_ = new FinalCutView(panel_stack_);
    panel_stack_->addTab(chain_, QStringLiteral("首尾帧链"));
    panel_stack_->addTab(tasks_, QStringLiteral("视频任务"));
    panel_stack_->addTab(final_, QStringLiteral("成片"));
    body_lay->addWidget(panel_stack_, 1);
    panel_lay->addWidget(panel_body, 1);

    // fp-f：面板底部常驻一行流水线状态（webui VideoFlow 的 fp-f）
    auto* panel_foot = new QWidget(panel_);
    panel_foot->setObjectName(QStringLiteral("fpFoot"));
    auto* foot_lay = new QHBoxLayout(panel_foot);
    foot_lay->setContentsMargins(14, 10, 14, 10); // views.css .fp-f p10 14
    foot_lay->setSpacing(theme::space::kSteps[2]);
    auto* foot_dot = new QLabel(QStringLiteral("●"), panel_foot);
    widgets::SetKind(foot_dot, "stateicon");
    widgets::SetTextColor(foot_dot, theme::Current().statusBusy);
    auto* foot_text = new QLabel(QStringLiteral("H3 生成 · RIFE 待跑 · Encode 排队"), panel_foot);
    widgets::SetKind(foot_text, "statemeta");
    auto* foot_spec = new QLabel(QStringLiteral("产物 → output/videos"), panel_foot);
    widgets::SetKind(foot_spec, "statemeta");
    foot_lay->addWidget(foot_dot, 0);
    foot_lay->addWidget(foot_text, 1);
    foot_lay->addWidget(foot_spec, 0);
    panel_lay->addWidget(panel_foot);

    auto* panel_host = new QWidget(stage);
    panel_host->setObjectName(QStringLiteral("floatHost"));
    panel_host_ = panel_host;
    auto* panel_host_lay = new QVBoxLayout(panel_host);
    panel_host_lay->setContentsMargins(0, 16, 16, 16);
    panel_host_lay->setSpacing(0);
    panel_host_lay->addWidget(panel_);
    stage_lay->addWidget(panel_host, 0, 0, Qt::AlignRight);

    // 胶片条的外缩 host：.float-strip 的 left16 bottom16 right384
    auto* film_host = new QWidget(stage);
    film_host->setObjectName(QStringLiteral("floatHost"));
    auto* film_host_lay = new QVBoxLayout(film_host);
    film_host_lay->setContentsMargins(16, 0, 384, 16);
    film_host_lay->setSpacing(0);
    // ⚠️ 这里仍是共享 grid cell + 对齐标志（Qt 布局模型下对 CSS absolute inset 的
    // 等效做法）：窄窗口时右侧 384 的让位会与面板抢位，**不要**改子控件浮动叠放。

    // ── 底部胶片条（webui .float-strip：left16 bottom16 right384）──
    // 胶片条自己不再排 head 行（设计稿里这一条只有「成片」前缀块 + 胶片格），
    // 16px 外缩交给外层 host，跟浮动工具栏 / 浮动面板同一套做法。
    film_ = new FilmStrip(film_host);
    stage_lay->addWidget(film_host, 0, 0, Qt::AlignLeft | Qt::AlignBottom);

    outer->addWidget(stage, 1);

    connect(validate, &QPushButton::clicked, this, &VideoFlowWorkspace::Validate);
    connect(fold_btn_, &QPushButton::clicked, this, &VideoFlowWorkspace::TogglePanel);
}

void VideoFlowWorkspace::TogglePanel() {
    panel_folded_ = !panel_folded_;
    if (panel_host_ != nullptr) {
        panel_host_->setVisible(!panel_folded_);
    }
    fold_btn_->setText(panel_folded_ ? QStringLiteral("▸") : QStringLiteral("▾"));
    fold_btn_->setToolTip(panel_folded_ ? QStringLiteral("展开面板")
                                         : QStringLiteral("折叠 / 展开面板（画布拿回整幅宽度）"));
}

void VideoFlowWorkspace::LoadMock() {
    canvas_->SetGraph({
        {"1", "LoadImage · 首帧", "LoadImage", 20, 50, 180, 120, {{"image", "IMAGE", true}, {"IMAGE", "IMAGE", false}}, "done"},
        {"2", "H3 视频生成", "H3Video", 280, 160, 190, 140, {{"first", "IMAGE", true}, {"last", "IMAGE", true}, {"VIDEO", "VIDEO", false}}, "running"},
        {"3", "RIFE 插帧", "RIFE", 540, 160, 180, 120, {{"video", "VIDEO", true}, {"VIDEO", "VIDEO", false}}, "todo"},
        {"4", "Encode 输出", "VideoEncode", 780, 160, 180, 120, {{"video", "VIDEO", true}}, "failed"},
    }, {{"1", "IMAGE", "2", "first", "IMAGE"}, {"2", "VIDEO", "3", "video", "VIDEO"}, {"3", "VIDEO", "4", "video", "VIDEO"}});
    canvas_->SelectNode("2");
    chain_->SetChain(flow::BuildVideoChain({
        {1, "S01_first.png", "S01_last.png", ""},
        {2, "S02_first.png", "S02_last.png", ""},
        {3, "S02_last.png", "S03_first.png", ""},
    }));
    tasks_->SetShots({{1, QStringLiteral("S01")}, {2, QStringLiteral("S02")}, {3, QStringLiteral("S03")}});
    tasks_->EnqueueAll();
    tasks_->ShowRunning();
    final_->SetVideos({{1, QStringLiteral("output/videos/S01.mp4")}, {2, QStringLiteral("output/videos/S02.mp4")}});
    // 胶片条：S01/S02 已出片（演示，不塞假缩略图 —— 首帧缺失时显示空态），
    // S03 待出片。连播列表最忌讳拿别的镜头的图凑数。
    film_->SetCells({
        {.code = QStringLiteral("S01"), .duration = QStringLiteral("48fps"), .ready = true,
         .videoPath = QStringLiteral("output/videos/S01.mp4")},
        {.code = QStringLiteral("S02"), .duration = QStringLiteral("48fps"), .ready = true,
         .videoPath = QStringLiteral("output/videos/S02.mp4")},
        {.code = QStringLiteral("S03"), .duration = QString(), .ready = false, .videoPath = QString()},
    });
    status_->setText(QStringLiteral("H3 / RIFE / Encode 节点已加载；链式断点与任务状态可见"));
}

void VideoFlowWorkspace::SetContext(std::filesystem::path db_path, std::filesystem::path project_dir) {
    db_path_ = std::move(db_path);
    project_dir_ = std::move(project_dir);
    status_->setText(QStringLiteral("已绑定项目：%1").arg(QString::fromStdString(project_dir_.string())));
}

void VideoFlowWorkspace::Validate() {
    flow::GenerationValidationInput input;
    input.object_info_ready = true;
    input.width = 1001;
    input.height = 513;
    input.length = 20;
    input.reference_count = 10;
    const auto result = flow::ValidateForSubmit(input, nullptr);
    status_->setText(QString::fromStdString(result.Describe()));
}

QString VideoFlowWorkspace::Probe() const {
    return QStringLiteral("canvas=%1; chain=%2; tasks=%3; final=%4; film=%5")
        .arg(canvas_->Probe(), chain_->Probe(), tasks_->Probe(), final_->Probe(), FilmProbe());
}

QString VideoFlowWorkspace::FilmProbe() const { return film_ != nullptr ? film_->Probe() : QString{}; }

QString VideoFlowWorkspace::ChainProbe() const { return chain_->Probe(); }
QString VideoFlowWorkspace::TaskProbe() const { return tasks_->Probe(); }
QString VideoFlowWorkspace::FinalProbe() const { return final_->Probe(); }

} // namespace shine::app
