#include "app/novel/NovelPipeline.h"

#include "core/Log.h"
#include "openai/OpenAIAnthropic.h"
#include "openai/OpenAIClient.h"
#include "openai/OpenAIConfig.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Json.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <thread>
#include <utility>

#include <yyjson.h>

#include <fmt/format.h>

namespace shine::app::novel {

agent::LlmCallFn MakeLlmCall(const std::atomic<bool>* cancel) {
    return [cancel](agent::LlmRole role, std::string_view instructions,
                    std::string_view user) -> std::expected<std::string, agent::AgentError> {
        // S16（`09` §2.4）：按阶段选模型 —— planner / writer / critic 三个配置项，
        // 空则回退 `openaiModelDefault`；解析统一在 `openai::ResolveModel`。
        const std::string model = openai::ResolveModel(agent::LlmRoleName(role));
        auto r = openai::LlmComplete(instructions, user, std::chrono::seconds{180}, cancel, model);
        if (!r) {
            return std::unexpected(agent::AgentError{r.error().code, r.error().message});
        }
        return *r;
    };
}

agent::LlmCreateRawFn MakeLlmCreateRaw() {
    return [](std::string_view ins, std::string_view input,
              std::string_view tools) -> std::expected<std::string, std::string> {
        // ★ S62：与 `MakeLlmCall` 同口径按**角色**选模型 —— extractor 走
        // `ResolveModel("extractor")`，不绕过 `09` §2.4 的分层路由。
        const std::string model = openai::ResolveModel(agent::LlmRoleName(agent::LlmRole::Extractor));
        // ★ S62：**网络层退避重试**（与 `NovelVisualStages` 的 `create` 同款 —— S46 就在
        // Agent 工具循环上踩过这个坑：`ssl handshake failed` 偶发，一次抖动会把**整轮**打掉）。
        // ⚠️ 退避**要够长**：S46 实测 1.5s 的退避连续 3 次都过不去，现象是"同一分钟里第 6 次
        //    请求开始被持续拒绝"（像是**服务端按新建连接数短时限流**）⇒ 按 4s / 10s / 20s 走。
        //    2026-09-20 复核：MiniMax 端点在连发时单请求耗时从 0.15s 涨到 1.2-2.5s、复用连接只要
        //    0.4s ⇒ 确实有"新建连接"维度的限流；而 libhv 用的是 **Windows Schannel**
        //    （`WITH_OPENSSL OFF`），慢握手下会**立即**返回 `HSSL_ERROR(-1)`（不是超时）。
        static constexpr int kBackoffMs[] = {4000, 10000, 20000};
        std::string lastErr;
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffMs[attempt - 1]));
            }
            auto r = openai::LlmCreateRaw(ins, input, tools, model);
            if (r) {
                return *r;
            }
            lastErr = r.error().message;
            log::Warn("LlmCreateRaw 失败（第 {} 次）：{}", attempt + 1, lastErr);
        }
        return std::unexpected(lastErr);
    };
}

