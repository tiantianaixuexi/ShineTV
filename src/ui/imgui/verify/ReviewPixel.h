#pragma once
// shine::imguiverify::detail —— 一张图的抓取 + 像素内容指纹
//
// 这个文件只管两件事，两件都不是判据，是判据的**输入**：
//   * Shoot —— 一张图的完整动作。主题与工作区都由调用方**显式**给，不吃「默认
//     状态恰好是我要的」。返回 false = 没抓到有效像素或编码失败。
//   * HashPixels —— 像素内容的 FNV-1a 64。消费它的只有两族判据，都写在
//     ReviewVerdict.cpp 里：「同一工作区在 5 套主题下的图两两不同」与
//     「受控图之间不许出现逐字节相同的两张」。
//
// 这两个函数**必须**留在同一族里：Shoot 顺手把这次抓的像素指纹交出去（hashOut），
// 判据于是读的是**真的画出来的那块缓冲**，不需要再抓一次、也不需要重新推断
// 「那张图当时到底是什么」。

#include "ui/imgui/host/Host.h"
#include "ui/imgui/pages/Shell.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace shine::imguiverify::detail {

// 每张图固定推进的帧数：ImGui 自绘控件是逐帧提交的，字体图集也要一帧才烘焙上传。
// 3 帧足以让「本帧新建的图元」进 draw list，再多只是刷同样的内容。
constexpr int kSettleFrames = 3;

// 像素内容的 FNV-1a 64。详见 ReviewVerdict.cpp 里两条判据对它的用法。
std::uint64_t HashPixels(const std::vector<std::uint8_t>& pixels);

// 一张图的完整动作：显式设主题 + 显式设工作区 → 推进 kSettleFrames 帧 →
// 读后备缓冲 → 存 PNG。hashOut 回传像素内容哈希（可传 nullptr）。
bool Shoot(imguiapp::Host& host, pages::Shell& shell, const std::filesystem::path& dir,
           std::string_view name, int workspace, shine::theme::ThemeId theme,
           std::uint64_t* hashOut);

} // namespace shine::imguiverify::detail
