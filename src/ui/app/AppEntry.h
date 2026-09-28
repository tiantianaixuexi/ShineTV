#pragma once

namespace shine::app {

// 初始化宿主并运行 Qt 主循环。命令行分派也由该入口统一处理。
[[nodiscard]] int RunApp(int argc, char** argv);

} // namespace shine::app
