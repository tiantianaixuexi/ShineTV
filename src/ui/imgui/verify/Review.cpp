#include "ui/imgui/verify/Review.h"

#include "core/Log.h"
#include "ui/imgui/verify/Capture.h"

#include <cstdio>
#include <string>
#include <vector>

namespace shine::imguiverify {
namespace {

using imguiapp::Host;
using pages::Shell;

// 每张图固定推进的帧数：ImGui 自绘控件是逐帧提交的，字体图集也要一帧才烘焙上传。
// 3 帧足以让「本帧新建的图元」进 draw list，再多只是刷同样的内容。
constexpr int kSettleFrames = 3;

// 一张图的完整动作：**显式**设主题 + 显式设工作区 → 推进 N 帧 → 抓后备缓冲 → 存 PNG。
// 纪律：前置动作写全，不依赖"默认状态恰好是我要的"（phases.md P6 第 3 条）。
// 返回 false = 没抓到有效像素或编码失败，计入 manifest 的 FAILED。
bool Shoot(Host& host, Shell& shell, const std::filesystem::path& dir, std::string_view name,
           int workspace, shine::theme::ThemeId theme) {
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
        shine::log::Info("review shot {} saved ({}x{}, ws={}, theme={})", name, width, height,
                         workspace, shine::theme::ThemeIdKey(theme));
        return true;
    }
    shine::log::Error("review shot {} FAILED (ws={}, theme={})", name, workspace,
                      shine::theme::ThemeIdKey(theme));
    return false;
}

// 主题轮：画廊 + 资产两页最密（覆盖全部控件与 7 色调），足以暴露主题映射漏项。
// 6 工作区全跑 × 5 主题 = 30 张，判读成本远高于收益；这里用「全页 × 当前主题」
// 加「密页 × 5 主题」两段覆盖，30 张的覆盖面一分不少。
constexpr int kThemedWorkspaces[] = {
    static_cast<int>(pages::Workspace::Gallery),
    static_cast<int>(pages::Workspace::Assets),
};

} // namespace

ReviewResult RunReview(Host& host, Shell& shell, const std::filesystem::path& outputDir) {
    ReviewResult result;
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    const std::filesystem::path manifest = outputDir / "manifest.txt";
    std::filesystem::remove(manifest, ec); // 重跑不追加，避免上一次的行混进来
    WriteManifest(manifest, "# shine imgui review — capture 1600x960, glReadPixels(GL_RGBA)");

    const auto grab = [&](const std::string& name, int workspace, shine::theme::ThemeId theme) {
        if (Shoot(host, shell, outputDir, name, workspace, theme)) {
            result.captured++;
        } else {
            result.failed++;
        }
    };

    // ---- 第一段：当前主题下 7 个工作区各一张 ----
    const shine::theme::ThemeId base = shine::theme::CurrentThemeId();
    for (int workspace = 0; workspace < pages::kWorkspaceCount; ++workspace) {
        grab(std::string("ws-") + shine::pages::WorkspaceIcon(workspace), workspace, base);
    }

    // ---- 第二段：两个最密的页 × 5 套主题 ----
    for (const shine::theme::ThemeId theme : shine::theme::kAllThemes) {
        for (const int workspace : kThemedWorkspaces) {
            grab(std::string("theme-") + std::string(shine::theme::ThemeIdKey(theme)) + "-" +
                     shine::pages::WorkspaceIcon(workspace),
                 workspace, theme);
        }
    }

    // 跑完回到起始主题，别把用户的 theme.json 留在取证用的最后一套上。
    shell.SetTheme(base);

    WriteManifest(manifest, "# shots: " + std::to_string(result.captured) +
                                "  failed: " + std::to_string(result.failed));
    shine::log::Info("review done: {} saved, {} failed -> {}", result.captured, result.failed,
                     outputDir.string());
    return result;
}

} // namespace shine::imguiverify
