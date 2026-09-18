#include "net/HttpClient.h"

#include "net/LibhvReady.h"
#include "util/Encoding.h"

#include <HttpClient.h>
#include <HttpMessage.h>
#include <fmt/format.h>

#include <memory>
#include <utility>

namespace shine::net {
namespace {

// libhv 类型在全局命名空间；必须用 :: 限定，避免与 shine::net::HttpResponse 同名遮蔽
using HvRequest = ::HttpRequest;
using HvResponse = ::HttpResponse;

[[nodiscard]] int TimeoutSecs(std::chrono::seconds s) noexcept { return static_cast<int>(s.count()); }

[[nodiscard]] HttpResponse FromResponse(const std::shared_ptr<::HttpResponse>& hvResp) {
    HttpResponse out;
    if (!hvResp) {
        out.error = "请求失败（无响应）";
        return out;
    }
    out.status = hvResp->status_code;
    out.body = hvResp->body;
    out.ok = hvResp->status_code >= 200 && hvResp->status_code < 300;
    if (!out.ok) {
        out.error = "HTTP " + std::to_string(hvResp->status_code);
    }
    return out;
}

[[nodiscard]] HttpResponse Fail(std::string_view err) {
    HttpResponse out;
    out.error = std::string{err};
    return out;
}

} // namespace

std::string NetworkErrorText(int ret) {
    const int code = ret < 0 ? -ret : ret;
    if (code >= 10000) { // WSA
        switch (code) {
        case 10061:
            return "连接被拒绝：目标端口没有服务在监听（WSA 10061）";
        case 10060:
            return "连接超时：地址不可达或被防火墙静默丢弃（WSA 10060）";
        case 10065:
            return "网络不可达：检查本机网络与目标地址（WSA 10065）";
        case 10013:
            return "访问该端口被拒绝：权限或防火墙拦截（WSA 10013）";
        case 11001:
            return "无法解析主机名：检查设置里的 URL（WSA 11001）";
        case 10054:
            return "连接被对端重置：服务可能刚重启（WSA 10054）";
        default:
            break;
        }
        const std::string sys = util::WinErrorMessage(static_cast<unsigned long>(code));
        return sys.empty() ? fmt::format("网络错误（WSA {}）", code)
                           : fmt::format("{}（WSA {}）", sys, code);
    }
    return fmt::format("连接失败：目标服务未启动或端口不通（libhv {}）", ret);
}

HttpResponse Send(const Request& req) {
    if (req.url.empty()) {
        return Fail("空 URL");
    }
    EnsureLibhvReady();
    HvRequest hv;
    if (req.method == "POST") {
        hv.method = HTTP_POST;
    } else if (req.method == "PUT") {
        hv.method = HTTP_PUT;
    } else if (req.method == "DELETE") {
        hv.method = HTTP_DELETE;
    } else {
        hv.method = HTTP_GET;
    }
    hv.url = req.url; // libhv 边界：string
    hv.timeout = TimeoutSecs(req.timeout);
    hv.body = req.body;
    for (const auto& [k, v] : req.headers) {
        hv.headers[k] = v;
    }
    auto resp = std::make_shared<::HttpResponse>();
    const int ret = http_client_send(&hv, resp.get());
    if (ret != 0) {
        HttpResponse out = Fail(fmt::format("网络错误: {}", NetworkErrorText(ret)));
        out.libhvRet = ret;
        return out;
    }
    return FromResponse(resp);
}

HttpResponse HttpGet(std::string_view url, std::chrono::seconds timeout) {
    Request req;
    req.method = "GET";
    req.url = std::string{url};
    req.timeout = timeout;
    return Send(req);
}

HttpResponse HttpPostJson(std::string_view url, std::string_view jsonBody,
                          std::chrono::seconds timeout) {
    Request req;
    req.method = "POST";
    req.url = std::string{url};
    req.timeout = timeout;
    req.body = std::string{jsonBody};
    req.headers["Content-Type"] = "application/json";
    return Send(req);
}

HttpResponse HttpPostEmpty(std::string_view url, std::chrono::seconds timeout) {
    return HttpPostJson(url, "{}", timeout);
}

std::expected<std::string, HttpError> Download(std::string_view url, std::chrono::seconds timeout) {
    const HttpResponse resp = HttpGet(url, timeout);
    if (!resp.ok) {
        if (!resp.error.empty()) {
            return std::unexpected(HttpError{resp.status, resp.error});
        }
        return std::unexpected(
            HttpError{resp.status, "下载失败（HTTP " + std::to_string(resp.status) + "）"});
    }
    if (resp.body.empty()) {
        return std::unexpected(HttpError{resp.status, "响应为空"});
    }
    return resp.body;
}

} // namespace shine::net
