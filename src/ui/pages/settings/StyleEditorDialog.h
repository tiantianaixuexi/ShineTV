#pragma once
// 样式编辑器对话框（P02-S8）：改 Token → 实时预览 → 另存为自定义主题。
// 组合 kit 控件（无新控件类型）；自定义主题入 ThemeService，重启仍可选。
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Surfaces.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class QLineEdit;
class QPushButton;

namespace shine::app {

class StyleEditorDialog final : public widgets::Dialog {
  public:
    explicit StyleEditorDialog(QWidget* parent = nullptr);

    // 程序化入口（自检/脚本）
    void SetTokenValue(int index, std::uint32_t rgba);
    [[nodiscard]] std::uint32_t TokenValue(int index) const;
    [[nodiscard]] bool SaveAs(const std::string& name); // 另存为自定义主题 + 激活

  private:
    [[nodiscard]] theme::ColorToken Compose() const; // 值数组 → ColorToken
    void Preview();                                  // 实时预览（ThemeService::Preview）

    std::array<std::uint32_t, theme::kColorTokenCount> values_{};
    std::vector<QPushButton*> swatches_;
    std::vector<QLineEdit*> hexes_;
    QLineEdit* name_ = nullptr;
};

} // namespace shine::app
