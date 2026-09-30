#pragma once
// shine::kit::Icon_Registry —— 48 个设计稿图标的路径表与惰性注册表
//
// 从 kit/Icon.cpp 拆出。这一族是**数据 + 查找**：
//   * kIconPaths —— Icon.jsx:3-48 的 46 条 d 路径逐字照抄（24 网格 / 1.6 描边），
//     末尾两个是本实现补的：minus（真正的减号，JSX 把 x 当减号用）与
//     flow（Gallery.jsx:140 传了未定义名字被兜底吞掉的 bug）。
//   * IconRegistry —— 首次取用时**一次性**把整表解析成 IconGlyph 并建名字索引；
//     之后每帧只做仿射变换 + 描边，不重解析。
//
// 单独成文件的理由：改动理由与 Icon_Path.h 正交 —— 这里是「加一个图标」
// （加一行字符串 + 可能换 fallback），那里是「改容错」。而且这张表是本文件里
// 唯一的数据源，把它和绘制混在一起时，「这个图标长什么样」要翻到几百行之外
// 去找，而「怎么画」又和它缠在同一屏。
//
// 出口只有 GetIcon() / IconNames()：注册表本身（Instance/Find/Names）是内部实现。
#include "ui/imgui/kit/Icon_Glyph.h"

#include <string_view>
#include <vector>

namespace shine::kit {

// 取图标。未知名字回落到 info（与 Icon.jsx:52 的兜底一致），但**不**照抄
// Gallery.jsx:140 传未定义 flow 的 bug —— flow 是本实现补的真图标。
[[nodiscard]] const IconGlyph* GetIcon(std::string_view name);

// 图标名清单（46 + minus + flow），供画廊与自检遍历。
[[nodiscard]] const std::vector<std::string_view>& IconNames();

} // namespace shine::kit
