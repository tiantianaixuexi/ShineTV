#pragma once
// shine::imguiapp::RunApp —— ImGui 前端的进程入口
//
// 照搬 Qt 版 AppEntry.cpp 的启动顺序（refactor/architecture.md §5），只换宿主：
//   ConfigureAppDataSandbox()  ← 必须在读任何路径之前（它改 APPDATA）
//     → CLI 分派（--mcp-stdio / --novel-*，行为与 Qt 版完全一致）
//     → 载入 5 套主题 JSON + theme.json 持久化
//     → Host（Win32 + WGL/GL3 + gpu::AttachDevice）
//     → async::Init / gallery::Init
//     → 帧循环
//     → gallery::Shutdown / async::Shutdown / log::Shutdown
//     → std::_Exit（故意不 join worker）
namespace shine::imguiapp {

// 返回值即进程退出码。ThemeDirOverride 供取证 harness 指向别处的主题目录。
[[nodiscard]] int RunApp(int argc, char** argv);

} // namespace shine::imguiapp
