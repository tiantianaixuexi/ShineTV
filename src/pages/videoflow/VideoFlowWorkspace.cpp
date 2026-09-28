#include "pages/videoflow/VideoFlowWorkspace.h"

#include "pages/videoflow/ChainView.h"
#include "pages/videoflow/FinalCutView.h"
#include "pages/videoflow/VideoTaskView.h"
#include "flow/FlowValidator.h"
#include "flow/VideoCatalog.h"
#include "flow/VideoChain.h"
#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"

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
    outer->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("出片流程 · H3 视频"), this);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    outer->addWidget(title);
    auto* validate = new widgets::Button(QStringLiteral("提交前参数校验"), widgets::Button::Variant::Secondary,
                                         widgets::Button::Size::Sm, this);
    outer->addWidget(validate);
    status_ = new QLabel(QStringLiteral("等待导入视频工作流"), this);
    widgets::SetKind(status_, "statedetail");
    outer->addWidget(status_);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    canvas_ = new shine::kit::FlowCanvas(splitter);
    canvas_->setMinimumWidth(560);
    splitter->addWidget(canvas_);
    auto* tabs = new QTabWidget(splitter);
    chain_ = new ChainView(tabs);
    tasks_ = new VideoTaskView(tabs);
    final_ = new FinalCutView(tabs);
    tabs->addTab(chain_, QStringLiteral("首尾帧链"));
    tabs->addTab(tasks_, QStringLiteral("视频任务"));
    tabs->addTab(final_, QStringLiteral("成片"));
    splitter->addWidget(tabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    outer->addWidget(splitter, 1);
    connect(validate, &QPushButton::clicked, this, &VideoFlowWorkspace::Validate);
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
    return QStringLiteral("canvas=%1; chain=%2; tasks=%3; final=%4")
        .arg(canvas_->Probe(), chain_->Probe(), tasks_->Probe(), final_->Probe());
}

QString VideoFlowWorkspace::ChainProbe() const { return chain_->Probe(); }
QString VideoFlowWorkspace::TaskProbe() const { return tasks_->Probe(); }
QString VideoFlowWorkspace::FinalProbe() const { return final_->Probe(); }

} // namespace shine::app
