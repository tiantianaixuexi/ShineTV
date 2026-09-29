#include "ui/pages/videoflow/VideoFlowPageModel.h"

#include "flow/FlowValidator.h"
#include "util/Encoding.h"

#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace shine::app {
namespace {

// 演示 / 验收之外，页面初始状态就是「什么都没导入」。
// 这行字是**真实状态**的描述（与迁移前 VideoFlowWorkspace 构造后的 status_ 同值）。
constexpr auto kIdleStatus = "等待导入视频工作流";
// 产物目录：与迁移前 FinalCutView::ExportScene("output/videos") 同一口径。
constexpr auto kOutputDir = "output/videos";

[[nodiscard]] QVariantMap NodeToMap(const VideoGraphNode& node) {
    QVariantMap map;
    map[QStringLiteral("id")] = node.id;
    map[QStringLiteral("title")] = node.title;
    map[QStringLiteral("sub")] = node.sub;
    map[QStringLiteral("icon")] = node.icon;
    map[QStringLiteral("x")] = node.x;
    map[QStringLiteral("y")] = node.y;
    map[QStringLiteral("state")] = node.state;
    return map;
}

[[nodiscard]] QVariantMap ChainRowToMap(const VideoChainRowFact& row) {
    QVariantMap map;
    map[QStringLiteral("fromCode")] = row.fromCode;
    map[QStringLiteral("toCode")] = row.toCode;
    map[QStringLiteral("policy")] = row.policy;
    map[QStringLiteral("tagText")] = row.tagText;
    map[QStringLiteral("tagTone")] = row.tagTone;
    map[QStringLiteral("detail")] = row.detail;
    return map;
}

[[nodiscard]] QVariantMap TaskRowToMap(const VideoTaskRowFact& row) {
    QVariantMap map;
    map[QStringLiteral("code")] = row.code;
    map[QStringLiteral("frameText")] = row.frameText;
    map[QStringLiteral("framePath")] = row.framePath;
    map[QStringLiteral("tagText")] = row.tagText;
    map[QStringLiteral("tagTone")] = row.tagTone;
    map[QStringLiteral("progress")] = row.progress;
    map[QStringLiteral("run")] = row.run;
    return map;
}

[[nodiscard]] QVariantMap CutRowToMap(const VideoCutRowFact& row) {
    QVariantMap map;
    map[QStringLiteral("code")] = row.code;
    map[QStringLiteral("path")] = row.path;
    map[QStringLiteral("tagText")] = row.tagText;
    map[QStringLiteral("tagTone")] = row.tagTone;
    return map;
}

} // namespace

VideoFlowPageModel::VideoFlowPageModel(QObject* parent) : QObject(parent) {
    status_ = QString::fromUtf8(kIdleStatus);
    // 首投影必须无条件发生：属性刚建好时各 View 都是空容器，
    // 若只靠 changed() 驱动，QML 首次求值会落在空 map 上（读出 undefined）。
    RebuildAll();
}

VideoFlowPageModel::~VideoFlowPageModel() = default;

void VideoFlowPageModel::SetContext(std::filesystem::path dbPath, std::filesystem::path projectDir) {
    dbPath_ = std::move(dbPath);
    projectDir_ = std::move(projectDir);
    status_ = QStringLiteral("已绑定项目：%1")
                  .arg(QString::fromStdString(util::PathToUtf8(projectDir_)));
    emit changed();
}

// ============================== 画布（①） ==============================

void VideoFlowPageModel::RebuildGraph() {
    graphNodesView_.clear();
    graphNodesView_.reserve(static_cast<qsizetype>(graph_.nodes.size()));
    for (const VideoGraphNode& node : graph_.nodes) {
        graphNodesView_.push_back(NodeToMap(node));
    }
    graphLinksView_.clear();
    graphLinksView_.reserve(static_cast<qsizetype>(graph_.links.size()));
    for (const VideoGraphLink& link : graph_.links) {
        QVariantMap map;
        map[QStringLiteral("from")] = link.from;
        map[QStringLiteral("to")] = link.to;
        graphLinksView_.push_back(map);
    }
    graphStateView_ = QVariantMap{
        {QStringLiteral("none"), graph_.Empty()},
        {QStringLiteral("nodes"), static_cast<int>(graph_.nodes.size())},
        {QStringLiteral("links"), static_cast<int>(graph_.links.size())},
        {QStringLiteral("selected"), selectedNodeId_},
    };
}

QVariantList VideoFlowPageModel::graphNodes() const { return graphNodesView_; }
QVariantList VideoFlowPageModel::graphLinks() const { return graphLinksView_; }
QVariantMap VideoFlowPageModel::graphState() const { return graphStateView_; }

