#pragma once

#include "CoreMinimal.h"

/**
 * 放大看图：显示节点上的"放大"按钮点下去会弹一个可缩放的独立窗口，
 * 以原图尺寸显示（比节点里的缩略图清楚），窗口能拖大、能滚动。
 *
 * 刷新规则（重要）：
 *  - 同一路径再次 Show → 重新读盘一次（ComfyUI 同名文件会被覆盖），并置顶；
 *  - 结果更新时调用 RefreshOpenWindows → 已打开的窗口重新读文件，
 *    如果原来那张已经不在当前结果里，就切到最新那张。
 */
namespace ShineImageZoom
{
    /** 打开图片放大窗口；同一张图已经开着就重读并置顶，不重复开窗。 */
    SHINEEDITOR_API void Show(const FString& ImagePath);

    /**
     * 结果更新时同步已打开的放大窗口。
     * @param CurrentPaths 当前这批结果图（按顺序，最后一张视为最新）
     */
    SHINEEDITOR_API void RefreshOpenWindows(const TArray<FString>& CurrentPaths);

    /** 关掉所有放大窗口。 */
    SHINEEDITOR_API void CloseAll();
}