// ★ S72：**Anthropic 协议适配器** —— 让 `agent::RunToolLoop` 走 MiniMax 的 Anthropic 兼容端点。
//
// 为什么必须换协议（2026-09-20 两条独立实测）：
//  ① 我们的 `/v1/responses` 路径拿到的**不是**标准结构化工具调用：`tool_choice:"required"` 与
//     `{"type":"function","name":…}` 两种写法都只回 `output_text`（把调用**当文本**写出来，甚至
//     编造一个不存在的工具错误）⇒ 工具循环"只认 `function_call`"根本读不到，"强制首轮查库"这条路
//     在这个端点上**不存在**。
//  ② `POST https://api.minimax.cn/anthropic/v1/messages`（文档
//     platform.minimax.cn/docs/api-reference/text-chat-anthropic）**返回标准 `tool_use` 块**：
//     `content:[{"type":"tool_use","id":"call_…","name":"list_entities","input":{…}}]`、
//     `stop_reason:"tool_use"` ✅ —— 而 `openai/OpenAIAnthropic.*` **早就实现了**这套
//     （`AnthropicTool` / `ParseAnthropicTurn` 的 `toolUses` / `rawContentJson`），只是工具循环没走它。
//
// 做法：**不动 `RunToolLoop`**（它按 Responses 形状维护 history）—— 在本层做双向翻译：
//   · 请求侧：Responses 的 input 数组 → Anthropic `messages`；扁平 tools → `input_schema`
//   · 响应侧：Anthropic 的 `tool_use` 块 → Responses 的 `function_call`（`arguments` 仍是**字符串**）
// ⇒ 协议细节全部收在装配层，`src/agent` 一行都不用改（也就不会把两套协议搅在一起）。
agent::LlmCreateRawFn MakeLlmCreateRawAnthropic() {
    return [](std::string_view ins, std::string_view input,
              std::string_view tools) -> std::expected<std::string, std::string> {
        const openai::LlmProfile prof = openai::ResolveActiveProfile();
        const std::string model = openai::ResolveModel(agent::LlmRoleName(agent::LlmRole::Extractor));
        const auto valJson = [](yyjson_val* v) -> std::string {
            if (!v) return {};
            size_t len = 0;
            char* t = yyjson_val_write(v, 0, &len);
            if (!t) return {};
            std::string s{t, len};
            std::free(t);
            return s;
        };
        // ① tools：Responses 扁平 `{type,name,description,parameters}` → Anthropic `{name,description,input_schema}`
        std::vector<openai::AnthropicTool> atools;
        if (yyjson_doc* tdoc = yyjson_read(tools.data(), tools.size(), 0)) {
            yyjson_val* arr = yyjson_doc_get_root(tdoc);
            size_t i = 0, n = 0;
            yyjson_val* t = nullptr;
            if (yyjson_is_arr(arr)) yyjson_arr_foreach(arr, i, n, t) {
                if (!yyjson_is_obj(t)) continue;
                openai::AnthropicTool at;
                at.name = util::json::GetStrCopy(t, "name");
                at.description = util::json::GetStrCopy(t, "description");
                at.inputSchemaJson = valJson(util::json::Get(t, "parameters"));
                if (at.inputSchemaJson.empty()) at.inputSchemaJson = R"({"type":"object","properties":{}})";
                if (!at.name.empty()) atools.push_back(std::move(at));
            }
            yyjson_doc_free(tdoc);
        }
        // ② input（Responses input items）→ Anthropic messages（相邻同 role 合并成一条）
        std::vector<std::pair<std::string, std::vector<std::string>>> turns;
        const auto pushBlock = [&turns](const std::string& role, const std::string& block) {
            if (!turns.empty() && turns.back().first == role) turns.back().second.push_back(block);
            else turns.push_back({role, {block}});
        };
        if (yyjson_doc* hdoc = yyjson_read(input.data(), input.size(), 0)) {
            yyjson_val* arr = yyjson_doc_get_root(hdoc);
            size_t i = 0, n = 0;
            yyjson_val* it = nullptr;
            if (yyjson_is_arr(arr)) yyjson_arr_foreach(arr, i, n, it) {
                if (!yyjson_is_obj(it)) continue;
                const std::string type = util::json::GetStrCopy(it, "type");
                if (type == "message") {
                    std::string text;
                    if (yyjson_val* c = util::json::GetArr(it, "content")) {
                        size_t ci = 0, cn = 0;
                        yyjson_val* b = nullptr;
                        yyjson_arr_foreach(c, ci, cn, b) { text += util::json::GetStrCopy(b, "text"); }
                    }
                    if (text.empty()) text = util::json::GetStrCopy(it, "text");
                    pushBlock("user", fmt::format(R"({{"type":"text","text":{}}})",
                                                  util::json::JsonQuote(text)));
                } else if (type == "function_call") {
                    const std::string id = util::json::GetStrCopy(it, "call_id");
                    const std::string name = util::json::GetStrCopy(it, "name");
                    std::string args = util::json::GetStrCopy(it, "arguments");
                    if (args.empty()) args = "{}";
                    pushBlock("assistant",
                              fmt::format(R"({{"type":"tool_use","id":"{}","name":"{}","input":{}}})",
                                          id, name, args));
                } else if (type == "function_call_output") {
                    const std::string id = util::json::GetStrCopy(it, "call_id");
                    pushBlock("user", fmt::format(
                                         R"({{"type":"tool_result","tool_use_id":"{}","content":{}}})",
                                         id, util::json::JsonQuote(util::json::GetStrCopy(it, "output"))));
                }
            }
            yyjson_doc_free(hdoc);
        }
        openai::AnthropicRequest areq;
        areq.model = model;
        areq.system = std::string{ins};
        areq.tools = std::move(atools);
        areq.disableThinking = true;
        // `max_tokens` 在本端点是**必填**；契约 JSON 实测 4–7KB，留足余量
        areq.maxTokens = 32768;
        for (auto& [role, blocks] : turns) {
            std::string cj = "[";
            for (std::size_t k = 0; k < blocks.size(); ++k) {
                if (k) cj += ",";
                cj += blocks[k];
            }
            cj += "]";
            areq.messages.push_back({.role = role, .contentJson = std::move(cj)});
        }
        if (areq.messages.empty()) {
            areq.messages.push_back(
                {.role = "user",
                 .contentJson = fmt::format(R"([{{"type":"text","text":{}}}])",
                                            util::json::JsonQuote(std::string{input}))});
        }
        // ★ S74：**仪表** —— 每轮把"到底塞了多少给模型"记下来（用户问："你到底塞了多少提示和工具给他"）。
        // 意义：这类系统的失败几乎都和"信噪比"有关（规则被淹没 / 背景太长），而以前我们只记"工具调用次数"，
        // 没人知道 payload 有多大 ⇒ 讨论只能靠猜。这里给出可核对的字节数。
        {
            std::size_t msgBytes = 0;
            for (const auto& m : areq.messages) {
                msgBytes += m.contentJson.size();
            }
            std::size_t toolBytes = 0;
            for (const auto& t : areq.tools) {
                toolBytes += t.name.size() + t.description.size() + t.inputSchemaJson.size() + 40;
            }
            log::Info("Anthropic 请求：instructions={} 字节 / tools={} 个·{} 字节 / messages={} 条·{} 字节",
                      areq.system.size(), areq.tools.size(), toolBytes, areq.messages.size(), msgBytes);
        }
        // ★ S74：**原始 payload 落盘**（用户："文件在哪来啊，输出出来给我看啊"）——
        // 只在设了 `SHINE_DUMP_LLM_REQ=<目录>` 时写，平时零副作用。
        // 为什么要这个：以前所有讨论都靠猜（"提示词太长了吧？"），却没人能把**实际发出去的字节**拿出来看。
        if (const char* dump = std::getenv("SHINE_DUMP_LLM_REQ"); dump && *dump) {
            static std::atomic<int> seq{0};
            const int n = ++seq;
            const std::filesystem::path dir{util::PathFromUtf8(dump)};
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            const std::string stem = fmt::format("extract_req_{:02d}", n);
            (void)util::WriteFileBytes(dir / (stem + "_instructions.txt"), areq.system);
            std::string toolsJson = "[";
            for (std::size_t i = 0; i < areq.tools.size(); ++i) {
                if (i) toolsJson += ",";
                toolsJson += fmt::format(R"({{"name":"{}","description":"{}","input_schema":{}}})",
                                         areq.tools[i].name, areq.tools[i].description,
                                         areq.tools[i].inputSchemaJson);
            }
            toolsJson += "]";
            (void)util::WriteFileBytes(dir / (stem + "_tools.json"), toolsJson);
            std::string msgs = "[";
            for (std::size_t i = 0; i < areq.messages.size(); ++i) {
                if (i) msgs += ",";
                msgs += fmt::format(R"({{"role":"{}","content":{}}})", areq.messages[i].role,
                                    areq.messages[i].contentJson);
            }
            msgs += "]";
            (void)util::WriteFileBytes(dir / (stem + "_messages.json"), msgs);
            log::Info("Anthropic 原始请求已落盘：{}\\{}_*.txt|json", dump, stem);
        }
        // ③ 调（与 Responses 同款退避 4s/10s/20s —— 端点会在"新建连接"维度短时限流）
        static constexpr int kBackoffMs[] = {4000, 10000, 20000};
        std::string lastErr;
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffMs[attempt - 1]));
            }
            auto turn = openai::AnthropicComplete(openai::AnthropicBaseUrlFor(prof), prof.apiKey,
                                                 areq, std::chrono::seconds{180});
            if (!turn) {
                lastErr = turn.error().message;
                log::Warn("Anthropic 工具循环失败（第 {} 次）：{}", attempt + 1, lastErr);
                continue;
            }
            // ④ 响应 → Responses 形状（`RunToolLoop` 因此无需改动）
            std::string outs;
            for (const auto& tu : turn->toolUses) {
                if (!outs.empty()) outs += ",";
                outs += fmt::format(
                    R"({{"type":"function_call","call_id":"{}","name":"{}","arguments":{}}})", tu.id,
                    tu.name,
                    util::json::JsonQuote(tu.inputJson.empty() ? std::string{"{}"} : tu.inputJson));
            }
            log::Info("Anthropic 工具循环：stop={} 工具 {} 个 / 文本 {} 字", turn->stopReason,
                      turn->toolUses.size(), turn->text.size());
            return fmt::format(R"({{"output":[{}],"output_text":{}}})", outs,
                               util::json::JsonQuote(turn->text));
        }
        return std::unexpected(lastErr);
    };
}

