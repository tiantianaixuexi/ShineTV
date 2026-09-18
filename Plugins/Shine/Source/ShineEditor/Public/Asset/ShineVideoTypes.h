#pragma once

#include "CoreMinimal.h"
#include "ShineVideoTypes.generated.h"

/**
 * 一个分镜的驱动方式。
 *
 * 这两条路各有取舍，是 P0 实测确认的（见 Scripts/H3/H3-SPEC.md 第五节）：
 *
 *   - Reference（ref2va）：吃参考图，**跨镜头身份一致**；但该节点
 *     【没有 first_frame / last_frame 输入】，上一段的末帧只能作为参考图"软锚定"，
 *     接缝不如硬关键帧严。
 *   - FirstLastFrame（fl2va）：吃首帧/末帧，可**硬锚定关键帧**、接缝最严；
 *     但该节点【没有参考图】，身份会随镜头漂。
 *
 * 该选哪条本身就是 P6 要对比实测的项，所以这里做成显式开关，不替使用者决定。
 */
UENUM()
enum class EShineVideoShotMode : uint8
{
    /** 参考图驱动（MiniMaxH3ReferenceToVideo）。 */
    Reference,

    /** 首帧/尾帧驱动（MiniMaxH3ImageToVideo）。 */
    FirstLastFrame
};

/**
 * 提示词里的引用语法（由 FShineMentionResolver 在 P3 解析，这里的注释是唯一权威说明）。
 *
 *   @image:<路径>      插入一张参考图。路径可以是素材库相对路径或绝对路径。
 *   @char:<角色资产>   插入一个角色资产携带的全部参考图。
 *   {{Mixed N}}        在提示词文本里【指代】第 N 张参考图。
 *
 * ⚠️ 关键语义（P0 实测坐实）：H3 的 `<Picture i>` 标签是 tokenizer 按**连接顺序自动
 * 生成**的（comfy/text_encoders/minimax.py:153-179），不是你写进提示词的声明。
 * 所以 `{{Mixed N}}` 的含义是"这里引用第 N 张"，而不是"这里定义第 N 张"。
 * 顺序 = 参考图连接顺序，无法靠提示词改变。
 */
namespace ShineVideoMention
{
    /** @image: 前缀。 */
    static const TCHAR* const ImagePrefix = TEXT("@image:");

    /** @char: 前缀。 */
    static const TCHAR* const CharacterPrefix = TEXT("@char:");
}

/**
 * 一个分镜（shot）。
 *
 * 字段按官方分镜表规范（Scripts/H3/reference/minimax-official/shot-table-spec.md）取舍，
 * 只保留对生成结果有实际影响的项；纯创作描述类字段交由 Prompt 承载。
 *
 * 采样默认值全部来自 P0 的实测基线（H3-SPEC.md 二、三节）：
 * 1280x704 / 124帧 / 24fps / 25步 / CFG 4.0 / denoise 1.0 / res_multistep + simple。
 * ⚠️ 1344x768 跑 ref2va **必卡死**，不要往上调。
 */
USTRUCT()
struct FShineVideoShot
{
    GENERATED_BODY()

    /** 分镜标题（面板显示用，不影响生成）。 */
    UPROPERTY(EditAnywhere, Category = "分镜")
    FString Title;

    /**
     * 提示词。支持 @image: / @char: / {{Mixed N}} 引用语法。
     * 建议按官方三段式结构写（参考 Scripts/H3/reference/minimax-official/）。
     */
    UPROPERTY(EditAnywhere, Category = "分镜", meta = (MultiLine = "true"))
    FString Prompt;

    /** 该分镜引用的参考图（素材库相对路径或绝对路径）。H3 上限 9 张。 */
    UPROPERTY(EditAnywhere, Category = "分镜")
    TArray<FString> ReferenceImages;

    /** 该分镜引用的角色资产路径，用于跨镜头身份一致。 */
    UPROPERTY(EditAnywhere, Category = "分镜")
    TArray<FString> CharacterAssetPaths;

    /** 驱动方式，见 EShineVideoShotMode。 */
    UPROPERTY(EditAnywhere, Category = "分镜")
    EShineVideoShotMode Mode = EShineVideoShotMode::Reference;

