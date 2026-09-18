#pragma once
// shine::util —— 系统"打开 / 定位"文件的**唯一入口**（P5.6 S1）
//
// 为什么单独一份：`app/output/OutputView.cpp`（双击 / 右键打开）与 `app/shots/ShotTableView.cpp`
// （分镜行「播放」）都要"交给系统默认程序"（视频 = 系统播放器，**不内嵌播放器**），
// 错误文案必须一致且是中文可操作的（`Doc/RULES-LANG.md`：A 版 API 文本要先转 UTF-8）。
#include "util/Encoding.h" // 顺带拿到 <windows.h>（目标里已全局定义 NOMINMAX / WIN32_LEAN_AND_MEAN）

#include <shellapi.h> // WIN32_LEAN_AND_MEAN 不会带进来，必须显式包含

#include <filesystem>
#include <string>
#include <system_error>

#include <fmt/format.h>

namespace shine::util {

// ShellExecute 的返回值 ≤ 32 时是错误码（`ShellExecuteW` 的约定），给中文解释
[[nodiscard]] inline std::string ShellErrorHint(INT_PTR code) {
    switch (code) {
    case 0: return "系统资源不足";
    case 2: return "找不到文件";
    case 3: return "找不到路径";
    case 5: return "访问被拒绝";
    case 11: return "文件格式无效";
    case 26: return "文件被其他程序占用";
    case 27: return "关联信息不完整";
    case 31: return "该类型没有关联任何程序";
    default: return "未知原因";
    }
}

// 交给系统默认程序打开。**返回空串 = 成功**；否则是中文错误（文件不存在 / 没有关联程序…）。
// ⚠️ 只在 UI 线程调（ShellExecuteW 会立刻返回，但可能短暂阻塞）。
[[nodiscard]] inline std::string ShellOpen(const std::filesystem::path& file) {
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) {
        return "文件不存在（可能已被移动或删除）：" + PathToUtf8(file);
    }
    const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(result);
    if (code <= 32) {
        return fmt::format("系统打不开该文件：{}（ShellExecute 返回 {}）—— {}", ShellErrorHint(code),
                           static_cast<long long>(code), PathToUtf8(file));
    }
    return {};
}

// 在资源管理器中定位该文件（选中）。**返回空串 = 成功**。
[[nodiscard]] inline std::string ShellReveal(const std::filesystem::path& file) {
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) {
        return "文件不存在（可能已被移动或删除）：" + PathToUtf8(file);
    }
    const std::wstring args = L"/select,\"" + file.wstring() + L"\"";
    const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(result);
    if (code <= 32) {
        return fmt::format("无法打开资源管理器：{}（ShellExecute 返回 {}）", ShellErrorHint(code),
                           static_cast<long long>(code));
    }
    return {};
}

} // namespace shine::util
