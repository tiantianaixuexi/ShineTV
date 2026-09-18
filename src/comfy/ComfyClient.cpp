#include "comfy/ComfyClient.h"

#include "comfy/ComfyHttp.h"
#include "core/Async.h"
#include "core/Log.h"
#include "util/Json.h"
#include "util/Strings.h"

#include <exec/start_detached.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>
#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>

namespace shine::comfy {
namespace {

std::once_flag g_poolOnce;
std::unique_ptr<exec::static_thread_pool> g_pool;

exec::static_thread_pool& Pool() {
    std::call_once(g_poolOnce, [] { g_pool = std::make_unique<exec::static_thread_pool>(2); });
    return *g_pool;
}

// 只读入参用 string_view，返回拥有的小写副本
[[nodiscard]] std::string Lower(std::string_view s) { return util::ToLower(s); }

// URL 查询参数转义（文件名常带空格/中文）
[[nodiscard]] std::string PercentEncode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// 读取字符串字段；数值字段也转成字符串（与原实现行为一致）。查键走 util::json::Get 收口。
[[nodiscard]] std::string ReadStr(yyjson_val* obj, const char* key) {
    if (!obj || !key) {
        return {};
    }
    yyjson_val* v = util::json::Get(obj, key);
    if (!v || yyjson_is_null(v)) {
        return {};
    }
    if (yyjson_is_str(v)) {
        const char* s = yyjson_get_str(v);
        return s ? s : "";
    }
    if (yyjson_is_num(v)) {
        return std::to_string(yyjson_get_sint(v));
    }
    return {};
}

double ReadNum(yyjson_val* obj, const char* key, double fallback = 0.0) {
    if (!obj || !key) {
        return fallback;
    }
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_num(v)) {
        return fallback;
    }
    return yyjson_get_num(v);
}

bool ReadBool(yyjson_val* obj, const char* key, bool fallback = false) {
    if (!obj || !key) {
        return fallback;
    }
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v) {
        return fallback;
    }
    return yyjson_get_bool(v);
}

bool ParseQueueEntry(yyjson_val* tuple, TaskState state, int index, QueueEntry& out) {
    if (!tuple || !yyjson_is_arr(tuple) || yyjson_arr_size(tuple) < 2) {
        return false;
    }
    yyjson_val* idVal = yyjson_arr_get(tuple, 1);
    if (!idVal || !yyjson_is_str(idVal)) {
        return false;
    }
    out.promptId = yyjson_get_str(idVal);
    out.state = state;
    out.queueIndex = index;
    yyjson_val* numVal = yyjson_arr_get(tuple, 0);
    if (numVal && yyjson_is_num(numVal)) {
        out.number = yyjson_get_num(numVal);
    }
    if (yyjson_arr_size(tuple) > 3) {
        yyjson_val* extra = yyjson_arr_get(tuple, 3);
        if (extra && yyjson_is_obj(extra)) {
            out.createdAtMillis = static_cast<std::int64_t>(ReadNum(extra, "create_time"));
        }
    }
    return true;
}

void ParseOutputMedia(yyjson_val* outputs, std::string_view promptId, HistoryEntry& entry) {
    if (!outputs || !yyjson_is_obj(outputs)) {
        return;
    }
    yyjson_obj_iter it;
    yyjson_obj_iter_init(outputs, &it);
    yyjson_val* key = nullptr;
    yyjson_val* val = nullptr;
    while ((key = yyjson_obj_iter_next(&it))) {
        val = yyjson_obj_iter_get_val(key);
        if (!val || !yyjson_is_obj(val)) {
            continue;
        }
        const std::string nodeId = yyjson_get_str(key) ? yyjson_get_str(key) : "";
        yyjson_obj_iter nodeIt;
        yyjson_obj_iter_init(val, &nodeIt);
        yyjson_val* outKey = nullptr;
        while ((outKey = yyjson_obj_iter_next(&nodeIt))) {
            yyjson_val* arr = yyjson_obj_iter_get_val(outKey);
            if (!arr || !yyjson_is_arr(arr)) {
                continue;
            }
            const size_t n = yyjson_arr_size(arr);
            for (size_t i = 0; i < n; ++i) {
                yyjson_val* item = yyjson_arr_get(arr, i);
                if (!item || !yyjson_is_obj(item)) {
                    continue;
                }
                HistoryMedia m;
                m.nodeId = nodeId;
                m.fileName = ReadStr(item, "filename");
                m.subfolder = ReadStr(item, "subfolder");
                m.type = ReadStr(item, "type");
                m.kind = ClassifyMediaKind(m.fileName);
                if (!m.fileName.empty()) {
                    entry.media.push_back(std::move(m));
                }
            }
        }
        (void)promptId;
    }
}

