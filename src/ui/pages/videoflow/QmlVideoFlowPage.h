#pragma once
// ui/pages/videoflow/QmlVideoFlowPage —— 出片工作区的 QML 宿主（QML 迁移）
//
// 定位：**替代** VideoFlowWorkspace（Widgets 版已随迁移退役删除）。对外接口
// 刻意保持同名同义，外壳 MainWindow 的接线基本不用改：
//   SetContext(db, dir) —— 绑定项目（迁移前只改状态行，语义不变）。
//
// 一个 QuickHost（整页一块）—— 迁移前是「画布 + 工具栏 + 面板 + 胶片条」四个
// QWidget 拼在同一个 grid cell 上（靠对齐标志模拟 CSS absolute）。QML 侧把这四块
// 画在同一个根 item 里，浮动关系是显式锚点，所以不再需要那套对齐标志。
//
// ⚠️ 取证为什么不是「抓子控件」：迁移前 P08 分别抓 Canvas() / Chain() / Tasks() /
//    Final() 四个子控件。QML 整页只有一个 QQuickWidget，拿不到子控件，于是改成
//    「抓整页 + 按 QML 报出来的几何裁剪」（regionPanel / regionCanvas / …）。
//    像素仍然来自同一次真实抓帧，裁剪不改变视口，也不引入假视口。
//
// 数据一律走 VideoFlowPageModel：QML 侧只 `import Shine 1.0` 拿 ThemeBridge，
// 业务数据由宿主 setContextProperty 注入为 `Page`。
#include "flow/VideoChain.h"
#include "ui/pages/videoflow/VideoFlowPageModel.h"
#include "ui/pages/videoflow/VideoFlowVisualData.h"

#include <QWidget>

#include <QImage>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::qml {
class QuickHost;
}

namespace shine::app {

class QmlVideoFlowPage : public QWidget {
    Q_OBJECT

  public:
    explicit QmlVideoFlowPage(QWidget* parent = nullptr);
    ~QmlVideoFlowPage() override;

    // —— 与迁移前的 VideoFlowWorkspace 同名同义（外壳 / 取证按这套调用）——
    void SetContext(std::filesystem::path dbPath, std::filesystem::path projectDir);
    void LoadMock();
    void Validate();
    void TogglePanel();
    // 面板页签（chain / task / cut）。QWidget 版是 findChild<QTabWidget*>() 换下标，
    // 现在页签状态在桥上，这里是它唯一的入口。
    void SetTab(const QString& key);
    void SelectNode(const QString& id);

    // 演示 / 验收驱动（原先是直接操作四个子控件，现在走桥上的同一批动作）
    void EnqueueAll();
    void ShowRunning();
    void SetChain(flow::VideoChain chain);
    void SetShots(VideoShotList shots);
    void SetVideos(VideoShotList videos);
    void SetFilmCells(std::vector<VideoFilmCellFact> cells);
    void SetFirstFrame(std::int64_t shotId, const std::filesystem::path& path);
    void PickFilmCell(int index);
    // 导出一场到指定目录（验收写临时目录用）；产品按钮走默认 output/videos
    bool ExportScene(const std::filesystem::path& dir);

    // —— 验收探针（与迁移前同名同格式）——
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] QString ChainProbe() const;
    [[nodiscard]] QString TaskProbe() const;
    [[nodiscard]] QString FinalProbe() const;
    [[nodiscard]] QString FilmProbe() const;
    [[nodiscard]] QString GraphProbe() const;
    [[nodiscard]] int NodeCount() const;
    [[nodiscard]] int LinkCount() const;
    // 当前页签键。**取代** findChild<QTabWidget*>()：那个反查在 QWidget 版就已经
    // 恒不成立（P09 的三处同类断言是历史残留），换成一个真能判的读数。
    [[nodiscard]] QString ActiveTab() const;

    // 取证：抓整页（region 为空）或按 QML 报的几何裁一块。
    // 返回 null 表示场景图没给出画面（调用方按「抓图失败」记，不写 saved）。
    [[nodiscard]] QImage GrabRegion(const QString& region = {});

  private:
    // 每个会改数据的动作之后都要跑一帧：changed() 只让 QML 绑定重算，
    // 场景图还得自己推一帧，否则抓到的还是回填前的那一帧。
    void PumpFrame();

    VideoFlowPageModel* model_ = nullptr;
    shine::qml::QuickHost* content_ = nullptr;
};

} // namespace shine::app
