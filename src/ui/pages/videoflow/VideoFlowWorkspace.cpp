#include "ui/pages/videoflow/VideoFlowWorkspace.h"

#include "ui/pages/videoflow/FilmStrip.h"
#include "ui/pages/videoflow/ChainView.h"
#include "ui/pages/videoflow/FinalCutView.h"
#include "ui/pages/videoflow/VideoTaskView.h"
#include "flow/FlowValidator.h"
#include "flow/VideoCatalog.h"
#include "flow/VideoChain.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace shine::app {

VideoFlowWorkspace::VideoFlowWorkspace(QWidget* parent) : QWidget(parent) { BuildUi(); }

void VideoFlowWorkspace::BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* canvas_host = new QWidget(this);
    auto* host_lay = new QVBoxLayout(canvas_host);
    host_lay->setContentsMargins(0, 0, 0, 0);
    host_lay->setSpacing(0);

    // 顶部浮动工具栏
    auto* toolbar = new QWidget(canvas_host);
    auto* tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                           theme::space::kSteps[2], theme::space::kSteps[1]);
    tb->setSpacing(theme::space::kSteps[1]);
    auto* flow_title = widgets::SectionTitle(QStringLiteral("出片流程 · H3 视频"), toolbar);
    auto* validate = new widgets::Button(QStringLiteral("提交前参数校验"), widgets::Button::Variant::Secondary,
                                         widgets::Button::Size::Sm, toolbar);
    status_ = new QLabel(QStringLiteral("等待导入视频工作流"), toolbar);
    widgets::SetKind(status_, "statedetail");
    tb->addWidget(flow_title);
    tb->addStretch(1);
    tb->addWidget(validate);
    tb->addWidget(status_);
    host_lay->addWidget(toolbar);

    canvas_ = new shine::kit::FlowCanvas(canvas_host);
    canvas_->setMinimumWidth(480);
    host_lay->addWidget(canvas_, 1);

    // ── 右侧浮动面板（可折叠）：首尾帧链 · 视频任务 · 成片 ──
    auto* split_row = new QWidget(canvas_host);
    auto* split_lay = new QHBoxLayout(split_row);
    split_lay->setContentsMargins(0, 0, 0, 0);
    split_lay->setSpacing(0);

    panel_ = new QWidget(split_row);
    panel_->setObjectName(QStringLiteral("floatPanel"));
    panel_->setMinimumWidth(320);
    panel_->setMaximumWidth(460);
    auto* panel_lay = new QVBoxLayout(panel_);
    panel_lay->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[2],
                                  theme::space::kSteps[2], theme::space::kSteps[2]);
    panel_lay->setSpacing(theme::space::kSteps[2]);

    auto* panel_head = new QWidget(panel_);
    auto* ph = new QHBoxLayout(panel_head);
    ph->setContentsMargins(0, 0, 0, 0);
    ph->setSpacing(theme::space::kSteps[1]);
    auto* panel_title = widgets::SectionTitle(QStringLiteral("出片参数"), panel_head);
    fold_btn_ = new QPushButton(QStringLiteral("▾"), panel_head);
    fold_btn_->setToolTip(QStringLiteral("折叠 / 展开面板（画布拿回整幅宽度）"));
    widgets::SetKind(fold_btn_, "iconbutton");
    widgets::SetSizeAttr(fold_btn_, "sm");
    ph->addWidget(panel_title, 1);
    ph->addWidget(fold_btn_, 0, Qt::AlignVCenter);
    panel_lay->addWidget(panel_head);

    panel_stack_ = new QTabWidget(panel_);
    chain_ = new ChainView(panel_stack_);
    tasks_ = new VideoTaskView(panel_stack_);
    final_ = new FinalCutView(panel_stack_);
    panel_stack_->addTab(chain_, QStringLiteral("首尾帧链"));
    panel_stack_->addTab(tasks_, QStringLiteral("视频任务"));
    panel_stack_->addTab(final_, QStringLiteral("成片"));
    panel_lay->addWidget(panel_stack_, 1);

    split_lay->addStretch(1);
    split_lay->addWidget(panel_);
    host_lay->addWidget(split_row, 1);

    // ── 底部胶片条 ──
    film_ = new FilmStrip(canvas_host);
    host_lay->addWidget(film_);

    outer->addWidget(canvas_host, 1);

    connect(validate, &QPushButton::clicked, this, &VideoFlowWorkspace::Validate);
    connect(fold_btn_, &QPushButton::clicked, this, &VideoFlowWorkspace::TogglePanel);
}

void VideoFlowWorkspace::TogglePanel() {
    panel_folded_ = !panel_folded_;
    panel_->setVisible(!panel_folded_);
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
