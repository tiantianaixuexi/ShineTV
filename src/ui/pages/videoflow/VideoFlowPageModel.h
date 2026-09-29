#pragma once
// ui/pages/videoflow/VideoFlowPageModel —— 出片页的 C++→QML 数据桥（QML 迁移）
//
// 定位：把出片页原本直接喂给五个 QWidget 控件的数据，原样投影给 QML 页。
// **零新增真值**：链式判定来自 flow::VideoChain、队列状态来自 flow::BatchRenderQueue、
// 行 → 字段的映射与状态词表全在 VideoFlowVisualData.h（迁移前写在各自的 Rebuild() 里）。
//
// ============================ 为什么全是 Q_PROPERTY ============================
// 与 AssetPageModel 同一纪律：**随变化重算的量一律属性 + NOTIFY**，方法只留给
// 「带参数的动作」。QML 绑定只对属性读建立依赖；方法调用在依赖图里是空的，
// 求值一次就永久缓存，之后发多少 changed() 都不会重算。
//
// 复合值统一用 QVariantList（of QVariantMap）/ QVariantMap：QML 侧直接就是
// JS 的数组 + 对象字面量，页面里 `Page.taskRows[i].code` 一行都不用改。
//
// ⚠️ **空态 map 必须带全零值键**：只回 {none:true} 的话，QML 侧读 `.summary` /
// `.spec` / `.text` 拿到 undefined → 一屏 "Unable to assign [undefined] to QString"。
// 判「有没有」用 `none` 这个标志位，不要对字段做 truthiness。
//
// 取景（zoom / viewX / viewY）是**纯视图状态**，留在 QML 侧自己算，不上桥 ——
// 上桥就成了第二个真值，且没有任何 C++ 消费者。
// 线程：全部在 GUI 线程同步完成（与迁移前一样：本页无网络、无图片解码，
// 最重的 IO 是一次几十字节的 scene_playlist.txt 写出）。
#include "flow/BatchRender.h"
#include "flow/VideoChain.h"
#include "ui/pages/videoflow/VideoFlowVisualData.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace shine::app {

class VideoFlowPageModel : public QObject {
    Q_OBJECT

  public:
    explicit VideoFlowPageModel(QObject* parent = nullptr);
    ~VideoFlowPageModel() override;

    // —— 生命周期：与迁移前的 VideoFlowWorkspace::SetContext 同名同义 ——
    void SetContext(std::filesystem::path dbPath, std::filesystem::path projectDir);

    // ===================== 画布（①） =====================
    // 节点：{id,title,sub,icon,x,y,state}。state 取 todo/run/done/fail。
    [[nodiscard]] Q_PROPERTY(QVariantList graphNodes READ graphNodes NOTIFY changed)
    QVariantList graphNodes() const;

    // 连线：[{from,to}]（设计稿的 .fnode 连线只认两端节点）
    [[nodiscard]] Q_PROPERTY(QVariantList graphLinks READ graphLinks NOTIFY changed)
    QVariantList graphLinks() const;

    // {none,nodes,links,selected}
    [[nodiscard]] Q_PROPERTY(QVariantMap graphState READ graphState NOTIFY changed)
    QVariantMap graphState() const;

    // 选中节点 id（空串 = 没选）。**必须是属性**：QML 侧的高亮环是绑定，
    // 方法调用在依赖图里是空的、只求值一次。
    [[nodiscard]] Q_PROPERTY(QString selectedNodeId READ selectedNodeId NOTIFY changed)
    QString selectedNodeId() const { return selectedNodeId_; }

    // ===================== 首尾帧链（②） =====================
    // 行：{fromCode,toCode,policy,tagText,tagTone,detail}
    [[nodiscard]] Q_PROPERTY(QVariantList chainRows READ chainRows NOTIFY changed)
    QVariantList chainRows() const;

    // {none,links,connected,broken,summary}
    [[nodiscard]] Q_PROPERTY(QVariantMap chainState READ chainState NOTIFY changed)
    QVariantMap chainState() const;

    // ===================== 视频任务（③） =====================
    // 行：{code,frameText,framePath,tagText,tagTone,progress,run}
    [[nodiscard]] Q_PROPERTY(QVariantList taskRows READ taskRows NOTIFY changed)
    QVariantList taskRows() const;

    // {none,jobs,frames,running,summary}
    [[nodiscard]] Q_PROPERTY(QVariantMap taskState READ taskState NOTIFY changed)
    QVariantMap taskState() const;

    // ===================== 成片（④） =====================
    // 行：{code,path,tagText,tagTone}
    [[nodiscard]] Q_PROPERTY(QVariantList cutRows READ cutRows NOTIFY changed)
    QVariantList cutRows() const;

    // 概览 KV：[{key,value,valueToken}]
    [[nodiscard]] Q_PROPERTY(QVariantList cutKv READ cutKv NOTIFY changed)
    QVariantList cutKv() const;

    // {none,count,exportPath,summary}
    [[nodiscard]] Q_PROPERTY(QVariantMap cutState READ cutState NOTIFY changed)
    QVariantMap cutState() const;

    // ===================== 胶片条（⑤） =====================
    // 格：{code,duration,ready,videoPath,picked}
    [[nodiscard]] Q_PROPERTY(QVariantList filmCells READ filmCells NOTIFY changed)
    QVariantList filmCells() const;

    // {none,cells,ready,picked}
    [[nodiscard]] Q_PROPERTY(QVariantMap filmState READ filmState NOTIFY changed)
    QVariantMap filmState() const;

    // ===================== 外壳 / 面板状态 =====================
    // 浮动面板折叠（QWidget 版的 TogglePanel）。折叠态由 QML 自己控制可见性，
    // 这里是给宿主 / 验收读的**同义状态**（单一真值在桥上，不在 QML 里另存一份）。
    [[nodiscard]] Q_PROPERTY(bool panelFolded READ panelFolded NOTIFY changed)
    bool panelFolded() const { return panel_folded_; }

