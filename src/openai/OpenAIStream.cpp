#include "openai/OpenAIStream.h"

#include "util/Json.h"

#include <yyjson.h>

#include <utility>

namespace shine::openai {

StreamEvent MapEventData(std::string_view json) {
    StreamEvent ev;
    if (json.empty()) {
        return ev;
    }
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        return ev;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    const std::string_view type = util::json::GetStr(root, "type");

    if (type == "response.created" || type == "response.in_progress") {
        ev.type = StreamEvent::Type::Created;
    } else if (type == "response.output_text.delta") {
        ev.type = StreamEvent::Type::TextDelta;
        ev.text = util::json::GetStrCopy(root, "delta");
    } else if (type == "response.completed" || type == "response.incomplete") {
        ev.type = StreamEvent::Type::Completed;
    } else if (type == "response.failed" || type == "error") {
        ev.type = StreamEvent::Type::Error;
        // {"type":"response.failed","response":{"error":{...}}} 或顶层 error
        yyjson_val* err = util::json::GetObj(root, "error");
        if (!err) {
            if (yyjson_val* resp = util::json::GetObj(root, "response")) {
                err = util::json::GetObj(resp, "error");
            }
        }
        ev.message = util::json::GetStrCopy(err, "message");
        if (ev.message.empty()) {
            ev.message = util::json::GetStrCopy(root, "message");
        }
        if (ev.message.empty()) {
            ev.message = "流式响应失败";
        }
    } else {
        ev.type = StreamEvent::Type::Other;
        ev.message = std::string{type};
    }
    yyjson_doc_free(doc);
    return ev;
}

std::vector<StreamEvent> SseParser::Feed(std::string_view chunk) {
    std::vector<StreamEvent> events;
    buf_.append(chunk);

    for (;;) {
        const auto sep = buf_.find("\n\n");
        const auto sepCr = buf_.find("\r\n\r\n");
        std::size_t cut = std::string::npos;
        std::size_t cutLen = 0;
        if (sep != std::string::npos && (sepCr == std::string::npos || sep < sepCr)) {
            cut = sep;
            cutLen = 2;
        } else if (sepCr != std::string::npos) {
            cut = sepCr;
            cutLen = 4;
        }
        if (cut == std::string::npos) {
            break;
        }

        // ⚠️ 必须先拷出 block 再 erase：erase 会使指向 buf_ 的 string_view 失效（UAF）
        const std::string block = buf_.substr(0, cut);
        buf_.erase(0, cut + cutLen);

        std::string data;
        std::size_t pos = 0;
        while (pos < block.size()) {
            auto eol = block.find('\n', pos);
            if (eol == std::string::npos) {
                eol = block.size();
            }
            std::string_view line{block.data() + pos, eol - pos};
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (line.starts_with("data:")) {
                std::string_view payload = line.substr(5);
                if (!payload.empty() && payload.front() == ' ') {
                    payload.remove_prefix(1);
                }
                data.assign(payload);
            }
            pos = eol + 1;
        }
        if (data.empty()) {
            continue;
        }
        if (data == "[DONE]") {
            break;
        }
        events.push_back(MapEventData(data));
    }
    return events;
}

} // namespace shine::openai
