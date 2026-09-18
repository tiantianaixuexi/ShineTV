#include "comfy/ComfySocket.h"

#include "core/Log.h"
#include "util/Time.h"

#include <WebSocketClient.h>
#include <evpp/EventLoopThread.h>

#include <yyjson.h>

#include <chrono>
#include <unordered_set>

namespace shine::comfy {
namespace {

double NowSec() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

std::string ReadStr(yyjson_val* obj, const char* key) {
    if (!obj) {
        return {};
    }
    yyjson_val* v = yyjson_obj_get(obj, key);
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

int ReadInt(yyjson_val* obj, const char* key) {
    if (!obj) {
        return 0;
    }
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_num(v)) {
        return 0;
    }
    return static_cast<int>(yyjson_get_num(v));
}

// `data.node` 的三态：0 = 字段不存在，1 = 显式 null，2 = 有值
// （executing 的 node == null 表示"全部完成"= S3 的完成语义；其余事件可能根本没有 node 字段）
constexpr int kNodeAbsent = 0, kNodeNull = 1, kNodeValue = 2;

int NodeFieldKind(yyjson_val* obj, const char* key) {
    if (!obj) {
        return kNodeAbsent;
    }
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (!v) {
        return kNodeAbsent;
    }
    return yyjson_is_null(v) ? kNodeNull : kNodeValue;
}

std::vector<std::string> ReadStrArray(yyjson_val* obj, const char* key) {
    std::vector<std::string> out;
    if (!obj) {
        return out;
    }
    yyjson_val* arr = yyjson_obj_get(obj, key);
    if (!arr || !yyjson_is_arr(arr)) {
        return out;
    }
    const size_t n = yyjson_arr_size(arr);
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        yyjson_val* v = yyjson_arr_get(arr, i);
        if (v && yyjson_is_str(v)) {
            if (const char* s = yyjson_get_str(v)) {
                out.emplace_back(s);
            }
        }
    }
    return out;
}

// 日志里只显示 promptId 前 8 位（`Doc/RULES-COMFY.md` §12.5 的日志模板）
std::string_view ShortId(std::string_view id) noexcept {
    return id.size() > 8 ? id.substr(0, 8) : id;
}

// 事件日志状态：只被 WS 线程访问（ComfySocket 是单例，文件级 static 足够，无需加锁）
struct EventLogState {
    bool hasStatus = false;
    int lastQueueRemaining = -1;
    std::string progressKey;
    std::int64_t progressLoggedMs = 0;
    std::unordered_set<std::string> unknownTypes;
};

EventLogState& LogState() {
    static EventLogState s;
    return s;
}

// 每个事件一行；progress 类**节流**（同节点 ≥1s 或到达 100% 才打印），避免每步刷屏；见 §12.2
void LogPromptEvent(const PromptEvent& ev, int nodeKind) {
    const std::string_view pid = ShortId(ev.promptId);
    // 以"事件最终认定的节点"为准（execution_error 取 node_id、progress_state 取 nodes 键），
    // 只有真正缺失/为 null 时才显示 `<null>` / `-`
    const std::string_view node = !ev.nodeId.empty() ? std::string_view{ev.nodeId}
                                   : (nodeKind == kNodeNull ? std::string_view{"<null>"} : std::string_view{"-"});


    if (ev.type == "progress" || ev.type == "progress_state") {
        const bool reachedEnd = ev.progressMax > 0 && ev.progressValue >= ev.progressMax;
        EventLogState& st = LogState();
        const std::string key = ev.promptId + '|' + ev.nodeId;
        const std::int64_t nowMs = util::NowMillis();
        if (!reachedEnd && key == st.progressKey && (nowMs - st.progressLoggedMs) < 1000) {
            return;
        }
        st.progressKey = key;
        st.progressLoggedMs = nowMs;
        log::Info("ws event {} prompt={} node={} {}/{}", ev.type, pid, node, ev.progressValue, ev.progressMax);
        return;
    }
    if (ev.type == "executed") {
        log::Info("ws event executed prompt={} node={} image={} subfolder={} type={}", pid, node, ev.imageFileName,
                  ev.imageSubfolder, ev.imageType);
        return;
    }
    if (ev.type == "execution_cached") {
        log::Info("ws event execution_cached prompt={} nodes={}", pid, ev.cachedNodeCount);
        return;
    }
    if (ev.type == "execution_error") {
        // 多行错误块（异常类型 / 消息 / 回溯 / 中文 hint）由 S8 统一落地；这里先保证事件行与关键字段不漏
        log::Warn("ws event execution_error prompt={} node={} type={} exception={}", pid, node, ev.errorNodeType,
                  ev.exceptionType);
        return;
    }
    if (ev.type == "execution_interrupted") {
        log::Warn("ws event execution_interrupted prompt={} node={} type={}", pid, node, ev.errorNodeType);
        return;
    }
    log::Info("ws event {} prompt={} node={}", ev.type, pid, node);
}

} // namespace

