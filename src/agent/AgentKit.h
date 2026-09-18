#pragma once
// 多 Agent：定义存库，可被 agent_meta 更新；职责分离，字段由 Agent 动态生成
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "agent/ToolRegistry.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelTypes.h"

namespace shine::agent {

struct AgentDefRow {
    novelcore::RowId id = 0;
    std::string agent_id;      // 稳定 id：novel_writer / character / field_builder…
    std::string name;          // 中文名
    std::string role_tags;     // 逗号分隔：write,chapter
    std::string system_prompt; // 该 Agent 的系统提示（可被 meta Agent 改）
    std::string tools_json;    // 允许的 tool 名数组 ["get_entity",…]
    std::string output_hint;   // 期望输出结构说明
    int enabled = 1;
    int is_builtin = 1;        // 内置可改 prompt，删需 force
    int version = 1;
    std::int64_t updated = 0;
};

// 一次调用：选中 Agent → 组 prompt →（可选）工具循环 → 结果
struct AgentRunRequest {
    std::string agent_id;
    std::string user_text;
    novelcore::RowId chapter_id = 0;
    novelcore::RowId entity_id = 0;
    std::string extra_json = "{}"; // 附加输入
};

struct AgentRunResult {
    std::string agent_id;
    std::string output_text;
    std::string output_json; // 若 Agent 要求结构化输出
    std::vector<std::string> used_tools;
    int tool_steps = 0;
};

class AgentKit {
public:
    AgentKit(db::sqlite::Database& db, bool allowWrite = true);

    // 迁移 agent_defs 并 seed 内置 Agent（幂等）
    [[nodiscard]] std::expected<void, novelcore::DbError> EnsureSchemaAndSeed();

    // —— 定义 CRUD（agent_meta 用）——
    [[nodiscard]] std::expected<novelcore::RowId, novelcore::DbError>
    UpsertAgentDef(const AgentDefRow& row);
    [[nodiscard]] std::expected<AgentDefRow, novelcore::DbError>
    GetAgentDef(std::string_view agentId) const;
    [[nodiscard]] std::expected<std::vector<AgentDefRow>, novelcore::DbError>
    ListAgentDefs(bool onlyEnabled = true) const;
    [[nodiscard]] std::expected<void, novelcore::DbError>
    SetAgentEnabled(std::string_view agentId, bool enabled);
    [[nodiscard]] std::expected<void, novelcore::DbError>
    DeleteAgentDef(std::string_view agentId, bool force = false);

    // 按任务文本路由到最合适的 agent_id（关键词 + 角色表；可被 meta 改 prompt 影响）
    [[nodiscard]] std::expected<std::string, novelcore::DbError>
    Route(std::string_view taskText) const;

    // 组装某 Agent 的完整 system prompt（内置默认 + 库内覆盖 + 动态字段说明）
    [[nodiscard]] std::expected<std::string, novelcore::DbError>
    BuildSystemPrompt(std::string_view agentId, novelcore::RowId chapterId = 0) const;

    // 注册该 Agent 允许的工具到 reg（含动态字段工具）
    void RegisterToolsFor(std::string_view agentId, ToolRegistry& reg) const;

    // Agent 可用工具解析：tools_json 白名单 ∩（本地工具 ∪ MCP novel_*）
    struct ResolvedTool {
        std::string name;   // 白名单中的逻辑名（无 novel_ 前缀）
        std::string mcpName; // MCP 侧名，如 novel_get_entity（可能为空）
        std::string source; // local | mcp | missing
        bool write = false;
    };
    [[nodiscard]] std::vector<ResolvedTool> ResolveTools(std::string_view agentId) const;

    // 以某 Agent 身份调用工具：未进白名单直接拒绝；优先本地，其次 MCP
    // 返回结果 JSON 文本
    [[nodiscard]] std::expected<std::string, ToolError>
    CallToolAsAgent(std::string_view agentId, std::string_view toolName, yyjson_val* args) const;

    // 组装「外部 LLM / MCP 客户端」执行包：只含该 Agent 的 prompt + 白名单工具，不注入全表
    struct AgentInvokePackage {
        std::string agent_id;
        std::string system_prompt;
        std::string tools_json; // 解析后的 MCP/本地工具名数组
        std::string routing_hint;
    };
    [[nodiscard]] std::expected<AgentInvokePackage, ToolError>
    BuildInvokePackage(std::string_view agentId, std::string_view taskText,
                       novelcore::RowId chapterId = 0) const;

    // 同步执行（worker）：mock 或真实 LLM create 回调
    using CreateFn = std::function<std::expected<std::string, std::string>(
        std::string_view instructions, std::string_view inputJson, std::string_view toolsJson)>;

    [[nodiscard]] std::expected<AgentRunResult, ToolError>
    Run(const AgentRunRequest& req, const CreateFn& create, ToolLoopStats* stats = nullptr) const;

    // 内置 Agent 默认定义（seed / 覆盖基线）
    [[nodiscard]] static std::vector<AgentDefRow> BuiltinAgents();
    [[nodiscard]] static std::string DefaultPromptFor(std::string_view agentId);

    [[nodiscard]] static bool RunSelfCheck();

private:
    db::sqlite::Database* db_;
    bool allowWrite_ = true;

    [[nodiscard]] novelcore::NovelGraph& Graph() const; // 内部持有共享 graph — 见 cpp
    mutable std::shared_ptr<novelcore::NovelGraph> graph_;
};

// 动态字段 + 图谱 + 元工具注册（供各 Agent 共用）
void RegisterAgentSharedTools(ToolRegistry& reg, db::sqlite::Database& db, bool allowWrite);

// 多 Agent 自检（含字段扩展、meta 更新、路由）
[[nodiscard]] bool RunMultiAgentSelfCheck();

} // namespace shine::agent
