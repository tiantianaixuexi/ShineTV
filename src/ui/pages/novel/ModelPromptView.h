#pragma once
// P04-S8「模型与 Prompt 面板」（用户口述序 = S8；PLAN 表里是 S10）判据：
//   * 模型分层：规划 / 写作 / 评审 / 提取 四档各自可配（`AppSettings` + `llm::ResolveModel`）
//   * **评审模型 ≠ 写作模型 时才允许 auto**（`06` §2.7 M5 = `novelcore::CrossReviewEffective`），
//     面板顶部一盏规则灯实时显示 allow/block + 中文原因
//   * 外置 Prompt 查看：四角色 prompt 只读（`agent::DefaultPrompt(role)`，可被
//     `<工程>/prompts/<role>.md` 覆盖），支持[复制]与[另存为覆盖文件]
//   * **Secrets 不显明文 Key**：Key 输入框 password 回显 + 面板只显示 `llm::MaskKey` 掩码
#include "ui/kit/controls/WidgetCommon.h" // QWidget

#include <QString>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class QLabel;
class QPlainTextEdit;
class QStackedWidget;

namespace shine::widgets {
class Button;
class Select;
class TextInput;
} // namespace shine::widgets

namespace shine::app {

// 角色定义（key = `llm::ResolveModel` 的 role 名；tier = `09` §2.4 档位）
struct ModelRoleDef {
    const char* role;  // planner / writer / critic / extractor
    const char* name;   // 中文名
    const char* tier;   // 中 / 高 / 高 / 低
    const char* note;   // 一句话职责
};
[[nodiscard]] const std::vector<ModelRoleDef>& ModelRoles();

class ModelPromptView : public QWidget {
  public:
    explicit ModelPromptView(QWidget* parent = nullptr);
    ~ModelPromptView() override;
    ModelPromptView(const ModelPromptView&) = delete;
    ModelPromptView& operator=(const ModelPromptView&) = delete;

    // 工程根（Prompt 覆盖文件目录 `<工程>/prompts/<role>.md`）；空 = 只看内置
    void SetProjectDir(const std::filesystem::path& dir);
    void Reload(); // 重读 Settings + Prompt

    // 设置某档模型（写 AppSettings 并落盘）；返回实际生效的模型 id
    [[nodiscard]] std::string SetRoleModel(std::string_view role, std::string_view modelId);
    // 保存 API Key（只写不读回；面板只显掩码）
    void SetApiKey(std::string_view key);
    // 另存 Prompt 覆盖文件（`prompts/<role>.md`）
    bool ExportPromptOverride(std::string_view role, std::string_view text, QString* err = nullptr);

    // auto 允许与否（评审模型 ≠ 写作模型；`novelcore::CrossReviewEffective`）
    [[nodiscard]] bool AutoAllowed() const;
    [[nodiscard]] QString AutoRuleText() const;

    // —— 自动化探针（S8 判据；产品代码不用）——
    [[nodiscard]] QString ModelProbe() const;  // 四档模型 + auto 规则灯 + 掩码
    [[nodiscard]] QString PromptProbe() const; // 四角色 prompt 来源（内置/外置）与长度

  private:
    QWidget* BuildModels();
    QWidget* BuildPrompt();
    void RefreshRuleLamp();
    QString EffectivePrompt(std::string_view role) const;
    void ShowRolePrompt(std::string_view role);
    static std::string PromptOverridePath(const std::filesystem::path& dir, std::string_view role);

    std::filesystem::path project_dir_;
    std::string current_role_; // 当前查看的 Prompt 角色
    std::vector<widgets::Select*> model_sel_;
    std::vector<QLabel*> model_now_;
    widgets::TextInput* key_input_ = nullptr;
    QLabel* key_mask_ = nullptr;
    QLabel* rule_ = nullptr;
    QLabel* hint_ = nullptr;
    QStackedWidget* prompt_stack_ = nullptr;
    QPlainTextEdit* prompt_view_ = nullptr;
    std::vector<widgets::Button*> role_btns_;
};

} // namespace shine::app
