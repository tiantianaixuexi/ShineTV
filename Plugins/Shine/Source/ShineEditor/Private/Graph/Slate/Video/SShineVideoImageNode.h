#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/Video/SShineVideoNode.h"

class SShineComfyMediaPreview;

/**
 * 带缩略图的视频节点（素材图片 / 分镜图）。
 *
 * 缩略图不是装饰：分镜图是"视频拿哪一张当参考/首帧"的判据，光看路径名根本分不清
 * 是哪一张；而且要能一眼看出"这张图还没有着落"。
 */
class SShineVideoImageNode : public SShineVideoNode
{
public:
    SLATE_BEGIN_ARGS(SShineVideoImageNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode);

protected:
    virtual TSharedRef<SWidget> BuildPreviewWidget() override;
    virtual FText GetExtraStatusText() const override;
    virtual bool HasExtraBody() const override;

private:
    /** 节点当前要显示的图（已解析成磁盘绝对路径；没有时返回空串）。 */
    FString ResolveDisplayPath() const;

    FReply HandleZoomClicked();
    FReply HandleReloadClicked();

    /**
     * 「出图」：让这个分镜图真的去出一张图（SD1.5 路线，见 `FShineSceneToImageWorkflowBuilder`）。
     *
     * 只有「分镜图」节点有这个按钮：它是分镜与视频之间那一级，`素材图片` 节点只是"引用一张
     * 现成的图"，给按钮反而会让人以为它能生成。
     */
    FReply HandleGenerateClicked();

    /** 出图按钮要不要显示 / 能不能点。 */
    EVisibility GetGenerateButtonVisibility() const;
    bool CanGenerate() const;

    /** 右下角弹一条提示（组图参数不合法、已经有任务在跑…）。 */
    void ShowNotification(const FString& Message, bool bSuccess) const;

    TSharedPtr<SShineComfyMediaPreview> MediaPreview;
};
