#pragma once

#include "CoreMinimal.h"
#include "Graph/ShineComfyGraphTypes.h"
#include "ShineComfyTypes.generated.h"

class FJsonObject;

UENUM()
enum class EShineComfyParameterOptionsSource : uint8
{
    None,
    StaticChoices,
    ModelFolder
};

USTRUCT()
struct FShineComfyNodePinDefinition
{
    GENERATED_BODY()

    UPROPERTY()
    FString Name;

    UPROPERTY()
    FString TypeName;

    UPROPERTY()
    FString PinCategory;

    UPROPERTY()
    bool bIsOptional = false;
};

USTRUCT()
struct FShineComfyNodeParameterDefinition
{
    GENERATED_BODY()

    UPROPERTY()
    FName Name;

    UPROPERTY()
    FText Label;

    UPROPERTY()
    EShineComfyParameterType Type = EShineComfyParameterType::Text;

    UPROPERTY()
    FString StringValue;

    UPROPERTY()
    double FloatValue = 0.0;

    UPROPERTY()
    int32 IntValue = 0;

    UPROPERTY()
    bool bBoolValue = false;

    UPROPERTY()
    TArray<FString> StringOptions;

    UPROPERTY()
    EShineComfyParameterOptionsSource OptionsSource = EShineComfyParameterOptionsSource::None;

    UPROPERTY()
    FString OptionsSourceName;
};

USTRUCT()
struct FShineComfyNodeDefinition
{
    GENERATED_BODY()

    UPROPERTY()
    FString NodeClassName;

    UPROPERTY()
    FString DisplayName;

    UPROPERTY()
    FString Category;

    UPROPERTY()
    FString Description;

    UPROPERTY()
    bool bIsApiNode = false;

    UPROPERTY()
    TArray<FShineComfyNodePinDefinition> InputPins;

    UPROPERTY()
    TArray<FShineComfyNodePinDefinition> OutputPins;

    UPROPERTY()
    TArray<FShineComfyNodeParameterDefinition> Parameters;
};

struct FShineComfyModelFolder
{
    FString FolderName;
    TArray<FString> ModelNames;
};

struct FShineComfyModelsResult
{
    bool bSuccess = false;
    bool bFromCache = false;
    FDateTime FetchedAt;
    FString ErrorMessage;
    TArray<FShineComfyModelFolder> Folders;
};

struct FShineComfyNodesResult
{
    bool bSuccess = false;
    bool bFromCache = false;
    FDateTime FetchedAt;
    FString ErrorMessage;
    TArray<FShineComfyNodeDefinition> Nodes;
};

struct FShineComfyFetchOptions
{
    bool bForceRefresh = false;
    bool bAllowStaleCacheOnError = true;
    FTimespan CacheTtl = FTimespan::Zero();
};

enum class EShineComfyQueueTaskState : uint8
{
    Pending,
    Running
};

struct FShineComfyQueueEntry
{
    FString PromptId;
    double Number = 0.0;
    int32 QueueIndex = INDEX_NONE;
    int64 CreatedAtMillis = 0;
    EShineComfyQueueTaskState State = EShineComfyQueueTaskState::Pending;
};

struct FShineComfyQueueResult
{
    bool bSuccess = false;
    FString ErrorMessage;
    int32 QueueRemaining = 0;
    TArray<FShineComfyQueueEntry> Running;
    TArray<FShineComfyQueueEntry> Pending;
};

struct FShineComfyPromptSubmitRequest
{
    TSharedPtr<FJsonObject> Prompt;
    FString ClientId;
    FString PromptId;
    bool bFront = false;
    TOptional<double> Number;
};

struct FShineComfyPromptSubmitResult
{
    bool bSuccess = false;
    FString ErrorMessage;
    FString PromptId;
    double Number = 0.0;
    FString RawNodeErrorsJson;
};

struct FShineComfyQueueOperationResult
{
    bool bSuccess = false;
    FString ErrorMessage;
};

/** /upload/image 与 /upload/mask 的返回。 */
struct FShineComfyUploadResult
{
    bool bSuccess = false;
    FString ErrorMessage;

    /** 上传后在 ComfyUI input 目录里的文件名（可直接写进 LoadImage 节点）。 */
    FString Name;
    FString Subfolder;
    FString Type;
};

struct FShineComfyHistoryImage
{
    FString FileName;
    FString Subfolder;
    FString Type;
};

/**
 * 一次任务产出的媒体种类。
 *
 * 为什么要分类而不是靠键名区分：ComfyUI 的 SaveVideo 走的是 ui.PreviewVideo，
 * 它的序列化结果是
 *     {"images": [...], "animated": [true]}      // comfy_api/latest/_ui.py:432-437
 * 也就是说 —— **视频和图片落在同一个 "images" 键里**，光看键名分不出来，只能靠
 * 扩展名判。而老式的 SaveWEBM / VHS_VideoCombine 用的是 "gifs" 键，PreviewAudio
 * 用 "audio" 键。所以三处都要收，不能只认 "images"。
 */
UENUM()
enum class EShineComfyMediaKind : uint8
{
    Image,
    Video,
    Audio,
    Other
};

