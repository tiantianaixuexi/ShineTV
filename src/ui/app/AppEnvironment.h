#pragma once

#include <filesystem>

namespace shine::app {

// Windows 环境块是 UTF-16；路径必须用宽字符 API 读取，避免按本地代码页误解码。
[[nodiscard]] std::filesystem::path EnvironmentPath(const wchar_t* name);

} // namespace shine::app
