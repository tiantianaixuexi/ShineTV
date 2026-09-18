#pragma once

#include "CoreMinimal.h"
#include "Asset/ShineVideoTypes.h"

class UShineVideoProject;
class FJsonObject;

/**
 * 把 UShineVideoProject 编译成 ComfyUI 的 API prompt JSON。
 *
 * ## 为什么不用 ShineComfyDirectorConfig 那套 JSON expansion
 *
 * `DirectorNode.json` + `FShineComfyDirectorConfig` 支持的是 `{$link: [...]}` 与
 * `{$paramTrue: "X"}` 这类模板展开，前提是**节点集合固定**。而 H3 有两个它表达不了的结构：
 *
 *   1. **变长参考列表**：ref2va 的参考图是 `ref_images.ref_image_0..8`，数量由分镜决定；
 *   2. **跨分镜依赖**：链式要把"上一段 VAEDecode 的末帧"接到下一段的参考图或首帧上，
 *      这是节点之间按分镜数量动态增长的连线，不是静态模板。
 *
 * 硬套会把这些复杂度塞进 JSON，把 `DirectorNode.json` 那套搞脏且难调试。所以这里纯 C++ 直出。
 *
 * ## 输出的图与 P0 实测基线严格一致
 *
 * 生成结果必须与 `Scripts/H3/probe_ref2va.json`（单段）和 `probe_chain2.json`（两段链式）
 * 逐字段一致——那两张图是**在本机真实跑出过 mp4 的**，builder 的验收基准就是它们。
 *
 * ## 边界：builder 不做 IO
 *
 * 它不读文件、不解析 `@char:`、不查素材库。喂进来的 `FShineVideoShot::ReferenceImages`
 * 必须是**已经解析好的**素材标识（解析归 P3 的 FShineMentionResolver）。
 * 这样 builder 是纯函数，好用 P0 的探针图做逐字段对拍。
 */
class SHINEEDITOR_API FShineMiniMaxH3WorkflowBuilder
{
public:
    /** 单个分镜在生成图里对应的节点，供面板做「每个 Node 的进度」定位。 */
    struct FShotPlan
    {
        /** 分镜下标。 */
        int32 ShotIndex = INDEX_NONE;

        /** 该分镜的 conditioning 节点 id（MiniMaxH3ReferenceToVideo 或 MiniMaxH3ImageToVideo）。 */
        FString ConditioningNodeId;

        /** 采样器节点 id（SamplerCustomAdvanced）。 */
        FString SamplerNodeId;

        /** 落盘节点 id（SaveVideo）。 */
        FString SaveVideoNodeId;

        /** 该分镜的参考图节点 id，**顺序即 `<Picture 1..N>` 的编号顺序**。 */
        TArray<FString> ReferenceImageNodeIds;

        /**
         * 该分镜的首帧节点 id（只有 FirstLastFrame 模式才非空）。
         *
         * Reference 模式没有这个输入（ref2va 节点只有 ref_images），所以这里是空的；
         * 面板据此定位"这张图是首帧还是参考图"，别把它混进 ReferenceImageNodeIds 里。
         * 注意它也可能是 `ImageFromBatch`（链式时首帧取的是上一段的末帧），不一定是 LoadImage。
         */
        FString FirstFrameNodeId;

        /** 是否接到了上一段的末帧。 */
        bool bChainedFromPrevious = false;
    };

    struct FBuildResult
    {
        bool bSuccess = false;
        FString ErrorMessage;

        /** 可直接 POST 给 /prompt 的图（不含 client_id；那是提交时的事）。 */
        TSharedPtr<FJsonObject> Prompt;

        /** 节点 id -> class_type。面板拿它把 progress_state 里的裸 id 翻译成人话。 */
        TMap<FString, FString> NodeClassTypes;

        /** 每个分镜的节点归属。 */
        TArray<FShotPlan> ShotPlans;

        /** 非致命问题（例如帧数被自动对齐）。有值时应当提示用户，但图仍然可用。 */
        TArray<FString> Warnings;

        /** 序列化成单行 JSON 字符串，便于存盘/对比/直接提交。 */
        FString ToJsonString() const;
    };

    /** 编译整个项目（所有可提交的分镜串成一张图）。 */
    static FBuildResult Build(const UShineVideoProject& Project);

    /**
     * 把帧数对齐到 H3 的 17k+5 网格。
     *
     * H3 的 `align_frame_count` 会把 length 向上对齐到 `n % 17 == 5`
     * （comfy_extras/nodes_minimax_h3.py:33-36），124 = 5.17 秒 @24fps。
     * 不对齐的话实际生成的帧数和你填的不一样，而且 `ImageFromBatch` 取的末帧下标会错。
     */
    static int32 AlignFrameCount(int32 RequestedLength);

    /** 帧数是否已经在 17k+5 网格上。 */
    static bool IsFrameCountOnGrid(int32 Frames);

    /** 把数值向上对齐到 Multiple 的倍数（H3 的宽高必须是 32 的倍数）。 */
    static int32 AlignToMultiple(int32 Value, int32 Multiple);

    /** H3 的参考图上限（`ref_images.ref_image_*` 的 max=9）。 */
    static constexpr int32 MaxReferenceImages = 9;

    /** H3 原生帧率，CreateVideo 必须用它（默认 30 是错的）。 */
    static constexpr double VideoFps = 24.0;
};