    // 面板头：{text,tagText,tagTone,hasShot}。选中了胶片格就给「镜号 · 状态」，
    // 没选给「出片参数」—— 与设计稿 fp-h 的「shot.code · shot.action」同位。
    [[nodiscard]] Q_PROPERTY(QVariantMap panelTitle READ panelTitle NOTIFY changed)
    QVariantMap panelTitle() const;

    // 面板底（fp-f）：{dotTone,text,spec,run}。**由真实状态算**（节点状态计数 /
    // 队列运行数），不写死文案 —— 迁移前那行「H3 生成 · RIFE 待跑 · Encode 排队」
    // 是常量，与页面实际状态无关。
    [[nodiscard]] Q_PROPERTY(QVariantMap panelFoot READ panelFoot NOTIFY changed)
    QVariantMap panelFoot() const;

    // 面板当前页签：chain / task / cut（QWidget 版是 QTabWidget 的当前下标）。
    // 同样是**属性**：QML 的 Seg 高亮是绑定，方法调用只求值一次。
    [[nodiscard]] Q_PROPERTY(QString tab READ tab NOTIFY changed)
    QString tab() const { return tab_; }

    // 顶部工具栏的状态行（校验结果 / 绑定项目 / 动作回执）
    [[nodiscard]] Q_PROPERTY(QString status READ status NOTIFY changed)
    QString status() const { return status_; }

    // ===================== 动作（Q_INVOKABLE） =====================
    // 灌演示数据集（画布 4 节点 3 连线 + 3 镜头链 + 3 条任务 + 2 条成片 + 3 格胶片）。
    // 打开页面**不会**自动灌 —— 见 VideoFlowVisualData.h 的说明。
    Q_INVOKABLE void loadMock();
    // 提交前参数校验（flow::ValidateForSubmit），结果写进 status。
    Q_INVOKABLE void validate();
    Q_INVOKABLE void togglePanel();
    Q_INVOKABLE void setTab(const QString& key);
    Q_INVOKABLE void selectNode(const QString& id);
    // 全部入队 / 推进一个任务到运行中
    Q_INVOKABLE void enqueueAll();
    Q_INVOKABLE void showRunning();
    // 系统播放器连播（与迁移前一样：只回执，不真的拉起播放器）
    Q_INVOKABLE void playAll();
    // 导出一场 → output/videos/scene_playlist.txt。
    // dir 为空走默认目录（QWidget 版是相对 CWD 的 "output/videos"，
    // 这里保持同口径）；验收要写临时目录时传显式路径。
    Q_INVOKABLE bool exportSceneTo(const QString& dir = {});
    Q_INVOKABLE void exportScene();
    // 点胶片格（只对就绪格有效）：选中它 → 面板头换成该镜状态
    Q_INVOKABLE void pickFilmCell(int index);
    // 首帧来源登记（P08-S6 覆盖这条：任务行要保留首帧路径）
    Q_INVOKABLE void setFirstFrame(int shotId, const QString& path);

    // —— 验收探针（与迁移前同名同格式，P08 按这几串解析）——
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] QString ChainProbe() const;
    [[nodiscard]] QString TaskProbe() const;
    [[nodiscard]] QString FinalProbe() const;
    [[nodiscard]] QString FilmProbe() const;
    [[nodiscard]] QString GraphProbe() const;
    // 画布规模（P08-S2 的 reuse-canvas 断言：节点 4 / 连线 3）
    [[nodiscard]] int NodeCount() const;
    [[nodiscard]] int LinkCount() const;
    // 当前页签键（验收用它判「切页真的生效了」，而不是靠 QTabWidget 反查）
    [[nodiscard]] QString ActiveTab() const { return tab_; }
    // 任务队列跑一次（ShowRunning 语义），供验收直接驱动
    void EnqueueAll();
    void ShowRunning();
    // 直接换链（验收要拍断链那一态）
    void SetChain(flow::VideoChain chain);
    void SetVideos(VideoShotList videos);
    void SetShots(VideoShotList shots);
    void SetFilmCells(std::vector<VideoFilmCellFact> cells);

  signals:
    void changed();

  private:
    void RebuildAll();
    void RebuildGraph();
    void RebuildChain();
    void RebuildTasks();
    void RebuildCut();
    void RebuildFilm();
    void RebuildPanelTitle();
    void RebuildPanelFoot();

    std::filesystem::path dbPath_;
    std::filesystem::path projectDir_;
    QString status_;
    QString tab_ = QStringLiteral("task"); // 与设计稿的默认页签一致（'task'）
    bool panel_folded_ = false;
    QString selectedNodeId_;
    int pickedFilm_ = -1;
    std::size_t framesCount_ = 0;

    VideoGraphFacts graph_;
    flow::VideoChain chain_;
    flow::BatchRenderQueue queue_;
    VideoShotList shots_;
    VideoShotList videos_;
    std::vector<std::pair<std::int64_t, std::filesystem::path>> frames_;
    std::vector<VideoFilmCellFact> film_;

    QVariantList graphNodesView_;
    QVariantList graphLinksView_;
    QVariantMap graphStateView_;
    QVariantList chainRowsView_;
    QVariantMap chainStateView_;
    QVariantList taskRowsView_;
    QVariantMap taskStateView_;
    QVariantList cutRowsView_;
    QVariantList cutKvView_;
    QVariantMap cutStateView_;
    QVariantList filmCellsView_;
    QVariantMap filmStateView_;
    QVariantMap panelTitleView_;
    QVariantMap panelFootView_;
};

} // namespace shine::app
