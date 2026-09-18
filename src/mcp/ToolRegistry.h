#pragma once
// shine::mcp::ToolRegistry —— 进程级 MCP 工具注册表（分模块）
//
// 各业务模块通过显式 RegisterMcpTools(reg) 注入工具；协议层（P7.3）只消费
// BuildToolsListJson / Call。与 shine::agent::ToolRegistry（小说 Agent）无关。
#include "mcp/MCPTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <yyjson.h>

namespace shine::mcp {

class ToolRegistry {
public:
    ToolRegistry() = default;

    // 进程级单例（业务启动时 bootstrap 用这个）
    [[nodiscard]] static ToolRegistry& Instance();

    // —— 模块 ——
    void EnsureModule(ModuleInfo info); // 同 id 已存在则只更新 title
    // 级联删除该模块工具 + 模块本身
    bool UnregisterModule(std::string_view id);
    void ClearModule(std::string_view id) { (void)UnregisterModule(id); }
    [[nodiscard]] std::vector<ModuleInfo> Modules() const;
    [[nodiscard]] std::vector<std::string> ToolNames(std::string_view moduleId) const;

    // —— 工具 ——
    // 同名覆盖（幂等）；name 空 / handler 空 → 拒绝并 warn
    void Register(Tool tool);
    [[nodiscard]] bool Unregister(std::string_view name);
    void Clear() noexcept;
    [[nodiscard]] const Tool* Find(std::string_view name) const;
    [[nodiscard]] std::size_t ToolCount() const noexcept { return tools_.size(); }
    [[nodiscard]] std::size_t ModuleCount() const noexcept { return modules_.size(); }

    // MCP tools/list：{"tools":[{"name","title","description","inputSchema"}]}
    // moduleId 不进 list；坏 schemaJson → 该工具 inputSchema 退化为 {"type":"object"}
    [[nodiscard]] std::string BuildToolsListJson() const;

    [[nodiscard]] CallOutcome Call(std::string_view name, yyjson_val* args);

private:
    std::vector<ModuleInfo> modules_;
    std::vector<Tool> tools_; // 注册顺序
};

} // namespace shine::mcp