template <typename T, typename MakeWork, typename OnUi>
void RunPipeline(MakeWork makeWork, OnUi onUi) {
    // move_only_function：用 shared_ptr 进入 stdexec，避免 sender 状态要求可拷贝
    auto cb = std::make_shared<OnUi>(std::move(onUi));
    auto work = stdexec::schedule(Pool().get_scheduler()) | stdexec::then(makeWork) |
                stdexec::then([cb](T result) {
                    async::PostToUi([cb, result = std::move(result)]() mutable {
                        if (*cb) {
                            (*cb)(std::move(result));
                        }
                    });
                });
    exec::start_detached(std::move(work));
}

} // namespace

std::string ClassifyMediaKind(std::string_view fileName) {
    const std::string lower = util::ToLower(fileName);
    const auto ends = [&](std::string_view ext) { return util::EndsWith(lower, ext); };
    if (ends(".png") || ends(".jpg") || ends(".jpeg") || ends(".webp") || ends(".bmp") || ends(".gif")) {
        return "image";
    }
    if (ends(".mp4") || ends(".webm") || ends(".mov") || ends(".mkv") || ends(".avi")) {
        return "video";
    }
    if (ends(".wav") || ends(".mp3") || ends(".flac") || ends(".ogg")) {
        return "audio";
    }
    return "other";
}

bool ParseQueueJson(std::string_view json, QueueResult& out) {
    out = QueueResult{};
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        out.error = "队列 JSON 解析失败";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    out.ok = true;
    yyjson_val* running = yyjson_obj_get(root, "queue_running");
    if (running && yyjson_is_arr(running)) {
        const size_t n = yyjson_arr_size(running);
        for (size_t i = 0; i < n; ++i) {
            QueueEntry e;
            if (ParseQueueEntry(yyjson_arr_get(running, i), TaskState::Running, static_cast<int>(i), e)) {
                out.running.push_back(std::move(e));
            }
        }
    }
    yyjson_val* pending = yyjson_obj_get(root, "queue_pending");
    if (pending && yyjson_is_arr(pending)) {
        const size_t n = yyjson_arr_size(pending);
        for (size_t i = 0; i < n; ++i) {
            QueueEntry e;
            if (ParseQueueEntry(yyjson_arr_get(pending, i), TaskState::Pending, static_cast<int>(i), e)) {
                out.pending.push_back(std::move(e));
            }
        }
    }
    yyjson_doc_free(doc);
    return true;
}

bool ParseHistoryJson(std::string_view json, int maxItems, HistoryResult& out) {
    out = HistoryResult{};
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        out.error = "历史 JSON 解析失败";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        out.error = "历史根不是对象";
        return false;
    }
    out.ok = true;
    int count = 0;
    yyjson_obj_iter it;
    yyjson_obj_iter_init(root, &it);
    yyjson_val* key = nullptr;
    while ((key = yyjson_obj_iter_next(&it))) {
        if (maxItems > 0 && count >= maxItems) {
            break;
        }
        yyjson_val* hist = yyjson_obj_iter_get_val(key);
        if (!hist || !yyjson_is_obj(hist)) {
            continue;
        }
        HistoryEntry entry;
        entry.promptId = yyjson_get_str(key) ? yyjson_get_str(key) : "";
        yyjson_val* status = yyjson_obj_get(hist, "status");
        if (status && yyjson_is_obj(status)) {
            entry.statusText = ReadStr(status, "status_str");
            entry.completedAtMillis = static_cast<std::int64_t>(ReadNum(status, "completed_at"));
            const std::string st = Lower(entry.statusText);
            entry.failed = st.find("error") != std::string::npos || st.find("fail") != std::string::npos;
        }
        yyjson_val* prompt = yyjson_obj_get(hist, "prompt");
        if (prompt && yyjson_is_arr(prompt) && yyjson_arr_size(prompt) > 3) {
            yyjson_val* extra = yyjson_arr_get(prompt, 3);
            if (extra && yyjson_is_obj(extra)) {
                entry.createdAtMillis = static_cast<std::int64_t>(ReadNum(extra, "create_time"));
            }
        }
        yyjson_val* outputs = yyjson_obj_get(hist, "outputs");
        ParseOutputMedia(outputs, entry.promptId, entry);
        out.entries.push_back(std::move(entry));
        ++count;
    }
    yyjson_doc_free(doc);
    return true;
}