struct ComfySocket::Impl {
    std::unique_ptr<hv::EventLoopThread> loopThread;
    std::shared_ptr<hv::WebSocketClient> client;
};

ComfySocket& ComfySocket::Instance() {
    static ComfySocket inst;
    return inst;
}

ComfySocket::~ComfySocket() { Shutdown(); }

bool ComfySocket::IsConnected() const {
    std::lock_guard lock(mutex_);
    return connected_;
}

void ComfySocket::AddPromptListener(PromptListener fn) {
    std::lock_guard lock(mutex_);
    promptListeners_.push_back(std::move(fn));
}

void ComfySocket::AddStatusListener(StatusListener fn) {
    std::lock_guard lock(mutex_);
    statusListeners_.push_back(std::move(fn));
}

void ComfySocket::AddBinaryListener(BinaryListener fn) {
    std::lock_guard lock(mutex_);
    binaryListeners_.push_back(std::move(fn));
}

void ComfySocket::AddStateListener(StateListener fn) {
    std::lock_guard lock(mutex_);
    stateListeners_.push_back(std::move(fn));
}

void ComfySocket::ClearListeners() {
    std::lock_guard lock(mutex_);
    promptListeners_.clear();
    statusListeners_.clear();
    stateListeners_.clear();
}

void ComfySocket::EmitState(ConnectionState s, std::string_view msg) {
    std::vector<StateListener> copy;
    {
        std::lock_guard lock(mutex_);
        state_ = s;
        copy = stateListeners_;
    }
    for (auto& fn : copy) {
        fn(s, msg);
    }
}

void ComfySocket::EnsureConnected(std::string_view baseUrl, std::string_view clientId) {
    const std::string normalized = NormalizeBaseUrl(baseUrl);
    if (normalized.empty() || clientId.empty()) {
        return;
    }
    bool needSetup = false;
    {
        std::lock_guard lock(mutex_);
        if (shutdown_) {
            return;
        }
        if (serviceUrl_ != normalized || clientId_ != clientId) {
            serviceUrl_ = normalized;
            clientId_ = std::string{clientId};
            connected_ = false;
            connecting_ = false;
            needSetup = true;
        }
        if (!impl_) {
            impl_ = std::make_unique<Impl>();
            needSetup = true;
        }
        if (!impl_->loopThread) {
            impl_->loopThread = std::make_unique<hv::EventLoopThread>();
            impl_->loopThread->start();
            needSetup = true;
        }
    }
    if (needSetup) {
        Setup(normalized, clientId);
    }
    ScheduleReconnect();
}

void ComfySocket::Setup(std::string_view baseUrl, std::string_view clientId) {
    Teardown();
    const std::string url = BuildWebSocketUrl(baseUrl, clientId);
    if (url.empty()) {
        return;
    }
    std::lock_guard lock(mutex_);
    wsUrl_ = url;
    if (!impl_ || !impl_->loopThread) {
        return;
    }
    auto client = std::make_shared<hv::WebSocketClient>(impl_->loopThread->loop());
    client->url = url;
    client->setPingInterval(15000);   // §12.1：每 15s 发一次 ping（由 libhv 负责发送与 pong 处理）
    client->onopen = [this]() { OnOpen(); };
    client->onclose = [this]() { OnClose(); };
    client->onmessage = [this](const std::string& msg) { OnMessage(msg); };
    impl_->client = client;
    connecting_ = true;
    client->open(url.c_str());
    log::Info("Comfy WS 连接中: {}", url);
}

