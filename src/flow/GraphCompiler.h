#pragma once
// shine::flow::CompileToApiJson —— 图模型（节点/端口/连线/值）→ ComfyUI API JSON
//
// 输出形态：`{ "<id>": { "class_type": "<ClassName>", "inputs": { "<输入名>": 值 } } }`
//   - 连线值 = `["<上游 id>", <上游输出槽位>]`；
//   - 控件值 = 按 `InputDef::widget` 转成 JSON 数字 / 布尔 / 字符串（不写字符串化数字！）；
//   - 可选输入无值可省略；**必填输入既无连线也无控件值 → 编译失败**（不产出 JSON）。
//
// 确定性：节点 id 按 `flow::CanvasNodes()` 的画布遍历顺序从 1 递增 → 同一张图两次编译字节一致。
#include <cstddef>
#include <string>
#include <vector>

namespace shine::flow {

struct CompileError {
    std::string nodeId;   // 图内节点 id（可能为空 —— 图级错误）
    std::string nodeName; // 人类可读（显示名）
    std::string message;  // 中文，可直接显示
};

struct CompileResult {
    bool ok = false;
    std::string apiJson; // ok==true 时有效
    std::vector<CompileError> errors;
    std::size_t nodeCount = 0;
};

[[nodiscard]] CompileResult CompileToApiJson();

} // namespace shine::flow