// S72：什么时候用 Anthropic 工具循环。
// 默认：**MiniMax / MiMo** 用 Anthropic（它们的 `/anthropic/v1/messages` 是标准 tool_use；
// 而 `/v1/responses` 的工具实现非标准，见上面注释）。其它 Provider 保持 Responses。
// 环境变量 `SHINE_TOOL_LOOP_PROTOCOL=anthropic|responses` 可强制覆盖（便于 A/B 对照）。
bool UseAnthropicToolLoop() {
    if (const char* e = std::getenv("SHINE_TOOL_LOOP_PROTOCOL"); e && *e) {
        return std::string_view{e} == "anthropic";
    }
    const openai::Provider p = openai::ResolveActiveProfile().provider;
    return p == openai::Provider::MiniMax || p == openai::Provider::MiMo;
}

bool CrossReviewEffective() {
    return openai::ResolveModel("critic") != openai::ResolveModel("writer");
}

std::string ChapterGenOutcome::Describe() const {
    if (!ok) {
        return "生成失败：" + error;
    }
    return fmt::format("生成成功：章《{}》{} 字 · 修订 {} 次 · 校验重做 {} 次 · LLM 调用 {} 次 · {}",
                       title, body.size(), revisions, validation_retries, llm_calls,
                       state_skipped ? "状态已提交（幂等命中）"
                                     : (state_committed
                                            ? fmt::format("状态已回写{}",
                                                          commit_note.empty()
                                                              ? std::string{}
                                                              : "（" + commit_note + "）")
                                            : "状态未回写"));
}

