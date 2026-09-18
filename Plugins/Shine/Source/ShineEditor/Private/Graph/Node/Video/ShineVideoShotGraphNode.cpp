#include "Graph/Node/Video/ShineVideoShotGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Video/ShineVideoShotImageGraphNode.h"
#include "Graph/ShineVideoGraphTypes.h"
#include "Graph/Slate/Video/SShineVideoShotNode.h"

using namespace ShineVideoShotNode;

namespace
{
    /** 槽位 pin 的内部名。刻意不带空格：pin 名会进 Comfy 图的 JSON，名字里带空格不好查。 */
    const TCHAR* const PicturePinPrefix = TEXT("Picture");
}

UShineVideoShotGraphNode::UShineVideoShotGraphNode()
{
    SetNodePresentation(
        FText::FromString(TEXT("分镜")),
        FText::FromString(TEXT("提示词 + 采样参数；左侧槽位的连线顺序就是 <Picture N> 的编号顺序")),
        FLinearColor(0.24f, 0.60f, 0.86f, 1.0f));

    Parameters.Reset();

    // 参数顺序 = 节点上的显示顺序：先写提示词，再是驱动方式，最后才是采样细节。
    AddTextParameter(TitleParameter, TEXT("标题"), FString());
    AddTextParameter(PromptParameter, TEXT("提示词"), FString(), /*bMultiLine=*/true);
    AddOptionParameter(ModeParameter, TEXT("驱动方式"), ModeReference, { ModeReference, ModeFirstLastFrame });
    AddOptionParameter(RefImageSizeParameter, TEXT("参考图尺寸"), RefSizeMatch, { RefSizeMatch, RefSizeMax });
    AddIntegerParameter(WidthParameter, TEXT("宽 (32 的倍数)"), 1280);
    AddIntegerParameter(HeightParameter, TEXT("高 (32 的倍数)"), 704);
    AddIntegerParameter(LengthParameter, TEXT("帧数 (17k+5 对齐)"), 124);
    AddIntegerParameter(StepsParameter, TEXT("采样步数"), 25);
    AddFloatParameter(CfgParameter, TEXT("CFG（>1 会翻倍耗时）"), 4.0);
    AddIntegerParameter(SeedParameter, TEXT("随机种子"), 42);
    AddFloatParameter(DenoiseParameter, TEXT("Denoise"), 1.0);
    AddFloatParameter(ShiftVideoParameter, TEXT("ShiftVideo"), 12.0);
    AddFloatParameter(ShiftAudioParameter, TEXT("ShiftAudio"), 3.0);
}

FText UShineVideoShotGraphNode::GetVideoNodeKind() const
{
    return FText::FromString(TEXT("分镜"));
}

FName UShineVideoShotGraphNode::GetNodePreset() const
{
    return Preset;
}

TSharedPtr<SGraphNode> UShineVideoShotGraphNode::CreateVisualWidget()
{
    return SNew(SShineVideoShotNode, this);
}

FName UShineVideoShotGraphNode::MakePicturePinName(int32 SlotIndex)
{
    return FName(*FString::Printf(TEXT("%s%d"), PicturePinPrefix, SlotIndex + 1));
}

int32 UShineVideoShotGraphNode::ParsePicturePinIndex(const FName& PinName)
{
    const FString Name = PinName.ToString();
    if (!Name.StartsWith(PicturePinPrefix))
    {
        return INDEX_NONE;
    }

    const FString Suffix = Name.RightChop(FCString::Strlen(PicturePinPrefix));
    if (Suffix.IsEmpty() || !Suffix.IsNumeric())
    {
        return INDEX_NONE;
    }

    const int32 OneBasedSlot = FCString::Atoi(*Suffix);
    return (OneBasedSlot >= 1 && OneBasedSlot <= PictureSlotCount) ? OneBasedSlot - 1 : INDEX_NONE;
}

