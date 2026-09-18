#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "ShineVideoCharacterGraphNode.generated.h"

/**
 * 角色节点。
 *
 * 它不自己存参考图，只引用一个 `UShineCharacterAsset`：同一个角色会在很多分镜里出现，
 * 把参考图散在每个分镜里手工填的话，改一次形象就得改所有分镜。
 *
 * 接进分镜的参考图槽位后，这个槽位会**展开成该角色资产的整份参考图序列**
 * （顺序就是资产里 ReferenceImages 的顺序），IdentityPrompt 也会拼进提示词。
 * 所以它在画布上的编号徽标是一段**区间**（例如 `4-7`），不是单独一个数字。
 */
UCLASS()
class UShineVideoCharacterGraphNode : public UShineVideoGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineVideoCharacterGraphNode();

    virtual FText GetVideoNodeKind() const override;
    virtual FName GetNodePreset() const override;
    virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

    /** 校验/编译共用的取参入口：拿到角色资产对象（拿不到返回 nullptr）。 */
    const class UShineCharacterAsset* ResolveCharacterAsset() const;

protected:
    virtual void BuildNodePins() override;
};

namespace ShineVideoCharacterNode
{
    const FName Preset(TEXT("VideoCharacter"));

    /** `UShineCharacterAsset` 的资产路径（/Game/...）。 */
    const FName CharacterAssetParameter(TEXT("CharacterAsset"));

    const FName OutputPin(TEXT("Character"));
}
