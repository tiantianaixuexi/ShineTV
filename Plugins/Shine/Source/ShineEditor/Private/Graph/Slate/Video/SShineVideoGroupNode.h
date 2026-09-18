#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/Video/SShineVideoNode.h"

class UShineVideoGroupGraphNode;
class SShineComfyMediaPreview;

/**
 * 视频组节点的外观：每一段一行，行里有自己的状态、步进和播放按钮。
 *
 * 这是 PLAN 3.4 第 3、4 条的落点：
 *   - 逐段进度：每段的状态与 `n/m` 步进分别显示（不是一根总进度条）；
 *   - 节点内播 mp4：产物落盘后这一段行里出现播放器，点「播放」直接在节点里看（含音频）。
 *
 * 段槽位用「＋一段 / －末段」按钮增删，而不是拖线时自动长 pin：自动增长必须在
 * `PinConnectionListChanged` 里改 pin 表 + `NotifyGraphChanged()`，而那个回调正处在引擎
 * 遍历 pin 的循环里，同时 `NotifyGraphChanged()` 会**立刻**销毁重建所有节点控件——
 * 拖线拖到一半把控件拆掉是崩溃的经典写法。按钮点击是普通 Slate 事件，安全。
 */
class SShineVideoGroupNode : public SShineVideoNode
{
public:
    SLATE_BEGIN_ARGS(SShineVideoGroupNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode);

protected:
    virtual TSharedRef<SWidget> BuildPreviewWidget() override;
    virtual FText GetExtraStatusText() const override;
    virtual bool HasExtraBody() const override;

private:
    UShineVideoGroupGraphNode* GetGroupNode() const;

    TSharedRef<SWidget> BuildSegmentRow(int32 SlotIndex);
    TSharedRef<SWidget> BuildSlotButtons();

    FText GetSegmentStatusText(int32 SlotIndex) const;
    FSlateColor GetSegmentStatusColor(int32 SlotIndex) const;
    FText GetSegmentPlayButtonText(int32 SlotIndex) const;

    FReply HandleToggleSegmentPlayback(int32 SlotIndex);
    FReply HandleAddSegmentSlot();
    FReply HandleRemoveSegmentSlot();

    /** 每段一行的播放器控件（下标与段槽位对齐；空槽位是 nullptr）。 */
    TArray<TSharedPtr<SShineComfyMediaPreview>> SegmentPreviews;
};
