#pragma once
// shine::video —— API 图校验器：把生成的 API JSON 对着**运行中 ComfyUI 的 `/object_info`** 过一遍
//
// 为什么必须有它（2026-09-17 的教训）：
//   ComfyUI 的 `/prompt` 校验**只查"必填输入缺失"，不拒绝多余/写错的输入名**
//   （`execution.py::validate_inputs` 只遍历 schema 里声明的输入）。
//   于是"把 `ref_images.ref_image_1` 写成 1 基（正确的是 0 基 `ref_image_0`）"这种错误
//   **不会报错，只会让参考图被静默丢掉** —— 生成的视频看起来"能跑"，但完全不是用户要的。
//   所以：类名与输入名必须拿运行中的节点定义对账，**编译期就挡住**。
//
// 校验内容（全部给**中文**错误）：
//   ① 每个节点的 `class_type` 在本机 `/object_info` 里存在；
//   ② 每个输入名是该节点声明的输入 —— 动态输入按 ComfyUI 自己的展开规则算：
//      * `COMFY_AUTOGROW_V3`（`template.prefix` + `max`）→ 合法键是 `父.prefix{i}`（**i 从 0 开始**）
//      * `COMFY_DYNAMICCOMBO_V3` → 父键的值必须是某个 `options[].key`，子键要落在该选项声明的输入里
//   ③ COMBO 的值必须在 `options` 里（拼错模型名/采样器名会在这里被抓住）；
//   ④ 每个**必填**输入都得给（值或连线）；
//   ⑤ 连线 `["<上游 id>", 槽位]` 指向的节点存在、槽位号不越界。
#include "comfy/ComfyNodeDef.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace shine::video {

// 节点定义查询：返回 nullptr = 本机没有这个类。用函数指针（无捕获 lambda 可直接赋），不进热路径。
using NodeDefLookup = const comfy::NodeTypeDef* (*)(std::string_view className);

struct GraphCheckIssue {
    std::string nodeId;
    std::string className;
    std::string inputName;
    std::string message; // 中文，可直接显示
};

struct GraphCheckResult {
    bool ok = false;                  // ok = 没有任何 issue
    std::size_t nodeCount = 0;
    std::size_t inputCount = 0;       // 输入名展开后的总数（诊断用）
    std::vector<GraphCheckIssue> issues;
};

// `lookup` 为空 → 返回 ok=true 且 note 说明"未校验"（离线自测用；**生产路径必须传**）
[[nodiscard]] GraphCheckResult ValidateApiGraph(std::string_view apiJson, NodeDefLookup lookup);

} // namespace shine::video
