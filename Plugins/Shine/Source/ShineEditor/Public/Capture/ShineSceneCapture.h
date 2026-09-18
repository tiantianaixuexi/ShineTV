#pragma once

#include "CoreMinimal.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

class FLevelEditorViewportClient;

/** 一次场景捕获的入参。 */
struct FShineCaptureRequest
{
    /** 相机世界坐标（厘米）。 */
    FVector Location = FVector::ZeroVector;

    /** 相机朝向。 */
    FRotator Rotation = FRotator::ZeroRotator;

    /** 相机 FOV（度）。 */
    float FOVAngle = 55.0f;

    int32 Width = 768;
    int32 Height = 768;

    /** 输出目录；留空则自动生成 Saved/ShineCapture/<时间戳>。 */
    FString OutputDirectory;

    /** 文件名前缀，例如 "Scene"；最终文件名会自动带上时间戳保证唯一。 */
    FString FilePrefix;

    bool bCaptureColor = true;
    bool bCaptureDepth = true;
    bool bCaptureNormal = true;

    /** 捕获完成后是否把三张图贴到关卡视口右侧预览。 */
    bool bShowPreview = true;

    /** 预览面板顶部显示的机位说明；留空则不显示。 */
    FString CameraLabel;

    /** 机位是否来自当前关卡视口（仅用于结果标注/日志）。 */
    bool bCameraFromViewport = false;
};

/** 单个通道的产物。 */
struct FShineCaptureFile
{
    FString Path;
    int64 Bytes = 0;

    /** 深度/法线通道的数值统计，便于判断是不是拍到了"纯色/空白"。 */
    double MinValue = 0.0;
    double MaxValue = 0.0;
    double MeanValue = 0.0;
    bool bHasStats = false;
    bool bRawNormal = false;
};

/** 一次场景捕获的结果。 */
struct FShineCaptureResult
{
    bool bSuccess = false;
    bool bAnyExported = false;
    FString ErrorMessage;

    FString Directory;
    FString UniquePrefix;
    int32 Width = 0;
    int32 Height = 0;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;

    /** 机位是不是取自当前关卡视口（false 表示用了兜底机位）。 */
    bool bCameraFromViewport = false;

    /** 机位说明，例如 "视口机位 P=-14.0 Y=-153.4 FOV 55"。 */
    FString CameraLabel;

    FShineCaptureFile Color;
    FShineCaptureFile Depth;
    FShineCaptureFile Normal;

    /** 把结果拼成 MCP / 日志用的多行文本。 */
    FString ToLogText() const;
};

/**
 * UE 场景 → 颜色 / 深度 / 法线 三张图。
 *
 * 深度按 min..max 归一化成"近亮远暗"；法线按 n*0.5+0.5 编码成标准法线贴图。
 * 两条都是 ControlNet 期望的输入格式。
 */
namespace ShineSceneCapture
{
    /** 当前正在操作的关卡视口（透视视图）。找不到时返回 nullptr。 */
    SHINEEDITOR_API FLevelEditorViewportClient* GetActiveViewportClient();

    /** 视口相机位置；拿不到时返回一个兜底机位。 */
    SHINEEDITOR_API FVector GetEditorViewportLocation();

    /** 视口相机朝向；拿不到时返回一个兜底朝向。 */
    SHINEEDITOR_API FRotator GetEditorViewportRotation();

    /** 视口相机 FOV；拿不到时返回 55。 */
    SHINEEDITOR_API float GetEditorViewportFOV();

    /** 是否成功读到了视口相机（false 表示用的是兜底机位）。 */
    SHINEEDITOR_API bool HasEditorViewport();

    /** 生成 Saved/ShineCapture/<时间戳> 形式的默认输出目录。 */
    SHINEEDITOR_API FString BuildDefaultCaptureDirectory();

    /** 给前缀加上毫秒级时间戳，避免同名文件被 ComfyUI 当成同一张图（命中缓存）。 */
    SHINEEDITOR_API FString MakeUniquePrefix(const FString& Prefix);

    /** 按指定机位捕获。 */
    SHINEEDITOR_API FShineCaptureResult Capture(const FShineCaptureRequest& Request);

    /**
     * 用当前关卡视口的机位捕获（"所见即所拍"）。
     * 如果视口不可用，会退回兜底机位并在日志里说明。
     */
    SHINEEDITOR_API FShineCaptureResult CaptureFromEditorViewport(
        int32 Width,
        int32 Height,
        const FString& OutputDirectory,
        const FString& FilePrefix,
        bool bCaptureColor,
        bool bCaptureDepth,
        bool bCaptureNormal,
        bool bShowPreview = true);
}