bool ParseObjectInfoJson(std::string_view json, ObjectInfoResult& out) {
    out = ObjectInfoResult{};
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        out.error = "object_info JSON 解析失败";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        out.error = "object_info 根不是对象";
        return false;
    }
    out.ok = true;
    out.rawJson = json;
    out.nodeClassCount = static_cast<int>(yyjson_obj_size(root));
    yyjson_doc_free(doc);
    // P3.1：结构化解析（形状自动识别 v2.0 / v1.0，见 comfy/ComfyNodeDef.h）。
    // 注：ParseNodeDefs 会再解析一遍 JSON —— 只为拿「类名 → 定义」映射；实测 1200 类约 10ms，
    //     远低于 200ms 预算，因此不给 yyjson_val* 开内部 API。
    out.nodes = ParseNodeDefs(json);
    int v2Count = 0;
    for (const NodeTypeDef& nd : out.nodes) {
        if (nd.format == NodeDefFormat::V2) {
            ++v2Count;
        }
    }
    out.format = v2Count > 0 ? NodeDefFormat::V2 : NodeDefFormat::V1;
    return true;
}

bool ParseSystemStatsJson(std::string_view json, SystemStatsResult& out) {
    out = SystemStatsResult{};
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        out.error = "system_stats JSON 解析失败";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* devices = yyjson_obj_get(root, "devices");
    if (devices && yyjson_is_arr(devices) && yyjson_arr_size(devices) > 0) {
        yyjson_val* dev = yyjson_arr_get(devices, 0);
        out.ok = true;
        out.deviceName = ReadStr(dev, "name");
        out.vramTotalBytes = ReadNum(dev, "vram_total");
        out.vramFreeBytes = ReadNum(dev, "vram_free");
    } else {
        out.ok = true;
        out.error = "无 GPU 设备信息";
    }
    yyjson_doc_free(doc);
    return out.vramTotalBytes > 0.0;
}

