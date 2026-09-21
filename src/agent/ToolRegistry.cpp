#include "agent/ToolRegistry.h"

#include "core/Log.h"
#include "openai/OpenAIClient.h"
#include "util/Json.h"

#include <fmt/format.h>

#include <yyjson.h>

#include <cstring>

namespace shine::agent {
namespace {

[[nodiscard]] std::string TruncateForLog(std::string_view s, std::size_t n = 80) {
    return s.size() <= n ? std::string{s} : std::string{s.substr(0, n)} + "…";
}

// 把 yyjson_val 序列化成字符串（工具结果 / args 指纹）
[[nodiscard]] std::string ValToJson(yyjson_val* v) {
    if (!v) return "null";
    size_t len = 0;
    char* t = yyjson_val_write(v, 0, &len);
    if (!t) return "null";
    std::string out{t, len};
    std::free(t);
    return out;
}

[[nodiscard]] yyjson_doc* MakeResultDoc(std::string_view json) {
    return yyjson_read(json.data(), json.size(), 0);
}

[[nodiscard]] yyjson_doc* MakeErrorDoc(std::string_view code, std::string_view msg) {
    const std::string j = fmt::format(R"({{"ok":false,"code":{},"message":{}}})",
                                      // 简单转义
                                      [&] {
                                          std::string c = "\"";
                                          for (const char ch : code) {
                                              if (ch == '"' || ch == '\\') c += '\\';
                                              c += ch;
                                          }
                                          return c + "\"";
                                      }(),
                                      [&] {
                                          std::string c = "\"";
                                          for (const char ch : msg) {
                                              if (ch == '"' || ch == '\\') c += '\\';
                                              c += ch;
                                          }
                                          return c + "\"";
                                      }());
    return MakeResultDoc(j);
}

} // namespace

// ★ S75：按名字摘掉（白名单收窄用，见 `AgentKit::Run`）
bool ToolRegistry::Unregister(std::string_view name) {
    for (auto it = tools_.begin(); it != tools_.end(); ++it) {
        if (*it && (*it)->Name() == name) {
            tools_.erase(it);
            return true;
        }
    }
    return false;
}

void ToolRegistry::Register(std::unique_ptr<Tool> tool) {
    if (tool) {
        tools_.push_back(std::move(tool));
    }
}

void ToolRegistry::Clear() noexcept { tools_.clear(); }

std::vector<std::string> ToolRegistry::Names() const {
    std::vector<std::string> out;
    out.reserve(tools_.size());
    for (const auto& t : tools_) {
        if (t) {
            out.emplace_back(t->Name());
        }
    }
    return out;
}

yyjson_mut_val* ToolRegistry::ExportOpenAiTools(yyjson_mut_doc* doc) const {
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    for (const auto& t : tools_) {
        yyjson_mut_val* fn = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, fn, "name", std::string{t->Name()}.c_str());
        yyjson_mut_obj_add_strcpy(doc, fn, "description", std::string{t->Description()}.c_str());
        if (yyjson_mut_val* params = t->Schema(doc)) {
            yyjson_mut_obj_add_val(doc, fn, "parameters", params);
        }
        yyjson_mut_val* wrapper = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, wrapper, "type", "function");
        yyjson_mut_obj_add_val(doc, wrapper, "function", fn);
        yyjson_mut_arr_add_val(arr, wrapper);
    }
    return arr;
}

// S44：**Responses 形状的 tools**（顶层扁平）—— 见头文件注释。真跑踩到：拿 Chat 形状发给
// MiniMax 的 `/v1/responses`，它回显 `"tools":[{"type":"function","name":"","parameters":null}]`
//（**名字都没解析出来**）⇒ 工具等于没注册 ⇒ "工具 0 次"。
yyjson_mut_val* ToolRegistry::ExportResponsesTools(yyjson_mut_doc* doc) const {
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    for (const auto& t : tools_) {
        yyjson_mut_val* fn = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, fn, "type", "function");
        yyjson_mut_obj_add_strcpy(doc, fn, "name", std::string{t->Name()}.c_str());
        yyjson_mut_obj_add_strcpy(doc, fn, "description", std::string{t->Description()}.c_str());
        if (yyjson_mut_val* params = t->Schema(doc)) {
            yyjson_mut_obj_add_val(doc, fn, "parameters", params);
        }
        yyjson_mut_arr_add_val(arr, fn);
    }
    return arr;
}

