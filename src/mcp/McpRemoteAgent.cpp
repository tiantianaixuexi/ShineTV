#include "mcp/McpRemoteAgent.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "mcp/McpBootstrap.h"
#include "mcp/ToolRegistry.h"
#include "novel/NovelMcpTools.h"
#include "openai/OpenAIClient.h"
#include "openai/OpenAIProvider.h"

#include <fmt/format.h>
#include <yyjson.h>

#include <string>
#include <vector>

namespace shine::mcp {
namespace {

[[nodiscard]] std::vector<openai::ChatToolDef> NovelMcpToolDefs() {
    RegisterAllModules(ToolRegistry::Instance());
    std::vector<openai::ChatToolDef> out;
    for (const char* name : {
             "novel_route_task",
             "novel_get_agent",
             "novel_agent_tools",
             "novel_list_entities",
             "novel_get_entity",
             "novel_get_relations",
             "novel_get_chapter",
             "novel_get_recent_chapters",
             "novel_get_foreshadows",
             "novel_get_secrets_for",
             "novel_search_memory",
             "novel_get_character_slice",
         }) {
        const Tool* t = ToolRegistry::Instance().Find(name);
        if (!t) continue;
        out.push_back(openai::ChatToolDef{
            .json = fmt::format(
                R"({{"type":"function","function":{{"name":"{}","description":"{}","parameters":{}}}}})",
                t->name, t->description, t->schemaJson.empty() ? "{}" : t->schemaJson)});
    }
    return out;
}

[[nodiscard]] std::string DefaultSystem() {
    return "你是小说工程外部助手。需要设定时调用 novel_* 工具，不要编造实体 id。最终用中文简要回答。";
}

[[nodiscard]] std::string TryInvokePackage(std::string_view agentId, std::string_view task,
                                           std::string& systemInOut) {
    const std::string args =
        fmt::format(R"({{"agent_id":"{}","task":"{}"}})", agentId, task);
    yyjson_doc* adoc = yyjson_read(args.data(), args.size(), 0);
    if (!adoc) {
        return {};
    }
    const auto inv = ToolRegistry::Instance().Call("novel_agent_invoke", yyjson_doc_get_root(adoc));
    yyjson_doc_free(adoc);
    if (!inv.ok()) {
        return {};
    }
    yyjson_doc* rdoc = yyjson_read(inv.text.data(), inv.text.size(), 0);
    if (!rdoc) {
        return {};
    }
    yyjson_val* root = yyjson_doc_get_root(rdoc);
    if (yyjson_val* sp = yyjson_obj_get(root, "system_prompt");
        yyjson_is_str(sp) && yyjson_get_str(sp)[0] != '\0') {
        systemInOut = yyjson_get_str(sp);
    }
    yyjson_doc_free(rdoc);
    return systemInOut;
}

} // namespace

std::expected<RemoteAgentOutcome, std::string> RunMcpBridgedAgent(std::string_view task,
                                                                  std::string_view agentId) {
    if (task.empty()) {
        return std::unexpected(std::string{"task 为空"});
    }
    RegisterAllModules(ToolRegistry::Instance());
    novelcore::SetMcpAllowWrite(Settings().mcpAllowWrite);

    std::string system = DefaultSystem();
    if (!agentId.empty()) {
        (void)TryInvokePackage(agentId, task, system);
    }

    const auto tools = NovelMcpToolDefs();
    if (tools.empty()) {
        return std::unexpected(std::string{"MCP 注册表无 novel 工具"});
    }
    const auto profile = openai::ResolveActiveProfile();
    if (profile.apiKey.empty()) {
        return std::unexpected(fmt::format(
            "未配置 LLM API Key（设置 Provider=MiMo/… 或环境变量）。model={}", profile.model));
    }

    RemoteAgentOutcome out;
    auto exec = [](std::string_view name, std::string_view argsJson) -> std::string {
        const std::string args = argsJson.empty() ? std::string{"{}"} : std::string{argsJson};
        yyjson_doc* adoc = yyjson_read(args.data(), args.size(), 0);
        yyjson_val* av = adoc ? yyjson_doc_get_root(adoc) : nullptr;
        const auto r = ToolRegistry::Instance().Call(name, av);
        if (adoc) yyjson_doc_free(adoc);
        return r.ok() ? r.text : fmt::format(R"({{"error":"{}"}})", r.text);
    };

    auto result =
        openai::RunChatToolLoop(system, std::string{task}, tools, exec, 20, &out.callLog, nullptr);
    if (!result) {
        return std::unexpected(
            fmt::format("{}：{}", result.error().code, result.error().message));
    }
    out.finalText = *result;
    return out;
}

bool RunMcpBridgedAgentLiveCheck() {
    const auto profile = openai::ResolveActiveProfile();
    if (profile.apiKey.empty()) {
        log::Warn("McpBridgedAgent 实联调跳过：无 API Key（Provider={}）", profile.model);
        return true;
    }
    auto r = RunMcpBridgedAgent(
        "列出当前小说工程中的主要人物名称（可调用 novel_list_entities kind=person）", "character");
    if (!r) {
        log::Error("McpBridgedAgent 实联调失败：{}", r.error());
        return false;
    }
    log::Info("McpBridgedAgent 实联调 OK：tool_calls={} preview={}", r->callLog.size(),
              r->finalText.substr(0, static_cast<std::size_t>(
                                          r->finalText.size() < 120 ? r->finalText.size() : 120)));
    return true;
}

} // namespace shine::mcp