void UShineVideoShotGraphNode::BuildNodePins()
{
    // 输入顺序 = 节点上从上到下的顺序 = 槽位编号顺序。先是剧本，再是 9 个参考图槽位。
    CreateNamedPin(EGPD_Input, ShineVideoPin::Script, ScriptPin);

    for (int32 SlotIndex = 0; SlotIndex < PictureSlotCount; ++SlotIndex)
    {
        CreateNamedPin(EGPD_Input, ShineVideoPin::Picture, MakePicturePinName(SlotIndex));
    }

    CreateNamedPin(EGPD_Output, ShineVideoPin::Shot, OutputPin);
}

void UShineVideoShotGraphNode::GetPicturePins(TArray<UEdGraphPin*>& OutPins) const
{
    OutPins.Reset(PictureSlotCount);

    // 按**槽位下标**而不是 Pins 数组顺序返回：Pins 的顺序理论上就是 BuildNodePins 的顺序，
    // 但槽位编号必须由下标唯一决定，不能有第二种可能。
    for (int32 SlotIndex = 0; SlotIndex < PictureSlotCount; ++SlotIndex)
    {
        OutPins.Add(FindPin(MakePicturePinName(SlotIndex)));
    }
}

const UEdGraphPin* UShineVideoShotGraphNode::GetPicturePin(int32 SlotIndex) const
{
    if (SlotIndex < 0 || SlotIndex >= PictureSlotCount)
    {
        return nullptr;
    }

    return FindPin(MakePicturePinName(SlotIndex));
}

int32 UShineVideoShotGraphNode::GetPictureOrdinal(int32 SlotIndex) const
{
    if (SlotIndex < 0 || SlotIndex >= PictureSlotCount)
    {
        return INDEX_NONE;
    }

    const UEdGraphPin* ThisPin = GetPicturePin(SlotIndex);
    if (!ThisPin || ThisPin->LinkedTo.Num() == 0)
    {
        return INDEX_NONE;
    }

    int32 Ordinal = GetPictureNumberOffset();
    for (int32 CandidateIndex = 0; CandidateIndex <= SlotIndex; ++CandidateIndex)
    {
        const UEdGraphPin* CandidatePin = GetPicturePin(CandidateIndex);
        if (CandidatePin && CandidatePin->LinkedTo.Num() > 0)
        {
            ++Ordinal;
        }
    }

    return Ordinal;
}

bool UShineVideoShotGraphNode::IsConsumedByShotImage() const
{
    // 变量名不能叫 OutputPin：本文件有 `using namespace ShineVideoShotNode;`，
    // 那里的 `OutputPin` 是 pin 名常量，同名局部变量在 UE 的告警级别下会直接报错（C4459）。
    const UEdGraphPin* ShotOutputPin = FindPin(ShineVideoShotNode::OutputPin);
    if (!ShotOutputPin)
    {
        return false;
    }

    for (const UEdGraphPin* LinkedPin : ShotOutputPin->LinkedTo)
    {
        if (LinkedPin && Cast<UShineVideoShotImageGraphNode>(LinkedPin->GetOwningNode()))
        {
            return true;
        }
    }

    return false;
}

int32 UShineVideoShotGraphNode::GetPictureNumberOffset() const
{
    // 分镜图排在槽位之前，所以槽位编号要后移一位。同一个分镜被多个分镜图引用时也只后移一位
    // ——那几张分镜图本身是同一次生成的产物，只该占一个参考图位置（编译器取最近的那个）。
    return IsConsumedByShotImage() ? 1 : 0;
}

int32 UShineVideoShotGraphNode::GetLinkedPictureCount() const
{
    int32 Count = 0;
    for (int32 SlotIndex = 0; SlotIndex < PictureSlotCount; ++SlotIndex)
    {
        const UEdGraphPin* Pin = GetPicturePin(SlotIndex);
        if (Pin && Pin->LinkedTo.Num() > 0)
        {
            ++Count;
        }
    }

    return Count;
}

FString UShineVideoShotGraphNode::GetPromptText() const
{
    const FShineComfyNodeParameter* Parameter = FindParameter(PromptParameter);
    return Parameter ? Parameter->StringValue : FString();
}

FString UShineVideoShotGraphNode::GetShotTitle() const
{
    const FShineComfyNodeParameter* Parameter = FindParameter(TitleParameter);
    return Parameter ? Parameter->StringValue.TrimStartAndEnd() : FString();
}
