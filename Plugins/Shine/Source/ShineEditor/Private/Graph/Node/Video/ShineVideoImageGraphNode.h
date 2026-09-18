#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "ShineVideoImageGraphNode.generated.h"

/**
 * 素材图片节点。
 *
 * 两种来源都走它：素材库里的图片，以及场景捕获出来的图（`ShineSceneCapture` 那条线）。
 * 图只是"磁盘上一个路径"，不导入成 UE 资产——节点里直接用 Slate 画出来
 * （见 `SShineComfyMediaPreview`），所以换一张图就是改一行文本，没有导入/重导入的麻烦。
 */
UCLASS()
class UShineVideoImageGraphNode : public UShineVideoGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineVideoImageGraphNode();

    virtual FText GetVideoNodeKind() const override;
    virtual FName GetNodePreset() const override;
    virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

    /** 当前要显示的图片路径（素材库相对路径或绝对路径；相对路径按素材库根解析）。 */
    FString GetImagePath() const;

protected:
    virtual void BuildNodePins() override;
};

namespace ShineVideoImageNode
{
    const FName Preset(TEXT("VideoImage"));

    const FName LabelParameter(TEXT("Label"));
    const FName ImagePathParameter(TEXT("ImagePath"));

    const FName OutputPin(TEXT("Image"));
}