void ComfySocket::Teardown() {
    std::shared_ptr<hv::WebSocketClient> client;
    {
        std::lock_guard lock(mutex_);
        connected_ = false;
        connecting_ = false;
        if (impl_) {
            client = impl_->client;
            impl_->client.reset();
        }
    }
    if (client) {
        client->onopen = nullptr;
        client->onmessage = nullptr;
        client->onclose = nullptr;
        client->close();
    }
}

void ComfySocket::Shutdown() {
    {
        std::lock_guard lock(mutex_);
        shutdown_ = true;
    }
    Teardown();
    std::unique_ptr<Impl> impl;
    {
        std::lock_guard lock(mutex_);
        impl = std::move(impl_);
        serviceUrl_.clear();
        wsUrl_.clear();
        clientId_.clear();
    }
    if (impl) {
        if (impl->loopThread) {
            impl->loopThread->stop();
        }
        impl.reset();
    }
}

void ComfySocket::OnOpen() {
    {
        std::lock_guard lock(mutex_);
        connected_ = true;
        connecting_ = false;
        failStreak_ = 0;
        lastFrameMonotonicMs_ = util::MonotonicMillis();   // 连接建立即视为"有活动"，避免刚连上就触发静默判定
    }
    log::Info("Comfy WS 已连接");
    EmitState(ConnectionState::Connected, "WS 已连接");
}

void ComfySocket::OnClose() {
    {
        std::lock_guard lock(mutex_);
        connected_ = false;
        connecting_ = false;
    }
    log::Warn("Comfy WS 断开，将自动重连");
    EmitState(ConnectionState::Connecting, "WS 断开，重连中");
    ScheduleReconnect();
}

std::int64_t ComfySocket::LastBinaryFrameMs() const noexcept {
    std::lock_guard lock(mutex_);
    return lastBinaryFrameMs_;
}

// 二进制预览帧（**WS 线程**）：payload 拷贝成拥有型后交给监听者；调用方负责节流与异步解码
void ComfySocket::HandleBinaryFrame(std::string_view msg) {
    constexpr std::size_t kHeaderBytes = 8; // 官方示例：前 8 字节头，其后是图像数据
    if (msg.size() <= kHeaderBytes) {
        return;
    }
    const std::string_view payload = msg.substr(kHeaderBytes);
    BinaryFrame frame;
    frame.payload.resize(payload.size());
    std::memcpy(frame.payload.data(), payload.data(), payload.size());
    const auto* p = reinterpret_cast<const unsigned char*>(payload.data());
    if (payload.size() > 8 && p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4E && p[3] == 0x47) {
        frame.format = "png";
    } else if (payload.size() > 3 && p[0] == 0xFF && p[1] == 0xD8) {
        frame.format = "jpeg";
    }
    const std::int64_t now = util::MonotonicMillis();
    frame.timestampMillis = now;

    std::vector<BinaryListener> listeners;
    {
        std::lock_guard lock(mutex_);
        lastBinaryFrameMs_ = now;
        listeners = binaryListeners_;
        // 日志降噪：最多 1s 一条（ComfyUI 每步都可能发预览帧）
        if (now - lastBinaryLogMs_ >= 1000) {
            lastBinaryLogMs_ = now;
            log::Info("ws binary frame {} 字节（格式 {}）", frame.payload.size(),
                      frame.format.empty() ? "未知" : frame.format);
        }
    }
    for (auto& fn : listeners) {
        fn(frame);
    }
}

