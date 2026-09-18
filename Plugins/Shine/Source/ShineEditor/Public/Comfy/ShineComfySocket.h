#pragma once

#include "Comfy/ShineComfyTypes.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "IWebSocket.h"

/**
 * 全项目共用的那一条 ComfyUI WebSocket（/ws）。
 *
 * ComfyUI 的规矩：提交 prompt 时带上 client_id，它就把这条 prompt 的
 * execution_start / progress / executed / execution_success / execution_error
 * 事件推给"同一个 client_id 的连接"。所以连接必须是唯一的、常开的，
 * 各功能都从这里订阅，而不是各开一条：
 *
 *   - ComfyUI 主面板：订阅 MessageEvent，自己解析队列 / 历史；
 *   - AI 贴图（ShineAIPaint）：订阅 PromptEvent，只关心自己那次任务跑完没有。
 *
 * 提交任务时统一用 GetClientId()，事件才会回到这条连接；
 * 多个消费者按 prompt_id 各自过滤。
 * 断线会自动重连，所以它是"一直连着"的。
 */
class SHINEEDITOR_API FShineComfySocket
{
public:
    /** 一条和某次 prompt 相关的事件（MessageEvent 的解析结果）。 */
    struct FPromptEvent
    {
        /** execution_start / executing / progress / executed / execution_success / execution_error / execution_interrupted。 */
        FString Type;
        FString PromptId;
        FString NodeId;

        int32 ProgressValue = 0;
        int32 ProgressMax = 0;

        /** execution_cached 命中的节点数（ComfyUI 会跳过输入没变的节点）。 */
        int32 CachedNodeCount = 0;

        /**
         * execution_cached 里命中的具体节点 id。
         *
         * 原先只有数量，面板就只能显示"跳过了 5 个节点"这种没法定位的信息；
         * 要显示"哪些节点被跳过"必须拿到 id 列表。
         */
        TArray<FString> CachedNodeIds;

        /**
         * progress_state 事件里的整张节点状态图。
         *
         * ComfyUI 0.3.x+ 会一次推来所有节点的 state 与步进，"每个 Node 的进度"直接
         * 用它即可，不需要自己拿 executing + progress 拼状态机。
         * 只有在当前 ComfyUI 支持该事件时才非空；为空则退回用 NodeId + ProgressValue。
         */
        TArray<FShineComfyNodeProgress> NodeStates;

        /**
         * executed 事件里带回的第一个产出（filename / subfolder / type）。
         *
         * 注意它【只取第一个】，且 SaveVideo 的视频产物与图片同在 "images" 键里 ——
         * 想拿到一次任务完整的产出清单，得用 FShineComfyClient::FetchHistoryForPrompt
         * 回读 history，这里只适合做"有东西出来了"的即时提示。
         */
        FString ImageFileName;
        FString ImageSubfolder;
        FString ImageType;

        bool bFailed = false;
        FString ErrorMessage;

        /**
         * execution_error 里出错的那个节点。
         *
         * 只给 exception_message 是不够的：H3 的 ref_images 键名写错时，报错是
         * "execute() got an unexpected keyword argument"，光看这句话根本不知道
         * 是哪个节点、哪一步炸的。node_id / node_type 才能定位。
         */
        FString ErrorNodeId;
        FString ErrorNodeType;
    };

    /** 进程内唯一实例。 */
    static FShineComfySocket& Get();

    /**
     * 保证连到这台 ComfyUI；换了地址会重连。
     * 连接是异步的，这里只负责发起，用 IsConnected() 查当前状态。
     */
    void EnsureConnected(const FString& BaseUrl);

    bool IsConnected() const { return bConnected; }

    /**
     * 提交任务时用的 client_id。
     * 第一次取时生成，之后固定 —— 必须和 TCP 连接建立时用的那个一致，
     * 否则 ComfyUI 不会把事件推过来。
     */
    const FString& GetClientId();

    // ---------- 事件（都在游戏线程触发） ----------

    /** 连上了。 */
    DECLARE_MULTICAST_DELEGATE(FOnConnected);
    FOnConnected ConnectedEvent;

    /** 连接失败（ComfyUI 没开是常态，不一定是错误）。 */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionError, const FString& /*ErrorMessage*/);
    FOnConnectionError ConnectionErrorEvent;

    /** 连接断了（会自动重连）。 */
    DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnClosed, int32 /*StatusCode*/, const FString& /*Reason*/, bool /*bWasClean*/);
    FOnClosed ClosedEvent;

    /** 原始消息：给需要自己解析的消费者。 */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessage, const FString& /*Message*/);
    FOnMessage MessageEvent;

    /** 解析好的执行事件：给只关心"我这次任务跑完没有"的消费者。 */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnPromptEvent, const FPromptEvent&);
    FOnPromptEvent PromptEvent;

    /** 断开并停止重连（模块卸载时调）。 */
    void Shutdown();

private:
    void SetupSocket();
    void TeardownSocket();
    void HandleRawMessage(const FString& Message);
    void HandleConnected();
    void HandleConnectionError(const FString& ErrorMessage);
    void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void ScheduleReconnect();
    bool ReconnectTick(float DeltaTime);

    void ParseAndBroadcastPromptEvent(const FString& Message);

    FString ServiceUrl;
    FString WebSocketUrl;
    FString ClientId;

    TSharedPtr<IWebSocket> Socket;

    FTSTicker::FDelegateHandle ReconnectHandle;
    bool bConnected = false;
    bool bConnecting = false;
    bool bShutdown = false;

    /** 重连间隔（秒）；ComfyUI 没起来时会一直按这个间隔重试。 */
    static constexpr float ReconnectIntervalSeconds = 2.0f;
};
