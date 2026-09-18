#pragma once
#include "comfy/ComfyClient.h"
#include "comfy/ComfyNodeDef.h"
#include "comfy/ComfyQueueModel.h"
#include "comfy/ComfyTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::comfy {

// Process-wide session: base URL, client_id, WS + HTTP + local queue model.
class ComfySession {
public:
    static ComfySession& Instance();

    void Init(std::string_view baseUrl);
    void Shutdown();
    void Tick(float dtSec);

    void SetBaseUrl(std::string_view baseUrl);
    // 注：BaseUrl()/ClientId() 保持 const std::string& —— 下游是 ImGui 的 "%s"/.c_str()，需要 NUL 结尾（Doc/RULES-LANG.md §13.5 例外 5）
    [[nodiscard]] const std::string& BaseUrl() const;
    [[nodiscard]] const std::string& ClientId() const;
    [[nodiscard]] ConnectionState State() const;
    [[nodiscard]] std::string LastError() const;

    [[nodiscard]] QueueModel& Queue();
    [[nodiscard]] const QueueModel& Queue() const;

    void Connect(); // force reconnect + refresh
    void RefreshQueue();
    void RefreshObjectInfo();
    void FetchHistory(int maxItems, HistoryCb cb);
    void SubmitPromptJson(std::string_view promptGraphJson, PromptCb cb);
    void Interrupt(OpCb cb);
    void FreeVram(OpCb cb);
    void FetchSystemStats(StatsCb cb);

    [[nodiscard]] int ObjectInfoNodeCount() const;
    [[nodiscard]] std::string ObjectInfoRaw() const;

    // —— P3.1：节点定义缓存与查询（结构化模型见 comfy/ComfyNodeDef.h）——
    [[nodiscard]] const std::vector<NodeTypeDef>& ObjectInfoNodes() const;
    [[nodiscard]] const NodeTypeDef* FindNodeDef(std::string_view className) const noexcept; // 找不到返回 nullptr
    [[nodiscard]] NodeDefFormat ObjectInfoFormat() const noexcept;  // 本机给的是 V1 还是 V2（日志/诊断用）

    // —— P3.0 S4：忙碌状态与健康摘要（判定规则见 `Doc/RULES-COMFY.md` §12.4）——
    // 注：内部要锁 QueueModel / ComfySocket，锁可能抛 std::system_error，故**不加 noexcept**
    //（与 `core/Log.h` 的快照函数同一理由）。
    [[nodiscard]] BusyState Busy() const;
    [[nodiscard]] std::string_view BusyDetail() const noexcept;    // 指向内部缓存，帧内有效
    [[nodiscard]] std::string_view HealthSummary() const noexcept; // 指向内部缓存，帧内有效
    [[nodiscard]] const ErrorDetail& LastErrorDetail() const noexcept { return lastErrorDetail_; }
    [[nodiscard]] std::chrono::milliseconds SinceLastEvent() const;

    // 中断当前任务：内部 /interrupt + 等 WS `execution_interrupted`（已中断 ≠ 失败）
    void RequestInterruptCurrent();

    // —— P3.0 S6：用 `/history/{prompt_id}` 的 `status` 补齐结论（WS 漏收/静默时调用），覆盖式更新 ——
    // 失败 → 写错误详情（status.messages）；成功 → 把本地仍在 Running 的行收尾为"完成（历史补齐）"
    void RequestHistoryOutcome(std::string_view promptId);

private:
    ComfySession() = default;
    void EnsureSocket();
    void PollHealth(float dtSec);
    void CheckWsSilence();
    void LogQueueLine();
    void RefreshHealthStrings();
    void LogBusyTransition(BusyState b);
    void RebuildObjectInfoIndex();

    std::string baseUrl_;
    std::string clientId_;
    std::string lastError_;
    ConnectionState state_ = ConnectionState::Disconnected;
    QueueModel queue_;
    float healthTimer_ = 0.f;
    float queueTimer_ = 0.f;
    float queueLogTimer_ = 0.f;
    bool objectInfoLoaded_ = false;
    int objectInfoNodeCount_ = 0;
    std::string objectInfoRaw_;
    // P3.1：结构化节点定义 + 按类名排序的索引（视图指向 objectInfoNodes_ 内的 className，
    // 二者同生共死 —— 只在 RefreshObjectInfo / SetBaseUrl 里一起重建，禁止单独改其中一个）
    std::vector<NodeTypeDef> objectInfoNodes_;
    std::vector<std::pair<std::string_view, std::size_t>> objectInfoIndex_;
    NodeDefFormat objectInfoFormat_ = NodeDefFormat::V1;
    bool inited_ = false;
    bool listenersBound_ = false;

    // S4 健康/忙碌
    std::string busyDetail_;
    std::string healthSummary_;
    ErrorDetail lastErrorDetail_;
    std::int64_t lastRunningSigChangeMs_ = 0;
    std::string lastRunningSig_;
    std::int64_t lastQueueLogMs_ = 0;
    std::string lastQueueLogSig_;
    std::int64_t lastSilenceProbeMs_ = 0;
    std::int64_t lastInterruptMs_ = 0;
    bool wsSilenceWarned_ = false;
    bool stalledWarned_ = false;
    bool historyFallbackRequested_ = false;
    BusyState lastLoggedBusy_ = BusyState::Idle;
};

} // namespace shine::comfy

