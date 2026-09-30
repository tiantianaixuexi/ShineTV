#pragma once
// shine::imguiverify::detail —— 取证流水线的输入 fixture
//
// 这里造的**不是产品数据**，是让取证流水线有东西可拍的输入文件：格式与
// business 层落盘格式完全一致的校验报告 JSON，加上产物页要扫的 output 目录。
//
// ⚠️ 纪律（这一族自己的理由全在 ReviewFixture.cpp 里）：**应用侧绝不造报告** ——
//    DrawDockReports 只读 business 层落盘的文件。fixture 只存在于输出目录下的
//    临时工程里，跑完由调用方把工程根还原回去。
//
// 少了 fixture 就是覆盖洞：页面拍到了、列表分支从来没被执行过，而 manifest 照样
// 记 saved。「图拍到了」与「分支跑到了」是两件事。

#include <cstdint>
#include <filesystem>

namespace shine::imguiverify::detail {

// 在 root 下造一份样例工程。返回 false = 写盘失败（调用方据此跳过那一族取证）。
bool SeedReportFixture(const std::filesystem::path& root);

// fixture 造的那两张**纯色设定图**的颜色（`assets/linwan_base.png` / `zhouyi_base.png`）。
//
// 为什么把颜色提出来当契约：取真要验「真图上屏了没有」，而这套截图**跨轮不可复现**
//（同一二进制连跑两轮，80 张里 48 张字节不同 —— 时钟 / 动画相位 / 日志时间戳），
// 所以「比两张图」不是可用判据；**数某个色值免疫**，是这一族唯一可靠的手段。
// 两个颜色刻意选在**五套主题调色板之外**：数到它就只能来自这张图，不会与控件撞色。
inline constexpr std::uint32_t kSheetColorA = 0xFF2D55u; // assets/linwan_base.png
inline constexpr std::uint32_t kSheetColorB = 0x7CFF00u; // assets/zhouyi_base.png

// 派生链四层的图（`visual_artifacts` 的 front / turnaround / base_body / wardrobe，
// 挂在资产 1 上）。**四张各一色** ⇒ 「链上第几层有图、第几层没有」可以**逐层按像素判**，
// 而不是只能看整体哈希 —— 层与层之间搞混了（比如 front 画到了 wardrobe 那格）
// 在灰度上几乎看不出来，但数色值一眼就能分开。
inline constexpr std::uint32_t kChainColorFront = 0x22C55Eu;    // assets/chain/front.png
inline constexpr std::uint32_t kChainColorTurn = 0xEAB308u;     // assets/chain/turnaround.png
inline constexpr std::uint32_t kChainColorBody = 0x8B5CF6u;     // assets/chain/base_body.png
inline constexpr std::uint32_t kChainColorWardrobe = 0x14B8A6u; // assets/chain/wardrobe.png

} // namespace shine::imguiverify::detail