// ============================== 首尾帧链（②） ==============================

void VideoFlowPageModel::RebuildChain() {
    chainRowsView_.clear();
    for (const VideoChainRowFact& row : VideoChainRows(chain_)) {
        chainRowsView_.push_back(ChainRowToMap(row));
    }
    const std::size_t broken = chain_.BrokenCount();
    // ⚠️ 空态也带全键：QML 侧面板底那行读 .summary，缺键就是 undefined。
    chainStateView_ = QVariantMap{
        {QStringLiteral("none"), chain_.links.empty()},
        {QStringLiteral("links"), static_cast<int>(chain_.links.size())},
        {QStringLiteral("connected"), static_cast<int>(chain_.links.size() - broken)},
        {QStringLiteral("broken"), static_cast<int>(broken)},
        {QStringLiteral("summary"),
         chain_.links.empty() ? QStringLiteral("暂无链式关系") : VideoChainProbe(chain_)},
    };
}

QVariantList VideoFlowPageModel::chainRows() const { return chainRowsView_; }
QVariantMap VideoFlowPageModel::chainState() const { return chainStateView_; }

// ============================== 视频任务（③） ==============================

void VideoFlowPageModel::RebuildTasks() {
    taskRowsView_.clear();
    int running = 0;
    for (const VideoTaskRowFact& row : VideoTaskRows(queue_, frames_)) {
        running += row.run ? 1 : 0;
        taskRowsView_.push_back(TaskRowToMap(row));
    }
    framesCount_ = frames_.size();
    taskStateView_ = QVariantMap{
        {QStringLiteral("none"), queue_.Snapshot().empty()},
        {QStringLiteral("jobs"), static_cast<int>(queue_.Snapshot().size())},
        {QStringLiteral("frames"), static_cast<int>(frames_.size())},
        {QStringLiteral("running"), running},
        {QStringLiteral("summary"),
         queue_.Snapshot().empty() ? QStringLiteral("没有待出片的镜头")
                                   : VideoTaskProbe(queue_, frames_.size())},
    };
}

QVariantList VideoFlowPageModel::taskRows() const { return taskRowsView_; }
QVariantMap VideoFlowPageModel::taskState() const { return taskStateView_; }

// ============================== 成片（④） ==============================

void VideoFlowPageModel::RebuildCut() {
    cutRowsView_.clear();
    for (const auto& [id, path] : videos_) {
        cutRowsView_.push_back(CutRowToMap(VideoCutRow(id, path)));
    }
    cutKvView_.clear();
    for (const VideoCutKvFact& kv : VideoCutKv(videos_, shots_)) {
        QVariantMap map;
        map[QStringLiteral("key")] = kv.key;
        map[QStringLiteral("value")] = kv.value;
        map[QStringLiteral("valueToken")] = kv.valueToken;
        cutKvView_.push_back(map);
    }
    cutStateView_ = QVariantMap{
        {QStringLiteral("none"), videos_.empty()},
        {QStringLiteral("count"), static_cast<int>(videos_.size())},
        {QStringLiteral("exportPath"), QString::fromLatin1(kOutputDir)},
        {QStringLiteral("summary"), VideoCutSummary(videos_)},
    };
}

QVariantList VideoFlowPageModel::cutRows() const { return cutRowsView_; }
QVariantList VideoFlowPageModel::cutKv() const { return cutKvView_; }
QVariantMap VideoFlowPageModel::cutState() const { return cutStateView_; }

// ============================== 胶片条（⑤） ==============================

void VideoFlowPageModel::RebuildFilm() {
    filmCellsView_.clear();
    int ready = 0;
    for (std::size_t i = 0; i < film_.size(); ++i) {
        const VideoFilmCellFact& cell = film_[i];
        ready += cell.ready ? 1 : 0;
        QVariantMap map;
        map[QStringLiteral("code")] = cell.code;
        map[QStringLiteral("duration")] = cell.duration;
        map[QStringLiteral("ready")] = cell.ready;
        map[QStringLiteral("videoPath")] = cell.videoPath;
        map[QStringLiteral("picked")] = static_cast<int>(i) == pickedFilm_;
        filmCellsView_.push_back(map);
    }
    filmStateView_ = QVariantMap{
        {QStringLiteral("none"), film_.empty()},
        {QStringLiteral("cells"), static_cast<int>(film_.size())},
        {QStringLiteral("ready"), ready},
        {QStringLiteral("picked"), pickedFilm_},
    };
}

QVariantList VideoFlowPageModel::filmCells() const { return filmCellsView_; }
QVariantMap VideoFlowPageModel::filmState() const { return filmStateView_; }

// ============================== 面板头 / 面板底 ==============================