bool ParsePromptSubmitJson(std::string_view json, PromptSubmitResult& out) {
    out = PromptSubmitResult{};
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        out.error = "prompt 提交响应解析失败";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    out.promptId = ReadStr(root, "prompt_id");
    out.number = ReadNum(root, "number");
    yyjson_val* err = yyjson_obj_get(root, "node_errors");
    if (err) {
        size_t len = 0;
        char* s = yyjson_val_write(err, 0, &len);
        if (s) {
            out.rawNodeErrorsJson.assign(s, len);
            free(s);
        }
        // P3.0 S7：node_errors → 节点级中文错误（`Doc/RULES-COMFY.md` §12.3 第 3 条的结构）
        if (yyjson_is_obj(err)) {
            yyjson_obj_iter it;
            yyjson_obj_iter_init(err, &it);
            while (yyjson_val* key = yyjson_obj_iter_next(&it)) {
                yyjson_val* node = yyjson_obj_iter_get_val(key);
                if (!node || !yyjson_is_obj(node)) {
                    continue;
                }
                const char* nodeIdC = yyjson_get_str(key);
                yyjson_val* errors = yyjson_obj_get(node, "errors");
                const size_t errCount = (errors && yyjson_is_arr(errors)) ? yyjson_arr_size(errors) : 0;
                if (errCount == 0) {
                    NodeError ne;
                    ne.nodeId = nodeIdC ? nodeIdC : "";
                    ne.nodeType = ReadStr(node, "class_type");
                    ne.message = "该节点校验失败";
                    ne.hint = ErrorHintFor("", ne.message);
                    out.nodeErrors.push_back(std::move(ne));
                    continue;
                }
                for (size_t i = 0; i < errCount; ++i) {
                    yyjson_val* e = yyjson_arr_get(errors, i);
                    if (!e || !yyjson_is_obj(e)) {
                        continue;
                    }
                    NodeError ne;
                    ne.nodeId = nodeIdC ? nodeIdC : "";
                    ne.nodeType = ReadStr(node, "class_type");
                    const std::string type = ReadStr(e, "type");
                    const std::string details = ReadStr(e, "details");
                    yyjson_val* extra = yyjson_obj_get(e, "extra_info");
                    if (extra && yyjson_is_obj(extra)) {
                        ne.inputName = ReadStr(extra, "input_name");
                        ne.receivedValue = ReadStr(extra, "received_value");
                    }
                    // 中文：优先 details（含"哪个字段什么值不合法"），其次 type
                    std::string msg = details.empty() ? type : details;
                    ne.message = fmt::format("节点 {}（{}）{}: {}", ne.nodeId,
                                             ne.nodeType.empty() ? "?" : ne.nodeType,
                                             ne.inputName.empty() ? "校验失败" : ne.inputName + " 不合法",
                                             msg.empty() ? "未知原因" : msg);
                    ne.hint = ErrorHintFor(type, details);
                    out.nodeErrors.push_back(std::move(ne));
                }
            }
        }
    }
    yyjson_val* errorVal = yyjson_obj_get(root, "error");
    if (errorVal && yyjson_is_obj(errorVal)) {
        out.error = ReadStr(errorVal, "message");
        if (out.error.empty()) {
            out.error = "提交被拒绝";
        }
        out.ok = false;
    } else if (!out.promptId.empty()) {
        out.ok = true;
    } else {
        out.ok = false;
        if (out.error.empty()) {
            out.error = "提交响应缺少 prompt_id";
        }
    }
    yyjson_doc_free(doc);
    return out.ok;
}

