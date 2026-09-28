#pragma once
// 新建项目向导（P03，UI.md §2.2）—— 模态 QDialog（setModal(true)），约 720×520，4 步：
//   ① 选模板（三个 Radio 互斥由本容器负责 + 当前模板描述，数据来自 ProjectService::Templates()）
//   ② 命名与位置（Field 项目名称 / 位置 + 浏览…；实时校验，出错禁用「下一步」；
//      「目录名自动 = 项目名称」写在 help，实时预览「将创建：<完整路径>」）
//   ③ 一句话创意（TextArea limit 200，可留空，help「可留空，稍后在总控台补」）
//   ④ 确认（KeyValue 名称/位置/模板/创意 + 目录树预览 PreviewTree，目录在前、文件在后）
// 顶部 widgets::Tabs 当步骤指示条（点击只允许回跳到达过的步，不跳未到步）；
// 底部 [取消 Ghost][上一步 Secondary][下一步/创建 Primary]；Enter 前进/创建、Esc 取消
//（表单有内容先弹中文确认；文本区聚焦时 Enter 保持换行，不抢前进）。
// 创建（步 3 主行动）：按钮 SetLoading + ProgressBar「建目录 / 建库 / 写模板…」→
//   svc->Create(Spec())；成功 Toast + accept() + SetOnCreated(ref)；失败 Toast 中文原因 +
//   回到出错步 + StepErrorText()。
// 中文路径全链路 UTF-8（util::PathFromUtf8 / PathToUtf8）；颜色只取 kit / theme Token。
#include "widget/data/Panels.h"
#include "widget/controls/Controls.h"
#include "widget/controls/Feedback.h"
#include "widget/controls/Inputs.h"
#include "widget/controls/Navigation.h"
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
    void Validate(); // 落字段错误 + 刷新按钮态（有错禁用「下一步」）
    void UpdatePathPreview();
    void UpdateTemplateDesc();
    void GoNext(); // Enter / 下一步（步 3 = 创建）
    void DoCreate();
    void RefreshConfirm();
    void RequestCancel(); // Esc / 取消：表单有内容先弹中文确认
    [[nodiscard]] shine::project::ProjectSpec ComposeSpec() const;
    [[nodiscard]] FieldProblem NameLocProblem() const;
    [[nodiscard]] QString PremiseProblem() const;
    [[nodiscard]] QString StepProblem(int step) const;
    [[nodiscard]] bool HasContent() const;

    shine::project::ProjectService* svc_ = nullptr;
    std::function<void(const shine::project::ProjectRef&)> on_created_;
    std::vector<shine::project::ProjectTemplate> templates_;
    int step_ = 0;
    int max_step_ = 0; // 到达过的最大步（Tabs 只允许点回已到达步）
    int template_index_ = 0;
    bool touched_ = false; // 用户编辑 / SetSpec 后才落字段错误（初始态不飘红）
    bool syncing_ = false; // 程序化回填中，抑制回调
    QString create_error_; // 最近一次创建失败的中文原因

    widgets::Tabs* steps_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::vector<widgets::Radio*> radios_;
    QLabel* desc_ = nullptr;
    widgets::TextInput* name_edit_ = nullptr;
    widgets::Field* name_field_ = nullptr;
    widgets::TextInput* loc_edit_ = nullptr;
    widgets::Field* loc_field_ = nullptr;
    QLabel* path_preview_ = nullptr;
    widgets::TextArea* premise_edit_ = nullptr;
    widgets::Field* premise_field_ = nullptr;
    data::KeyValue* summary_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    widgets::ProgressBar* progress_ = nullptr;
    widgets::Button* prev_ = nullptr;
    widgets::Button* next_ = nullptr;
    widgets::Button* cancel_ = nullptr;
};

} // namespace shine::app
