#include "comfy/ComfyHttp.h"

#include "core/Log.h"
#include "net/HttpClient.h"
#include "net/LibhvReady.h"
#include "util/Encoding.h"

#include <HttpClient.h>
#include <HttpMessage.h>
#include <fmt/format.h>

#include <utility>

namespace shine::comfy {
namespace {

// Comfy 语境的网络错误文案（在 net::NetworkErrorText 之上补业务提示）
[[nodiscard]] std::string ComfyNetworkErrorText(int ret) {
    const int code = ret < 0 ? -ret : ret;
    if (code >= 10000) {
        switch (code) {
        case 10061:
            return "连接被拒绝：端口没有服务在监听，ComfyUI 是否已启动？（WSA 10061）";
        case 10060:
            return "连接超时：地址不可达或被防火墙静默丢弃（WSA 10060）";
        case 10065:
            return "网络不可达：检查本机网络与 ComfyUI 地址（WSA 10065）";
        case 10013:
            return "访问该端口被拒绝：权限或防火墙拦截（WSA 10013）";
        case 11001:
            return "无法解析主机名：检查设置里的 ComfyUI 地址（WSA 11001）";
        case 10054:
            return "连接被对端重置：服务可能刚重启（WSA 10054）";
        default:
            break;
        }
        return net::NetworkErrorText(ret);
    }
    return fmt::format("连接失败：ComfyUI 未启动或端口不通（libhv {}）", ret);
}

void DecorateComfyError(net::HttpResponse& r) {
    if (!r.ok && r.libhvRet != 0) {
        r.error = fmt::format("网络错误: {}", ComfyNetworkErrorText(r.libhvRet));
    }
}

HttpResponse FromNet(const net::HttpResponse& n) {
    HttpResponse out;
    out.ok = n.ok;
    out.status = n.status;
    out.body = n.body;
    out.error = n.error;
    return out;
}

HttpResponse Fail(std::string_view err) {
    HttpResponse out;
    out.error = std::string{err};
    return out;
}

} // namespace

// 兼容旧符号：实现下沉到 shine::net（唯一 call_once）
void EnsureLibhvReady() { net::EnsureLibhvReady(); }

HttpResponse HttpGet(std::string_view url, std::chrono::seconds timeout) {
    if (url.empty()) {
        return Fail("空 URL");
    }
    auto n = net::HttpGet(url, timeout);
    DecorateComfyError(n);
    return FromNet(n);
}

HttpResponse HttpPostJson(std::string_view url, std::string_view jsonBody,
                          std::chrono::seconds timeout) {
    if (url.empty()) {
        return Fail("空 URL");
    }
    auto n = net::HttpPostJson(url, jsonBody, timeout);
    DecorateComfyError(n);
    return FromNet(n);
}

HttpResponse HttpPostEmpty(std::string_view url, std::chrono::seconds timeout) {
    return HttpPostJson(url, "{}", timeout);
}

std::expected<std::string, HttpError> HttpDownloadBinary(std::string_view url,
                                                         std::chrono::seconds timeout) {
    const auto n = net::Download(url, timeout);
    if (!n) {
        return std::unexpected(HttpError{n.error().status, n.error().message});
    }
    return *n;
}

HttpResponse HttpUploadImage(std::string_view url, std::string_view fileName,
                             std::string_view fileBytes,
                             const std::map<std::string, std::string>& fields,
                             std::chrono::seconds timeout) {
    if (url.empty()) {
        return Fail("空 URL");
    }
    static const char* kBoundary = "----ShineTVBoundary7d4a6e";
    std::string body;
    for (const auto& [k, v] : fields) {
        body += "--";
        body += kBoundary;
        body += "\r\nContent-Disposition: form-data; name=\"";
        body += k;
        body += "\"\r\n\r\n";
        body += v;
        body += "\r\n";
    }
    body += "--";
    body += kBoundary;
    body += "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"";
    body += fileName;
    body += "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    body += fileBytes;
    body += "\r\n--";
    body += kBoundary;
    body += "--\r\n";

    net::Request req;
    req.method = "POST";
    req.url = std::string{url};
    req.timeout = timeout;
    req.body = std::move(body);
    req.headers["Content-Type"] = std::string("multipart/form-data; boundary=") + kBoundary;
    auto n = net::Send(req);
    DecorateComfyError(n);
    return FromNet(n);
}

} // namespace shine::comfy
