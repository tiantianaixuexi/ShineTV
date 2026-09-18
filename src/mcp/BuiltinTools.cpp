#include "mcp/BuiltinTools.h"

#include "comfy/ComfySession.h"
#include "comfy/ComfyTypes.h"
#include "core/Log.h"
#include "graph/GraphCompiler.h"
#include "graph/GraphHost.h"
#include "mcp/McpBootstrap.h"
#include "mcp/Schema.h"
#include "util/Json.h"

#include <fmt/format.h>
#include <yyjson.h>

#include <string>
#include <utility>

namespace shine::mcp {
namespace {

[[nodiscard]] std::string EmptySchema() {
    auto* doc = schema::NewDoc();
    auto* s = schema::Object(doc);
    const std::string out = schema::ToJsonString(doc, s);
    yyjson_mut_doc_free(doc);
    return out;
}

} // namespace

void RegisterComfyBuiltinTools(ToolRegistry& reg) {
    reg.EnsureModule(ModuleInfo{.id = "comfy", .title = "ComfyUI 桥接"});

    reg.Register(Tool{
        .name = "comfy_ping",
        .title = "Comfy 连接状态",
        .description = "返回 ComfyUI 健康摘要（未连接时明确提示，不发起超时探测）。",
        .moduleId = "comfy",
        .schemaJson = EmptySchema(),
        .handler = [](yyjson_val*) -> CallOutcome {
            auto& s = comfy::ComfySession::Instance();
            const std::string health{s.HealthSummary()};
            if (health.empty() || s.State() == comfy::ConnectionState::Disconnected) {
                return CallOutcome::Fail(CallStatus::InternalError,
                                         "ComfyUI 未连接：请先在设置里配置服务地址并连接");
            }
            return CallOutcome::Ok(health);
        },
    });

    reg.Register(Tool{
        .name = "comfy_queue",
        .title = "Comfy 队列快照",
        .description = "返回本地队列统计与最近任务行（pending/running/failed）。",
        .moduleId = "comfy",
        .schemaJson = EmptySchema(),
        .handler = [](yyjson_val*) -> CallOutcome {
            auto& s = comfy::ComfySession::Instance();
            if (s.State() == comfy::ConnectionState::Disconnected) {
                return CallOutcome::Fail(CallStatus::InternalError,
                                         "ComfyUI 未连接：请先在设置里配置服务地址并连接");
            }
            const auto c = s.Queue().CountsSnapshot();
            const auto rows = s.Queue().Snapshot();
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* o = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, o);
            yyjson_mut_obj_add_int(doc, o, "pending", c.pending);
            yyjson_mut_obj_add_int(doc, o, "running", c.running);
            yyjson_mut_obj_add_int(doc, o, "failed", c.failed);
            yyjson_mut_val* arr = yyjson_mut_arr(doc);
            yyjson_mut_obj_add_val(doc, o, "rows", arr);
            const std::size_t n = rows.size() < 20 ? rows.size() : std::size_t{20};
            for (std::size_t i = 0; i < n; ++i) {
                yyjson_mut_val* r = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strncpy(doc, r, "promptId", rows[i].promptId.data(),
                                           rows[i].promptId.size());
                yyjson_mut_obj_add_strncpy(doc, r, "label", rows[i].label.data(),
                                           rows[i].label.size());
                yyjson_mut_obj_add_strncpy(doc, r, "error", rows[i].error.data(),
                                           rows[i].error.size());
                yyjson_mut_arr_add_val(arr, r);
            }
            size_t len = 0;
            char* t = yyjson_mut_val_write(o, 0, &len);
            std::string text = t ? std::string{t, len} : "{}";
            if (t) std::free(t);
            yyjson_mut_doc_free(doc);
            return CallOutcome::Ok(std::move(text));
        },
    });

    {
        auto* doc = schema::NewDoc();
        const std::string_view required[] = {std::string_view{"promptJson"}};
        auto* s = schema::Object(doc, required);
        schema::AddString(doc, s, "promptJson", "ComfyUI API 格式 prompt JSON", true);
        const std::string sch = schema::ToJsonString(doc, s);
        yyjson_mut_doc_free(doc);
        reg.Register(Tool{
            .name = "comfy_submit",
            .title = "提交 Comfy prompt",
            .description = "提交 API 格式 prompt；异步排队，立即返回已受理说明。完整 promptId 以 UI 队列为准。",
            .moduleId = "comfy",
            .schemaJson = sch,
            .handler = [](yyjson_val* args) -> CallOutcome {
                auto& s = comfy::ComfySession::Instance();
                if (s.State() == comfy::ConnectionState::Disconnected) {
                    return CallOutcome::Fail(CallStatus::InternalError,
                                             "ComfyUI 未连接：请先在设置里配置服务地址并连接");
                }
                const std::string promptJson =
                    util::json::GetStrCopy(args, "promptJson");
                if (promptJson.empty()) {
                    return CallOutcome::Fail(CallStatus::BadArguments, "缺少参数 promptJson");
                }
                s.SubmitPromptJson(promptJson, [](comfy::PromptSubmitResult r) {
                    if (!r.ok) {
                        log::Warn("mcp comfy_submit：{}", r.error);
                    } else {
                        log::Info("mcp comfy_submit accepted promptId={}", r.promptId);
                    }
                });
                return CallOutcome::Ok(R"({"accepted":true})");
            },
        });
    }

