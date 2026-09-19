#include "mcp/McpBootstrap.h"

#include "app/novel/NovelPipeline.h" // S18：注入生成一章的实现（与 UI/CLI 同一条路）
#include "core/Log.h"
#include "mcp/BuiltinTools.h"
#include "mcp/Schema.h"
#include "novel/NovelDb.h"
#include "novel/NovelMcpTools.h"

#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <yyjson.h>

namespace shine::mcp {
namespace {

[[nodiscard]] std::string EmptySchemaJson() {
    auto* doc = schema::NewDoc();
    auto* s = schema::Object(doc);
    const std::string out = schema::ToJsonString(doc, s);
    yyjson_mut_doc_free(doc);
    return out;
}

// 结果文本用 yyjson 拼，避免 title/id 含引号时产出非法 JSON
[[nodiscard]] std::string WriteVal(yyjson_mut_val* v) {
    if (!v) return "null";
    size_t len = 0;
    char* s = yyjson_mut_val_write(v, 0, &len);
    if (!s) return "null";
    std::string out{s, len};
    std::free(s);
    return out;
}

[[nodiscard]] CallOutcome HandlePing(yyjson_val*) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_bool(doc, o, "pong", true);
    yyjson_mut_obj_add_strncpy(doc, o, "server", kServerName.data(), kServerName.size());
    yyjson_mut_obj_add_strncpy(doc, o, "version", kServerVersion.data(), kServerVersion.size());
    CallOutcome r = CallOutcome::Ok(WriteVal(o));
    yyjson_mut_doc_free(doc);
    return r;
}

[[nodiscard]] CallOutcome HandleRegistryInfo(yyjson_val*) {
    auto& reg = ToolRegistry::Instance();
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* o = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, o);
    yyjson_mut_obj_add_uint(doc, o, "toolCount", reg.ToolCount());
    yyjson_mut_obj_add_uint(doc, o, "moduleCount", reg.ModuleCount());
    yyjson_mut_val* mods = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, o, "modules", mods);
    for (const auto& m : reg.Modules()) {
        yyjson_mut_val* item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strncpy(doc, item, "id", m.id.data(), m.id.size());
        yyjson_mut_obj_add_strncpy(doc, item, "title", m.title.data(), m.title.size());
        yyjson_mut_arr_add_val(mods, item);
    }
    yyjson_mut_val* demo = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, o, "demoTools", demo);
    for (const auto& n : reg.ToolNames("demo")) {
        yyjson_mut_arr_add_strncpy(doc, demo, n.c_str(), n.size());
    }
    CallOutcome r = CallOutcome::Ok(WriteVal(o));
    yyjson_mut_doc_free(doc);
    return r;
}

} // namespace

void RegisterDemoModule(ToolRegistry& reg) {
    reg.EnsureModule(ModuleInfo{.id = "demo", .title = "MCP 演示/自检"});

    reg.Register(Tool{
        .name = "mcp_ping",
        .title = "MCP 存活探测",
        .description = "返回 pong 与服务器名称/版本，用于验证注册表可用。",
        .moduleId = "demo",
        .schemaJson = EmptySchemaJson(),
        .handler = HandlePing,
    });

    reg.Register(Tool{
        .name = "mcp_registry_info",
        .title = "注册表信息",
        .description = "返回工具数量、模块列表与 demo 工具名，便于诊断分模块注册。",
        .moduleId = "demo",
        .schemaJson = EmptySchemaJson(),
        .handler = HandleRegistryInfo,
    });
}

void RegisterAllModules(ToolRegistry& reg) {
    // 顺序：demo → comfy → graph → gallery → media → video → novel
    RegisterDemoModule(reg);

    // 小说多 Agent + 动态字段（P10：只读默认，写受 SHINE_MCP_ALLOW_WRITE 约束）
    novelcore::RegisterMcpTools(reg);

    // S18：把「生成一章」的实现注入给 MCP（`novel_generate_chapter`）——
    // 用的就是 UI/CLI 那条路（`NovelPipeline`）：同一份按 `LlmRole` 选模型的回调、
    // 同一份前置判定（空 Key 直接给可读原因）。装配层做这件事，`src/novel` 不依赖 `openai`。
    novelcore::SetChapterGenerator([](::shine::db::sqlite::Database& db, std::int64_t chapter_id) {
        std::filesystem::path projectDir;
        if (auto& inst = novelcore::NovelDb::Instance(); inst.isOpen()) {
            projectDir = inst.path().parent_path();
        }
        return app::novel::GenerateOneChapter(db, chapter_id, projectDir, 2, nullptr, nullptr)
            .Describe();
    });

    // P7.4：comfy_* + shinetv_* 内置工具（幂等同名覆盖）
    RegisterAllBuiltinTools(reg);

    log::Info("mcp RegisterAllModules：modules={} tools={}", reg.ModuleCount(), reg.ToolCount());
}

