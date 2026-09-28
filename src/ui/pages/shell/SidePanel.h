#pragma once
// shine::app —— 左侧栏（四区骨架的第 2 区，Ctrl+B 折叠/展开）。
//
// 定位：**工作区导航的归属地**。webui 设计稿把「书/卷/章树」「实体树」
// 「章节→镜头树」全部收进左侧栏，页面本身只留内容区；这里按同一口径重建，
// 页面把自己已有的导航控件**借**给侧栏显示（所有权仍在页面，见 AdoptNav）。
//
// 借用而不转移的所有权，和 RightPanel 的段是同一个理由：页面自己 new 出来、
// 自己拿指针刷新（RebuildTree / RebuildAssets），侧栏只负责摆放。
// 换工作区时 ClearNav() 把上一个导航 widget 还给页面（重新挂回页面原父级、
// 隐藏），绝不能 deleteLater —— 否则页面下一次刷新就是野指针。
//
// 空态：当前工作区没有导航时显示 kit::EmptyState，不写「即将接入」占位文案。
#include "ui/kit/controls/WidgetCommon.h"

#include <QFrame>
#include <QString>

class QLabel;
class QStackedWidget;
class QVBoxLayout;

namespace shine::app {

class SidePanel : public QFrame {
  public:
    explicit SidePanel(QWidget* parent = nullptr);

    // 把某个工作区的导航 widget 借进侧栏。reparent 由 Qt 完成，
    // 页面持有的指针依然有效（同一个 QWidget 对象）。
    // hostBox = 导航在页面里的原宿主（分栏容器）；originPage = 拥有它的页面本体。
    // 页面本体记下来，是因为 reparent 之后导航已不再是它的子孙，
    // 页面销毁时只能靠这个指针判断「侧栏里的导航归谁管」。
    void AdoptNav(const QString& workspace, QWidget* nav, QWidget* hostBox,
                  QWidget* originPage);

    // 归还当前导航给页面（只换父级 + 隐藏，不销毁）。workspace 名仅用于日志/探针。
    void ClearNav();

    // 把借出的导航还给它的原宿主盒（页面被关闭 / 切走时调用）。
    // 原宿主由 AdoptNav 记下；归还后页面重新拿到完整两列布局，不会留下一格空白。
    void ReturnNavTo(QWidget* box);

    void SetActiveWorkspace(const QString& workspace);

    // 当前是否有导航内容（探针与折叠判定用）
    [[nodiscard]] bool HasNav() const;
    [[nodiscard]] QString ActiveWorkspace() const { return active_; }
    // 当前借在侧栏里的导航本体（没有则 nullptr）。页面销毁前必须先取回。
    [[nodiscard]] QWidget* CurrentNav() const;
    // 借出导航的页面（所有权页面）。页面要销毁时据此判断是否必须先归还。
    [[nodiscard]] QWidget* CurrentNavOwner() const { return origin_page_; }

  private:
    QWidget* TakeCurrentNav();

    class QLabel* caption_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget* empty_ = nullptr;
    QVBoxLayout* empty_lay_ = nullptr;
    class QLabel* empty_text_ = nullptr;
    QWidget* host_ = nullptr;
    QVBoxLayout* host_lay_ = nullptr;
    // 借出时的原宿主盒（页面分栏容器）；归还时挂回这里的第一格
    QWidget* origin_box_ = nullptr;
    // 借出导航的页面本体：导航被 reparent 后不再是它的子孙，靠这个指针判断归属
    QWidget* origin_page_ = nullptr;
    QString active_;
};

} // namespace shine::app
