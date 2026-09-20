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
    // S41 我在这里手写补过一次 `\r`/`\t`，但那是**打补丁**：项目里还有 6 处同款手写实现，
    // 各自漏的字符还不一样（`AgentKit` 连 `\t` 都漏）。统一走这一个入口。
    history = fmt::format(R"([{{"role":"user","content":{}}}])", util::json::JsonQuote(userText));

    // tools 导出
    yyjson_mut_doc* tdoc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* tools = reg.ExportOpenAiTools(tdoc);
    size_t tlen = 0;
    char* tjson = yyjson_mut_val_write(tools, 0, &tlen);
    const std::string toolsJson = tjson ? std::string{tjson, tlen} : "[]";
    if (tjson) std::free(tjson);
    yyjson_mut_doc_free(tdoc);

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
                if (util::json::GetStr(item, "type") != "function_call") continue;
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
            const std::string item = fmt::format(
                R"({{"type":"function_call","call_id":"{}","name":"{}","arguments":{}}})"
                R"(,{{"type":"function_call_output","call_id":"{}","output":{}}})",
                callId, name, argsStr.empty() ? "{}" : argsStr, callId, resultJson);
            // history 是 [...]
            if (!history.empty() && history.back() == ']') {
                history.pop_back();
                if (history.size() > 1 && history[history.size() - 1] == '[') {
                    history += item;
                } else {
                    history += ",";
                    history += item;
                }
                history += "]";
            }
        }
        yyjson_doc_free(doc);
    }
    return std::unexpected(std::string{"工具循环超出最大迭代"});
}

} // namespace shine::agent