bool ParseHistoryErrorDetail(std::string_view historyJson, std::string_view promptId, ErrorDetail& out) {
    out = ErrorDetail{};
    if (historyJson.empty() || promptId.empty()) {
        return false;
    }
    yyjson_doc* doc = yyjson_read(historyJson.data(), historyJson.size(), 0);
    if (!doc) {
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* hist = root ? yyjson_obj_getn(root, promptId.data(), promptId.size()) : nullptr;
    if (!hist || !yyjson_is_obj(hist)) {
        yyjson_doc_free(doc);
        return false;
    }
    const auto readStrArr = [](yyjson_val* obj, const char* key) {
        std::vector<std::string> v;
        yyjson_val* arr = obj ? yyjson_obj_get(obj, key) : nullptr;
        if (arr && yyjson_is_arr(arr)) {
            const size_t n = yyjson_arr_size(arr);
            for (size_t i = 0; i < n; ++i) {
                yyjson_val* e = yyjson_arr_get(arr, i);
                if (e && yyjson_is_str(e)) {
                    if (const char* s = yyjson_get_str(e)) {
                        v.emplace_back(s);
                    }
                }
            }
        }
        return v;
    };

    bool found = false;
    yyjson_val* status = yyjson_obj_get(hist, "status");
    yyjson_val* messages = status ? yyjson_obj_get(status, "messages") : nullptr;
    if (messages && yyjson_is_arr(messages)) {
        const size_t n = yyjson_arr_size(messages);
        for (size_t i = 0; i < n; ++i) {
            yyjson_val* item = yyjson_arr_get(messages, i);
            if (!item || !yyjson_is_arr(item) || yyjson_arr_size(item) < 2) {
                continue;
            }
            yyjson_val* nameVal = yyjson_arr_get(item, 0);
            yyjson_val* data = yyjson_arr_get(item, 1);
            const char* name = (nameVal && yyjson_is_str(nameVal)) ? yyjson_get_str(nameVal) : nullptr;
            if (!name || !data || !yyjson_is_obj(data)) {
                continue;
            }
            const std::string evName{name};
            if (evName == "execution_error") {
                // execution_error 优先级最高：找到就定稿
                out.valid = true;
                out.source = "history";
                out.promptId = std::string{promptId};
                out.nodeId = ReadStr(data, "node_id");
                out.nodeType = ReadStr(data, "node_type");
                out.exceptionType = ReadStr(data, "exception_type");
                out.exceptionMessage = ReadStr(data, "exception_message");
                out.traceback = readStrArr(data, "traceback");
                out.hint = ErrorHintFor(out.exceptionType, out.exceptionMessage);
                found = true;
                break;
            }
            if (evName == "execution_interrupted" && !found) {
                out.valid = true;
                out.source = "history";
                out.promptId = std::string{promptId};
                out.nodeId = ReadStr(data, "node_id");
                out.nodeType = ReadStr(data, "node_type");
                out.exceptionType = "execution_interrupted";
                out.exceptionMessage = "任务被中断";
                out.hint = "任务被中断：可直接重新提交";
                found = true;
            }
        }
    }
    if (!found) {
        // 没有 messages 时用 status_str 兜底（如 "error"）
        const std::string st = Lower(status && yyjson_is_obj(status) ? ReadStr(status, "status_str") : std::string{});
        if (st.find("error") != std::string::npos || st.find("fail") != std::string::npos) {
            out.valid = true;
            out.source = "history(status_str)";
            out.promptId = std::string{promptId};
            out.exceptionType = "history_error";
            out.exceptionMessage = "该任务在历史里标记为失败，但未返回详细错误（可能已被清理）";
            out.hint = ErrorHintFor("", out.exceptionMessage);
            found = true;
        }
    }
    yyjson_doc_free(doc);
    return found;
}

void FetchHistoryOutcomeAsync(std::string_view baseUrl, std::string_view promptId, HistoryOutcomeCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/history/" + std::string{promptId});
    RunPipeline<HistoryOutcome>(
        [url, id = std::string{promptId}]() {
            HistoryOutcome o;
            const HttpResponse resp = HttpGet(url);
            if (!resp.ok) {
                o.error.source = "history";
                o.error.exceptionMessage = resp.error.empty() ? resp.body : resp.error;
                return o;
            }
            yyjson_doc* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
            if (!doc) {
                return o;
            }
            yyjson_val* root = yyjson_doc_get_root(doc);
            yyjson_val* hist = root ? yyjson_obj_getn(root, id.data(), id.size()) : nullptr;
            if (hist && yyjson_is_obj(hist)) {
                o.found = true;
                const std::string st =
                    Lower(ReadStr(yyjson_obj_get(hist, "status"), "status_str"));
                o.failed = st.find("error") != std::string::npos || st.find("fail") != std::string::npos;
                if (o.failed) {
                    // 细节尽力而为：status.messages 里可能已被清理
                    (void)ParseHistoryErrorDetail(resp.body, id, o.error);
                    if (!o.error.valid) {
                        o.error.valid = true;
                        o.error.source = "history(status_str)";
                        o.error.promptId = id;
                        o.error.exceptionType = "history_error";
                        o.error.exceptionMessage = "历史显示该任务失败，但未返回详细错误（可能已被清理）";
                        o.error.hint = ErrorHintFor("", o.error.exceptionMessage);
                    }
                }
            }
            yyjson_doc_free(doc);
            return o;
        },
        std::move(cb));
}

void FetchQueueAsync(std::string_view baseUrl, QueueCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/queue");
    RunPipeline<QueueResult>(
        [url]() {
            QueueResult r;
            const HttpResponse resp = HttpGet(url);
            if (!resp.ok) {
                r.error = resp.error.empty() ? resp.body : resp.error;
                return r;
            }
            if (!ParseQueueJson(resp.body, r)) {
                r.ok = false;
            }
            return r;
        },
        std::move(cb));
}

void FetchObjectInfoAsync(std::string_view baseUrl, ObjectInfoCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/object_info");
    RunPipeline<ObjectInfoResult>(
        [url]() {
            ObjectInfoResult r;
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{30});
            if (!resp.ok) {
                r.error = resp.error.empty() ? resp.body : resp.error;
                return r;
            }
            ParseObjectInfoJson(resp.body, r);
            return r;
        },
        std::move(cb));
}

void FetchHistoryAsync(std::string_view baseUrl, int maxItems, HistoryCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/history");
    RunPipeline<HistoryResult>(
        [url, maxItems]() {
            HistoryResult r;
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{30});
            if (!resp.ok) {
                r.error = resp.error.empty() ? resp.body : resp.error;
                return r;
            }
            ParseHistoryJson(resp.body, maxItems, r);
            return r;
        },
        std::move(cb));
}

