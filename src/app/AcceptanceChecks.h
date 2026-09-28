#pragma once

#include <optional>
#include <string_view>

class QApplication;

namespace shine::app {

class MainWindow;

namespace acceptance {

// 按原入口顺序登记主窗口验收设施。
// 返回事件循环退出码表示某个独立模式已接管进程；空值表示继续正常主循环。
[[nodiscard]] std::optional<int> RegisterChecks(QApplication& app, MainWindow& window,
                                                 std::wstring_view commandLine);

} // namespace acceptance
} // namespace shine::app
