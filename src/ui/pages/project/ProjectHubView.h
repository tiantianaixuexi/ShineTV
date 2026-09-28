#pragma once
// 项目列表首屏（P03，UI.md §2.1）—— 1:1 对齐 webui/src/styles/views.css:5-131「项目 Hub」：
//
//   背景 views.css:9-12  两层 radial-gradient（accent-dim 18% -10% / info 9% 95% 0%）叠 bg.void，
//                      改由 paintEvent 自绘（QSS 无 radial-gradient；水墨主题下 accent = 墨色，
//                      自然退化成「墨色方块、无辉光」，与设计稿同源）。
//   容器 views.css:14-18 max-width 1080 居中 + padding 40 / 32 / 60（左右居中留白按视口实时算）。
//   头区 views.css:19-54 hub-hero(gap 18 / mb 34) + hub-logo(52 r14 grad-accent + shadow-accent)
//                      + hub-title(26 w800) + hub-sub(text.secondary) + hub-chips(右对齐 Tag×3)。
//   工具条 views.css:55-63 gap 10 / mb 18；搜索 280 + Segmented + 右对齐 [打开…][新建项目]。
//   计数 views.css:64-71 hub-count(text-muted 12 w600，mt22 mb10) =「最近项目 · 共 N 个项目」。
//   网格 views.css:72-76 repeat(auto-fill, minmax(240px,1fr)) gap 14 → QGridLayout + resizeEvent 重算列数。
//   卡片 views.css:77-120 封面固定 120 高（cover-tag 绝对定位 10,10）+ pbody padding 12/14/13
//                      + pname 14 w700 + 副标题（project.json 的 premise，省略号）
//                      + pmeta（模板名 · 相对时间，text-muted）+ pfoot（顶部分隔线 + [打开][目录][⋯]）
//                      + hover = accent-glow 描边 + shadow-2 抬升。
//   页脚 views.css:122-130 居中快捷键提示（Kbd + text-muted 12）。
//   空态 / 错误态：Card 包 kit EmptyState / ErrorState（views.css:158-163 的 <Card><.empty> 结构）。
//
// 真实数据口径：卡片内容全部来自 ProjectService::Recent() + project.json（名称 / premise / 模板 /
//   lastOpened），**不塞任何演示假数据**；没有 premise / 模板时该行留空而不是编数字。
// 颜色全部来自 kit / theme Token（零内联色值，check-layers rule 3）；封面不用外部图片资源。
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "project/Project.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QScrollArea;

namespace shine::app {

class ProjectHubView : public QWidget {
    Q_OBJECT
  public:
    explicit ProjectHubView(shine::project::ProjectService* svc, QWidget* parent = nullptr);
    void Refresh(); // 重读最近列表并重建卡片
    void SetOnOpenProject(std::function<void(const shine::project::ProjectRef&)> cb);
    void SetOnCreateProject(std::function<void()> cb); // 「新建项目」按钮
    void SetOnOpenPath(std::function<void()> cb);      // 「打开…」按钮（外部目录选择由集成方做）
    // 自检/截图辅助：
    [[nodiscard]] int CardCount() const;
    [[nodiscard]] QString StatusText() const; // 空态/加载/正常 的当前状态短语（中文）

  protected:
    void resizeEvent(QResizeEvent* ev) override;
    void paintEvent(QPaintEvent* ev) override; // views.css:9-12 两层径向光晕 + bg.void

  private:
    [[nodiscard]] QString HubQss() const; // 页面局部 QSS（换肤后重挂）
    // 网格可用宽度：由本页自身宽度推导（.hub-inner 定宽居中后减去左右 padding）。
    // 不能读 grid_host_->width() —— 布局尚未跑完时它是 0 或上一帧的旧值。
    [[nodiscard]] int AvailableCardWidth() const;
    [[nodiscard]] int ColumnCount() const; // views.css:72-76 的 auto-fill 折算
    void BuildCards();  // 依过滤词 + 排序重建卡片网格
    void UpdateState(); // 空态 / 错误态 / 无匹配 / 正常 显隐 + 状态短语
    void SortEntries();
    void OpenEntry(const shine::project::RecentEntry& entry);
    void RemoveEntry(const shine::project::RecentEntry& entry); // 只移除登记，不删文件
    void ShowCardMenu(const shine::project::RecentEntry& entry, const QPoint& globalPos);
    void RebuildIndex();

    shine::project::ProjectService* svc_ = nullptr;
    std::function<void(const shine::project::ProjectRef&)> on_open_;
    std::function<void()> on_create_;
    std::function<void()> on_open_path_;
    std::vector<shine::project::RecentEntry> entries_;
    QString filter_;
    int sort_ = 0; // 0 最近打开 / 1 名称
    bool loading_ = false;
    bool index_error_ = false;
    int cols_ = 0;

    QScrollArea* scroll_ = nullptr;
    QHBoxLayout* center_row_ = nullptr; // views.css:14-18 margin:0 auto 的居中留白
    widgets::Button* new_btn_ = nullptr;
    widgets::Button* open_btn_ = nullptr;
    widgets::SearchBox* search_ = nullptr;
    widgets::Segmented* sort_seg_ = nullptr;
    widgets::Spinner* spinner_ = nullptr;
    widgets::EmptyState* empty_ = nullptr;
    widgets::ErrorState* error_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* no_match_ = nullptr;
    QWidget* grid_host_ = nullptr;
    QGridLayout* grid_ = nullptr;
    std::vector<QWidget*> cards_;
};

} // namespace shine::app
