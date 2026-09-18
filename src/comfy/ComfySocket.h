#pragma once
#include "comfy/ComfyTypes.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace shine::comfy {

// Singleton ComfyUI /ws connection (libhv WebSocketClient + EventLoopThread).
// All listener callbacks are invoked from the WS thread; Session re-posts to UI.
class ComfySocket {
public:
    using PromptListener = std::function<void(const PromptEvent&)>;
    using StatusListener = std::function<void(const StatusEvent&)>;
    // 回调在 WS 线程同步触发；msg 是视图，需要留存请在回调内自行拷贝
    using StateListener = std::function<void(ConnectionState, std::string_view)>;
    // P4.5：二进制预览帧（帧内数据已是拥有型，可直接搬走）
    using BinaryListener = std::function<void(const BinaryFrame&)>;

    static ComfySocket& Instance();

    void EnsureConnected(std::string_view baseUrl, std::string_view clientId);
    void Shutdown();
    [[nodiscard]] bool IsConnected() const;

    // —— P3.0 S3：帧静默计时（`Doc/RULES-COMFY.md` §12.1）——
    // 记录"最近一次收到任何 WS 帧"的单调时间（**含二进制预览帧**）；连接从未收到帧时返回到连接建立的间隔。
    [[nodiscard]] std::chrono::milliseconds SinceLastFrame() const noexcept;
    [[nodiscard]] std::int64_t LastFrameMonotonicMs() const noexcept;
    // 最近一次收到**二进制预览帧**的时间（0 = 从未收到 → P4.5 走 /view 兜底）
    [[nodiscard]] std::int64_t LastBinaryFrameMs() const noexcept;

    void AddPromptListener(PromptListener fn);
    void AddStatusListener(StatusListener fn);
    void AddStateListener(StateListener fn);
    // P4.5：WS 二进制预览帧（回调在 **WS 线程**执行 —— 只能做线程安全的事，重活要 RunOnWorker）
    void AddBinaryListener(BinaryListener fn);
    void ClearListeners();

private:
    void HandleBinaryFrame(std::string_view msg); // P4.5：二进制预览帧 → BinaryListener

    ComfySocket() = default;
    ~ComfySocket();
    ComfySocket(const ComfySocket&) = delete;
    ComfySocket& operator=(const ComfySocket&) = delete;

    void Setup(std::string_view baseUrl, std::string_view clientId);
    void Teardown();
    void OnOpen();
    void OnClose();
    void OnMessage(std::string_view msg);
    void EmitState(ConnectionState s, std::string_view msg);
    void ScheduleReconnect();

    mutable std::mutex mutex_;
    std::string serviceUrl_;
    std::string wsUrl_;
    std::string clientId_;
    ConnectionState state_ = ConnectionState::Disconnected;
    bool connected_ = false;
    bool connecting_ = false;
    bool shutdown_ = false;
    double lastConnectAttemptSec_ = 0.0;
    int failStreak_ = 0;
    std::int64_t lastFrameMonotonicMs_ = 0;   // 最近一次收到任何帧（S3 帧静默计时）
    std::int64_t lastBinaryFrameMs_ = 0;      // 最近一次收到二进制预览帧（P4.5）
    std::int64_t lastBinaryLogMs_ = 0;        // 预览帧日志降噪（最多 1s 一条）

    std::vector<PromptListener> promptListeners_;
    std::vector<StatusListener> statusListeners_;
    std::vector<StateListener> stateListeners_;
    std::vector<BinaryListener> binaryListeners_;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace shine::comfy
