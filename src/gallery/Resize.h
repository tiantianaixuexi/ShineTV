#pragma once
// shine::gallery::Resize —— 缩略图缩放（G-S6 S1）
//
// 自研：整数因子 box 均值降采样 + 双线性收尾；保持宽高比，长边 ≤ maxSide。
// 不引入第三方缩放库（见 `Plan/任务/G-图片库.md` §3.3）。
#include "gallery/Image.h"

#include <cstdint>

namespace shine::gallery {

// 失败（非法图 / maxSide==0 / 分配失败）返回未 `valid()` 的对象。
// 源图长边已 ≤ maxSide 时返回 1:1 拷贝（便于统一走 GPU 上传路径）。
[[nodiscard]] Image ResizeBox(const Image& src, std::uint32_t maxSide);

} // namespace shine::gallery
