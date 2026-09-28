#pragma once
// 新建项目向导（P03，UI.md §2.2）—— 模态 QDialog（setModal(true)），约 600×560，4 步，
// 结构对齐 webui ProjectHub.jsx:16-113 的 Wizard：
//   头 ui.css:955-965 .modal-h：✦ accent 字形 + 「新建项目」标题 + 右对齐步骤条；
//            步骤条按 ui.css:1031-1072 .steps 自绘（kit 只有下划线式 Tabs，无 Steps 控件）
//   ① 选模板   jsx:34-52 三张可点卡片（图标 + 名称 + 一句说明 + 选中打勾），
//              选中态 = accent 描边（数据来自 ProjectService::Templates()）
//   ② 命名与位置 jsx:53-70 Field 项目名称 / Field 位置（help「项目目录将创建在该路径下」）+ 浏览…
//              + fill-muted 卡片预览「将创建目录」的完整路径与骨架首层
//              实时校验，出错禁用「下一步」；位置 help 在填名后补「目录名自动 = 项目名称」
//   ③ 一句话创意 jsx:71-82 TextArea limit 200，标题带 (N/200) 计数，help 说明可留空
//   ④ 确认     jsx:83-98 摘要卡（名称 / 路径 / 模板 Tag / 有无创意 Tag）+ 一句后续说明，
//              并保留 PreviewTree 的目录树预览（真实骨架数据，比设计稿的一行文本更全）
// 底 ui.css:970-976 .modal-f：左侧一行小字说明 + 右对齐 [取消 Ghost][上一步 Secondary]
//            [下一步 / 创建 Primary]；Enter 前进/创建、Esc 取消
//（表单有内容先弹中文确认；文本区聚焦时 Enter 保持换行，不抢前进）。
// 创建（步 3 主行动）：按钮 SetLoading + ProgressBar「建目录 / 建库 / 写模板…」→
//   svc->Create(Spec())；成功 Toast + accept() + SetOnCreated(ref)；失败 Toast 中文原因 +
//   回到出错步 + StepErrorText()。
// 中文路径全链路 UTF-8（util::PathFromUtf8 / PathToUtf8）；颜色只取 kit / theme Token。
#include "ui/kit/data/Panels.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/Inputs.h"
#include "project/Project.h"

#include <QDialog>
#include <QString>

#include <functional>
#include <vector>

class QKeyEvent;
class QLabel;
class QStackedWidget;
class QTreeWidget;

namespace shine::app {

class ProjectWizardDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ProjectWizardDialog(shine::project::ProjectService* svc, QWidget* parent = nullptr);
    void SetOnCreated(std::function<void(const shine::project::ProjectRef&)> cb);
    [[nodiscard]] int CurrentStep() const; // 0..3
    void GoToStep(int step);               // 仅供自检/截图/自动化
    [[nodiscard]] shine::project::ProjectSpec Spec() const; // 当前表单值
    // 自检/截图辅助（不弹文件框）：
    void SetSpec(const shine::project::ProjectSpec& spec); // 回填表单（含路径），触发校验
    [[nodiscard]] QString StepErrorText() const; // 当前步的错误文案（无错为空串）

  protected:
    void keyPressEvent(QKeyEvent* ev) override;

  private:
    struct FieldProblem {
        QString text;
        bool on_name = true;
    };

    void BuildUi();
    [[nodiscard]] QString WizardQss() const; // 页面局部 QSS（换肤后重挂）
    void Validate(); // 落字段错误 + 刷新按钮态（有错禁用「下一步」）
    void UpdatePathPreview();
    void GoNext(); // Enter / 下一步（步 3 = 创建）
    void DoCreate();
    void RefreshConfirm();
    void RequestCancel(); // Esc / 取消：表单有内容先弹中文确认
    void SelectTemplate(int index); // ① 步卡片选中（互斥由本容器负责）
    [[nodiscard]] shine::project::ProjectSpec ComposeSpec() const;
    [[nodiscard]] FieldProblem NameLocProblem() const;
    [[nodiscard]] QString PremiseProblem() const;
    [[nodiscard]] QString StepProblem(int step) const;
    [[nodiscard]] bool HasContent() const;

    shine::project::ProjectService* svc_ = nullptr;
    std::function<void(const shine::project::ProjectRef&)> on_created_;
    std::vector<shine::project::ProjectTemplate> templates_;
    int step_ = 0;
    int max_step_ = 0; // 到达过的最大步（步骤条只允许点回已到达步）
    int template_index_ = 0;
    bool touched_ = false; // 用户编辑 / SetSpec 后才落字段错误（初始态不飘红）
    bool syncing_ = false; // 程序化回填中，抑制回调
    QString create_error_; // 最近一次创建失败的中文原因

    QWidget* steps_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::vector<QWidget*> template_cards_;
    widgets::TextInput* name_edit_ = nullptr;
    widgets::Field* name_field_ = nullptr;
    widgets::TextInput* loc_edit_ = nullptr;
    widgets::Field* loc_field_ = nullptr;
    QLabel* path_preview_ = nullptr;
    QLabel* premise_caption_ = nullptr; // ③ 步标题里的 (N/200) 计数
    widgets::Field* premise_field_ = nullptr;
    widgets::TextArea* premise_edit_ = nullptr;
    QLabel* confirm_name_ = nullptr;
    QLabel* confirm_path_ = nullptr;
    widgets::Tag* confirm_tpl_tag_ = nullptr;
    widgets::Tag* confirm_idea_tag_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    widgets::ProgressBar* progress_ = nullptr;
    widgets::Button* prev_ = nullptr;
    widgets::Button* next_ = nullptr;
    widgets::Button* cancel_ = nullptr;
};

} // namespace shine::app
