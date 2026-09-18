#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * 一次「参考图 → SD1.5 出图」的全部输入。
 *
 * 字段与 `Resources/DirectorNode.json` 里那支图片路线（`EnableImageGen` 分支）逐项对应，
 * 默认值也照抄那里——不是为了好看，而是为了让"builder 产出的图"能和
 * `UShineComfyGraph::ExportComfyPromptToJson()` 导出的图**逐字段对拍**（见下）。
 *
 * 图名必须是**已经存在于 ComfyUI input 目录**的文件名（`LoadImage` 的 `image` 字段），
 * builder 自己不读盘、不上传——上传归 `FShineImageTaskRunner`。
 */
struct FShineSceneToImageRequest
{
    /** 正向提示词。进 CLIPTextEncode 的 text。 */
    FString PositivePrompt;

    /** 反向提示词。SD1.5 是真的吃这个的（与 H3 那个"只作创作记录"的字段不同）。 */
    FString NegativePrompt =
        TEXT("blurry, lowres, watermark, text, signature, deformed geometry, extra limbs, jpeg artifacts, oversaturated");

    /** 颜色参考图（img2img 的底图）。必填。 */
    FString ColorImageName;

    /**
     * 深度 / 法线图（各驱动一支 ControlNet）。
     *
     * **留空就是不接 ControlNet**：两支 `ControlNetLoader` + 两支 `ControlNetApplyAdvanced`
     * 整体不会出现，conditioning 直接从 CLIPTextEncode 进采样器。不做"接一支空图"的兜底
     * ——空文件名的 LoadImage 在 ComfyUI 里是执行期报错，比少接一支更难排查。
     */
    FString DepthImageName;
    FString NormalImageName;

    FString DepthControlNet = TEXT("control_v11f1p_sd15_depth.pth");
    FString NormalControlNet = TEXT("control_v11p_sd15_normalbae.pth");

    FString Checkpoint = TEXT("v1-5-pruned-emaonly.safetensors");

    int32 Width = 768;
    int32 Height = 768;

    int32 Steps = 30;
    double Cfg = 7.5;
    int32 Seed = 12345;
    double Denoise = 0.78;

    double DepthStrength = 0.7;
    double NormalStrength = 0.38;

    /** ControlNet 的结束步占比（`end_percent`），两支共用。 */
    double ControlEndPercent = 0.85;

    FString SamplerName = TEXT("dpmpp_2m");
    FString Scheduler = TEXT("karras");

    /** SaveImage 的 filename_prefix。 */
    FString OutputPrefix = TEXT("Shine/SceneToImage");

    bool HasDepthControlNet() const { return !DepthImageName.TrimStartAndEnd().IsEmpty(); }
    bool HasNormalControlNet() const { return !NormalImageName.TrimStartAndEnd().IsEmpty(); }
};

/**
 * 把 `FShineSceneToImageRequest` 编译成 ComfyUI 的 API prompt JSON。
 *
 * ## 为什么不复用 DirectorNode.json 那套 JSON expansion
 *
 * 那套模板的前提是"节点集合固定"，而它只能通过"先有一颗 Director 节点资产、再导出"来用：
 * 画布上的分镜图节点要出图时，手上并没有 `UShineComfyGraph`，为了出一张图去新建一个资产
 * 纯属绕路。所以这里像 `FShineMiniMaxH3WorkflowBuilder` 一样纯 C++ 直出。
 *
 * ## 验收基准
 *
 * 同一套参数下，本 builder 产出的图必须与 `UShineComfyGraph::ExportComfyPromptToJson()`
 * （一颗 `EnableImageGen=true` 的 Director 节点）导出的图**逐字段一致**（节点 id 与
 * `filename_prefix` 除外）。对拍用 `Scripts/H3/DiffProbeGraph.py`，它已经为"忽略
 * ControlNet 支"做了通道（`--ignore-class`），所以"不接 ControlNet"那一种形态也能对拍。
 *
 * 这条约束是**刻意**的：DirectorNode.json 那支图片路线是在用户机器上真实跑通过的，
 * 而 ComfyUI 对未知输入键是静默忽略的，"提交成功"证明不了接线正确。
 *
 * ## 边界
 *
 * 纯函数：不读文件、不联网、不查模型库。图名是喂进来的。
 */
class SHINEEDITOR_API FShineSceneToImageWorkflowBuilder
{
public:
    struct FBuildResult
    {
        bool bSuccess = false;
        FString ErrorMessage;

        /** 可直接 POST 给 /prompt 的图（不含 client_id；那是提交时的事）。 */
        TSharedPtr<FJsonObject> Prompt;

        /** 节点 id -> class_type，用来把 progress_state 里的裸 id 翻译成人话。 */
        TMap<FString, FString> NodeClassTypes;

        /** 采样器节点 id（KSampler）：出图的步进进度只看它。 */
        FString SamplerNodeId;

        /**
         * 落盘节点 id（SaveImage）。
         *
         * 回读产物**只认这个节点**：DirectorNode.json 那支路线同时接了 SaveImage 与
         * PreviewImage，后者会把同一张图再写一份到 temp 目录，history 里因此有两条记录。
         */
        FString SaveImageNodeId;

        /** 非致命问题（宽高被对齐、没有 ControlNet…）。有值时应当提示用户，但图仍然可用。 */
        TArray<FString> Warnings;

        /** 序列化成单行 JSON 字符串，便于存盘/对比/直接提交。 */
        FString ToJsonString() const;
    };

    static FBuildResult Build(const FShineSceneToImageRequest& Request);

    /**
     * SD 系宽高的硬约束是 8 的倍数，而 SD1.5 的常识值是 **64**（分辨率桶）。
     * 不对齐会出"图能出但构图糊成一团"这种不报错的坏结果，所以这里显式对齐并提示。
     */
    static constexpr int32 SizeMultiple = 64;

    /** 把数值向上对齐到 Multiple 的倍数（Multiple <= 0 或 Value <= 0 时原样返回）。 */
    static int32 AlignToMultiple(int32 Value, int32 Multiple = SizeMultiple);
};
