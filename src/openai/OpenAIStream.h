#pragma once
// OpenAI Responses SSE：字节流 → StreamEvent
#include "openai/OpenAITypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::openai {

// 增量 SSE 解析器：喂任意边界的数据块，吐出完整事件
class SseParser {
public:
    // 喂入原始 chunk；返回本批解析出的完整事件
    [[nodiscard]] std::vector<StreamEvent> Feed(std::string_view chunk);

    // 流结束时若缓冲区仍有残缺事件则丢弃
    void Reset() noexcept { buf_.clear(); }

private:
    std::string buf_;
};

// 将一条 SSE 的 `data:` JSON 映射为 StreamEvent
[[nodiscard]] StreamEvent MapEventData(std::string_view json);

// Client::Stream 用：把 transport 结果 + 已收文本转成 done
// （实现在 OpenAIStream.cpp 与 OpenAIClient.cpp 协作）

} // namespace shine::openai
