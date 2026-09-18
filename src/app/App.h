#pragma once

namespace shine::app {

bool Init();
void Shutdown();
void DrawFrame();

// 自检模式（自动化验收用，见 `SHINE_EXIT_AFTER_SEC` 环境变量）：
// 到时返回 true，主循环据此正常退出 —— 让"跑起来看效果"的验收不需要外部杀进程。
[[nodiscard]] bool ShouldExit() noexcept;

} // namespace shine::app
