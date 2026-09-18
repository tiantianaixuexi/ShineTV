#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "ShineVideoShotImageGraphNode.generated.h"

/**
 * 分镜图节点。
 *
 * 它是"分镜"和"视频"之间的那一级（PLAN 3.2 第 3 条）：
 * 先把分镜提示词出的图固定下来，再让 H3 拿这张图当参考 / 首帧出视频。
 * 这一步不能省——H3 的 ref2va 靠参考图锚定身份与构图，直接拿一张素材图当参考
 * 会让"这个分镜到底长什么样"在两次生成之间漂掉。
 *
 * 图的来源有两种，都在这个节点上：
 *   1. **上游生成的**：跑一次出图链路后把产物路径回填进来（`SetResultImagePaths`，
 *      和 Comfy 图里的显示节点同一套机制）；
 *   2. **直接引用的**：`图片路径` 参数手工填一个素材 / 上游图。
 *
 * 编译时**优先用已生成的产物**（`ResultImagePaths[0]`），没有再退回参数里的路径。
 */
UCLASS()
class UShineVideoShotImageGraphNode : public UShineVideoGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineVideoShotImageGraphNode();

    virtual FText GetVideoNodeKind() const override;
    virtual FName GetNodePreset() const override;
    virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

    /**
     * 这一级最终要交给视频的图片路径。优先级：
     *   1. 已生成的产物（`ResultImagePaths` 最后一张）；
     *   2. 手工填的「图片路径」；
     *   3. 接进来的那根线（`图片` 输入，直接引用上游的素材图片 / 另一个分镜图）。
     */
    FString GetImagePath() const;

protected:
    virtual void BuildNodePins() override;

private:
    /**
     * `GetImagePath` 的真正实现。
     *
     * `Depth` 只用来防环：`图片` 输入是同类别的 Image → Image，两个分镜图节点互接是合法的连线，
     * 不设上限就会栈溢出。
     */
    FString ResolveImagePath(int32 Depth) const;
};

namespace ShineVideoShotImageNode
{
    const FName Preset(TEXT("VideoShotImage"));

    const FName TitleParameter(TEXT("Title"));
    const FName ImagePathParameter(TEXT("ImagePath"));

    const FName ShotPin(TEXT("Shot"));
    const FName ImagePin(TEXT("ImageSource"));

    const FName OutputPin(TEXT("Image"));
}
