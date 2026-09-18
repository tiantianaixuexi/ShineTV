#pragma once

#include "CoreMinimal.h"
#include "Asset/ShineVideoTypes.h"
#include "UObject/Object.h"
#include "ShineVideoProject.generated.h"

class UShineVideoGraph;

/**
 * 视频工作台的项目资产。
 *
 * 一个项目 = 一批分镜 + 一套模型/输出配置。它只描述"要生成什么"，
 * 不持有任何 ComfyUI 连接或运行期状态——那些归 FShineVideoTaskRunner（P3）管。
 *
 * 与 UShineComfyAsset 的关系：那个是"节点图编辑器"的资产（用户手搭图），
 * 这个是"视频工作台"的资产（用户填分镜表）。两者互不依赖。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Shine 视频项目"))
class SHINEEDITOR_API UShineVideoProject : public UObject
{
    GENERATED_BODY()

public:
    UShineVideoProject();

    virtual void PostLoad() override;

    /**
     * 取出创作期的画布，没有就建一张。
     *
     * 画布是**权威的创作入口**：剧本 / 角色 / 素材 / 分镜 / 分镜图 / 视频组六类节点连好之后，
     * 编译一遍写进下面的 `Shots`，`Shots` 才是提交时唯一被读的东西。所以即时不用画布、
     * 只手工填 `Shots`，资产也照常能跑（老项目不会被这一步破坏）。
     */
    UShineVideoGraph* GetOrCreateGraph();

    /**
     * 自检并修回明显非法的取值：空地址、空产物前缀、越界的参考图数量。
     * 交给任务执行器在提交前调一次——它会先把运行期字段清干净，避免上一次的产物被
     * 误当成这一次的结果。
     *
     * @return true 表示有改动（调用方可以据此提示用户）。
     */
    bool Sanitize();

    /** ComfyUI 地址。留默认即本机。 */
    UPROPERTY(EditAnywhere, Category = "ComfyUI")
    FString ComfyBaseUrl = TEXT("http://127.0.0.1:8188");

    /** 项目名（面板标题用）。 */
    UPROPERTY(EditAnywhere, Category = "项目")
    FString ProjectName;

    /**
     * 全片共用的负面提示词。
     *
     * ⚠️ 注意 H3 的负向【不是】用这个字符串直接编码的：P0 实测确认负向必须用
     * `ConditioningZeroOut` 作用在 H3 自家的正向 conditioning 上（否则缺
     * minimax_frame_count / minimax_refs / minimax_token_tags 这些扩展键，结构不一致，
     * 还要多跑一次 32B 文本编码器）。见 H3-SPEC.md 坑 4。
     *
     * 所以**视频**那条线上这个字段只作为创作记录保存。等 P6 实测完"带文本的负向是否更好"，
     * 再决定要不要让 builder 真的用它构造第二条 H3 conditioning。
     *
     * 但**分镜图**那条线（SD1.5）是真的吃它的：留空就用 builder 里的 SD1.5 默认负向。
     */
    UPROPERTY(EditAnywhere, Category = "项目", meta = (MultiLine = "true"))
    FString NegativePrompt;

    /**
     * 创作期的节点画布（剧本 / 角色 / 素材 / 分镜 / 分镜图 / 视频组 + 连线）。
     *
     * `Instanced`：它连着它的节点一起存在这个资产里，不需要单独一份资产文件。
     */
    UPROPERTY(VisibleAnywhere, Instanced, Category = "图")
    TObjectPtr<UShineVideoGraph> Graph;

    /** 分镜列表，按顺序生成；`bChainFromPrevious` 的分镜会接上一段的末帧。 */
    UPROPERTY(EditAnywhere, Category = "分镜")
    TArray<FShineVideoShot> Shots;

    /**
     * 项目用到的角色资产路径表（供 `@char:<名字>` 解析）。
     * P3 的 FShineMentionResolver 按 DisplayName 或资产名匹配。
     */
    UPROPERTY(EditAnywhere, Category = "分镜")
    TArray<FString> CharacterAssetPaths;

    // ---------------------------------------------------------------- 模型（文件名已由 P0 实测确认可用）

    /** 扩散模型。ref2va 走参考图，fl2va 走首尾帧（放在 unet/ 目录下）。 */
    UPROPERTY(EditAnywhere, Category = "模型")
    FString UnetName = TEXT("minimax_h3_ref2va_pruned_int8_convrot.safetensors");

    /** 文本编码器。CLIPLoader 的 type 必须 = "minimax"，别改。 */
    UPROPERTY(EditAnywhere, Category = "模型")
    FString ClipName = TEXT("qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors");

    /** 视频 VAE（VAEDecode 用它取 nested latent 的视频流）。 */
    UPROPERTY(EditAnywhere, Category = "模型")
    FString VideoVaeName = TEXT("minimax_h3_video_vae_fp16.safetensors");

    /** 音频 VAE（VAEDecodeAudio 用它取 nested latent 的音频流）。两个 VAE 都是必需的。 */
    UPROPERTY(EditAnywhere, Category = "模型")
    FString AudioVaeName = TEXT("minimax_h3_audio_vae_fp32.safetensors");

    /** Turbo LoRA（LightX2V 蒸馏）。留空 = 不叠，走 25 步基线。 */
    UPROPERTY(EditAnywhere, Category = "模型")
    FString TurboLoraName;

    /**
     * Turbo LoRA 强度。**只在 TurboLoraName 非空时生效**。
     *
     * 这里刻意不写 EditCondition：UE 的 EditCondition 不支持 FString 的方法调用
     * （`!TurboLoraName.IsEmpty()` 会解析失败并每次建属性表都刷一条 LogEditCondition 错误），
     * 而为一个可选强度再引入一个 bool 开关只会让数据模型更绕。留空名字本身就是"不叠"。
     */
    UPROPERTY(EditAnywhere, Category = "模型", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    double TurboLoraStrength = 1.0;

    // ---------------------------------------------------------------- 输出

    /** SaveVideo 的 filename_prefix。产物落在 ComfyUI 输出目录下的这个前缀位置。 */
    UPROPERTY(EditAnywhere, Category = "输出")
    FString FilenamePrefix = TEXT("Shine/H3");

    // ---------------------------------------------------------------- 分镜图（SD1.5）

    /**
     * 出**分镜图**用的参数，与视频那套完全独立。
     *
     * 分镜是两级（PLAN 3.2 第 3 条）：先用 SD1.5 把这一段的图定下来，再让 H3 拿它当
     * 参考 / 首帧出视频。所以这里既不是"视频参数的一部分"，也不该塞进 `FShineVideoShot`
     * ——那个结构从头到尾只描述"一段视频怎么生成"，一个字段都没为出图让路。
     *
     * 默认值全部照抄 `Resources/DirectorNode.json` 的图片分支（那支路线在本机实测跑通过），
     * 唯一例外是种子/尺寸这类"每张图应该不一样"的项也已经给了确定默认值。
     *
     * 采样、解码、落盘这些节点由 `FShineSceneToImageWorkflowBuilder` 直出（纯 C++），
     * 用它们的前提是**能和 Director 导出的图逐字段对拍**（`Scripts/H3/DiffProbeGraph.py`）。
     */

    UPROPERTY(EditAnywhere, Category = "分镜图")
    FString ImageCheckpoint = TEXT("v1-5-pruned-emaonly.safetensors");

    /** 深度 / 法线两支 ControlNet 的模型名。 */
    UPROPERTY(EditAnywhere, Category = "分镜图")
    FString ImageDepthControlNet = TEXT("control_v11f1p_sd15_depth.pth");

    UPROPERTY(EditAnywhere, Category = "分镜图")
    FString ImageNormalControlNet = TEXT("control_v11p_sd15_normalbae.pth");

    /**
     * 出图分辨率。必须是 64 的倍数（builder 会对齐并提示）。
     *
     * 刻意不跟着分镜的 Width/Height：那是 H3 的画布（1280×704 @24fps），而 SD1.5 在
     * 512~768 之间出图最稳；分镜图给 H3 当参考图时，H3 自己会按 ref_image_size 缩放。
     */
    UPROPERTY(EditAnywhere, Category = "分镜图")
    int32 ImageWidth = 768;

    UPROPERTY(EditAnywhere, Category = "分镜图")
    int32 ImageHeight = 768;

    UPROPERTY(EditAnywhere, Category = "分镜图")
    int32 ImageSteps = 30;

    UPROPERTY(EditAnywhere, Category = "分镜图")
    double ImageCfg = 7.5;

    /**
     * img2img 重绘强度。0.5 左右 ="保住 UE 场景的构图，只换风格"；
     * 0.78（Director 默认）会明显重画，适合从一张参考图往目标画面靠。
     */
    UPROPERTY(EditAnywhere, Category = "分镜图", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    double ImageDenoise = 0.78;

    UPROPERTY(EditAnywhere, Category = "分镜图")
    int32 ImageSeed = 12345;

    UPROPERTY(EditAnywhere, Category = "分镜图")
    double ImageDepthStrength = 0.7;

    UPROPERTY(EditAnywhere, Category = "分镜图")
    double ImageNormalStrength = 0.38;

    /** ControlNet 的结束步占比：调小 = 后面几步放开，风格更容易偏离参考图。 */
    UPROPERTY(EditAnywhere, Category = "分镜图", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    double ImageControlEndPercent = 0.85;

    UPROPERTY(EditAnywhere, Category = "分镜图")
    FString ImageSamplerName = TEXT("dpmpp_2m");

    UPROPERTY(EditAnywhere, Category = "分镜图")
    FString ImageScheduler = TEXT("karras");

    /** SaveImage 的 filename_prefix（每张分镜图会在它后面再拼上段名）。 */
    UPROPERTY(EditAnywhere, Category = "分镜图")
    FString ImageOutputPrefix = TEXT("Shine/SceneToImage");

    // ---------------------------------------------------------------- 查询

    /** 取某个分镜（越界返回 nullptr）。 */
    FShineVideoShot* GetShot(int32 Index)
    {
        return Shots.IsValidIndex(Index) ? &Shots[Index] : nullptr;
    }

    const FShineVideoShot* GetShot(int32 Index) const
    {
        return Shots.IsValidIndex(Index) ? &Shots[Index] : nullptr;
    }

    /** 项目是否可以提交：至少有一个可提交的分镜。 */
    bool HasSubmittableShot() const
    {
        for (const FShineVideoShot& Shot : Shots)
        {
            if (Shot.IsSubmittable())
            {
                return true;
            }
        }
        return false;
    }

    /** H3 侧一次任务里参考图的硬上限（ref_images.ref_image_* 的 max=9）。 */
    static constexpr int32 MaxReferenceImagesPerShot = 9;
};
