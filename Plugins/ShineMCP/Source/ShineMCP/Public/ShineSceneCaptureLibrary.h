#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShineSceneCaptureLibrary.generated.h"

/**
 * 场景视图捕获：把当前编辑器关卡从指定机位渲染成
 * 颜色（FinalColor）/ 深度（SceneDepth）/ 法线（Normal）三张 PNG。
 *
 * 三张图正好对应 AI 生成里最常用的三种引导信号：
 *   Color  → img2img / 结构引导
 *   Depth  → ControlNet depth
 *   Normal → ControlNet normal
 *
 * 所有函数都返回 JSON 文本，方便 MCP / Python / 蓝图三处复用。
 */
UCLASS()
class SHINEMCP_API UShineSceneCaptureLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * 捕获三张视图。
     * @param Location         机位世界坐标
     * @param Rotation         机位朝向
     * @param FOVAngle         视场角（水平，度）
     * @param Width/Height     输出分辨率
     * @param OutputDirectory  输出目录，留空则用 Saved/ShineCapture/<时间戳>
     * @param FilePrefix       文件名前缀
     * @param bCaptureColor/bCaptureDepth/bCaptureNormal 要捕获哪些通道
     */
    UFUNCTION(BlueprintCallable, Category = "Shine|Capture", meta = (AdvancedDisplay = "bCaptureColor,bCaptureDepth,bCaptureNormal"))
    static FString CaptureSceneViews(
        FVector Location,
        FRotator Rotation,
        float FOVAngle,
        int32 Width,
        int32 Height,
        const FString& OutputDirectory,
        const FString& FilePrefix,
        bool bCaptureColor = true,
        bool bCaptureDepth = true,
        bool bCaptureNormal = true);

    /** 使用当前编辑器视口相机的机位与 FOV 捕获。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Capture")
    static FString CaptureFromEditorViewport(
        int32 Width,
        int32 Height,
        const FString& OutputDirectory,
        const FString& FilePrefix,
        bool bCaptureColor = true,
        bool bCaptureDepth = true,
        bool bCaptureNormal = true);

    /** 读取当前编辑器视口相机状态（位置 / 朝向 / FOV）。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Capture")
    static FString GetEditorViewportCamera();

    /** 让相机看向目标点。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Capture")
    static FRotator LookAtRotation(FVector From, FVector To);

    /** 计算当前关卡的包围盒（用于自动给一个能框住全场景的机位）。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Capture")
    static FString GetLevelBounds();

private:
    static FString CaptureInternal(
        const FVector& Location,
        const FRotator& Rotation,
        float FOVAngle,
        int32 Width,
        int32 Height,
        const FString& OutputDirectory,
        const FString& FilePrefix,
        bool bCaptureColor,
        bool bCaptureDepth,
        bool bCaptureNormal);
};
