#pragma once

#include "CoreMinimal.h"
#include "ShineAIPaintTypes.generated.h"

class UObject;

/** 画笔工具模式。 */
UENUM()
enum class EShineAIPaintTool : uint8
{
    /** 在底色图层上画。 */
    Paint UMETA(DisplayName = "画底色"),
    /** 擦回初始底色。 */
    Erase UMETA(DisplayName = "擦除"),
    /** 往遮罩图层上画。 */
    MaskPaint UMETA(DisplayName = "画遮罩"),
    /** 擦遮罩。 */
    MaskErase UMETA(DisplayName = "擦遮罩")
};

/** 遮罩语义。 */
UENUM()
enum class EShineAIPaintMaskMode : uint8
{
    /** 遮罩区 = 保护区，AI 不更新。 */
    Protect UMETA(DisplayName = "遮罩区 = 保护区（AI 不更新）"),
    /** 遮罩区 = 只更新这里。 */
    Editable UMETA(DisplayName = "遮罩区 = 只更新这里")
};

/** AI 后端。 */
UENUM()
enum class EShineAIPaintBackend : uint8
{
    /** ComfyUI：上传原图+遮罩 → 内置 inpaint 工作流 → 取回结果。 */
    ComfyUI UMETA(DisplayName = "ComfyUI（inpaint 工作流）"),
    /** 通用 HTTP：POST JSON（base64 原图/遮罩）→ 返回 JSON（base64 结果图）。 */
    GenericHttp UMETA(DisplayName = "通用 HTTP（base64）")
};

/** 一次 AI 更新贴图的参数。 */
USTRUCT()
struct FShineAIPaintSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "AI")
    EShineAIPaintBackend Backend = EShineAIPaintBackend::ComfyUI;

    UPROPERTY(EditAnywhere, Category = "AI")
    EShineAIPaintMaskMode MaskMode = EShineAIPaintMaskMode::Protect;

    /** ComfyUI 服务地址；勾选"跟随 ShineComfy 资产"时会被资产里的地址覆盖。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    FString ServiceUrl = TEXT("http://127.0.0.1:8188");

    /** 通用 HTTP 模式的目标 URL；留空则用 ServiceUrl。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    FString GenericEndpoint;

    UPROPERTY(EditAnywhere, Category = "AI")
    FString Prompt = TEXT("seamless pbr texture, highly detailed, consistent lighting");

    UPROPERTY(EditAnywhere, Category = "AI")
    FString NegativePrompt = TEXT("blurry, low quality, watermark, text");

    /** ComfyUI 的 checkpoint 文件名。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    FString CheckpointName;

    UPROPERTY(EditAnywhere, Category = "AI", meta = (ClampMin = "1", ClampMax = "200"))
    int32 Steps = 20;

    UPROPERTY(EditAnywhere, Category = "AI", meta = (ClampMin = "0.1", ClampMax = "30.0"))
    float CFG = 7.0f;

    UPROPERTY(EditAnywhere, Category = "AI", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float Denoise = 0.85f;

    /** -1 表示每次随机。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    int32 Seed = -1;

    /** inpaint 时向外扩张遮罩的像素数。 */
    UPROPERTY(EditAnywhere, Category = "AI", meta = (ClampMin = "0", ClampMax = "128"))
    int32 GrowMaskBy = 6;

    /** 可选 Authorization 头（远程服务需要鉴权时填写）。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    FString AuthorizationHeader;
};

/** 预览场景里的灯光类型。 */
UENUM()
enum class EShineAIPaintLightType : uint8
{
    Directional UMETA(DisplayName = "方向光"),
    Point UMETA(DisplayName = "点光"),
    Spot UMETA(DisplayName = "聚光")
};

/**
 * 预览场景里的一盏灯（纯运行时，不序列化）。
 * 用共享指针持有，方便"放置灯光"面板和视口同时引用同一份数据。
 */
struct FShineAIPaintPreviewLight
{
    FString Name;
    EShineAIPaintLightType Type = EShineAIPaintLightType::Point;
    bool bEnabled = true;

    FVector Location = FVector(300.0f, 0.0f, 300.0f);
    FRotator Rotation = FRotator(-45.0f, 0.0f, 0.0f);

    FLinearColor Color = FLinearColor::White;
    float Intensity = 5000.0f;

    /** 只用于列表显示。 */
    FString GetTypeLabel() const
    {
        switch (Type)
        {
        case EShineAIPaintLightType::Directional: return TEXT("方向光");
        case EShineAIPaintLightType::Spot:        return TEXT("聚光");
        default:                                  return TEXT("点光");
        }
    }
};

/** 被指认的一个网格体目标（运行时状态，不序列化）。 */
struct FShineAIPaintTarget
{
    /** UStaticMesh 或 USkeletalMesh。 */
    TWeakObjectPtr<UObject> MeshAsset;

    FString DisplayName;

    bool IsValidTarget() const { return MeshAsset.IsValid(); }
};
