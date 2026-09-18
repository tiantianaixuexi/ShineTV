#pragma once

#include "CoreMinimal.h"

/**
 * 把刚捕获的（颜色 / 深度 / 法线）图片直接贴在关卡视口的右侧，
 * 方便在编辑器里肉眼确认"送进 AI 的到底是什么"。
 *
 * 实现方式：为每张图建一个瞬时 UTexture2D + FSlateBrush，
 * 组成一个贴在视口右上角的 overlay 控件（SEditorViewport::AddOverlayWidget）。
 * 预览层本身不接收鼠标事件，不会挡住视口操作。
 */
struct FShineCapturePreviewItem
{
    /** 标题，例如 "Color" / "Depth" / "Normal"。 */
    FString Label;

    /** 磁盘上的图片路径（PNG）。 */
    FString FilePath;

    /** 可选的补充说明，例如分辨率或统计值。 */
    FString Detail;
};

namespace ShineCapturePreview
{
    /**
     * 在关卡视口右侧显示预览；会先清掉上一次的预览。
     * @param Header 面板顶部的一行说明（通常写"用的哪个机位"），留空则不显示。
     */
    SHINEEDITOR_API void Show(const TArray<FShineCapturePreviewItem>& Items, const FString& Header = FString());

    /** 移除预览。 */
    SHINEEDITOR_API void Clear();

    /** 当前是否有预览在显示。 */
    SHINEEDITOR_API bool IsVisible();
}
