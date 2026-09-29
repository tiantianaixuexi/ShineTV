#include "ui/imgui/verify/Review.h"

#include "core/Log.h"
#include "ui/imgui/verify/Capture.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace shine::imguiverify {
namespace {

using imguiapp::Host;
using pages::Shell;

// 每张图固定推进的帧数：ImGui 自绘控件是逐帧提交的，字体图集也要一帧才烘焙上传。
// 3 帧足以让「本帧新建的图元」进 draw list，再多只是刷同样的内容。
constexpr int kSettleFrames = 3;

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
bool Shoot(Host& host, Shell& shell, const std::filesystem::path& dir, std::string_view name,
           int workspace, shine::theme::ThemeId theme, std::uint64_t* hashOut) {
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

// 主题轮：画廊 + 资产两页最密（覆盖全部控件与 7 色调），足以暴露主题映射漏项。
// 6 工作区全跑 × 5 主题 = 30 张，判读成本远高于收益；这里用「全页 × 当前主题」
// 加「密页 × 5 主题」两段覆盖，30 张的覆盖面一分不少。
// 主题轮覆盖哪些页。P6.3 要求「6 工作区 × 5 主题 = 30 组」（phases.md:356），
// 早先只挑了画廊 + 资产两页，于是总控/小说/分镜/出图/出片在 4 套非默认主题下
// **从未被截过** —— 主题映射在这些页上漏项不会被任何证据图发现。
// 现在全量覆盖，判读成本靠文件名分组与 md5 排重来控制。
constexpr int kThemedWorkspaces[] = {
    static_cast<int>(pages::Workspace::Overview),
    static_cast<int>(pages::Workspace::Novel),
    static_cast<int>(pages::Workspace::Assets),
    static_cast<int>(pages::Workspace::Storyboard),
    static_cast<int>(pages::Workspace::ImageFlow),
    static_cast<int>(pages::Workspace::VideoFlow),
};

} // namespace

ReviewResult RunReview(Host& host, Shell& shell, const std::filesystem::path& outputDir) {
    ReviewResult result;
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    // ⚠️ 文件名是**脚本契约**：scripts/run_reviews.ps1:56 读的是 `shots-manifest.txt`。
    //    早先这里叫 `manifest.txt`，于是脚本永远读到 NO-REPORT，verdict 恒空 ——
    //    整个取证流水线一直在假绿。改名后还要在末尾写 `overall=PASS|FAIL`
    //    （脚本 :61 靠这条正则判 verdict），缺了它同样判不出来。
    const std::filesystem::path manifest = outputDir / kManifestName;
    std::filesystem::remove(manifest, ec); // 重跑不追加，避免上一次的行混进来
    WriteManifest(manifest, "# shine imgui review — capture 1600x960, glReadPixels(GL_RGBA)");

    const auto grab = [&](const std::string& name, int workspace, shine::theme::ThemeId theme) {
        std::uint64_t hash = 0;
        if (Shoot(host, shell, outputDir, name, workspace, theme, &hash)) {
            result.captured++;
        } else {
            result.failed++;
        }
        return hash;
    };

    // ---- 第一段：当前主题下全部工作区各一张（含组件画廊）----
    const shine::theme::ThemeId base = shine::theme::CurrentThemeId();
    for (int workspace = 0; workspace < pages::kWorkspaceCount; ++workspace) {
        grab(std::string("ws-") + shine::pages::WorkspaceIcon(workspace), workspace, base);
    }

    // ---- 第二段：6 个业务工作区 × 5 套主题（phases.md P6.3 的 30 组）----
    // 每个工作区记下 5 个哈希，末尾检查两两不同：**主题没真正切过去**时 5 张会完全一样。
    std::map<int, std::vector<std::uint64_t>> themedHashes;
    for (const shine::theme::ThemeId theme : shine::theme::kAllThemes) {
        for (const int workspace : kThemedWorkspaces) {
            themedHashes[workspace].push_back(grab(
                std::string("theme-") + std::string(shine::theme::ThemeIdKey(theme)) + "-" +
                    shine::pages::WorkspaceIcon(workspace),
                workspace, theme));
        }
    }

    // 跑完回到起始主题，别把用户的 theme.json 留在取证用的最后一套上。
    shell.SetTheme(base);

    // ---- 判据二：同一工作区在 5 套主题下的像素必须两两不同 ----
    // 只判「PNG 写出来了」的旧门禁会漏掉「主题压根没加载」这种整轮崩掉的情况。
    int identicalPairs = 0;
    for (const auto& [workspace, hashes] : themedHashes) {
        for (std::size_t i = 0; i < hashes.size(); ++i) {
            for (std::size_t j = i + 1; j < hashes.size(); ++j) {
                if (hashes[i] != 0 && hashes[i] == hashes[j]) {
                    ++identicalPairs;
                    shine::log::Error(
                        "review: ws={} themes #{} and #{} produced BYTE-IDENTICAL pixels — "
                        "theme switch did not take effect",
                        shine::pages::WorkspaceIcon(workspace), i, j);
                }
            }
        }
    }

    // ⚠️ overall 行必须存在且与退出码一致（脚本 :66 要求 PASS↔0 / FAIL↔1）。
    const bool pass = result.failed == 0 && identicalPairs == 0;
    WriteManifest(manifest, "# shots: " + std::to_string(result.captured) +
                                "  failed: " + std::to_string(result.failed) +
                                "  identical-theme-pairs: " + std::to_string(identicalPairs));
    WriteManifest(manifest, std::string("overall=") + (pass ? "PASS" : "FAIL"));
    shine::log::Info("review done: {} saved, {} failed -> {}", result.captured, result.failed,
                     outputDir.string());
    return result;
}

} // namespace shine::imguiverify
