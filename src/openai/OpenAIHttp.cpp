#include "openai/OpenAIHttp.h"

#include "comfy/ComfyHttp.h" // EnsureLibhvReady
#include "core/Log.h"
#include "util/Encoding.h"

#include <HttpClient.h>
#include <HttpMessage.h>
#include <hlog.h>

#include <fmt/format.h>

#include <cstring>
#include <memory>
#include <utility>

// libhv HttpRequest/HttpResponse 在全局命名空间
using HvRequest = HttpRequest;
using HvResponse = HttpResponse;

namespace shine::openai {
namespace {

[[nodiscard]] int TimeoutSecs(std::chrono::seconds s) noexcept {
    return static_cast<int>(s.count());
}

[[nodiscard]] std::chrono::seconds ConnectTimeoutOf(std::chrono::seconds timeout) noexcept {
    using namespace std::chrono_literals;
    constexpr auto kConnect = 15s;
    return timeout < kConnect ? timeout : kConnect;
}

// 与 comfy::NetworkErrorText 同源，文案改成 OpenAI 语境
[[nodiscard]] std::string NetworkErrorText(int ret) {
    const int code = ret < 0 ? -ret : ret;
    if (code >= 10000) {
        switch (code) {
        case 10061:
            return "连接被拒绝：OpenAI 端点未监听，或 base URL 写错（WSA 10061）";
        case 10060:
            return "连接超时：地址不可达或被防火墙静默丢弃（WSA 10060）";
        case 10065:
            return "网络不可达：检查本机网络与 openaiBaseUrl（WSA 10065）";
        case 11001:
            return "无法解析主机名：检查设置里的 openaiBaseUrl（WSA 11001）";
        case 10054:
            return "连接被对端重置：服务可能刚重启（WSA 10054）";
        default:
            break;
        }
        const std::string sys = util::WinErrorMessage(static_cast<unsigned long>(code));
        return sys.empty() ? fmt::format("网络错误（WSA {}）", code) : fmt::format("{}（WSA {}）", sys, code);
    }
    return fmt::format("连接失败：OpenAI 端点不通或 TLS 握手失败（libhv {}）", ret);
}

struct UrlParts {
    std::string host;
    int port = 443;
    std::string path = "/";
    bool https = true;
};

[[nodiscard]] std::expected<UrlParts, std::string> ParseUrl(std::string_view url) {
    if (url.empty()) {
        return std::unexpected(std::string{"空 URL"});
    }
    UrlParts out;
    std::string_view rest = url;
    if (rest.starts_with("https://")) {
        out.https = true;
        out.port = 443;
        rest.remove_prefix(8);
    } else if (rest.starts_with("http://")) {
        out.https = false;
        out.port = 80;
        rest.remove_prefix(7);
    } else {
        return std::unexpected(std::string{"URL 必须以 http:// 或 https:// 开头"});
    }
    const auto slash = rest.find('/');
    const std::string_view hostPort = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
    out.path = (slash == std::string_view::npos) ? "/" : std::string{rest.substr(slash)};
    const auto colon = hostPort.find(':');
    if (colon == std::string_view::npos) {
        out.host = std::string{hostPort};
    } else {
        out.host = std::string{hostPort.substr(0, colon)};
        int port = 0;
        for (const char c : hostPort.substr(colon + 1)) {
            if (c < '0' || c > '9') {
                return std::unexpected(std::string{"URL 端口非法"});
            }
            port = port * 10 + (c - '0');
        }
        if (port <= 0 || port > 65535) {
            return std::unexpected(std::string{"URL 端口非法"});
        }
        out.port = port;
    }
    if (out.host.empty()) {
        return std::unexpected(std::string{"URL 缺少主机名"});
    }
    return out;
}

[[nodiscard]] HttpTransportResult FailTransport(std::string_view err) {
    HttpTransportResult r;
    r.error = std::string{err};
    return r;
}

[[nodiscard]] int ParseHttpStatus(std::string_view statusLine) noexcept {
    // HTTP/1.1 200 OK
    if (!statusLine.starts_with("HTTP/")) {
        return 0;
    }
    const auto sp = statusLine.find(' ');
    if (sp == std::string_view::npos) {
        return 0;
    }
    const auto sp2 = statusLine.find(' ', sp + 1);
    const auto num = statusLine.substr(sp + 1, (sp2 == std::string_view::npos ? statusLine.size() : sp2) - sp - 1);
    int status = 0;
    for (const char c : num) {
        if (c >= '0' && c <= '9') {
            status = status * 10 + (c - '0');
        } else {
            break;
        }
    }
    return status;
}

} // namespace

HttpTransportResult PostJson(std::string_view url, std::string_view bearer, std::string_view jsonBody,
                             std::chrono::seconds timeout) {
    if (bearer.empty()) {
        return FailTransport("未配置 OpenAI API 密钥（设置或环境变量 OPENAI_API_KEY）");
    }
    if (jsonBody.empty()) {
        return FailTransport("请求体为空");
    }
    if (const auto parts = ParseUrl(url); !parts) {
        return FailTransport(parts.error());
    }

    comfy::EnsureLibhvReady();

    HvRequest req;
    req.method = HTTP_POST;
    req.url = std::string{url};
    req.timeout = TimeoutSecs(timeout);
    req.body = std::string{jsonBody};
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "Bearer " + std::string{bearer};
    req.headers["Accept"] = "application/json";

    auto resp = std::make_shared<HvResponse>();
    const int ret = http_client_send(&req, resp.get());
    if (ret != 0) {
        return FailTransport(NetworkErrorText(ret));
    }
    HttpTransportResult out;
    out.status = resp->status_code;
    out.body = std::move(resp->body);
    if (out.status < 200 || out.status >= 300) {
        out.error = fmt::format("HTTP {}", out.status);
    }
    return out;
}

HttpTransportResult
PostJsonHeaders(std::string_view url, const std::vector<std::pair<std::string, std::string>>& headers,
                std::string_view jsonBody, std::chrono::seconds timeout) {
    if (jsonBody.empty()) {
        return FailTransport("请求体为空");
    }
    if (const auto parts = ParseUrl(url); !parts) {
        return FailTransport(parts.error());
    }
    comfy::EnsureLibhvReady();
    HvRequest req;
    req.method = HTTP_POST;
    req.url = std::string{url};
    req.timeout = TimeoutSecs(timeout);
    req.body = std::string{jsonBody};
    req.headers["Content-Type"] = "application/json";
    req.headers["Accept"] = "application/json";
    for (const auto& [k, v] : headers) {
        req.headers[k] = v;
    }
    auto resp = std::make_shared<HvResponse>();
    const int ret = http_client_send(&req, resp.get());
    if (ret != 0) {
        return FailTransport(NetworkErrorText(ret));
    }
    HttpTransportResult out;
    out.status = resp->status_code;
    out.body = std::move(resp->body);
    if (out.status < 200 || out.status >= 300) {
        out.error = fmt::format("HTTP {}", out.status);
    }
    return out;
}

HttpTransportResult PostSse(std::string_view url, std::string_view bearer, std::string_view jsonBody,
                            std::chrono::seconds timeout, const SseChunkFn& on_chunk) {
    if (bearer.empty()) {
        return FailTransport("未配置 OpenAI API 密钥（设置或环境变量 OPENAI_API_KEY）");
    }
    if (jsonBody.empty()) {
        return FailTransport("请求体为空");
    }
    if (!on_chunk) {
        return FailTransport("缺少 SSE 回调");
    }
    auto parts = ParseUrl(url);
    if (!parts) {
        return FailTransport(parts.error());
    }

    comfy::EnsureLibhvReady();

    http_client_t* cli = http_client_new(parts->host.c_str(), parts->port, parts->https ? 1 : 0);
    if (!cli) {
        return FailTransport("创建 HTTP 客户端失败");
    }
    struct ClientGuard {
        http_client_t* c = nullptr;
        ~ClientGuard() {
            if (c) {
                http_client_close(c);
                http_client_del(c);
            }
        }
    } guard{cli};

    http_client_set_timeout(cli, TimeoutSecs(timeout));
    const int conn = http_client_connect(cli, parts->host.c_str(), parts->port, parts->https ? 1 : 0,
                                         TimeoutSecs(ConnectTimeoutOf(timeout)));
    if (conn < 0) {
        return FailTransport(NetworkErrorText(conn));
    }

    std::string head;
    head.reserve(512 + jsonBody.size());
    head += "POST " + parts->path + " HTTP/1.1\r\n";
    head += "Host: " + parts->host;
    if (parts->port != (parts->https ? 443 : 80)) {
        head += ":" + std::to_string(parts->port);
    }
    head += "\r\n";
    head += "Content-Type: application/json\r\n";
    head += "Authorization: Bearer " + std::string{bearer} + "\r\n";
    head += "Accept: text/event-stream\r\n";
    head += "Cache-Control: no-cache\r\n";
    head += "Connection: close\r\n";
    head += "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n\r\n";
    head += jsonBody;

    if (const int ns = http_client_send_data(cli, head.data(), static_cast<int>(head.size())); ns < 0) {
        return FailTransport(fmt::format("发送请求失败（libhv {}）", ns));
    }

    std::string buffer;
    buffer.reserve(8192);
    bool headers_done = false;
    HttpTransportResult out;
    char chunk[4096];

    for (;;) {
        const int n = http_client_recv_data(cli, chunk, static_cast<int>(sizeof(chunk)));
        if (n < 0) {
            if (!headers_done) {
                return FailTransport(NetworkErrorText(n));
            }
            break; // 已收流：对端断开视为结束
        }
        if (n == 0) {
            break;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));

        if (!headers_done) {
            const auto hdrEnd = buffer.find("\r\n\r\n");
            if (hdrEnd == std::string::npos) {
                if (buffer.size() > 64 * 1024) {
                    return FailTransport("HTTP 响应头过大");
                }
                continue;
            }
            const auto lineEnd = buffer.find("\r\n");
            out.status = ParseHttpStatus(std::string_view{buffer}.substr(0, lineEnd));
            buffer.erase(0, hdrEnd + 4);
            headers_done = true;
        }

        if (!headers_done || buffer.empty()) {
            continue;
        }
        if (out.status >= 200 && out.status < 300) {
            if (!on_chunk(std::string_view{buffer})) {
                break;
            }
        } else {
            out.body += buffer;
        }
        buffer.clear();
    }

    if (!headers_done) {
        return FailTransport("响应不完整：未读到 HTTP 头");
    }
    if (out.status >= 400) {
        out.error = fmt::format("HTTP {}", out.status);
    }
    return out;
}

} // namespace shine::openai