ChapterGenOutcome
GenerateOneChapter(::shine::db::sqlite::Database& db, std::int64_t chapter_id,
                   const std::filesystem::path& project_dir, int max_revisions,
                   const std::atomic<bool>* cancel,
                   const std::function<void(const agent::GenerateChapterProgress&)>& on_progress,
                   bool resume) {
                   ChapterGenOutcome out;
                   if (chapter_id <= 0) {
        out.error = "需要 chapter_id（> 0）";
        return out;
    }
    if (!db.isOpen()) {
        out.error = "数据库未打开";
        return out;
    }
    // `09` §2.1 前置③：没配 Key 就**别发请求**，直接给可读原因（UI 与 CLI 同一判定）
    const auto profile = openai::ResolveActiveProfile();
    if (profile.apiKey.empty()) {
        out.error = fmt::format("未配置 {} 的 API Key（设置 → LLM）",
                                std::string{openai::ProviderLabel(profile.provider)});
        return out;
    }

    agent::NovelDirector dir(db, MakeLlmCall(cancel));
    agent::GenerateChapterRequest req;
    req.chapter_id = chapter_id;
    req.user_hint = "续写本章";
    req.max_revisions = max_revisions > 0 ? max_revisions : 2;
    // S19（`03` §2.7 P1/P5）：续跑语义由调用方决定（UI 按钮 = false；连跑 = true）
    req.resume = resume;
    // ★ S72：**单章生成这条路也得注入** `create_raw` —— 原先只有 `FillPreconditions`（连跑路径）
    // 注入，于是 `--novel-generate` 的 EXTRACT **静默退回单轮**（真跑实证 2026-09-20：第 13 章那次
    // 日志里既没有"工具循环协议"也没有"Anthropic 工具循环"两行，说明工具循环根本没跑）。
    // 这与 S62 注释里承诺的"UI 与 CLI 都从这里过"**不一致** —— 现在两条路都注入。
    req.create_raw = UseAnthropicToolLoop() ? MakeLlmCreateRawAnthropic() : MakeLlmCreateRaw();
    if (!project_dir.empty()) {
        // 工程根与快照目录**显式下发**（`work/` 与 `snapshots/` 都按它落盘）
        req.project_dir = util::PathToUtf8(project_dir);
        req.snapshot_dir = util::PathToUtf8(project_dir / "snapshots");
    }

    auto r = dir.GenerateChapter(req, on_progress);
    if (!r) {
        out.error = r.error().message;
        return out;
    }
    out.ok = true;
    out.title = r->title;
    out.body = r->body;
    out.revisions = r->revisions;
    out.validation_retries = r->validation_retries;
    out.llm_calls = r->llm_calls;
    out.state_committed = r->state_committed;
    out.state_skipped = r->state_skipped;
    out.commit_note = r->commit_note;
    return out;
}

