#pragma once
// 项目列表首屏（P03，UI.md §2.1）—— 启动第一屏：
//   顶行「ShineTV Studio」大标题 + [新建项目]（Primary）[打开…]（Secondary）；
//   其下一行 SearchBox「搜索项目…」（按名称过滤）+ 排序 Segmented（最近打开 / 名称）；
//   「最近项目」小标题 + 卡片网格（QGridLayout，卡片约 220×172，自适应换行）。
// 卡片 = Card(Elevated)：16:9 自绘封面占位（Token 色渐变 + 首字大字，绝不闪白）
//   + 项目名（Semibold）+ 模板中文名 · 相对时间（今天/昨天/N 天前/更早日期）；
//   单击 = svc->Open 打开项目；右键菜单：打开 / 在资源管理器中显示 / 从列表移除（不删文件）。
// 状态规范（UI.md §3）：空态 EmptyState「还没有项目」+ 主行动「新建项目」；
//   加载中 StatusText()=「加载中」；索引损坏 → ErrorState + 重试重建索引；打开失败 → Toast。
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
class QLabel;
class QResizeEvent;

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

  private:
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
