#include "ui/imgui/verify/ReviewPixel.h"

#include "core/Log.h"
#include "ui/imgui/verify/Capture.h"

#include <vector>

namespace shine::imguiverify::detail {

// 像素内容的 FNV-1a 64。用途不是密码学，是「同一工作区在 5 套主题下的图必须互不相同」
// 这条判据 —— 主题没加载上时 5 张会**逐字节相同**，而只判「PNG 写出来了」的旧门禁
// 会照样报 overall=PASS（实测：exe 放到没有 themes/ 的目录里跑，37 张只有 13 张唯一，
// manifest 依旧 PASS）。这条判据把那种假绿变成 FAIL。
std::uint64_t HashPixels(const std::vector<std::uint8_t>& pixels) {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint8_t b : pixels) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

// 一张图的完整动作：**显式**设主题 + 显式设工作区 → 推进 N 帧 → 抓后备缓冲 → 存 PNG。
// 纪律：前置动作写全，不依赖"默认状态恰好是我要的"（phases.md P6 第 3 条）。
// 返回 false = 没抓到有效像素或编码失败，计入 manifest 的 FAILED。
// 第三个出参回传像素内容哈希，供「主题真的切了没有」判据用。
bool Shoot(imguiapp::Host& host, pages::Shell& shell, const std::filesystem::path& dir,
           std::string_view name, int workspace, shine::theme::ThemeId theme,
           std::uint64_t* hashOut) {
    shell.SetTheme(theme);
    shell.SetWorkspace(workspace);
    host.PumpFrames(kSettleFrames, [&shell](float dt) { shell.DrawFrame(dt); });

    // ⚠️ 抓图只能发生在 PumpFrames 之后、SwapBuffers 之前。
    // glReadPixels 默认读 GL_BACK，SwapBuffers 之后后备缓冲内容未定义。
    RECT client{};
    GetClientRect(host.window(), &client);
    const auto width = static_cast<std::uint32_t>(client.right);
    const auto height = static_cast<std::uint32_t>(client.bottom);
    const std::vector<std::uint8_t> pixels = host.CaptureBackBuffer();

    if (GrabAndSave(pixels, width, height, dir, name)) {
        if (hashOut != nullptr) {
            *hashOut = HashPixels(pixels);
        }
        shine::log::Info("review shot {} saved ({}x{}, ws={}, theme={})", name, width, height,
                         workspace, shine::theme::ThemeIdKey(theme));
        return true;
    }
    shine::log::Error("review shot {} FAILED (ws={}, theme={})", name, workspace,
                      shine::theme::ThemeIdKey(theme));
    return false;
}

} // namespace shine::imguiverify::detail