void VideoFlowPageModel::RebuildPanelTitle() {
    // 与设计稿 fp-h 同位：选中镜头就给「镜号 + 状态标」，没选给面板名。
    // ⚠️ 只有**就绪**格能选中（未出片的格在 QML 侧就是不可点的），所以这里
    // 取 pickedFilm_ 之前先判 ready，别给一个不存在的镜头编状态。
    const bool has_shot = pickedFilm_ >= 0 && pickedFilm_ < static_cast<int>(film_.size()) &&
                          film_[static_cast<std::size_t>(pickedFilm_)].ready;
    panelTitleView_ = QVariantMap{
        {QStringLiteral("hasShot"), has_shot},
        {QStringLiteral("text"),
         has_shot ? film_[static_cast<std::size_t>(pickedFilm_)].code : QStringLiteral("出片参数")},
        {QStringLiteral("tagText"), has_shot ? QStringLiteral("已出片") : QString()},
        {QStringLiteral("tagTone"), has_shot ? QStringLiteral("ok") : QStringLiteral("idle")},
    };
}

void VideoFlowPageModel::RebuildPanelFoot() {
    // 由真实状态算，不写死文案：数画布节点状态 + 队列运行数。
    int done = 0;
    int run = 0;
    int todo = 0;
    int fail = 0;
    for (const VideoGraphNode& node : graph_.nodes) {
        (node.state == QStringLiteral("done") ? done
         : node.state == QStringLiteral("run")  ? run
         : node.state == QStringLiteral("fail") ? fail
                                                : todo) += 1;
    }
    const int queued = taskStateView_.value(QStringLiteral("running")).toInt();
    const QString text =
        graph_.Empty()
            ? QString::fromUtf8(kIdleStatus)
            : QStringLiteral("运行 %1 · 待跑 %2 · 完成 %3").arg(run + queued).arg(todo).arg(done);
    const bool busy = (run + queued) > 0;
    panelFootView_ = QVariantMap{
        {QStringLiteral("dotTone"),
         graph_.Empty() ? QStringLiteral("idle")
                        : (busy ? QStringLiteral("busy")
                                : (fail > 0 ? QStringLiteral("danger")
                                            : (todo == 0 ? QStringLiteral("ok") : QStringLiteral("idle"))))},
        {QStringLiteral("run"), busy},
        {QStringLiteral("text"), text},
        {QStringLiteral("spec"), QStringLiteral("产物 → %1").arg(QString::fromLatin1(kOutputDir))},
    };
}

QVariantMap VideoFlowPageModel::panelTitle() const { return panelTitleView_; }
QVariantMap VideoFlowPageModel::panelFoot() const { return panelFootView_; }

void VideoFlowPageModel::RebuildAll() {
    RebuildGraph();
    RebuildChain();
    RebuildTasks();
    RebuildCut();
    RebuildFilm();
    RebuildPanelTitle();
    RebuildPanelFoot();
}

// ============================== 动作 ==============================

void VideoFlowPageModel::loadMock() {
    const VideoDemoSet demo = VideoDemo();
    graph_ = demo.graph;
    chain_ = flow::BuildVideoChain(demo.shots);
    queue_.Clear();
    shots_ = demo.taskShots;
    videos_ = demo.videos;
    film_ = demo.film;
    frames_.clear();
    pickedFilm_ = -1;
    selectedNodeId_ = demo.selectedNodeId;
    EnqueueAll();
    ShowRunning();
    status_ = QStringLiteral("H3 / RIFE / Encode 节点已加载；链式断点与任务状态可见");
    RebuildAll();
    emit changed();
}

void VideoFlowPageModel::validate() {
    // 输入与迁移前 VideoFlowWorkspace::Validate 完全一致（演示值：奇数宽高 /
    // 20 帧 / 10 张参考图），判据口径不能因为换渲染层而变。
    flow::GenerationValidationInput input;
    input.object_info_ready = true;
    input.width = 1001;
    input.height = 513;
    input.length = 20;
    input.reference_count = 10;
    const auto result = flow::ValidateForSubmit(input, nullptr);
    status_ = QString::fromStdString(result.Describe());
    emit changed();
}

void VideoFlowPageModel::togglePanel() {
    panel_folded_ = !panel_folded_;
    emit changed();
}

void VideoFlowPageModel::setTab(const QString& key) {
    // 只认三个已知页签：外来 key 会让 QML 的 Seg 全部落空（value 对不上谁都不亮）。
    if (key != QStringLiteral("chain") && key != QStringLiteral("task") &&
        key != QStringLiteral("cut")) {
        return;
    }
    if (tab_ == key) {
        return;
    }
    tab_ = key;
    emit changed();
}