    reg.Register(Tool{
        .name = "comfy_interrupt",
        .title = "中断当前 Comfy 任务",
        .description = "请求中断当前执行（已中断 ≠ 失败）。",
        .moduleId = "comfy",
        .schemaJson = EmptySchema(),
        .handler = [](yyjson_val*) -> CallOutcome {
            auto& s = comfy::ComfySession::Instance();
            if (s.State() == comfy::ConnectionState::Disconnected) {
                return CallOutcome::Fail(CallStatus::InternalError,
                                         "ComfyUI 未连接：请先在设置里配置服务地址并连接");
            }
            s.RequestInterruptCurrent();
            return CallOutcome::Ok(R"({"requested":true})");
        },
    });
}

void RegisterShineTvBuiltinTools(ToolRegistry& reg) {
    reg.EnsureModule(ModuleInfo{.id = "shinetv", .title = "ShineTV 能力"});

    reg.Register(Tool{
        .name = "shinetv_status",
        .title = "ShineTV 状态",
        .description = "返回 Comfy 连接摘要、MCP 工具数与最近一次 tools/call。",
        .moduleId = "shinetv",
        .schemaJson = EmptySchema(),
        .handler = [](yyjson_val*) -> CallOutcome {
            auto& cs = comfy::ComfySession::Instance();
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* o = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, o);
            const std::string health{cs.HealthSummary()};
            yyjson_mut_obj_add_strncpy(doc, o, "comfy", health.data(), health.size());
            yyjson_mut_obj_add_uint(doc, o, "toolCount", ToolRegistry::Instance().ToolCount());
            yyjson_mut_obj_add_uint(doc, o, "moduleCount", ToolRegistry::Instance().ModuleCount());
            size_t len = 0;
            char* t = yyjson_mut_val_write(o, 0, &len);
            std::string text = t ? std::string{t, len} : "{}";
            if (t) std::free(t);
            yyjson_mut_doc_free(doc);
            return CallOutcome::Ok(std::move(text));
        },
    });

    reg.Register(Tool{
        .name = "shinetv_graph_compile",
        .title = "编译当前节点图",
        .description = "画布图 → ComfyUI API JSON；失败时返回中文错误列表。",
        .moduleId = "shinetv",
        .schemaJson = EmptySchema(),
        .handler = [](yyjson_val*) -> CallOutcome {
            const auto r = graph::CompileToApiJson();
            if (!r.ok) {
                std::string msg = "图编译失败";
                for (const auto& e : r.errors) {
                    msg += fmt::format("；{}({}): {}", e.nodeName, e.nodeId, e.message);
                }
                return CallOutcome::Fail(CallStatus::InternalError, msg);
            }
            yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* o = yyjson_mut_obj(doc);
            yyjson_mut_doc_set_root(doc, o);
            yyjson_mut_obj_add_uint(doc, o, "nodeCount", r.nodeCount);
            yyjson_mut_obj_add_strncpy(doc, o, "apiJson", r.apiJson.data(), r.apiJson.size());
            size_t len = 0;
            char* t = yyjson_mut_val_write(o, 0, &len);
            std::string text = t ? std::string{t, len} : "{}";
            if (t) std::free(t);
            yyjson_mut_doc_free(doc);
            return CallOutcome::Ok(std::move(text));
        },
    });

    reg.Register(Tool{
        .name = "shinetv_graph_submit",
        .title = "编译并提交当前图",
        .description = "编译画布图并提交 ComfyUI；成功返回 promptId。",
        .moduleId = "shinetv",
        .schemaJson = EmptySchema(),
        .handler = [](yyjson_val*) -> CallOutcome {
            auto& cs = comfy::ComfySession::Instance();
            if (cs.State() == comfy::ConnectionState::Disconnected) {
                return CallOutcome::Fail(CallStatus::InternalError,
                                         "ComfyUI 未连接：请先在设置里配置服务地址并连接");
            }
            const auto r = graph::CompileToApiJson();
            if (!r.ok) {
                std::string msg = "图编译失败";
                for (const auto& e : r.errors) {
                    msg += fmt::format("；{}({}): {}", e.nodeName, e.nodeId, e.message);
                }
                return CallOutcome::Fail(CallStatus::InternalError, msg);
            }
            cs.SubmitPromptJson(r.apiJson, [](comfy::PromptSubmitResult pr) {
                if (pr.ok) {
                    log::Info("mcp shinetv_graph_submit promptId={}", pr.promptId);
                } else {
                    log::Warn("mcp shinetv_graph_submit：{}", pr.error);
                }
            });
            return CallOutcome::Ok(R"({"accepted":true})");
        },
    });
}

void RegisterAllBuiltinTools(ToolRegistry& reg) {
    // 幂等：同名覆盖
    RegisterDemoModule(reg);
    RegisterComfyBuiltinTools(reg);
    RegisterShineTvBuiltinTools(reg);
}

} // namespace shine::mcp
