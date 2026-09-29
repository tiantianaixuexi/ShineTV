#pragma once
// P04-S1 小说工作区骨架（PLAN §5 S1 / UI.md §1）：
//   左：书 → 卷 → 章 树（DataTree）+ 章节卡片列表（状态点）；
//   中：当前章节（标题/状态/摘要/正文预览）；右：属性（KeyValue）/ 预览（P05/P07 接入）。
// 判据：千章项目滚动流畅；选章即时切换（树 ↔ 卡片双向同步）。
// 多书系列（一部一库）：书 = project::ListBooks（默认书=项目根，系列书=books/<书名>/）。
#include "ui/kit/controls/WidgetCommon.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QStandardItem;
class QWidget;

#include <filesystem>

namespace shine::data {
class DataTree;
class KeyValue;
} // namespace shine::data

namespace shine::project {
struct ProjectRef;
}

namespace shine::app {

class ChapterFlowView;
class DraftView;
class AutoRunPanel;
class InitChainView;
class ModelPromptView;
class ReviewView;
class StateDiffView;
class WorldBoardView;

// StatusTagRow —— 「一组固定文案的 Tag，靠可见性切换当前项」。
// webui 的 Tag 文案随状态走（`<Tag tone={st.tone}>{st.label}</Tag>`），而 kit::Tag
// 的文字与 tone 都是构造期固定的（kit 已冻结），运行期改文案只能重建控件。
// 状态取值是有限枚举，所以改成「每个取值预建一个 Tag + 只切可见性」：
// 不重建控件、不产生泄漏，视觉与设计稿一致。
class StatusTagRow : public QWidget {
  public:
    // items：{文案, tone}；key：与文案同序的取值键
    explicit StatusTagRow(const QStringList& keys, const QStringList& texts,
                          const QStringList& tones, QWidget* parent = nullptr);

    void Show(const QString& key); // 切到该键（未知键 → 全部隐藏）

  private:
    QHash<QString, QWidget*> tag_of_;
    QString current_;
};

class NovelWorkspace : public QWidget {
  public:
    explicit NovelWorkspace(QWidget* parent = nullptr);

    // 载入项目所有书；lastNovel = project.json ui.lastNovel（空 = 默认书）→ 预选该书首章。
    void LoadFromRef(const project::ProjectRef& ref, const std::string& lastNovel);

    // —— 自动化探针（S1 判据；产品代码不用）——
    [[nodiscard]] qint64 LastLoadMs() const { return load_ms_; }
    [[nodiscard]] int TreeChapterCount() const { return tree_chapters_; }
    [[nodiscard]] int CardCount() const;
    bool SelectChapterAt(int flatIndex); // 选章即时切换（树/卡双向同步）
    [[nodiscard]] QString SelectionProbe() const;
    [[nodiscard]] QListWidget* CardList() const { return cards_; }
    // 交给外壳右侧检查器承载的属性内容（页面自己不再摆右栏，见 NovelWorkspace.cpp 布局段）
    [[nodiscard]] QWidget* InspectorBody() const { return inspector_body_; }
    // 借给外壳左侧栏的导航（书→卷→章 树 + 章节卡片）。所有权仍在本页；外壳换工作区时收回。
    [[nodiscard]] QWidget* NavWidget() const { return nav_host_; }
    // 导航在页面里的原宿主（分栏容器）：外壳归还导航时挂回这里的第一格
    [[nodiscard]] QWidget* NavHostBox() const { return nav_box_; }
    // 设定台（P04-S2）：模式行 [设定] 的内容页；S3+ 的关系/伏笔页也落在这里
    [[nodiscard]] WorldBoardView* WorldBoard() const { return world_; }
    // 初始化链（P04-S4）：模式行 [初始化] 的内容页（I1–I16 流水线 + 门禁 N1–N14 + 前情导入）
    [[nodiscard]] InitChainView* InitChain() const { return init_; }
    // 章节流水线（P04-S5）：模式行 [流水线] 的内容页（T1–T17 StageFlow + 产物 + 断点续跑）
    [[nodiscard]] ChapterFlowView* Flow() const { return flow_; }
    // 正文草稿（P04-S6）：[章节] 页中栏正文区（流式 / 中断落盘 / 重试 / 手改哈希）
    [[nodiscard]] DraftView* Draft() const { return draft_; }
    // 评审与校验（P04-S7）：[评审] 页（rubric 8 维 + K01–K29 + 修复轮入口）
    [[nodiscard]] ReviewView* Review() const { return review_; }
    // 模型与 Prompt（P04-S8）：[模型] 页（四档模型分层 + auto 规则灯 + 外置 Prompt + Key 掩码）
    [[nodiscard]] ModelPromptView* ModelPrompt() const { return modelPrompt_; }
    // 状态提交（P04-S9）：[状态] 页（StateDiff + G1–G5 + 唯一 COMMIT 入口 + 回滚）
    [[nodiscard]] StateDiffView* StateDiffPanel() const { return stateDiff_; }
    // 无人值守（P04-S10）：[自动] 页（三模式 / 预算 / S1–S12 / 检查点 / 报告）
    [[nodiscard]] AutoRunPanel* AutoRun() const { return autoRun_; }
    // 自动化/截图包：按中心页序号切换（0 章节 … 7 自动）。
    void ShowPage(int index) { SwitchCenter(index); }

