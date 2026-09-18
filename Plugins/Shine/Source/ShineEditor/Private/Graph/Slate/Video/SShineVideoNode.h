#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/SShineComfyGraphStandardNode.h"
#include "Styling/SlateColor.h"

class UShineVideoGraphNodeBase;

/**
 * 视频域节点的公共外观：在标题下面加一条"节点身体"。
 *
 * 它本身只画两样东西：
 *   1. 子类的预览内容（缩略图 / 逐段播放器；`BuildPreviewWidget`）；
 *   2. 运行期状态（空闲 / 排队中 / 运行中 n/m / 完成 / 失败）。
 *
 * 参数区、pin、配色全部走 `SShineComfyGraphStandardNode`，一行都不重写。
 */
class SShineVideoNode : public SShineComfyGraphStandardNode
{
public:
    SLATE_BEGIN_ARGS(SShineVideoNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode);

protected:
    virtual TSharedRef<SWidget> BuildExtraBodyWidget() override;

    /** 子类往身体里放的内容（缩略图 / 逐段行）。默认空。 */
    virtual TSharedRef<SWidget> BuildPreviewWidget();

    /** 子类追加的状态文字（跟在运行状态后面）。默认空。 */
    virtual FText GetExtraStatusText() const;

    /** 身体要不要显示（默认：跑过或有状态文字时才显示，免得一堆空条）。 */
    virtual bool HasExtraBody() const;

    UShineVideoGraphNodeBase* GetVideoNode() const;

    FText GetRuntimeStateText() const;
    FSlateColor GetRuntimeStateColor() const;
};
