#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "ShineVideoShotGraphNode.generated.h"

/**
 * 分镜节点：提示词 + 采样参数 + 参考图槽位。
 *
 * ## 参考图槽位为什么是固定 9 个
 *
 * H3 的参考图上限就是 **9 张**（`ref_images.ref_image_*` 的 max=9，见 H3-SPEC.md 坑 2），
 * 超出的会被**静默忽略**。所以槽位数正好等于上限，既不会出现"填了 12 张但只有 9 张生效"，
 * 也不需要动态增删 pin 那套（动态 pin 在拖动连线的回调里改 pin 表很容易把节点控件拆掉，
 * 得不偿失）。
 *
 * ## 槽位编号 = `<Picture N>`
 *
 * H3 的 `<Picture i>` 是 tokenizer 按**参考图连接顺序自动生成**的，改不了。
 * 所以这里把"空槽位跳过之后，这个槽位会排到第几"直接算出来显示在节点上
 * （`GetPictureOrdinal`）——用户在画布上看到的就是提交后真实生效的编号。
 */
UCLASS()
class UShineVideoShotGraphNode : public UShineVideoGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineVideoShotGraphNode();

    virtual FText GetVideoNodeKind() const override;
    virtual FName GetNodePreset() const override;
    virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

    /** H3 的参考图硬上限（`ref_images.ref_image_*` 的 max=9）。 */
    static constexpr int32 PictureSlotCount = 9;

    /** 槽位下标（0 基）→ pin 名。 */
    static FName MakePicturePinName(int32 SlotIndex);

    /** pin 名 → 槽位下标（不是参考图槽位时返回 INDEX_NONE）。 */
    static int32 ParsePicturePinIndex(const FName& PinName);

    /** 按槽位顺序取参考图槽位 pin（长度恒为 PictureSlotCount）。 */
    void GetPicturePins(TArray<class UEdGraphPin*>& OutPins) const;

    const class UEdGraphPin* GetPicturePin(int32 SlotIndex) const;

    /**
     * 这个槽位在本次提交里是第几张参考图（1 基）。
     *
     * 空槽位会被跳过——所以 `第 3 槽` 可能是 `<Picture 2>`。返回 INDEX_NONE 表示
     * 这个槽位没接线、不参与编号。节点上用这个值画编号徽标。
     */
    int32 GetPictureOrdinal(int32 SlotIndex) const;

    /**
     * 下游接了分镜图节点时，分镜图会占掉 `<Picture 1>`，槽位编号整体后移一位。
     *
     * 这个偏移与 `FShineVideoGraphCompiler` 用的是同一处判断（它直接调这个函数），
     * 所以画布上显示的编号和提交后真实生效的编号不会各说各话。
     */
    int32 GetPictureNumberOffset() const;

    /** 分镜的输出是否接到了某个分镜图节点。 */
    bool IsConsumedByShotImage() const;

    /** 已经接线的槽位数（= 本次提交的参考图张数，不含角色展开的部分）。 */
    int32 GetLinkedPictureCount() const;

    /** 提示词（加了 @image:/@char:/{{Mixed N}} 的原文，没解析）。 */
    FString GetPromptText() const;

    /** 分镜标题。 */
    FString GetShotTitle() const;

protected:
    virtual void BuildNodePins() override;
};

namespace ShineVideoShotNode
{
    const FName Preset(TEXT("VideoShot"));

    const FName TitleParameter(TEXT("Title"));
    const FName PromptParameter(TEXT("Prompt"));
    const FName ModeParameter(TEXT("Mode"));
    const FName RefImageSizeParameter(TEXT("RefImageSize"));
    const FName WidthParameter(TEXT("Width"));
    const FName HeightParameter(TEXT("Height"));
    const FName LengthParameter(TEXT("Length"));
    const FName StepsParameter(TEXT("Steps"));
    const FName CfgParameter(TEXT("Cfg"));
    const FName SeedParameter(TEXT("Seed"));
    const FName DenoiseParameter(TEXT("Denoise"));
    const FName ShiftVideoParameter(TEXT("ShiftVideo"));
    const FName ShiftAudioParameter(TEXT("ShiftAudio"));

    const FName ScriptPin(TEXT("Script"));
    const FName OutputPin(TEXT("Shot"));

    /** `Mode` 参数的取值（写死在 UI 上，编译时按字符串匹配）。 */
    const TCHAR* const ModeReference = TEXT("参考图 Reference");
    const TCHAR* const ModeFirstLastFrame = TEXT("首尾帧 FirstLastFrame");

    /** `RefImageSize` 参数的取值（H3 节点只认这两个）。 */
    const TCHAR* const RefSizeMatch = TEXT("match");
    const TCHAR* const RefSizeMax = TEXT("max");
}
