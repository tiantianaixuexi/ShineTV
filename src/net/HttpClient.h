#pragma once
// shine::net —— 出站 HTTP 客户端（libhv 薄封装，无业务语义）
//
// 供 comfy / openai / mcp 自检等共用：URL/JSON 业务留在各模块，网络层只在这里。
// **仅 worker 线程调用**（与 RULES-AI 一致）；调用前 EnsureLibhvReady() 由 Send 内部兜底。
// 错误文案 UTF-8；禁止直接展示 libhv `http_client_strerror()`（GBK，见 Encoding.h）。
#include <chrono>
#include <expected>
#include <map>
#include <string>
#include <string_view>

namespace shine::net {

struct HttpError {
    int status = 0;      // HTTP 状态码；网络层失败时为 0
    std::string message; // 中文 / 可直接显示
};

struct HttpResponse {
    bool ok = false;
    int status = 0;
    std::string body;  // 二进制安全
    std::string error; // ok=false 时的中文原因
    int libhvRet = 0;  // libhv client 返回码；0 表示未走到网络层错误（便于业务改写文案）
};

struct Request {
    std::string method = "GET"; // "GET" / "POST" / ...
    std::string url;
    std::string body;
    std::map<std::string, std::string> headers;
    std::chrono::seconds timeout{15};
};

// 发送请求（内部 EnsureLibhvReady）。失败时 ok=false + error。
[[nodiscard]] HttpResponse Send(const Request& req);

[[nodiscard]] HttpResponse HttpGet(std::string_view url,
                                   std::chrono::seconds timeout = std::chrono::seconds{15});
[[nodiscard]] HttpResponse HttpPostJson(std::string_view url, std::string_view jsonBody,
                                        std::chrono::seconds timeout = std::chrono::seconds{60});
[[nodiscard]] HttpResponse HttpPostEmpty(std::string_view url,
                                         std::chrono::seconds timeout = std::chrono::seconds{15});

// GET 成功则返回 body 字节（二进制安全）
[[nodiscard]] std::expected<std::string, HttpError> Download(std::string_view url,
                                                             std::chrono::seconds timeout =
                                                                 std::chrono::seconds{60});

// 网络错误中文文案（通用语境；业务模块可在外层再包一层）
[[nodiscard]] std::string NetworkErrorText(int libhvRet);

} // namespace shine::net
