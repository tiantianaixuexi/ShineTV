#pragma once
// 自建 Agent 桥：MiMo/MiniMax/OpenAI（Settings.llmProvider）+ 进程内 MCP 工具
// 外部同款流程：HTTP MCP tools/list → 自己调 LLM API → tools/call /mcp
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace shine::mcp {

struct RemoteAgentOutcome {
    std::string finalText;
    std::vector<std::string> callLog;
};

// 用当前 Provider（可 MiMo）跑一轮：system 来自 novel_agent_invoke（或默认）
// tools：MCP 注册表中 novel 只读工具（名字带 novel_ 前缀，与 HTTP tools/list 一致）
// exec：默认走 ToolRegistry::Instance().Call
[[nodiscard]] std::expected<RemoteAgentOutcome, std::string>
RunMcpBridgedAgent(std::string_view task, std::string_view agentId = {});

// 有 Key 才能真跑；无 Key 返回错误文案。自检默认不调网络。
[[nodiscard]] bool RunMcpBridgedAgentLiveCheck();

} // namespace shine::mcp