std::expected<yyjson_doc*, ToolError> ToolRegistry::Execute(std::string_view name,
                                                            yyjson_val* args) {
    for (auto& t : tools_) {
        if (t->Name() == name) {
            log::Info("Tool 执行：{} args={}", name, TruncateForLog(ValToJson(args)));
            return t->Execute(args);
        }
    }
    return std::unexpected(ToolError{"not_found", fmt::format("未知工具: {}", name)});
}

void ToolRegistry::ResetLoopState() noexcept {
    steps_ = 0;
    repeatCount_ = 0;
    lastKey_.clear();
}

std::string ToolRegistry::CheckLoopGuard(std::string_view name, std::string_view argsCanon) {
    if (steps_ >= kMaxToolCalls) {
        return fmt::format("工具调用超过上限 {} 次", kMaxToolCalls);
    }
    const std::string key = std::string{name} + "|" + std::string{argsCanon};
    if (key == lastKey_) {
        ++repeatCount_;
        if (repeatCount_ >= kMaxRepeat) {
            return fmt::format("同一工具连续重复 {} 次，中止", kMaxRepeat);
        }
    } else {
        repeatCount_ = 1;
        lastKey_ = key;
    }
    return {};
}

void ToolRegistry::NoteCall(std::string_view, std::string_view) { ++steps_; }

