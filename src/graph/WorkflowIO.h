#pragma once
// shine::graph::WorkflowIO —— 三条读写链路
//
// 1. 自家图 `graph.json`（VNS 格式，见 GraphHost 的 SaveGraph/LoadGraph）；
// 2. **API 格式** `{"<id>":{"class_type":…,"inputs":…}}`（提交 `/prompt` 用）；
// 3. **ComfyUI 工作流 JSON v1.0**（编辑器格式：`version:1` + `nodes`/`links`/`groups`/`reroutes`/`extra`），
//    只读兼容旧版 **0.4**；字段照 `Doc/RULES-COMFY.md` §12.6 C，**不自造**。
//
// **工作流 JSON（编辑器）≠ API 格式**：前者带坐标/分组，后者才是提交格式。
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace shine::graph {

enum class WorkflowFormat { Unknown, ApiFormat, WorkflowV1, WorkflowV0_4 };

// 按**内容**判定（不看扩展名）
[[nodiscard]] WorkflowFormat DetectFormat(std::string_view jsonText) noexcept;
[[nodiscard]] const char* WorkflowFormatLabel(WorkflowFormat f) noexcept;

struct ImportReport {
    bool ok = false;
    WorkflowFormat format = WorkflowFormat::Unknown;
    std::size_t nodes = 0;
    std::size_t links = 0;
    std::size_t reroutes = 0;
    std::vector<std::string> unknownTypes; // 未注册的 class_type（**不静默丢弃**）
    std::vector<std::string> warnings;     // 中文，可直接显示
};

// 导出：编译当前图 → 写 API JSON 文件
bool ExportApiJson(std::string_view filePath);
// 导入 API JSON：建节点 + 按 inputs 推断连线
ImportReport ImportApiJson(std::string_view jsonText);

// 导出：ComfyUI 工作流 JSON v1.0（能被前端打开）
bool ExportWorkflowV1(std::string_view filePath);
// 导入：工作流 v1.0（+ 只读兼容 0.4）
ImportReport ImportWorkflowJson(std::string_view jsonText);

// 清空画布（导入前用；与 GraphHost::ClearGraph 同义）
void ClearGraph();

} // namespace shine::graph