    /**
     * 首帧图（仅 FirstLastFrame 模式使用；Reference 模式下忽略）。
     * @image: 语法解析出的第一张图也会被当作首帧。
     */
    UPROPERTY(EditAnywhere, Category = "分镜")
    FString FirstFrameImage;

    /**
     * 是否把上一段分镜的末帧接进来。
     *
     * Reference 模式下它作为**额外的一张参考图**（软锚定）；
     * FirstLastFrame 模式下它作为**首帧**（硬锚定）。这就是"链式"的实现方式——
     * ComfyUI 里不存在 MotionContext 这种东西（已全仓检索确认）。
     */
    UPROPERTY(EditAnywhere, Category = "分镜")
    bool bChainFromPrevious = false;

    // ---------------------------------------------------------------- 分辨率与时长

    /** 宽度。必须是 32 的倍数。 */
    UPROPERTY(EditAnywhere, Category = "采样")
    int32 Width = 1280;

    /** 高度。必须是 32 的倍数。 */
    UPROPERTY(EditAnywhere, Category = "采样")
    int32 Height = 704;

    /**
     * 帧数。会被自动对齐到 H3 的 17k+5 网格（124 = 5.17 秒 @24fps）。
     * 不要手工填 123 这种不在网格上的值——builder 会替你对齐，但那样你就不知道实际跑了几帧。
     */
    UPROPERTY(EditAnywhere, Category = "采样")
    int32 Length = 124;

    // ---------------------------------------------------------------- 采样

    UPROPERTY(EditAnywhere, Category = "采样")
    int32 Steps = 25;

    /**
     * CFG。**注意成本**：CFG > 1 时每一步要跑正向+负向两次前向，GPU 时间约翻倍。
     * 官方模板用的是等价 CFG=1 的 BasicGuider（见 H3-SPEC.md 坑 6），4.0 是有意增强。
     */
    UPROPERTY(EditAnywhere, Category = "采样")
    double Cfg = 4.0;

    UPROPERTY(EditAnywhere, Category = "采样")
    int32 Seed = 42;

    UPROPERTY(EditAnywhere, Category = "采样")
    double Denoise = 1.0;

    UPROPERTY(EditAnywhere, Category = "采样")
    double ShiftVideo = 12.0;

    UPROPERTY(EditAnywhere, Category = "采样")
    double ShiftAudio = 3.0;

    /**
     * 参考图的尺寸策略：match / max。
     *
     * match：把每张参考图缩到与生成画布同像素量（只缩不放）。
     * max：用参考管线的 2048px 短边，身份保真最好，但参考 token 会贯穿每个采样步，
     *      **可能慢好几倍**（节点 tooltip 原话）。这是 P6 要实测的维度之一。
     *
     * 合法值只有 "match" / "max"，builder 会校验。这里刻意不写 GetOptions——
     * USTRUCT 里无法挂 UFUNCTION，写了会导致 UHT 报错。
     */
    UPROPERTY(EditAnywhere, Category = "采样")
    FString RefImageSize = TEXT("match");

    // ---------------------------------------------------------------- 运行期状态（由任务执行器回填）

    /** 最近一次提交得到的 prompt_id。 */
    UPROPERTY(VisibleAnywhere, Category = "运行期")
    FString LastPromptId;

    /** 最近一次生成的产物文件名（相对 ComfyUI 输出目录）。 */
    UPROPERTY(VisibleAnywhere, Category = "运行期")
    TArray<FString> LastOutputFiles;

    /** 最近一次的错误信息（成功时为空）。 */
    UPROPERTY(VisibleAnywhere, Category = "运行期")
    FString LastError;

    /** 提交前的最小自检：没有提示词、也没有任何参考图/首帧时，这个分镜跑不出东西。 */
    bool IsSubmittable() const
    {
        return !Prompt.TrimStartAndEnd().IsEmpty()
            || ReferenceImages.Num() > 0
            || CharacterAssetPaths.Num() > 0
            || !FirstFrameImage.IsEmpty();
    }
};
