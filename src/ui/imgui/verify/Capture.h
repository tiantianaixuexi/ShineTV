#pragma once
// shine::imguiverify —— D3D11 取证抓图（P6.1）
//
// Qt 版的 47 个 verify 文件全靠 QWidget::grab / QQuickWidget::grabToImage，
// 在 ImGui 下全部失效 —— ImGui 不保留任何"控件树"，只有帧缓冲。
// 所以取证改成：渲染完一帧后 glReadPixels 读后备缓冲 → PNG。
//
// 保留的既有契约（scripts/*.ps1 依赖，改了会连带改脚本）：
//   * manifest 行格式  "<name> <bytes> saved|FAILED"
//   * 全部 SHINE_* 环境变量名与分派点
//
// 取证纪律（refactor/phases.md P6，本机反复踩过）：
//   1. 多图跑完**先排 md5**。任意两张逐字节相同 = 有一张没拍到它承诺的状态。
//   2. 等待结果写进 manifest（"converged|TIMEOUT"），别丢返回值。
//   3. 每张图的前置动作显式写全，别依赖"默认状态恰好是我要的"。
//   4. 像素差不是内容指标：跨进程 ~2000px 噪声，跨构建 ~40000px。
//   5. 抓图必须在 SwapBuffers **之前**：glReadPixels 读 GL_BACK，交换后内容未定义。
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::imguiverify {

// 后备缓冲是 BGRA，PNG 要 RGBA —— 这里做通道交换。
// 返回 false = 像素尺寸为 0 或编码失败。
bool SavePng(const std::vector<std::uint8_t>& bgraPixels, std::uint32_t width,
             std::uint32_t height, const std::filesystem::path& path);

// 写一行 manifest（追加）。返回写入的字节数。
void WriteManifest(const std::filesystem::path& manifest, std::string_view line);

// 抓一张图的完整动作：抓像素 → 存盘 → 写 manifest 行。
// 返回 true = saved。
bool GrabAndSave(const std::vector<std::uint8_t>& bgraPixels, std::uint32_t width,
                 std::uint32_t height, const std::filesystem::path& dir, std::string_view name);

} // namespace shine::imguiverify