std::expected<std::string, std::string>
RunToolLoop(ToolRegistry& reg, std::string_view instructions, std::string_view userText,
            const CreateWithToolsFn& create, ToolLoopStats* stats) {
    if (!create) {
        return std::unexpected(std::string{"缺少 create 回调"});
    }
    reg.ResetLoopState();
    if (stats) {
        stats->steps = 0;
        stats->callLog.clear();
    }

    // 对话历史：input 数组（Responses API input items）
    std::string history; // JSON 数组字符串，手工维护
    // S42：**不要手写 JSON 转义** —— 交给 `util::json::JsonQuote`（yyjson 实现，永远正确）。
    // S44：**item 形状也要对** —— Responses 的 input item 是**结构化**的
    //（`{type:"message",role,content:[{type:"input_text",text}]}`），不是 Chat 那种
    // `{role,content:"字符串"}`。真跑：MiniMax 直接拒收 ——
    // `Invalid request: input is neither string nor array of items`。
    history = fmt::format(
        R"([{{"type":"message","role":"user","content":[{{"type":"input_text","text":{}}}]}}])",
        util::json::JsonQuote(userText));

    // tools 导出
    // S44：**必须用 Responses 形状**（顶层扁平）—— 本循环的 `create` 只对接 Responses
    //（`LlmCreateRaw`）。原先用 `ExportOpenAiTools`（Chat 形状，嵌一层 `function`）⇒
    // MiniMax 的 `/v1/responses` 回显 `"name":"","parameters":null`（**名字都没解析出来**）
    // ⇒ 工具等于没注册。
    yyjson_mut_doc* tdoc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* tools = reg.ExportResponsesTools(tdoc);
    size_t tlen = 0;
    char* tjson = yyjson_mut_val_write(tools, 0, &tlen);
    const std::string toolsJson = tjson ? std::string{tjson, tlen} : "[]";
    if (tjson) std::free(tjson);
    yyjson_mut_doc_free(tdoc);

    // ★ S78：**首轮预置"权威 id 目录"** —— 由**系统**先替模型调一次 `list_id_directory`，把结果作为
    // **第一轮的工具结果**放进对话。这不是"往 prompt 里塞数据"（S62 试过并回退），也不是改措辞 ——
    // 而是**系统执行、AI 消费**：模型一开口就已经看到"库里有哪些 kind、各自 id 是多少"。
    // 依据（真跑实证）：模型**经常一次工具都不调**（第 15/18 章 `工具调用 0 次`）却照样往 `*_id`
    // 字段里填数字 ⇒ 把事实在它开口前摆好，K03 一类失败自然下降。
    // ⚠️ 只对"注册表里真给了这个工具"的 agent 生效（白名单已收窄：没给它就没有这一步）。
    const auto appendItem = [](std::string& h, const std::string& item) {
        if (!h.empty() && h.back() == ']') {
            h.pop_back();
            if (h.size() > 1 && h[h.size() - 1] == '[') {
                h += item;
            } else {
                h += ",";
                h += item;
            }
            h += "]";
        }
    };
    {
        bool hasDirTool = false;
        for (const std::string& n : reg.Names()) {
            if (n == "list_id_directory") {
                hasDirTool = true;
                break;
            }
        }
        // ⚠️ S81：**默认关闭**（实测：开了它反而让模型"一次工具都不调"）。
        // 🔴 真跑 A/B（同一套机制、同一模型、相邻两章）：
        //   预置 **ON**  → `工具调用 0 次`（第 18 章连续三次），要 1–2 轮重做才勉强提交；
        //   预置 **OFF** → `工具调用 1 次`，**一次过、直接提交**（第 19 章）。
        // 原因（用户先怀疑、实测确认）：我们伪造了一条 `assistant(function_call list_id_directory)`
        // 记录塞进对话 ⇒ 模型读到"**我已经查过了**" ⇒ **它就不再自己调工具了**。
        // ⇒ **伪造 assistant 轮 = 教会模型"不用查"**。教训：**别替模型伪造它的动作** ——
        //    事实可以预置（放进工具结果/上下文），但**不能伪装成"它自己调用过"**。
        // 保留开关 `SHINE_TOOL_PREFETCH=1` 仅供对照实验（默认关）。
        if (!(std::getenv("SHINE_TOOL_PREFETCH") != nullptr &&
              *std::getenv("SHINE_TOOL_PREFETCH") == '1')) {
            hasDirTool = false;
        }
        if (hasDirTool) {
            yyjson_doc* adoc = yyjson_read("{}", 2, 0);
            auto prefetched =
                reg.Execute("list_id_directory", adoc ? yyjson_doc_get_root(adoc) : nullptr);
            if (adoc) {
                yyjson_doc_free(adoc);
            }
            if (prefetched) {
                const std::string resultJson = ValToJson(yyjson_doc_get_root(*prefetched));
                yyjson_doc_free(*prefetched);
                appendItem(history,
                           fmt::format(
                               R"({{"type":"function_call","call_id":"prefetch_dir","name":"list_id_directory","arguments":"{{}}"}})"
                               R"(,{{"type":"function_call_output","call_id":"prefetch_dir","output":{}}})",
                               util::json::JsonQuote(resultJson)));
                log::Info("工具循环：**系统预置** list_id_directory 结果（{} 字节；模型未调用即已可见）",
                          resultJson.size());
            }
        }
    }

    for (int iter = 0; iter < ToolRegistry::kMaxToolCalls + 2; ++iter) {
        auto resp = create(instructions, history, toolsJson);
        if (!resp) {
            return resp;
        }
        // resp 是 output_text 或完整响应？这里 create 返回 output_text；
        // 但 function_call 需要完整 raw。约定：create 返回完整 JSON 响应体。
        yyjson_doc* doc = yyjson_read(resp->data(), resp->size(), 0);
        if (!doc) {
            // 纯文本完成
            return *resp;
        }
        yyjson_val* root = yyjson_doc_get_root(doc);
        // 若无 function_call，取 output_text 结束
        bool hasCall = false;
        std::vector<std::pair<std::string, std::string>> calls; // id, name+args
        if (yyjson_val* output = util::json::GetArr(root, "output")) {
            size_t i = 0, n = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(output, i, n, item) {
                if (!yyjson_is_obj(item)) continue;
                // S44：**兼容两种事件名** —— OpenAI Responses 用 `function_call`，MiniMax 的
                // Responses 兼容端用 **`function`**（真跑实测）。只认前者会**静默忽略**掉所有
                // 工具调用（现象：模型说"让我用工具查库"，而循环报"工具 0 次"）。
                const std::string itype = util::json::GetStrCopy(item, "type");
                if (itype != "function_call" && itype != "function") continue;
                hasCall = true;
                const std::string callId = util::json::GetStrCopy(item, "call_id");
                const std::string name = util::json::GetStrCopy(item, "name");
                const std::string args = util::json::GetStrCopy(item, "arguments");
                calls.emplace_back(callId.empty() ? fmt::format("call_{}", i) : callId,
                                   name + "\x1f" + args);
            }
        }
        if (!hasCall) {
            std::string text = util::json::GetStrCopy(root, "output_text");
            if (text.empty()) {
                // 拼 message text
                if (yyjson_val* output = util::json::GetArr(root, "output")) {
                    size_t i = 0, n = 0;
                    yyjson_val* item = nullptr;
                    yyjson_arr_foreach(output, i, n, item) {
                        if (util::json::GetStr(item, "type") != "message") continue;
                        if (yyjson_val* content = util::json::GetArr(item, "content")) {
                            size_t ci = 0, cn = 0;
                            yyjson_val* c = nullptr;
                            yyjson_arr_foreach(content, ci, cn, c) {
                                if (util::json::GetStr(c, "type") == "output_text") {
                                    text += util::json::GetStrCopy(c, "text");
                                }
                            }
                        }
                    }
                }
            }
            yyjson_doc_free(doc);
            return text;
        }

        // 执行 calls，追加 function_call_output
        for (const auto& [callId, nameArgs] : calls) {
            const auto sep = nameArgs.find('\x1f');
            const std::string name = nameArgs.substr(0, sep);
            const std::string argsStr =
                sep == std::string::npos ? "{}" : nameArgs.substr(sep + 1);
            const std::string guard = reg.CheckLoopGuard(name, argsStr);
            if (!guard.empty()) {
                yyjson_doc_free(doc);
                return std::unexpected(guard);
            }
            reg.NoteCall(name, argsStr);
            if (stats) {
                ++stats->steps;
                stats->callLog.push_back(fmt::format("{}({})", name, TruncateForLog(argsStr, 40)));
            }
            yyjson_doc* adoc = yyjson_read(argsStr.data(), argsStr.size(), 0);
            yyjson_val* aroot = adoc ? yyjson_doc_get_root(adoc) : nullptr;
            auto er = reg.Execute(name, aroot);
            std::string resultJson;
            if (er) {
                resultJson = ValToJson(yyjson_doc_get_root(*er));
                yyjson_doc_free(*er);
            } else {
                resultJson = fmt::format(R"({{"ok":false,"code":"{}","message":"{}"}})",
                                         er.error().code, er.error().message);
            }
            if (adoc) yyjson_doc_free(adoc);
            // 追加到 history
            // S45：**回填的这两个字段必须是「字符串」**（Responses 规范）——
            // `function_call.arguments` 与 `function_call_output.output` 都是**JSON 文本字符串**，
            // 不是 JSON 对象。真跑实测：传对象（`"arguments":{...}`）⇒ MiniMax 直接拒收
            // （`input is neither string nor array of items`，报错位置正指向工具结果那一段 ——
            //  而那段里能看到 `is_system`，说明**工具其实已经被成功调用了**，死在回填而非调用）。
            // ⚠️ 转义交给 `util::json::JsonQuote`（S42 统一的唯一入口），别手写。
            const std::string item = fmt::format(
                R"({{"type":"function_call","call_id":"{}","name":"{}","arguments":{}}})"
                R"(,{{"type":"function_call_output","call_id":"{}","output":{}}})",
                callId, name, util::json::JsonQuote(argsStr.empty() ? "{}" : argsStr), callId,
                util::json::JsonQuote(resultJson));
            // history 是 [...]（追加逻辑与上面的预置共用 `appendItem`，只此一份）
            appendItem(history, item);
        }
        yyjson_doc_free(doc);
    }
    return std::unexpected(std::string{"工具循环超出最大迭代"});
}

} // namespace shine::agent
