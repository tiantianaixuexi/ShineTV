#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/Video/SShineVideoNode.h"

/**
 * 分镜节点的外观：把每个参考图槽位"最终会变成第几张"直接标在 pin 旁边。
 *
 * 这是 PLAN 3.2 第 1 条的落点——H3 的 `<Picture i>` 是 tokenizer 按**连接顺序**自动编号的，
 * 提示词改不了它，所以在画布上必须看得见编号。空槽位标"空"，接了的槽位标 `<Picture N>`，
 * N 与 `UShineVideoShotGraphNode::GetPictureOrdinal` 完全一致（编译器用的也是那个函数）。
 */
class SShineVideoShotNode : public SShineVideoNode
{
public:
    SLATE_BEGIN_ARGS(SShineVideoShotNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode);

    virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

protected:
    virtual FText GetExtraStatusText() const override;
    virtual bool HasExtraBody() const override;

private:
    /** 参考图槽位 pin 左边的编号徽标（不是槽位时不返回任何东西）。 */
    TSharedRef<SWidget> MakePictureBadgeWidget(const class UEdGraphPin* Pin) const;
};