  private:
    struct VolumeNode {
        qint64 id = 0;
        int ord = 0;
        QString title;
    };
    struct ChapterNode {
        qint64 id = 0;
        qint64 volumeId = 0;
        QString book;
        QString volumeTitle;
        int ord = 0;
        QString title;
        QString status;
        QString summary;
        QString body;
        int words = 0;
    };
    struct BookNode {
        QString title;
        bool isDefault = false;
        std::filesystem::path dbPath;    // 一部一库（DraftView/WorldBoard 落库用）
        std::filesystem::path workDir;   // 该书工作目录（ReviewView 产物核对用）
        std::vector<VolumeNode> volumes;
    };

    void RebuildTree();
    void RebuildCards();
    void ShowCurrent();
    // 「本章产物」三行：按当前章刷 tooltip 里的真实产物路径
    void RefreshArtifacts();
    void SyncPage(int page);       // 只给**可见页**同步当前章（S1 选章耗时判据）
    void SwitchCenter(int index); // 模式行切换：0=[章节] 1=[设定]

    std::vector<BookNode> books_;
    std::vector<ChapterNode> chapters_;
    QHash<int, QStandardItem*> tree_item_;   // 平铺下标 → 树节点
    QHash<int, QListWidgetItem*> card_item_; // 平铺下标 → 卡片
    int current_ = -1;
    bool syncing_ = false;
    qint64 load_ms_ = 0;
    int tree_chapters_ = 0;

    data::DataTree* tree_ = nullptr;
    QListWidget* cards_ = nullptr;
    QLabel* title_ = nullptr;
    StatusTagRow* status_ = nullptr; // 章头状态标记（未写/草稿/待评审/已提交/失败）
    StatusTagRow* top_status_ = nullptr; // 顶栏同一状态的标记（.ntop-right 内）
    StatusTagRow* run_ = nullptr;     // 顶栏运行态标记（空闲 / 运行中）
    class QTimer* run_poll_ = nullptr; // 运行态轮询（发起生成后短时运行，无 running 即停）
    int idle_ticks_ = 0;               // 连续多少次轮询没看到 running
    QLabel* meta_ = nullptr;          // 章头右侧计数（正文实计字数 · 出自卷）
    QLabel* summary_ = nullptr;
    DraftView* draft_ = nullptr; // 正文草稿（P04-S6 DraftView：流式/中断/重试/哈希）
    data::KeyValue* props_ = nullptr;
    QWidget* inspector_body_ = nullptr;
    std::vector<QWidget*> artifact_rows_; // 「本章产物」三行（同序 kChapterArtifacts），只用来刷 tooltip
    // 借给外壳左侧栏的导航容器（章节树 + 章节卡片）；外壳收回时重新挂回 nav_box_
    QWidget* nav_host_ = nullptr;
    QWidget* nav_box_ = nullptr;
    QStackedWidget* centerStack_ = nullptr; // 中栏：[章节]/[设定]/[初始化]/[流水线]/[评审]/[模型]/[状态]/[自动] 八页
    WorldBoardView* world_ = nullptr;
    InitChainView* init_ = nullptr;
    ChapterFlowView* flow_ = nullptr;
    ReviewView* review_ = nullptr;
    ModelPromptView* modelPrompt_ = nullptr;
    StateDiffView* stateDiff_ = nullptr;
    AutoRunPanel* autoRun_ = nullptr;
    QPushButton* modeChapters_ = nullptr;
    QPushButton* modeWorld_ = nullptr;
    QPushButton* modeInit_ = nullptr;
    QPushButton* modeFlow_ = nullptr;
    QPushButton* modeReview_ = nullptr;
    QPushButton* modeModel_ = nullptr;
    QPushButton* modeState_ = nullptr;
    QPushButton* modeAuto_ = nullptr;
};

} // namespace shine::app