USTRUCT()
struct FShineComfyHistoryMedia
{
    GENERATED_BODY()

    /**
     * 产出它的节点 id。
     *
     * 为什么必须留着：history 的 outputs 是按节点 id 分组的，但解析时会拍平成一个列表。
     * 视频工作台一次提交会跑好几个分镜，每个分镜有自己的 SaveVideo 节点——
     * 不留节点 id 就只能靠"产物的先后顺序"猜哪段是哪段，一旦有节点被缓存跳过就会错位。
     */
    UPROPERTY()
    FString NodeId;

    UPROPERTY()
    FString FileName;

    UPROPERTY()
    FString Subfolder;

    UPROPERTY()
    FString Type;

    UPROPERTY()
    EShineComfyMediaKind Kind = EShineComfyMediaKind::Other;

    /** 带 subfolder 的相对路径（ComfyUI 用正斜杠），用于显示与拼本地路径。 */
    FString BuildRelativePath() const
    {
        return Subfolder.IsEmpty() ? FileName : FString::Printf(TEXT("%s/%s"), *Subfolder, *FileName);
    }

    bool IsVideo() const { return Kind == EShineComfyMediaKind::Video; }
    bool IsImage() const { return Kind == EShineComfyMediaKind::Image; }
    bool IsAudio() const { return Kind == EShineComfyMediaKind::Audio; }
};

struct FShineComfyHistoryEntry
{
    FString PromptId;
    FString StatusText;
    FString SummaryText;
    FString RawJson;
    int64 CreatedAtMillis = 0;
    int64 CompletedAtMillis = 0;
    bool bFailed = false;

    /** 图片产出。保留原字段不动，既有调用方（图里的 Preview / Gallery 节点）不受影响。 */
    TArray<FShineComfyHistoryImage> Images;

    /** 规范化后的全部媒体（图片/视频/音频统一在这里），视频工作台用这个。 */
    TArray<FShineComfyHistoryMedia> Media;
};

/** 从 /view 抓一份媒体（图/视频/音频）到本地的结果。 */
struct FShineComfyMediaDownloadResult
{
    bool bSuccess = false;
    FString ErrorMessage;
    FString LocalPath;
    int64 BytesWritten = 0;
};

/** POST /api/free 的结果：卸载模型缓存以释放显存。 */
struct FShineComfyFreeVramResult
{
    bool bSuccess = false;
    FString ErrorMessage;
};

/**
 * /api/system_stats 的结果：显存门禁用它决定"现在能不能提交"。
 *
 * ⚠️ 只有 vram_free 有意义，torch_vram_free 不能拿来判断：
 * ComfyUI 现在带 Dynamic VRAM(AIMDO) 这套自定义分配器，它的占用根本不在 torch 的
 * 分配器账上（实测 torch_vram_free 只有十几 MB，而此时整卡空闲 22GB+）。
 * 反过来，读取值偏低也是**正常现象**——这套分配器的取向就是"更激进地占满显存"，
 * 所以门禁的顺序必须是【先 /api/free 卸载，再轮询这个值回到阈值】。
 */
struct FShineComfySystemStatsResult
{
    bool bSuccess = false;
    FString ErrorMessage;

    FString DeviceName;

    /** 整卡总显存（字节）。 */
    double VramTotalBytes = 0.0;

    /** 整卡当前空闲显存（字节，驱动口径）。 */
    double VramFreeBytes = 0.0;

    double GetVramFreeGb() const { return VramFreeBytes / (1024.0 * 1024.0 * 1024.0); }
    double GetVramTotalGb() const { return VramTotalBytes / (1024.0 * 1024.0 * 1024.0); }
    bool HasDevice() const { return VramTotalBytes > 0.0; }
};

/**
 * 单个节点的执行状态，来自 ComfyUI 的 progress_state 事件。
 *
 * 为什么用 progress_state 而不是自己拼：ComfyUI 0.3.x+ 会直接把**整张图的节点状态**
 * 一次推过来（每个节点带 state 与 value/max），而老的 executing + progress 两个事件
 * 只能拼出"当前是哪个节点、它跑到第几步"，拿不到 pending/skipped/finished 的全貌。
 * 面板要显示"每个 Node 的进度"，前者天然够用。
 *
 * State 的取值是 ComfyUI 的字符串：pending / running / finished / skipped / error。
 */
struct FShineComfyNodeProgress
{
    FString NodeId;
    FString State;
    int32 Value = 0;
    int32 Max = 0;

    bool IsRunning() const { return State == TEXT("running"); }
    bool IsFinished() const { return State == TEXT("finished") || State == TEXT("skipped"); }
    bool IsSkipped() const { return State == TEXT("skipped"); }
    bool IsFailed() const { return State == TEXT("error"); }

    /** 该节点是否带步进（只有采样器这类才有 max > 0）。 */
    bool HasSteps() const { return Max > 0; }
};

struct FShineComfyHistoryResult
{
    bool bSuccess = false;
    FString ErrorMessage;
    TArray<FShineComfyHistoryEntry> Entries;
};