bool RunRegistrySelfCheck() {
    bool pass = true;
    auto fail = [&](std::string_view why) {
        log::Error("mcp 自检 FAIL：{}", why);
        pass = false;
    };

    ToolRegistry local;
    // 1) 同名覆盖幂等：再次注册 demo，数量不变
    RegisterDemoModule(local);
    RegisterDemoModule(local);
    if (local.ToolCount() != 2) fail(fmt::format("同名覆盖后 ToolCount={} 期望 2", local.ToolCount()));
    if (local.ModuleCount() != 1) fail(fmt::format("模块数 {} 期望 1", local.ModuleCount()));

    // 2) EnsureModule 幂等
    local.EnsureModule(ModuleInfo{.id = "demo", .title = "改名"});
    if (local.ModuleCount() != 1) fail("EnsureModule 重复后模块数不是 1");
    if (const auto mods = local.Modules(); mods.empty() || mods[0].title != "改名") {
        fail("EnsureModule 未更新 title");
    }

    // 3) tools/list
    const std::string list = local.BuildToolsListJson();
    yyjson_doc* ldoc = yyjson_read(list.data(), list.size(), 0);
    if (!ldoc) {
        fail("BuildToolsListJson 不是合法 JSON");
    } else {
        yyjson_val* root = yyjson_doc_get_root(ldoc);
        yyjson_val* tools =
            (root && yyjson_is_obj(root)) ? yyjson_obj_get(root, "tools") : nullptr;
        if (!tools || !yyjson_is_arr(tools)) {
            fail("tools/list 缺少 tools 数组");
        } else {
            bool foundPing = false;
            size_t idx = 0, max = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(tools, idx, max, item) {
                yyjson_val* name = yyjson_obj_get(item, "name");
                yyjson_val* schema = yyjson_obj_get(item, "inputSchema");
                if (name && yyjson_is_str(name) && yyjson_get_str(name) == std::string_view{"mcp_ping"}) {
                    foundPing = true;
                    if (!schema || !yyjson_is_obj(schema)) fail("mcp_ping 的 inputSchema 不是对象");
                }
            }
            if (!foundPing) fail("tools/list 未包含 mcp_ping");
        }
        yyjson_doc_free(ldoc);
    }

    // 4) Call Ok
    {
        const auto r = local.Call("mcp_ping", nullptr);
        if (!r.ok()) fail("mcp_ping 应 Ok");
        else if (r.text.find("pong") == std::string::npos) fail("mcp_ping 文本缺 pong");
        else if (r.text.find(kServerName) == std::string::npos) fail("mcp_ping 文本缺 server 名");
    }
    // 4b) Call Ok：空对象参数
    {
        yyjson_doc* edoc = yyjson_read("{}", 2, 0);
        const auto r = local.Call("mcp_ping", yyjson_doc_get_root(edoc));
        if (edoc) yyjson_doc_free(edoc);
        if (!r.ok()) fail("mcp_ping + {} 应 Ok");
    }

    // 5) Call NotFound
    {
        const auto r = local.Call("no_such_tool", nullptr);
        if (r.status != CallStatus::NotFound) fail("未知工具应 NotFound");
        if (r.text.find("no_such_tool") == std::string::npos) fail("NotFound 文本应含工具名");
    }

    // 6) Call BadArguments（非对象）
    {
        yyjson_doc* adoc = yyjson_read(R"([1,2,3])", 7, 0);
        const auto r = local.Call("mcp_ping", yyjson_doc_get_root(adoc));
        if (adoc) yyjson_doc_free(adoc);
        if (r.status != CallStatus::BadArguments) fail("数组参数应 BadArguments");
    }

    // 7) ClearModule 后 demo 工具消失
    local.EnsureModule(ModuleInfo{.id = "other", .title = "其它"});
    local.Register(Tool{
        .name = "other_tool",
        .title = "其它",
        .description = "非 demo",
        .moduleId = "other",
        .schemaJson = EmptySchemaJson(),
        .handler = [](yyjson_val*) { return CallOutcome::Ok("ok"); },
    });
    local.ClearModule("demo");
    if (local.Find("mcp_ping") != nullptr) fail("ClearModule(demo) 后 mcp_ping 仍在");
    if (local.Find("other_tool") == nullptr) fail("ClearModule(demo) 误删了 other_tool");
    if (local.ModuleCount() != 1) fail("ClearModule 后模块数应为 1（仅 other）");

    // 8) handler 异常不崩
    local.Register(Tool{
        .name = "boom",
        .title = "",
        .description = "抛异常",
        .moduleId = "other",
        .schemaJson = EmptySchemaJson(),
        .handler = [](yyjson_val*) -> CallOutcome { throw std::runtime_error("test-boom"); },
    });
    {
        const auto r = local.Call("boom", nullptr);
        if (r.status != CallStatus::InternalError) fail("抛异常应 InternalError");
        if (r.text.find("test-boom") == std::string::npos) fail("异常文本应含 what");
    }

    // 进程单例也跑一遍 bootstrap（幂等）
    RegisterAllModules(ToolRegistry::Instance());
    RegisterAllModules(ToolRegistry::Instance());
    if (ToolRegistry::Instance().Find("mcp_ping") == nullptr) fail("单例注册后缺 mcp_ping");
    if (ToolRegistry::Instance().Find("novel_route_task") == nullptr) {
        fail("单例注册后缺 novel_route_task");
    }
    if (ToolRegistry::Instance().Find("novel_agent_tools") == nullptr) {
        fail("单例注册后缺 novel_agent_tools");
    }

    // novel 调度/白名单自检
    if (!novelcore::RunNovelMcpSelfCheck()) {
        fail("novel MCP 自检失败");
        pass = false;
    }

    log::Info("mcp 自检 {}", pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace shine::mcp
