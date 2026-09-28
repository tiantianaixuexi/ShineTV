#pragma once

#include <optional>

class QApplication;

namespace shine::app {

// 加载主题/动效并运行主窗口创建前的验收分支。
// 返回事件循环退出码表示该分支已接管进程；空值表示继续正常启动。
[[nodiscard]] std::optional<int> RunStartupChecks(QApplication& app);

} // namespace shine::app
