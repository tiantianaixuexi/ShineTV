#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ShineCharacterAsset.generated.h"

/**
 * 角色资产：跨镜头身份一致的载体。
 *
 * 为什么需要它：H3 的 ref2va 是靠**参考图**来锚定身份的，而同一个角色会在很多个分镜里
 * 出现。如果把参考图散在每个分镜里手工填，改一次角色形象就要改所有分镜。做成独立资产后，
 * 分镜只写 `@char:<该资产路径>`，改形象只改一处。
 *
 * ⚠️ H3 的参考图上限是 **9 张**（`ref_images.ref_image_*` 的 max=9，
 * 见 H3-SPEC.md 坑 2），超过会被静默忽略——所以这里在提交前就拦住。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Shine 角色"))
class SHINEEDITOR_API UShineCharacterAsset : public UObject
{
    GENERATED_BODY()

public:
    UShineCharacterAsset();

    /**
     * 自检：参考图超过 H3 上限时裁到上限，空的显示名用资产名兜底。
     * 裁掉的是**末尾**的图，这样 `<Picture 1..k>` 的编号不会整体错位。
     *
     * @return true 表示有改动。
     */
    bool Sanitize();

    /** 显示名。也会作为默认的身份描述前缀拼进提示词。 */
    UPROPERTY(EditAnywhere, Category = "角色")
    FString DisplayName;

    /**
     * 参考图列表（素材库相对路径或绝对路径）。
     *
     * 顺序即生成的 `<Picture 1>..<Picture N>` 顺序（tokenizer 按连接顺序自动编号），
     * 所以调整顺序会改变提示词里 `{{Mixed N}}` 的指代关系。
     */
    UPROPERTY(EditAnywhere, Category = "角色")
    TArray<FString> ReferenceImages;

    /**
     * 身份描述，会作为一段固定文本拼进引用该角色的提示词。
     * 例："the same woman with short black hair and a grey coat"。
     * 留空则只靠参考图（H3 的参考图本身就能提供很强的身份信息）。
     */
    UPROPERTY(EditAnywhere, Category = "角色", meta = (MultiLine = "true"))
    FString IdentityPrompt;

    /**
     * 是否锁定随机种子。
     *
     * 同一角色跨多个分镜时用同一个种子有助于一致性，但也会让镜头运动变呆。
     * 默认关闭——H3 的身份主要靠参考图，锁定种子不是必需手段。
     */
    UPROPERTY(EditAnywhere, Category = "角色")
    bool bLockSeed = false;

    UPROPERTY(EditAnywhere, Category = "角色", meta = (EditCondition = "bLockSeed"))
    int32 LockedSeed = 42;

    /** H3 的参考图硬上限（`ref_image_*` 的 max=9）。 */
    static constexpr int32 MaxReferenceImages = 9;

    /** 该角色的参考图是否已经达到上限。 */
    bool IsOverReferenceLimit() const
    {
        return ReferenceImages.Num() > MaxReferenceImages;
    }
};
