#pragma once

// shine::app::EnvironmentPath —— 读 Windows 环境块里的路径。
//
// P7 从 Qt 树搬过来的：原来在 src/ui/app/，那边整个目录连着 shine_kit / shine_qml /
// find_package(Qt6) 一起删，ImGui 前端只用到这一个函数。搬到 host/ 下与 AppEntry 同居，
// 让 src/ui/ 下只剩 src/ui/imgui 一棵树（AGENTS.md 的「所有 UI 收敛在唯一根目录」）。
//
// ⚠️ 不许改成读窄字符的环境块：Windows 的环境变量是 UTF-16，按本地 ANSI 码页读
//    会把中文路径解成乱码，而工程根、中文项目名都是合法路径。

#include <filesystem>

namespace shine::app {

// Windows 环境块是 UTF-16；路径必须用宽字符 API 读取，避免按本地代码页误解码。
[[nodiscard]] std::filesystem::path EnvironmentPath(const wchar_t* name);

} // namespace shine::app
