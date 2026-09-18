#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ShineComfyPathSettings.generated.h"

/**
 * 视频生成的加速档位。
 *
 * 这不是"画质偏好"，而是**实测出来的可信组合**——每一档都由 P6 的基准测试跑过、
 * 有真实耗时与画质记录才允许写进来。所以这里刻意保留 Benchmark 作为"未定稿"档，
 * 档位定稿前工作台默认用它，避免把没验证过的参数当默认值发出去。
 *
 * 三个可变动的维度（P0 实测已确认它们各自独立影响耗时）：
 *   1. 采样步数（25 / 8 / 4）
 *   2. 是否叠加 Turbo LoRA（LightX2V 蒸馏）
 *   3. 是否开 CFG 4.0 —— 注意这会**翻倍** GPU 时间（每步多跑一次负向前向）
 */
UENUM()
enum class EShineComfyAccelerationPreset : uint8
{
    /** 未定稿：用图里写死的参数，不做任何档位改写。档位定稿前走这个。 */
    Benchmark,

    /** 25 步、不叠 Turbo、CFG 4.0。最慢，作为画质上限参照。 */
    Quality,

    /** 8 步 + Turbo LoRA、CFG 1。日常主力。 */
    Balanced,

    /** 4 步 + Turbo LoRA、CFG 1。预览/试构图用；快速运动与音轨可能退化。 */
    Draft
};

/**
 * Shine ↔ ComfyUI 的文件路径设置。
 *
 * ComfyUI 的 API（含 /internal/folder_paths）并不暴露它的 output 目录，
 * 但把目录告诉我们之后，Shine 就能**直接读 ComfyUI 写出的原图**：
 * 不复制到项目里、不重新下载、也不导入成 UE 纹理资产。
 */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "Shine Comfy"))
class SHINEEDITOR_API UShineComfyPathSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    /**
     * ComfyUI 的 output 目录，例如 H:/ComfyUI-aki-v3/ComfyUI/output
     * （就是 ComfyUI 目录下那个 output 文件夹）。
     *
     * 填了以后：直接读原图显示，零复制。
     * 留空：退回"从 /view 接口抓一份到 Saved/ShineComfy"——只有 ComfyUI 跑在别的机器上时才需要。
     */
    UPROPERTY(config, EditAnywhere, Category = "ComfyUI")
    FString ComfyUIOutputDirectory;

    /**
     * ComfyUI 视频产出的目录，例如 H:/ComfyUI-aki-v3/ComfyUI/output/video。
     *
     * 留空则退回用上面的 ComfyUIOutputDirectory。
     * 单独列一个是因为 SaveVideo 的 filename_prefix 里通常带 "video/"，产物会落在
     * output/video 子目录下，而视频往往要放在容量更大的盘上、和图片分开管理。
     */
    UPROPERTY(config, EditAnywhere, Category = "ComfyUI")
    FString ComfyUIVideoOutputDirectory;

    /**
     * 素材库根目录：提示词里 @image: 引用和拖拽进来的素材都相对这个目录解析。
     *
     * 留空则退回 <项目>/Saved/ShineMedia。
     * 之所以要它，是因为 UE 项目目录常常不在大容量盘上，而参考图/参考视频体积不小。
     */
    UPROPERTY(config, EditAnywhere, Category = "ComfyUI")
    FString MediaLibraryDirectory;

    /** 加速档位，见 EShineComfyAccelerationPreset。档位定稿前保持 Benchmark。 */
    UPROPERTY(config, EditAnywhere, Category = "Video")
    EShineComfyAccelerationPreset AccelerationPreset = EShineComfyAccelerationPreset::Benchmark;

    /**
     * 提交任务前要求的最低空闲显存（GB），默认 18。
     *
     * H3 ref2va 底座 + Qwen3-VL-32B 文本编码器会同时常驻，显存不够时 ComfyUI
     * 不会干脆报错，而是**开始往内存/磁盘换页，表现为"越来越慢直到卡死"**——
     * 实测 1344×768 跑 ref2va 必卡死就是这么来的。所以宁可在这里等，也不能带着
     * 不足的显存进去。任务执行器每段前会 /api/free 卸载缓存再循环等待这个阈值。
     */
    UPROPERTY(config, EditAnywhere, Category = "Video", meta = (ClampMin = "0.0", ClampMax = "24.0"))
    double MinFreeVramGb = 18.0;

    virtual FName GetCategoryName() const override;
};
