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

// 数出某个**纯色**在缓冲区里出现了多少像素。`rgb` 是 `0xRRGGBB`。
//
// ⚠️ 缓冲是 **RGBA**（`host.CaptureBackBuffer()` 的原样；`Capture.cpp` 里那句
//    「输入已经是 RGBA8，直接编码 —— 不要再交换通道」是权威），`R` 在偏移 0。
//    这里第一版按 `Capture.h` 的形参名当成 BGRA、读偏移 2 ⇒ **恒数到 0**，
//    而 0 恰好也是「图没上屏」的读数 —— 它伪装成了产品缺陷，白查了一轮。
//    那个误导人的形参名已订正（`bgraPixels` → `rgbaPixels`）。
//
// 这个函数存在的原因：这套截图**跨轮不可复现**（同一二进制连跑两轮，80 张里 48 张字节不同），
// 所以「真图上屏了没有」「纹理被淘汰后有没有自己回来」这类问题只能靠数色值来判，
// 不能靠比两张图。
std::uint32_t CountColorPixels(const std::vector<std::uint8_t>& pixels, std::uint32_t rgb);

// 一张图的完整动作：显式设主题 + 显式设工作区 + **显式定项目中心开态** →
// 推进 kSettleFrames 帧 → 读后备缓冲 → 存 PNG。hashOut 回传像素内容哈希（可传 nullptr）。
// pixelsOut 回传同一块缓冲的副本，供 `CountColorPixels` 用（可传 nullptr）。
//
// ⚠️ `hubShot` 就是那第三件「前置动作」：项目中心是整屏替身
//    （`Shell::DrawFrame` 在 `hubOpen_` 为真时**只画它**），而**应用启动就落在它上面**
//    （webui `store.jsx:13` 的 `useState('hub')` ⇒ C++ 侧 `ShellLayout::hubOpen_` 初值
//    为 true）。所以：
//      * `hubShot = false`（缺省，拍工作区）—— 必须先把项目中心**关掉**，否则 89 张
//        证据图会整屏都是项目中心那一页，而 manifest 里写的却是 ws-/theme- 的名字
//        （图名与内容不符，且**照样 saved**）；
//      * `hubShot = true` —— 拍的就是这个初始页本身，必须让它开着。
bool Shoot(imguiapp::Host& host, pages::Shell& shell, const std::filesystem::path& dir,
           std::string_view name, int workspace, shine::theme::ThemeId theme,
           std::uint64_t* hashOut, std::vector<std::uint8_t>* pixelsOut = nullptr,
           bool hubShot = false);

} // namespace shine::imguiverify::detail
