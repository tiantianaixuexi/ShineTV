#include "comfy/ComfyTypes.h"

#include "util/Random.h"
#include "util/Strings.h"

namespace shine::comfy {

const char* ConnectionStateLabel(ConnectionState s) noexcept {
    switch (s) {
    case ConnectionState::Connecting: return "连接中";
    case ConnectionState::Connected: return "已连接";
    case ConnectionState::Error: return "错误";
    default: return "未连接";
    }
}

const char* TaskStateLabel(TaskState s) noexcept {
    switch (s) {
    case TaskState::Pending: return "排队";
    case TaskState::Running: return "运行中";
    case TaskState::Done: return "完成";
    case TaskState::Failed: return "失败";
    case TaskState::Cancelled: return "已取消";
    default: return "未知";
    }
}

const char* BusyStateLabel(BusyState s) noexcept {
    switch (s) {
    case BusyState::Idle: return "空闲";
    case BusyState::Queued: return "排队中";
    case BusyState::Running: return "执行中";
    case BusyState::Interrupted: return "已中断";
    default: return "疑似卡住";
    }
}

std::string ErrorHintFor(std::string_view exceptionType, std::string_view message) {
    const std::string all = util::ToLower(std::string{exceptionType}) + ' ' + util::ToLower(std::string{message});
    const auto has = [&all](std::string_view needle) { return all.find(needle) != std::string::npos; };

    if (has("out of memory") || has("outofmemory") || has("cuda error")) {
        return "显存/内存不足：降低分辨率或步数，或先执行「释放显存」";
    }
    if (has("value_not_in_list") || has("not in list") || has("no such file") || has("file not found") ||
        has("does not exist")) {
        return "模型或选项名不存在：检查 ckpt_name / lora / 自定义节点选项，或先「刷新 object_info」";
    }
    if (has("modulenotfound") || has("importerror")) {
        return "节点依赖缺失：请在 ComfyUI 侧为该节点安装依赖后重启服务";
    }
    if (has("connection") || has("timeout") || has("timed out") || has("refused")) {
        return "网络/连接问题：确认 ComfyUI 正在运行且地址正确（设置里的 ComfyUI 地址）";
    }
    if (has("interrupt")) {
        return "任务被中断：可直接重新提交";
    }
    if (has("prompt_outputs_failed_validation") || has("invalid")) {
        return "输入校验失败：检查模型中填写的参数是否在取值范围内";
    }
    return "查看上方回溯定位；必要时在 ComfyUI 中单独运行该工作流复现";
}

std::string NormalizeBaseUrl(std::string_view baseUrl) {
    // 先去掉前导空格/制表符，再去掉尾部空白与 '/'（与原实现逐字符等价；工具见 util/Strings.h）
    const auto head = baseUrl.find_first_not_of(" \t");
    baseUrl = (head == std::string_view::npos) ? std::string_view{} : baseUrl.substr(head);
    baseUrl = util::TrimEnd(baseUrl, " \t/\r\n");
    if (baseUrl.empty()) {
        return {};
    }
    std::string s{baseUrl};
    if (s.rfind("http://", 0) != 0 && s.rfind("https://", 0) != 0 && s.rfind("ws://", 0) != 0 && s.rfind("wss://", 0) != 0) {
        s = "http://" + s;
    }
    // ws -> http for base
    if (s.rfind("ws://", 0) == 0) {
        s = "http://" + s.substr(5);
    } else if (s.rfind("wss://", 0) == 0) {
        s = "https://" + s.substr(6);
    }
    return s;
}

std::string BuildApiUrl(std::string_view baseUrl, std::string_view relativePath) {
    std::string base = NormalizeBaseUrl(baseUrl);
    if (base.empty()) {
        return {};
    }
    std::string rel{relativePath};
    if (!rel.empty() && rel.front() != '/') {
        rel = "/" + rel;
    }
    return base + rel;
}

std::string BuildWebSocketUrl(std::string_view baseUrl, std::string_view clientId) {
    const std::string base = NormalizeBaseUrl(baseUrl);
    if (base.empty()) {
        return {};
    }
    // ComfyUI 官方约定：ws://<host>/ws?clientId=<uuid>（clientId 是大小写敏感的查询参数）
    const bool tls = base.rfind("https://", 0) == 0;
    std::string url = tls ? "wss://" : "ws://";
    url.append(base.substr(tls ? 8 : 7));
    url.append("/ws?clientId=");
    url.append(clientId); // std::string::append(std::string_view) 是标准接口，无中间拷贝
    return url;
}

std::string MakeClientId() { return util::RandomId32(); }
std::string MakePromptId() { return util::RandomId32(); }

} // namespace shine::comfy