void SubmitPromptAsync(std::string_view baseUrl, const PromptSubmitRequest& req, PromptCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/prompt");
    RunPipeline<PromptSubmitResult>(
        [url, req]() {
            PromptSubmitResult r;
            // Wrap user graph into ComfyUI /prompt envelope if not already.
            std::string body = req.promptJson;
            if (body.find("\"prompt\"") == std::string::npos) {
                body = "{\"prompt\":" + req.promptJson + ",\"client_id\":\"" + req.clientId + "\"";
                if (!req.promptId.empty()) {
                    body += ",\"prompt_id\":\"" + req.promptId + "\"";
                }
                if (req.front) {
                    body += ",\"front\":true";
                }
                body += "}";
            }
            const HttpResponse resp = HttpPostJson(url, body, std::chrono::seconds{120});
            if (!resp.ok && resp.status != 400) {
                r.error = resp.error.empty() ? resp.body : resp.error;
                return r;
            }
            // Comfy returns 400 with error body on validation failure.
            ParsePromptSubmitJson(resp.body, r);
            if (!r.ok && r.error.empty() && !resp.body.empty()) {
                r.error = resp.body;
            }
            return r;
        },
        std::move(cb));
}

void InterruptAsync(std::string_view baseUrl, OpCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/interrupt");
    RunPipeline<OperationResult>(
        [url]() {
            OperationResult r;
            const HttpResponse resp = HttpPostEmpty(url, std::chrono::seconds{10});
            r.ok = resp.ok;
            r.error = resp.ok ? "" : (resp.error.empty() ? resp.body : resp.error);
            return r;
        },
        std::move(cb));
}

void FreeVramAsync(std::string_view baseUrl, OpCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/api/free");
    RunPipeline<OperationResult>(
        [url]() {
            OperationResult r;
            const HttpResponse resp = HttpPostEmpty(url, std::chrono::seconds{30});
            r.ok = resp.ok;
            r.error = resp.ok ? "" : (resp.error.empty() ? resp.body : resp.error);
            return r;
        },
        std::move(cb));
}

void FetchSystemStatsAsync(std::string_view baseUrl, StatsCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/api/system_stats");
    RunPipeline<SystemStatsResult>(
        [url]() {
            SystemStatsResult r;
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{10});
            if (!resp.ok) {
                r.error = resp.error.empty() ? resp.body : resp.error;
                return r;
            }
            // 解析失败时 r.error 已由解析函数内部写入，这里显式忽略返回值以符合 [[nodiscard]] 约定
            (void)ParseSystemStatsJson(resp.body, r);
            return r;
        },
        std::move(cb));
}

std::string BuildViewUrl(std::string_view baseUrl, std::string_view fileName, std::string_view subfolder,
                         std::string_view type) {
    std::string path = "/view?filename=" + PercentEncode(fileName);
    if (!subfolder.empty()) {
        path += "&subfolder=" + PercentEncode(subfolder);
    }
    if (!type.empty()) {
        path += "&type=" + PercentEncode(type);
    }
    return BuildApiUrl(baseUrl, path);
}

void FetchViewAsync(std::string_view baseUrl, std::string_view fileName, std::string_view subfolder,
                    std::string_view type, BinaryCb cb) {
    const std::string url = BuildViewUrl(baseUrl, fileName, subfolder, type);
    RunPipeline<BinaryResult>(
        [url]() {
            BinaryResult r;
            const auto bytes = HttpDownloadBinary(url, std::chrono::seconds{60});
            if (!bytes.has_value()) {
                r.error = bytes.error().message;
                return r;
            }
            r.ok = true;
            r.bytes = *bytes;
            return r;
        },
        std::move(cb));
}

void PingAsync(std::string_view baseUrl, OpCb cb) {
    const std::string url = BuildApiUrl(baseUrl, "/system_stats");
    RunPipeline<OperationResult>(
        [url]() {
            OperationResult r;
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{5});
            r.ok = resp.ok;
            r.error = resp.ok ? "" : (resp.error.empty() ? resp.body : resp.error);
            return r;
        },
        std::move(cb));
}

} // namespace shine::comfy
