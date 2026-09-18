#pragma once
// shine::util —— **编码与路径边界**（本项目唯一入口，不要再各模块自写转换）
//
// 背景（2026-09-16 实测踩坑）：本项目内部约定**文本一律 UTF-8**，而 Windows 上到处是别的编码：
//   * Win32 的 **A 版** API（`FormatMessageA`、CRT `strerror`、libhv `socket_strerror()`）返回的是
//     **当前 ANSI 代码页**（本机 `GetACP()==936`/GBK）字节；
//   * Win32 的 **W 版** API / `std::filesystem::path` 用的是 **UTF-16**；
//   * ImGui / 我们自己的 `std::string` 用 **UTF-8**。
//
// 乱码/失败长什么样：
//   * GBK 字节塞进 UTF-8 的 `std::string` → **ImGui 把每个非法字节渲染成一个 `?`**
//     （实测 UI 显示「网络错误: ?????????」，其实是 GBK「函数不正确。 」9 字节）；
//   * `std::string(w.begin(), w.end())` 这种"逐字符截断"→ 中文路径直接报废（设置文件路径踩过）；
//   * `std::filesystem::path{std::string}`（窄字符构造）按 **ANSI 代码页**解释 UTF-8 → 中文目录名错；
//   * `path.string()` 反向同理 → 中文路径变乱码/抛异常；
//   * 用**窄字符串**打开文件（`std::ifstream(std::string)` / `fopen`）在中文路径下打不开。
//
// 三条硬规则：
//   1. 进 UI / 进日志 / 进 JSON 的文本 → **UTF-8**；
//   2. 要喂 W 版 API 的文本 → 先 `Utf8ToUtf16()`；只能拿 A 版文本 → `AcpToUtf8()` 转一次；
//   3. **路径一律用 `std::filesystem::path` 传递与打开**，进出字符串走 `PathFromUtf8 / PathToUtf8`。
//
// 相关：纯字符串处理（trim / lower / split…）在 `util/Strings.h`；文件读写用 `util/File.h`。
#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace shine::util {

// ============ 1. 文本：UTF-16 / ANSI ↔ UTF-8 ============

// UTF-16（Windows 原生宽字符；W 版 API 的返回值）→ UTF-8
[[nodiscard]] inline std::string Utf16ToUtf8(std::wstring_view utf16) {
    if (utf16.empty()) {
        return {};
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), out.data(), need, nullptr, nullptr);
    return out;
}

// UTF-8（我们的 `std::string`）→ UTF-16（喂 W 版 API）
[[nodiscard]] inline std::wstring Utf8ToUtf16(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}

// 当前 ANSI 代码页（CP_ACP）→ UTF-8。**给 A 版 API 的返回值用**
[[nodiscard]] inline std::string AcpToUtf8(std::string_view acp) {
    if (acp.empty()) {
        return {};
    }
    const int need = MultiByteToWideChar(CP_ACP, 0, acp.data(), static_cast<int>(acp.size()), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(need), L'\0');
    MultiByteToWideChar(CP_ACP, 0, acp.data(), static_cast<int>(acp.size()), wide.data(), need);
    return Utf16ToUtf8(wide);
}

// UTF-8 → 当前 ANSI 代码页（只有"必须传 A 版 API"时才用；优先改 W 版）
[[nodiscard]] inline std::string Utf8ToAcp(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const std::wstring wide = Utf8ToUtf16(utf8);
    const int need = WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(need), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), out.data(), need, nullptr, nullptr);
    return out;
}

// Win32/WSA 系统消息（**W 版**取，天然不会有 ANSI 乱码）；取不到（或消息为空）返回空串。
// `code` 可以是 Win32 错误码，也可以是 WSA 错误码（如 10061 = 连接被拒绝）。
[[nodiscard]] inline std::string WinErrorMessage(unsigned long code) {
    wchar_t buffer[512] = {};
    const DWORD written = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
                                             FORMAT_MESSAGE_MAX_WIDTH_MASK,
                                         nullptr, code, 0, buffer, 512, nullptr);
    if (written == 0) {
        return {};
    }
    const std::wstring_view text{buffer, written};
    const auto begin = text.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(L" \t\r\n");
    return Utf16ToUtf8(text.substr(begin, end - begin + 1));
}

// ============ 2. 路径：UTF-8 ⇄ std::filesystem::path ============

// UTF-8 字符串（设置 / JSON / UI 里的路径）→ `path`
// ⚠️ 不要写 `std::filesystem::path{std::string}`：那按 **ANSI 代码页**解释，中文会错
[[nodiscard]] inline std::filesystem::path PathFromUtf8(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    return std::filesystem::path{Utf8ToUtf16(utf8)};
}

// `path` → UTF-8（**日志 / UI / 剪贴板 / 存 JSON 都用它**，别用 `path.string()`）
[[nodiscard]] inline std::string PathToUtf8(const std::filesystem::path& path) {
    return Utf16ToUtf8(path.wstring());
}

// 只要文件名部分（列表/标题常用）
[[nodiscard]] inline std::string FileNameToUtf8(const std::filesystem::path& path) {
    return Utf16ToUtf8(path.filename().wstring());
}

} // namespace shine::util
