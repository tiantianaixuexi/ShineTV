#pragma once
// shine::comfy::ComfyWorkflows —— **从运行中的 ComfyUI 服务器取工作流**（P3.7）
//
// 为什么要有它：手写图（照 Python 源码猜输入名）一定会和服务器定义漂移
// （2026-09-17 就栽过：autogrow 子键写成 1 基，`/prompt` 还**静默忽略**多余键）。
// 正确做法是**把服务器自己带的工作流取下来 → 解析 → 生成节点**，再在此基础上改。
//
// 三个来源（2026-09-17 在 ComfyUI 0.36.0 上**逐个实测 200**，不是照文档猜的）：
//   1. `Template`   —— 内置模板（`workflow_templates` 包，官方 H3 工作流就在这）
//                      目录 `GET /templates/index.json`（571 KB）
//                      正文 `GET /templates/<name>.json`（如 `video_minimax_h3_r2v.json`，43 KB）
//   2. `CustomNode` —— custom_nodes 自带的示例工作流
//                      目录 `GET /workflow_templates` → `{"<模块名>": ["<模板名>", …]}`
//                      正文 `GET /api/workflow_templates/<模块名>/<模板名>.json`
//   3. `User`       —— 用户在前端保存的工作流
//                      目录 `GET /userdata?dir=workflows&recurse=true` → `["a.json", "sub/b.json"]`
//                      正文 `GET /userdata/workflows%2F<相对路径>`
//                      ⚠️ 斜杠必须转义成 `%2F`（aiohttp 的 `{file}` 不跨 `/`）；本机返回 `[]` 属正常
//
// 取回来的文本**不在这里解析成图**：交给 `graph/WorkflowIO.h` 的
// `DetectFormat` + `ImportApiJson` / `ImportWorkflowJson`（P3.6 已就位，两种格式都能吃）。
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::comfy {

enum class WorkflowSource : int {
    Template = 0, // 内置模板
    CustomNode,   // custom_nodes 示例
    User,         // 我保存的工作流
};

// 中文分组名（直接进 UI）
[[nodiscard]] const char* WorkflowSourceLabel(WorkflowSource s) noexcept;

struct RemoteWorkflow {
    WorkflowSource source = WorkflowSource::Template;
    std::string name;        // 唯一键（模板名 / `模块/名` / `workflows/相对路径`）
    std::string title;       // 显示名（模板有 title；其余用文件名）
    std::string category;    // 分组：模板分类（如 "Video"）/ 模块名 / "我保存的"
    std::string fetchPath;   // **相对 API 路径且已百分号转义**（以 `/` 开头），拼 baseUrl 即可
    std::string description; // 模板描述（可空，UI 当 tooltip）
};

struct RemoteWorkflowList {
    bool ok = false;
    std::string error;                 // 中文；`ok=false` 时可直接显示
    std::vector<RemoteWorkflow> items; // 按 模板 → 示例 → 我的 三段排好序
    std::vector<std::string> warnings; // 某个来源失败但其它成功（如"没装模板包"）
};
using RemoteWorkflowListCb = std::function<void(RemoteWorkflowList)>;

// 拉目录：worker 线程做 3 个 GET，回调在 UI 线程（MEMORY.md 异步任务规范）
void FetchWorkflowListAsync(std::string_view baseUrl, RemoteWorkflowListCb cb);

struct RemoteWorkflowText {
    bool ok = false;
    std::string error; // 中文
    std::string text;  // 工作流 JSON 原文（编辑器格式或 API 格式，交给 DetectFormat 判定）
};
using RemoteWorkflowTextCb = std::function<void(RemoteWorkflowText)>;

// 拉单个工作流正文
void FetchWorkflowAsync(std::string_view baseUrl, const RemoteWorkflow& ref, RemoteWorkflowTextCb cb);

// —— 以下纯解析函数**离线可测**（不碰网络）：UI 与自测共用同一份口径 ——
// 模板目录 `index.json`：顶层是 [{moduleName, category, title, type, templates:[{name,title,description,…}]}]
[[nodiscard]] bool ParseTemplateIndex(std::string_view json, std::vector<RemoteWorkflow>& out, std::string& error);
// `/workflow_templates`：顶层是 {模块名: [模板名, …]}
[[nodiscard]] bool ParseCustomNodeTemplateList(std::string_view json, std::vector<RemoteWorkflow>& out,
                                               std::string& error);
// `/userdata?dir=workflows`：顶层是 ["相对路径", …]
[[nodiscard]] bool ParseUserWorkflowList(std::string_view json, std::vector<RemoteWorkflow>& out, std::string& error);

// `workflows/子目录/名 字.json` → `/userdata/workflows%2F%E5%AD%90...`（对外暴露是为了自测断言转义规则）
[[nodiscard]] std::string BuildUserWorkflowFetchPath(std::string_view relativeUtf8);

} // namespace shine::comfy