void FillPreconditions(novelcore::RunRequest& req, std::int64_t max_total_llm_calls) {
    req.llm_ready = !openai::ResolveActiveProfile().apiKey.empty();
    req.cross_review_ok = CrossReviewEffective();
    req.max_total_llm_calls = max_total_llm_calls;
    // S62：注入"原始响应"通道 → EXTRACT 走工具循环（UI 与 CLI 都从这里过，一处注入两处生效）
    // S72：**协议选择** —— MiniMax / MiMo 走 Anthropic 兼容端点（标准结构化 `tool_use`），
    // 其余 Provider 走 Responses。实测依据见 `MakeLlmCreateRawAnthropic` 的注释。
    req.create_raw = UseAnthropicToolLoop() ? MakeLlmCreateRawAnthropic() : MakeLlmCreateRaw();
    log::Info("工具循环协议：{}", UseAnthropicToolLoop() ? "anthropic(/anthropic/v1/messages)" : "responses(/v1/responses)");
}

novelcore::RunOutcome RunOnce(::shine::db::sqlite::Database& db, const novelcore::RunRequest& req,
                              const std::atomic<bool>* cancel,
                              const std::function<void(const novelcore::RunProgress&)>& on_progress) {
    novelcore::NovelRunLoop loop(db, MakeLlmCall(cancel));
    novelcore::RunRequest r = req;
    if (on_progress) {
        r.on_progress = on_progress;
    }
    if (cancel != nullptr) {
        r.cancel = [cancel]() { return cancel->load(); };
    }
    return loop.Run(r);
}

} // namespace shine::app::novel
