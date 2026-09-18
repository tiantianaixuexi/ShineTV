#include "comfy/ComfySession.h"

#include "comfy/ComfyHttp.h"
#include "comfy/ComfySocket.h"
#include "core/Async.h"
#include "core/Log.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <algorithm>

namespace shine::comfy {
namespace {

std::string ShortId(std::string_view id) {
    return id.size() > 8 ? std::string{id.substr(0, 8)} : std::string{id};
}

// `Doc/RULES-COMFY.md` §12.5 的多行错误块：异常类型 / 消息 / 回溯前 3 行 / 中文 hint / 来源
void LogErrorDetail(const ErrorDetail& d) {
    log::Error("exec failed prompt={} node={} type={}", ShortId(d.promptId),
               d.nodeId.empty() ? std::string{"-"} : d.nodeId,
               d.nodeType.empty() ? std::string{"?"} : d.nodeType);
    if (!d.exceptionType.empty()) {
        log::Error("  exception_type : {}", d.exceptionType);
    }
    if (!d.exceptionMessage.empty()) {
        log::Error("  exception_msg  : {}", d.exceptionMessage);
    }
    const size_t tbLines = std::min<size_t>(d.traceback.size(), 3);
    for (size_t i = 0; i < tbLines; ++i) {
        log::Error("  traceback     : {}", d.traceback[i]);
    }
    if (!d.hint.empty()) {
        log::Error("  hint          : {}", d.hint);
    }
    if (!d.source.empty()) {
        log::Error("  source        : {}", d.source);
    }
}

// WS `execution_error` → ErrorDetail（§12.3 第 1 条）
ErrorDetail BuildWsErrorDetail(const PromptEvent& ev) {
    ErrorDetail d;
    d.valid = true;
    d.source = "ws";
    d.promptId = ev.promptId;
    d.nodeId = ev.errorNodeId;
    d.nodeType = ev.errorNodeType;
    d.exceptionType = ev.exceptionType;
    d.exceptionMessage = !ev.exceptionMessage.empty() ? ev.exceptionMessage : ev.errorMessage;
    d.traceback = ev.traceback;
    d.hint = ErrorHintFor(ev.exceptionType, d.exceptionMessage);
    return d;
}

} // namespace

ComfySession& ComfySession::Instance() {
    static ComfySession inst;
    return inst;
}

void ComfySession::Init(std::string_view baseUrl) {
    clientId_ = MakeClientId();
    baseUrl_ = NormalizeBaseUrl(baseUrl.empty() ? "http://127.0.0.1:8188" : baseUrl);
    inited_ = true;
    state_ = ConnectionState::Connecting;
    lastError_.clear();
    lastRunningSig_.clear();
    lastRunningSigChangeMs_ = util::MonotonicMillis();
    lastQueueLogMs_ = 0;
    lastQueueLogSig_.clear();
    lastSilenceProbeMs_ = 0;
    lastInterruptMs_ = 0;
    wsSilenceWarned_ = false;
    stalledWarned_ = false;
    historyFallbackRequested_ = false;
    lastErrorDetail_ = ErrorDetail{};
    EnsureLibhvReady(); // 必须在任何 worker / WS 碰 libhv 之前（惰性 logger 并发首次使用会死锁，见 ComfyHttp.h）
    log::Info("ComfySession 初始化 baseUrl={} clientId={}", baseUrl_, clientId_);
    RefreshHealthStrings();
    EnsureSocket();
    Connect();
}

void ComfySession::Shutdown() {
    ComfySocket::Instance().Shutdown();
    ComfySocket::Instance().ClearListeners();
    queue_.Clear();
    inited_ = false;
    listenersBound_ = false;
}

void ComfySession::EnsureSocket() {
    if (listenersBound_) {
        return;
    }
    auto& sock = ComfySocket::Instance();
    sock.AddStateListener([this](ConnectionState s, std::string_view msg) {
        // ⚠️ 回调在 **WS 线程**（state_/healthSummary_ 等只允许 UI 线程改）→ 一律投递回 UI 线程
        const std::string m{msg};
        async::PostToUi([this, s, m]() {
            state_ = s;
            lastError_ = (s == ConnectionState::Connected) ? std::string{} : m;
            if (s == ConnectionState::Connected) {
                log::Info("Comfy 已连接");
                wsSilenceWarned_ = false;
                stalledWarned_ = false;
                historyFallbackRequested_ = false;
                lastRunningSigChangeMs_ = util::MonotonicMillis();
                RefreshQueue();
            }
            RefreshHealthStrings();
        });
    });
    sock.AddPromptListener([this](const PromptEvent& ev) {
        // ⚠️ 本回调在 **WS 线程**执行：队列模型自身线程安全，其余状态一律投递回 UI 线程更新
        queue_.ApplyPromptEvent(ev);
        if (ev.type == "execution_error") {
            const ErrorDetail d = BuildWsErrorDetail(ev);
            LogErrorDetail(d); // 日志线程安全，立即打印保证时序（§12.5）
            async::PostToUi([this, d]() {
                lastErrorDetail_ = d;                 // 覆盖式：同一 promptId 只保留一份
                queue_.AttachErrorDetail(d.promptId, d);
                RefreshHealthStrings();
            });
        } else if (ev.type == "execution_interrupted") {
            // §12.2：已中断 ≠ 失败 —— 只记一行 warn，不写入 LastErrorDetail、不标 Failed
            log::Warn("execution interrupted（已中断 ≠ 失败）prompt={} node={} type={}", ShortId(ev.promptId),
                      ev.errorNodeId.empty() ? std::string{"-"} : ev.errorNodeId,
                      ev.errorNodeType.empty() ? std::string{"-"} : ev.errorNodeType);
        }
    });
    sock.AddStatusListener([this](const StatusEvent& se) { queue_.ApplyStatus(se); });
    listenersBound_ = true;
}

void ComfySession::Connect() {
    if (baseUrl_.empty()) {
        return;
    }
    EnsureSocket();
    state_ = ConnectionState::Connecting;
    ComfySocket::Instance().EnsureConnected(baseUrl_, clientId_);
    PingAsync(baseUrl_, [this](OperationResult r) {
        if (r.ok) {
            lastError_.clear();
            if (!objectInfoLoaded_) {
                RefreshObjectInfo();
            }
        } else {
            const auto c = queue_.CountsSnapshot();
            if (c.busy()) {
                // §12.4：带着任务时探活超时不算故障
                log::Warn("service busy, probe timeout ignored: running={} pending={}", c.running, c.pending);
            } else {
                lastError_ = r.error;
            }
        }
        RefreshHealthStrings();
    });
    RefreshQueue();
}

void ComfySession::SetBaseUrl(std::string_view baseUrl) {
    const std::string normalized = NormalizeBaseUrl(baseUrl);
    if (normalized == baseUrl_) {
        return;
    }
    baseUrl_ = normalized;
    objectInfoLoaded_ = false;
    objectInfoRaw_.clear();
    objectInfoNodeCount_ = 0;
    objectInfoNodes_.clear();
    objectInfoIndex_.clear();
    objectInfoFormat_ = NodeDefFormat::V1;
    queue_.Clear();
    lastErrorDetail_ = ErrorDetail{};
    log::Info("Comfy BaseUrl -> {}", baseUrl_);
    Connect();
}

const std::string& ComfySession::BaseUrl() const { return baseUrl_; }
const std::string& ComfySession::ClientId() const { return clientId_; }
ConnectionState ComfySession::State() const { return state_; }
std::string ComfySession::LastError() const { return lastError_; }
QueueModel& ComfySession::Queue() { return queue_; }
const QueueModel& ComfySession::Queue() const { return queue_; }
int ComfySession::ObjectInfoNodeCount() const { return objectInfoNodeCount_; }
std::string ComfySession::ObjectInfoRaw() const { return objectInfoRaw_; }
const std::vector<NodeTypeDef>& ComfySession::ObjectInfoNodes() const { return objectInfoNodes_; }
NodeDefFormat ComfySession::ObjectInfoFormat() const noexcept { return objectInfoFormat_; }

// 视图索引：只按 className 排序，二分查找 O(log n)
void ComfySession::RebuildObjectInfoIndex() {
    objectInfoIndex_.clear();
    objectInfoIndex_.reserve(objectInfoNodes_.size());
    for (std::size_t i = 0; i < objectInfoNodes_.size(); ++i) {
        objectInfoIndex_.emplace_back(std::string_view{objectInfoNodes_[i].className}, i);
    }
    std::sort(objectInfoIndex_.begin(), objectInfoIndex_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
}

const NodeTypeDef* ComfySession::FindNodeDef(std::string_view className) const noexcept {
    if (className.empty()) {
        return nullptr;
    }
    const auto it = std::lower_bound(
        objectInfoIndex_.begin(), objectInfoIndex_.end(), className,
        [](const std::pair<std::string_view, std::size_t>& entry, std::string_view key) { return entry.first < key; });
    if (it == objectInfoIndex_.end() || it->first != className) {
        return nullptr;
    }
    return &objectInfoNodes_[it->second];
}
std::string_view ComfySession::BusyDetail() const noexcept { return busyDetail_; }
std::string_view ComfySession::HealthSummary() const noexcept { return healthSummary_; }

void ComfySession::RefreshQueue() {
    if (baseUrl_.empty()) {
        return;
    }
    FetchQueueAsync(baseUrl_, [this](QueueResult r) {
        if (!r.ok) {
            lastError_ = r.error;
            return;
        }
        lastError_.clear();
        queue_.ApplyQueueResult(r);
        // S6：本地在跑、服务端队列里却没有了 → 说明漏收了结果事件 → 用 /history 补齐（§12.3 第 2 条）
        for (const std::string& pid : queue_.TakeStaleRunningIds(r)) {
            log::Warn("prompt={} 已不在服务端队列（疑似漏收结果事件）→ 用 /history 补齐", ShortId(pid));
            RequestHistoryOutcome(pid);
        }
    });
}

void ComfySession::RefreshObjectInfo() {
    if (baseUrl_.empty()) {
        return;
    }
    FetchObjectInfoAsync(baseUrl_, [this](ObjectInfoResult r) {
        if (!r.ok) {
            lastError_ = r.error;
            log::Warn("object_info 拉取失败: {}", r.error);
            return;
        }
        objectInfoLoaded_ = true;
        objectInfoNodeCount_ = r.nodeClassCount;
        objectInfoRaw_ = r.rawJson;
        objectInfoNodes_ = std::move(r.nodes); // P3.1：结构化定义（v2 优先 / v1 兼容）
        objectInfoFormat_ = r.format;
        RebuildObjectInfoIndex();
        std::size_t withInputs = 0;
        std::size_t inputCount = 0;
        for (const NodeTypeDef& nd : objectInfoNodes_) {
            if (!nd.inputs.empty()) {
                ++withInputs;
            }
            inputCount += nd.inputs.size();
        }
        log::Info("object_info cached: {} 个节点类可查询（format {}，{} 个含输入，共 {} 个输入）",
                  objectInfoNodes_.size(), NodeDefFormatLabel(objectInfoFormat_), withInputs, inputCount);
    });
}

void ComfySession::FetchHistory(int maxItems, HistoryCb cb) {
    FetchHistoryAsync(baseUrl_, maxItems, std::move(cb));
}

void ComfySession::SubmitPromptJson(std::string_view promptGraphJson, PromptCb cb) {
    PromptSubmitRequest req;
    req.promptJson = std::string{promptGraphJson};
    req.clientId = clientId_;
    SubmitPromptAsync(baseUrl_, req, [this, cb = std::move(cb)](PromptSubmitResult r) mutable {
        if (r.ok) {
            log::Info("已提交 prompt {}", ShortId(r.promptId));
            historyFallbackRequested_ = false;
            RefreshQueue();
        } else {
            // S7：/prompt 400 的 error + node_errors → 节点级中文错误（§12.3 第 3 条）
            ErrorDetail d;
            d.valid = true;
            d.source = "prompt400";
            d.promptId = r.promptId;
            d.exceptionType = "prompt_rejected";
            d.exceptionMessage = r.error.empty() ? "提交被拒绝" : r.error;
            if (!r.nodeErrors.empty()) {
                const NodeError& ne = r.nodeErrors.front();
                d.nodeId = ne.nodeId;
                d.nodeType = ne.nodeType;
                d.exceptionMessage = ne.message;
                d.hint = ne.hint;
            } else {
                d.hint = ErrorHintFor("", d.exceptionMessage);
            }
            log::Error("提交失败: {}", r.error.empty() ? "校验未通过" : r.error);
            for (const NodeError& ne : r.nodeErrors) {
                log::Error("  submit rejected: node {} {} {}", ne.nodeId.empty() ? "-" : ne.nodeId,
                           ne.nodeType.empty() ? "?" : ne.nodeType, ne.message);
            }
            LogErrorDetail(d);
            lastErrorDetail_ = d;
            RefreshHealthStrings();
        }
        if (cb) {
            cb(std::move(r));
        }
    });
}

void ComfySession::Interrupt(OpCb cb) {
    InterruptAsync(baseUrl_, [cb = std::move(cb)](OperationResult r) mutable {
        if (r.ok) {
            log::Info("已发送 /interrupt");
        } else {
            log::Warn("interrupt 失败: {}", r.error);
        }
        if (cb) {
            cb(std::move(r));
        }
    });
}

void ComfySession::RequestInterruptCurrent() {
    lastInterruptMs_ = util::MonotonicMillis();
    log::Info("请求中断当前任务（/interrupt）");
    Interrupt([](OperationResult r) {
        if (!r.ok) {
            log::Warn("interrupt 失败: {}", r.error);
        }
    });
}

void ComfySession::FreeVram(OpCb cb) {
    FreeVramAsync(baseUrl_, std::move(cb));
}

void ComfySession::FetchSystemStats(StatsCb cb) {
    FetchSystemStatsAsync(baseUrl_, std::move(cb));
}

void ComfySession::RequestHistoryOutcome(std::string_view promptId) {
    if (promptId.empty() || baseUrl_.empty()) {
        return;
    }
    const std::string id{promptId};
    FetchHistoryOutcomeAsync(baseUrl_, id, [this, id](HistoryOutcome o) {
        if (!o.found) {
            log::Warn("history 未找到 {}（可能仍在运行或已被清理）", ShortId(id));
            return;
        }
        if (o.failed) {
            ErrorDetail d = o.error;
            if (!d.valid) {
                d.valid = true;
                d.source = "history";
                d.promptId = id;
                d.exceptionType = "history_error";
                d.exceptionMessage = "历史显示失败，但没有可用的错误详情";
                d.hint = ErrorHintFor("", d.exceptionMessage);
            }
            lastErrorDetail_ = d;              // 覆盖式：同一 promptId 只保留一份
            queue_.AttachErrorDetail(id, d);
            LogErrorDetail(d);
        } else {
            // WS 漏收了 execution_success → 本地收尾，避免任务永远显示"运行中"
            queue_.MarkDone(id, "完成（历史补齐）");
            log::Info("history 补齐: prompt={} 已完成（WS 漏收 execution_success）", ShortId(id));
        }
        RefreshHealthStrings();
    });
}

void ComfySession::LogQueueLine() {
    const auto c = queue_.CountsSnapshot();
    const std::string sig = fmt::format("running={} pending={}", c.running, c.pending);
    const std::int64_t nowMs = util::MonotonicMillis();
    // §12.5：`queue:` 行每 5s 一条，**仅在数值变化时**打印
    if (sig != lastQueueLogSig_ && nowMs - lastQueueLogMs_ >= 5000) {
        lastQueueLogMs_ = nowMs;
        lastQueueLogSig_ = sig;
        log::Info("queue: {}", sig);
    }
}

void ComfySession::CheckWsSilence() {
    if (state_ != ConnectionState::Connected) {
        return;
    }
    const auto silent = ComfySocket::Instance().SinceLastFrame();
    const auto counts = queue_.CountsSnapshot();

    if (silent < std::chrono::milliseconds{30000}) {
        wsSilenceWarned_ = false;
        stalledWarned_ = false;
        return;
    }

    if (counts.busy()) {
        // §12.4：忙碌时的静默**不算断线**；90s 且队列未变化才提示"疑似卡住（仍在队列中）"
        if (silent >= std::chrono::milliseconds{90000} && !stalledWarned_) {
            stalledWarned_ = true;
            log::Warn("ws silent {}s while busy (running={} pending={}) → 疑似卡住（仍在队列中）",
                      silent.count() / 1000, counts.running, counts.pending);
            if (!historyFallbackRequested_) {
                historyFallbackRequested_ = true;
                RequestHistoryOutcome(queue_.ActivePromptId()); // WS 漏收时用 /history 补齐（§12.3 第 2 条）
            }
        }
        return;
    }

    // 空闲静默：先用 HTTP 确认服务器是否真的没了（避免把"健康的静默连接"反复重连 —— §12.1 的实现细化：
    // 30s 无帧只触发一次探活，探活也失败才判定断开；杀进程这类场景由 libhv 的 onclose 立即捕获）
    const std::int64_t nowMs = util::MonotonicMillis();
    if (nowMs - lastSilenceProbeMs_ < 20000) {
        return;
    }
    lastSilenceProbeMs_ = nowMs;
    PingAsync(baseUrl_, [this, silent](OperationResult r) {
        if (!r.ok) {
            log::Warn("ws silent {}s 且 HTTP 探活失败（{}）→ 判定连接已断，转重连", silent.count() / 1000, r.error);
            lastError_ = "WS 静默超时且 HTTP 探活失败";
            state_ = ConnectionState::Connecting;
            ComfySocket::Instance().EnsureConnected(baseUrl_, clientId_);
        } else if (!wsSilenceWarned_) {
            wsSilenceWarned_ = true;
            log::Info("ws silent {}s but http alive → 保持连接（空闲静默属正常）", silent.count() / 1000);
        }
        RefreshHealthStrings();
    });
}

void ComfySession::RefreshHealthStrings() {
    const auto c = queue_.CountsSnapshot();
    QueueModel::ActiveView av;
    const bool hasActive = queue_.ActiveViewSnapshot(av);

    switch (state_) {
    case ConnectionState::Connected:
        break;
    case ConnectionState::Connecting:
        healthSummary_ = lastError_.empty() ? "连接中" : fmt::format("连接中（{}）", lastError_);
        break;
    case ConnectionState::Error:
        healthSummary_ = fmt::format("连接错误（{}）", lastError_.empty() ? "原因未知" : lastError_);
        break;
    default:
        healthSummary_ = lastError_.empty() ? "未连接" : fmt::format("未连接（{}）", lastError_);
        break;
    }

    if (state_ == ConnectionState::Connected) {
        switch (Busy()) {
        case BusyState::Running:
            healthSummary_ = av.nodeId.empty()
                                 ? "已连接 · 执行中"
                                 : fmt::format("已连接 · 执行中 · 节点 {}{}", av.nodeId,
                                               av.nodeType.empty() ? std::string{} : (" " + av.nodeType));
            break;
        case BusyState::Stalled:
            healthSummary_ = "已连接 · 疑似卡住（仍在队列中）";
            break;
        case BusyState::Queued:
            healthSummary_ = fmt::format("已连接 · 排队中（前面 {} 个）", c.pending);
            break;
        case BusyState::Interrupted:
            healthSummary_ = "已连接 · 已中断";
            break;
        default:
            healthSummary_ = "已连接 · 空闲";
            break;
        }
    }

    if (!hasActive) {
        busyDetail_ = (state_ == ConnectionState::Connected) ? "空闲" : healthSummary_;
        LogBusyTransition(Busy());
        return;
    }
    std::string s = av.nodeType.empty() ? std::string{"正在执行"} : fmt::format("正在执行 {}", av.nodeType);
    if (!av.nodeId.empty()) {
        s += fmt::format("（节点 {}）", av.nodeId);   // execution_start 阶段还不知道节点，就不写"（节点 ?）"
    }
    if (av.max > 0) {
        s += fmt::format("，进度 {}/{}", av.value, av.max);
    }
    const int rem = queue_.QueueRemaining();
    if (rem > 0) {
        s += fmt::format("，队列剩余 {}", rem);
    }
    busyDetail_ = std::move(s);
    LogBusyTransition(Busy());
}

// 忙碌状态类别变化时打一行日志（诊断 + 验收都需要"能看出现在在干什么"，且不会每帧刷屏）
void ComfySession::LogBusyTransition(BusyState b) {
    if (b == lastLoggedBusy_) {
        return;
    }
    lastLoggedBusy_ = b;
    log::Info("busy state -> {}（{}）", BusyStateLabel(b), busyDetail_);
}

BusyState ComfySession::Busy() const {
    const auto c = queue_.CountsSnapshot();
    if (c.running > 0) {
        // §12.4：Stalled 需同时满足「无任何 WS 帧 90s」+「queue_running 内容未变」
        const bool silent = ComfySocket::Instance().SinceLastFrame() > std::chrono::milliseconds{90000};
        if (silent && queue_.RunningSignature() == lastRunningSig_) {
            return BusyState::Stalled;
        }
        return BusyState::Running;
    }
    if (c.pending > 0) {
        return BusyState::Queued;
    }
    if (lastInterruptMs_ > 0 && util::MonotonicMillis() - lastInterruptMs_ < 15000) {
        return BusyState::Interrupted;
    }
    return BusyState::Idle;
}

std::chrono::milliseconds ComfySession::SinceLastEvent() const {
    std::int64_t ms = ComfySocket::Instance().SinceLastFrame().count();
    if (lastRunningSigChangeMs_ > 0) {
        const std::int64_t q = util::MonotonicMillis() - lastRunningSigChangeMs_;
        if (q >= 0 && q < ms) {
            ms = q;
        }
    }
    return std::chrono::milliseconds{ms};
}

void ComfySession::PollHealth(float dtSec) {
    healthTimer_ += dtSec;
    queueTimer_ += dtSec;
    queueLogTimer_ += dtSec;

    if (queueTimer_ >= 2.0f) {
        queueTimer_ = 0.f;
        RefreshQueue();
        ComfySocket::Instance().EnsureConnected(baseUrl_, clientId_);
    }
    if (queueLogTimer_ >= 5.0f) {
        queueLogTimer_ = 0.f;
        LogQueueLine();
    }
    if (healthTimer_ >= 5.0f) {
        healthTimer_ = 0.f;
        // 只在"未连接"时探活；已连接时不做 HTTP 打扰（§12.4：健康探活不参与卡死判定）
        if (state_ != ConnectionState::Connected) {
            PingAsync(baseUrl_, [this](OperationResult r) {
                if (r.ok) {
                    lastError_.clear();
                    if (state_ == ConnectionState::Error || state_ == ConnectionState::Disconnected) {
                        state_ = ConnectionState::Connecting;
                    }
                } else {
                    const auto c = queue_.CountsSnapshot();
                    if (c.busy()) {
                        // §12.4：忙碌时探活超时 → 只打 warn，**连接状态保持不变**
                        log::Warn("service busy, probe timeout ignored: running={} pending={}, queue_remaining={}",
                                  c.running, c.pending, queue_.QueueRemaining());
                    } else {
                        lastError_ = r.error;
                        state_ = ConnectionState::Error;
                    }
                }
                RefreshHealthStrings();
            });
        }
        if (!objectInfoLoaded_ && !baseUrl_.empty()) {
            RefreshObjectInfo();
        }
    }
}

void ComfySession::Tick(float dtSec) {
    if (!inited_) {
        return;
    }
    PollHealth(dtSec);

    // running 集合变化 = "队列变化"，用于"队列未变化"判定与 SinceLastEvent
    const std::string sig = queue_.RunningSignature();
    if (sig != lastRunningSig_) {
        lastRunningSig_ = sig;
        lastRunningSigChangeMs_ = util::MonotonicMillis();
    }

    CheckWsSilence();
    RefreshHealthStrings();
}

} // namespace shine::comfy