void ComfySocket::OnMessage(std::string_view msg) {
    // 任何帧（含二进制预览帧）都刷新"最近收帧时间"——§12.1 的"30s 无任何帧"判定依据
    {
        std::lock_guard lock(mutex_);
        lastFrameMonotonicMs_ = util::MonotonicMillis();
    }
    // 二进制预览帧（P4.5）：官方示例用 `out[8:]` 取图；头内字段划分官方未给出 → 不解析头，靠魔数判格式
    if (msg.empty() || msg[0] != '{') {
        HandleBinaryFrame(msg);
        return;
    }
    yyjson_doc* doc = yyjson_read(msg.data(), msg.size(), 0);
    if (!doc) {
        log::Warn("ws message JSON 解析失败（{} 字节），已忽略", msg.size());
        return;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    const std::string type = ReadStr(root, "type");
    yyjson_val* data = yyjson_obj_get(root, "data");

    if (type == "status") {
        StatusEvent se;
        if (data) {
            yyjson_val* exec = yyjson_obj_get(data, "exec_info");
            if (exec) {
                se.execInfoQueueRemaining = ReadInt(exec, "queue_remaining");
            }
        }
        std::vector<StatusListener> copy;
        {
            std::lock_guard lock(mutex_);
            copy = statusListeners_;
        }
        for (auto& fn : copy) {
            fn(se);
        }
        // §12.2：status 事件只在 queue_remaining 变化时打印（ComfyUI 每 1–2s 推一次，全打会刷屏）
        EventLogState& st = LogState();
        if (!st.hasStatus || st.lastQueueRemaining != se.execInfoQueueRemaining) {
            st.hasStatus = true;
            st.lastQueueRemaining = se.execInfoQueueRemaining;
            log::Info("ws event status queue_remaining={}", se.execInfoQueueRemaining);
        }
        yyjson_doc_free(doc);
        return;
    }

    static constexpr const char* kHandled[] = {
        "execution_start", "executing",         "progress",   "progress_state", "executed",
        "execution_success", "execution_error", "execution_interrupted", "execution_cached",
    };
    bool handled = false;
    for (const char* h : kHandled) {
        if (type == h) {
            handled = true;
            break;
        }
    }
    if (!handled) {
        // 未识别类型：每种只报一次（自定义节点会发很多私有事件，不能刷屏）；
        // `Doc/RULES-COMFY.md` §12「版本差异提醒」要求这些类型可诊断，便于按本机版本补齐。
        if (!type.empty()) {
            EventLogState& st = LogState();
            if (st.unknownTypes.size() < 32 && st.unknownTypes.insert(type).second) {
                log::Info("ws event {} (未识别，已忽略)", type);
            }
        }
        yyjson_doc_free(doc);
        return;
    }
    if (!data) {
        log::Warn("ws event {} 缺 data 字段，已忽略", type);
        yyjson_doc_free(doc);
        return;
    }

    PromptEvent ev;
    ev.type = type;
    ev.timestampMillis = util::NowMillis();   // 收到事件的本机时间（S3 的帧静默计时用）
    ev.promptId = ReadStr(data, "prompt_id");
    // executing 的 data.node == null 表示"全部执行完毕"（S3 的完成语义），这里保留该信息仅供日志区分
    const int nodeKind = NodeFieldKind(data, "node");
    ev.nodeId = nodeKind == kNodeValue ? ReadStr(data, "node") : std::string{};

    if (type == "progress") {
        ev.progressValue = ReadInt(data, "value");
        ev.progressMax = ReadInt(data, "max");
        ev.currentNodeType = ReadStr(data, "node_type");   // 部分版本带，缺则空
    } else if (type == "progress_state") {
        // 结构随版本变化（§12.1 已注明）：{"nodes": {"<nodeId>": {value,max,state,…}}}
        // 取"进度最大"的那个节点作为展示对象；缺字段/异形结构一律静默跳过，不报错
        yyjson_val* nodes = yyjson_obj_get(data, "nodes");
        if (nodes && yyjson_is_obj(nodes)) {
            yyjson_obj_iter iter;
            yyjson_obj_iter_init(nodes, &iter);
            while (yyjson_val* k = yyjson_obj_iter_next(&iter)) {
                yyjson_val* v = yyjson_obj_iter_get_val(k);
                const int val = ReadInt(v, "value");
                const int mx = ReadInt(v, "max");
                if (mx > 0 && (mx > ev.progressMax || (mx == ev.progressMax && val > ev.progressValue))) {
                    const char* key = yyjson_get_str(k);
                    ev.nodeId = key ? key : "";
                    ev.progressValue = val;
                    ev.progressMax = mx;
                }
            }
        }
    } else if (type == "executing") {
        ev.currentNodeType = ReadStr(data, "node_type");   // 新版本可能带，缺则空
    } else if (type == "execution_cached") {
        ev.cachedNodeIds = ReadStrArray(data, "nodes");
        ev.cachedNodeCount = static_cast<int>(ev.cachedNodeIds.size());
    } else if (type == "executed") {
        ev.currentNodeType = ReadStr(data, "node_type");
        yyjson_val* output = yyjson_obj_get(data, "output");
        yyjson_val* images = output ? yyjson_obj_get(output, "images") : nullptr;
        if (images && yyjson_is_arr(images) && yyjson_arr_size(images) > 0) {
            yyjson_val* img = yyjson_arr_get(images, 0);
            ev.imageFileName = ReadStr(img, "filename");
            ev.imageSubfolder = ReadStr(img, "subfolder");
            ev.imageType = ReadStr(img, "type");
        }
    } else if (type == "execution_error") {
        // 官方 execution_error 的 data 与 /history 里那份完全一致（§12.3）→ 取全字段
        ev.failed = true;
        ev.errorNodeId = ReadStr(data, "node_id");
        ev.errorNodeType = ReadStr(data, "node_type");
        ev.nodeId = ev.errorNodeId;          // 该事件"关于哪个节点"= 出错节点（日志与 UI 都用它）
        ev.currentNodeType = ev.errorNodeType;
        ev.exceptionType = ReadStr(data, "exception_type");
        ev.exceptionMessage = ReadStr(data, "exception_message");
        ev.traceback = ReadStrArray(data, "traceback");
        ev.executedNodes = ReadStrArray(data, "executed");
        // QueueModel 现在读 errorMessage：保留旧字段语义（S8 再统一成多行错误块）
        ev.errorMessage = !ev.exceptionMessage.empty() ? ev.exceptionMessage : ev.exceptionType;
    } else if (type == "execution_interrupted") {
        // §12.2：**已中断 ≠ 失败**（任务行状态走 Cancelled），因此这里不再置 failed
        ev.errorNodeId = ReadStr(data, "node_id");
        ev.errorNodeType = ReadStr(data, "node_type");
        ev.nodeId = ev.errorNodeId;
        ev.currentNodeType = ev.errorNodeType;
        ev.executedNodes = ReadStrArray(data, "executed");
        ev.errorMessage = "任务被中断";
    }

    // 每个事件一行（progress 类节流）；必须在派发前打印，避免监听者耗时影响日志时序
    LogPromptEvent(ev, nodeKind);

    std::vector<PromptListener> copy;
    {
        std::lock_guard lock(mutex_);
        copy = promptListeners_;
    }
    for (auto& fn : copy) {
        fn(ev);
    }
    yyjson_doc_free(doc);
}

std::int64_t ComfySocket::LastFrameMonotonicMs() const noexcept {
    std::lock_guard lock(mutex_);
    return lastFrameMonotonicMs_;
}

std::chrono::milliseconds ComfySocket::SinceLastFrame() const noexcept {
    std::int64_t last = 0;
    {
        std::lock_guard lock(mutex_);
        last = lastFrameMonotonicMs_;
    }
    if (last <= 0) {
        return std::chrono::milliseconds{0};
    }
    const std::int64_t delta = util::MonotonicMillis() - last;
    return std::chrono::milliseconds{delta > 0 ? delta : 0};
}

void ComfySocket::ScheduleReconnect() {
    // EnsureConnected is polled from Session::Tick; just record time.
    std::lock_guard lock(mutex_);
    lastConnectAttemptSec_ = NowSec();
}

} // namespace shine::comfy