void VideoFlowPageModel::selectNode(const QString& id) {
    const bool exists =
        std::any_of(graph_.nodes.begin(), graph_.nodes.end(),
                    [&id](const VideoGraphNode& node) { return node.id == id; });
    selectedNodeId_ = exists ? id : QString();
    RebuildGraph();
    emit changed();
}

void VideoFlowPageModel::EnqueueAll() {
    queue_.Clear();
    for (const auto& [id, label] : shots_) {
        queue_.Add(id, label.toStdString(), "H3 视频", flow::BatchPriority::ShotVideo);
    }
    RebuildTasks();
}

void VideoFlowPageModel::ShowRunning() {
    if (queue_.Snapshot().empty()) {
        EnqueueAll();
    }
    (void)queue_.StartNext();
    RebuildTasks();
}

void VideoFlowPageModel::enqueueAll() {
    EnqueueAll();
    emit changed();
}

void VideoFlowPageModel::showRunning() {
    ShowRunning();
    emit changed();
}

void VideoFlowPageModel::playAll() {
    // 与迁移前 FinalCutView 的「系统播放器连播」同口径：只回执，不拉起播放器。
    status_ = videos_.empty() ? QStringLiteral("还没有镜头视频可连播")
                              : QStringLiteral("已交给系统播放器播放列表");
    emit changed();
}

bool VideoFlowPageModel::exportSceneTo(const QString& dir) {
    const QString target = dir.isEmpty() ? QString::fromLatin1(kOutputDir) : dir;
    const bool ok = VideoExportScene(videos_, std::filesystem::path(util::PathFromUtf8(target.toStdString())));
    status_ = ok ? QStringLiteral("已导出到 %1").arg(target) : QStringLiteral("导出失败");
    // Q_INVOKABLE 返回 false 在 QML 侧没有任何提示，所以状态行必须自己说明。
    emit changed();
    return ok;
}

void VideoFlowPageModel::exportScene() { (void)exportSceneTo(QString::fromLatin1(kOutputDir)); }

void VideoFlowPageModel::pickFilmCell(int index) {
    if (index < 0 || index >= static_cast<int>(film_.size())) {
        return;
    }
    if (!film_[static_cast<std::size_t>(index)].ready) {
        return; // 未出片的格不可点（QML 侧同样置灰）
    }
    pickedFilm_ = pickedFilm_ == index ? -1 : index; // 再点一次取消选中
    RebuildFilm();
    RebuildPanelTitle();
    RebuildPanelFoot();
    emit changed();
}

void VideoFlowPageModel::setFirstFrame(int shotId, const QString& path) {
    frames_.emplace_back(static_cast<std::int64_t>(shotId),
                         std::filesystem::path(util::PathFromUtf8(path.toStdString())));
    RebuildTasks();
    emit changed();
}

void VideoFlowPageModel::SetChain(flow::VideoChain chain) {
    chain_ = std::move(chain);
    RebuildChain();
    RebuildPanelFoot();
    emit changed();
}

void VideoFlowPageModel::SetVideos(VideoShotList videos) {
    videos_ = std::move(videos);
    RebuildCut();
    emit changed();
}

void VideoFlowPageModel::SetShots(VideoShotList shots) {
    shots_ = std::move(shots);
    RebuildCut();
    emit changed();
}

void VideoFlowPageModel::SetFilmCells(std::vector<VideoFilmCellFact> cells) {
    film_ = std::move(cells);
    if (pickedFilm_ >= static_cast<int>(film_.size())) {
        pickedFilm_ = -1;
    }
    RebuildFilm();
    RebuildPanelTitle();
    emit changed();
}

// ============================== 探针 ==============================

QString VideoFlowPageModel::GraphProbe() const { return VideoGraphProbe(graph_, selectedNodeId_); }

QString VideoFlowPageModel::ChainProbe() const { return VideoChainProbe(chain_); }

QString VideoFlowPageModel::TaskProbe() const { return VideoTaskProbe(queue_, frames_.size()); }

QString VideoFlowPageModel::FinalProbe() const { return VideoCutProbe(videos_); }

QString VideoFlowPageModel::FilmProbe() const { return VideoFilmProbe(film_); }

QString VideoFlowPageModel::Probe() const {
    return QStringLiteral("canvas=%1; chain=%2; tasks=%3; final=%4; film=%5")
        .arg(GraphProbe(), ChainProbe(), TaskProbe(), FinalProbe(), FilmProbe());
}

int VideoFlowPageModel::NodeCount() const { return static_cast<int>(graph_.nodes.size()); }

int VideoFlowPageModel::LinkCount() const { return static_cast<int>(graph_.links.size()); }

} // namespace shine::app
