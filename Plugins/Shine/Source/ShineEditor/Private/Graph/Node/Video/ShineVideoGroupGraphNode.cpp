#include "Graph/Node/Video/ShineVideoGroupGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "FileMediaSource.h"
#include "Graph/ShineVideoGraphTypes.h"
#include "Graph/Slate/Video/SShineVideoGroupNode.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"

using namespace ShineVideoGroupNode;

namespace
{
    const TCHAR* const SegmentPrefix = TEXT("Segment");
    const TCHAR* const ChainPrefix = TEXT("Chain");
    const TCHAR* const OutputPrefix = TEXT("VideoOut");

    /** 统一解析 "<前缀><1 基数字>"，失败返回 INDEX_NONE。 */
    int32 ParseIndexedPinName(const FName& PinName, const TCHAR* Prefix)
    {
        const FString Name = PinName.ToString();
        if (!Name.StartsWith(Prefix))
        {
            return INDEX_NONE;
        }

        const FString Suffix = Name.RightChop(FCString::Strlen(Prefix));
        if (Suffix.IsEmpty() || !Suffix.IsNumeric())
        {
            return INDEX_NONE;
        }

        const int32 OneBased = FCString::Atoi(*Suffix);
        return (OneBased >= 1) ? OneBased - 1 : INDEX_NONE;
    }
}

UShineVideoGroupGraphNode::UShineVideoGroupGraphNode()
{
    SetNodePresentation(
        FText::FromString(TEXT("视频组")),
        FText::FromString(TEXT("#1..#N 按顺序出视频；把上一段的输出接回本段的「链」就是链式")),
        FLinearColor(0.13f, 0.61f, 0.58f, 1.0f));

    Parameters.Reset();

    AddTextParameter(TitleParameter, TEXT("名称"), TEXT("视频组"));
    AddTextParameter(PrefixParameter, TEXT("产物前缀（留空用项目默认）"), FString());
}

FText UShineVideoGroupGraphNode::GetVideoNodeKind() const
{
    return FText::FromString(TEXT("视频组"));
}

FName UShineVideoGroupGraphNode::GetNodePreset() const
{
    return Preset;
}

TSharedPtr<SGraphNode> UShineVideoGroupGraphNode::CreateVisualWidget()
{
    return SNew(SShineVideoGroupNode, this);
}

FName UShineVideoGroupGraphNode::MakeSegmentInputPinName(int32 SegmentIndex)
{
    return FName(*FString::Printf(TEXT("%s%d"), SegmentPrefix, SegmentIndex + 1));
}

FName UShineVideoGroupGraphNode::MakeChainInputPinName(int32 SegmentIndex)
{
    return FName(*FString::Printf(TEXT("%s%d"), ChainPrefix, SegmentIndex + 1));
}

FName UShineVideoGroupGraphNode::MakeSegmentOutputPinName(int32 SegmentIndex)
{
    return FName(*FString::Printf(TEXT("%s%d"), OutputPrefix, SegmentIndex + 1));
}

int32 UShineVideoGroupGraphNode::ParseSegmentInputPinIndex(const FName& PinName)
{
    return ParseIndexedPinName(PinName, SegmentPrefix);
}

int32 UShineVideoGroupGraphNode::ParseChainInputPinIndex(const FName& PinName)
{
    return ParseIndexedPinName(PinName, ChainPrefix);
}

int32 UShineVideoGroupGraphNode::ParseSegmentOutputPinIndex(const FName& PinName)
{
    return ParseIndexedPinName(PinName, OutputPrefix);
}

void UShineVideoGroupGraphNode::BuildNodePins()
{
    for (int32 SegmentIndex = 0; SegmentIndex < DefaultSegmentSlots; ++SegmentIndex)
    {
        CreateSegmentPins(SegmentIndex);
    }
}

void UShineVideoGroupGraphNode::CreateSegmentPins(int32 SegmentIndex)
{
    // 一个段的 pin 顺序：段输入 → （第 2 段起）链式输入 → 段输出。
    // 段输入必须排在同段其它 pin 前面，因为编译器取段序是"按 Segment 前缀的名字下标"，
    // 不依赖 Pins 数组的物理顺序，但人在画布上看到的第一根线应该就是"这一段用哪张图"。
    CreateNamedPin(EGPD_Input, ShineVideoPin::Image, MakeSegmentInputPinName(SegmentIndex));

    if (HasChainInput(SegmentIndex))
    {
        CreateNamedPin(EGPD_Input, ShineVideoPin::Video, MakeChainInputPinName(SegmentIndex));
    }

    CreateNamedPin(EGPD_Output, ShineVideoPin::Video, MakeSegmentOutputPinName(SegmentIndex));
}

bool UShineVideoGroupGraphNode::IsChainLink(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin)
{
    if (!OutputPin || !InputPin)
    {
        return false;
    }

    const UShineVideoGroupGraphNode* GroupNode = Cast<UShineVideoGroupGraphNode>(OutputPin->GetOwningNode());
    if (!GroupNode || GroupNode != InputPin->GetOwningNode())
    {
        return false;
    }

    const int32 OutputSegment = ParseSegmentOutputPinIndex(OutputPin->PinName);
    const int32 ChainSegment = ParseChainInputPinIndex(InputPin->PinName);
    if (OutputSegment == INDEX_NONE || ChainSegment == INDEX_NONE)
    {
        return false;
    }

    return ChainSegment == OutputSegment + 1;
}

void UShineVideoGroupGraphNode::GetSegmentInputPins(TArray<UEdGraphPin*>& OutPins) const
{
    const int32 SlotCount = GetSegmentSlotCount();
    OutPins.Reset(SlotCount);

    for (int32 SegmentIndex = 0; SegmentIndex < SlotCount; ++SegmentIndex)
    {
        OutPins.Add(FindPin(MakeSegmentInputPinName(SegmentIndex)));
    }
}

const UEdGraphPin* UShineVideoGroupGraphNode::GetChainInputPin(int32 SegmentIndex) const
{
    return HasChainInput(SegmentIndex) ? FindPin(MakeChainInputPinName(SegmentIndex)) : nullptr;
}

const UEdGraphPin* UShineVideoGroupGraphNode::GetSegmentOutputPin(int32 SegmentIndex) const
{
    return FindPin(MakeSegmentOutputPinName(SegmentIndex));
}

int32 UShineVideoGroupGraphNode::GetSegmentSlotCount() const
{
    // 槽位数以 pin 为准（它是唯一真相）：`Segment1`、`Segment2`… 连续编号，遇到第一个
    // 不存在的就停。用 pin 而不是一个 int 参数，是为了让"画布上看得见的槽位"
    // 和"编译器认的槽位"永远是同一个东西。
    int32 SlotCount = 0;
    while (SlotCount < MaxSegments && FindPin(MakeSegmentInputPinName(SlotCount)))
    {
        ++SlotCount;
    }

    return SlotCount;
}

bool UShineVideoGroupGraphNode::CanAddSegmentSlot() const
{
    return GetSegmentSlotCount() < MaxSegments;
}

bool UShineVideoGroupGraphNode::CanRemoveLastSegmentSlot() const
{
    const int32 SlotCount = GetSegmentSlotCount();
    if (SlotCount <= 1)
    {
        return false;
    }

    const UEdGraphPin* LastPin = FindPin(MakeSegmentInputPinName(SlotCount - 1));
    if (LastPin && LastPin->LinkedTo.Num() > 0)
    {
        return false;
    }

    const UEdGraphPin* LastChainPin = GetChainInputPin(SlotCount - 1);
    return !LastChainPin || LastChainPin->LinkedTo.Num() == 0;
}

bool UShineVideoGroupGraphNode::AddSegmentSlot()
{
    if (!CanAddSegmentSlot())
    {
        return false;
    }

    const int32 NewIndex = GetSegmentSlotCount();

    Modify();
    CreateSegmentPins(NewIndex);

    if (UEdGraph* Graph = GetGraph())
    {
        Graph->NotifyGraphChanged();
    }

    return true;
}

bool UShineVideoGroupGraphNode::RemoveLastSegmentSlot()
{
    if (!CanRemoveLastSegmentSlot())
    {
        return false;
    }

    const int32 LastIndex = GetSegmentSlotCount() - 1;

    Modify();

    // RemovePin 会顺带断开 pin 自己的连线；`CanRemoveLastSegmentSlot` 已经确认最后一段
    // 没有连线，所以这里不会把别人的线吃掉。
    if (UEdGraphPin* ChainPin = FindPin(MakeChainInputPinName(LastIndex)))
    {
        RemovePin(ChainPin);
    }

    if (UEdGraphPin* OutputPin = FindPin(MakeSegmentOutputPinName(LastIndex)))
    {
        RemovePin(OutputPin);
    }

    if (UEdGraphPin* SegmentPin = FindPin(MakeSegmentInputPinName(LastIndex)))
    {
        RemovePin(SegmentPin);
    }

    if (UEdGraph* Graph = GetGraph())
    {
        Graph->NotifyGraphChanged();
    }

    return true;
}

void UShineVideoGroupGraphNode::SetSegmentCaptions(const TArray<FString>& InCaptions)
{
    SegmentCaptions = InCaptions;
}

void UShineVideoGroupGraphNode::SetSegmentOutputFile(int32 SegmentIndex, const FString& InFile)
{
    if (SegmentIndex < 0)
    {
        return;
    }

    if (SegmentOutputFiles.Num() <= SegmentIndex)
    {
        SegmentOutputFiles.SetNum(SegmentIndex + 1);
    }

    SegmentOutputFiles[SegmentIndex] = InFile;
}

const FString& UShineVideoGroupGraphNode::GetSegmentCaption(int32 SegmentIndex) const
{
    static const FString Empty;
    return SegmentCaptions.IsValidIndex(SegmentIndex) ? SegmentCaptions[SegmentIndex] : Empty;
}

const FString& UShineVideoGroupGraphNode::GetSegmentOutputFile(int32 SegmentIndex) const
{
    static const FString Empty;
    return SegmentOutputFiles.IsValidIndex(SegmentIndex) ? SegmentOutputFiles[SegmentIndex] : Empty;
}

void UShineVideoGroupGraphNode::SetSegmentProgress(int32 SegmentIndex, EShineVideoNodeRuntimeState InState, int32 InStep, int32 InStepMax, const FString& InDetail)
{
    if (SegmentIndex < 0 || SegmentIndex >= MaxSegments)
    {
        return;
    }

    const int32 RequiredSize = SegmentIndex + 1;
    if (SegmentStates.Num() < RequiredSize)
    {
        SegmentStates.SetNum(RequiredSize);
        SegmentStepValues.SetNum(RequiredSize);
        SegmentStepMaxes.SetNum(RequiredSize);
    }

    SegmentStates[SegmentIndex] = InState;
    SegmentStepValues[SegmentIndex] = InStep;
    SegmentStepMaxes[SegmentIndex] = InStepMax;

    if (!InDetail.IsEmpty())
    {
        // 段详情只在出错时值得留在画布上（正常时的"25/25"看步进就够了）。
        if (InState == EShineVideoNodeRuntimeState::Failed)
        {
            SetRuntimeState(InState, InDetail, InStep, InStepMax);
        }
    }
}

EShineVideoNodeRuntimeState UShineVideoGroupGraphNode::GetSegmentState(int32 SegmentIndex) const
{
    return SegmentStates.IsValidIndex(SegmentIndex) ? SegmentStates[SegmentIndex] : EShineVideoNodeRuntimeState::Idle;
}

void UShineVideoGroupGraphNode::GetSegmentStep(int32 SegmentIndex, int32& OutStep, int32& OutStepMax) const
{
    OutStep = SegmentStepValues.IsValidIndex(SegmentIndex) ? SegmentStepValues[SegmentIndex] : 0;
    OutStepMax = SegmentStepMaxes.IsValidIndex(SegmentIndex) ? SegmentStepMaxes[SegmentIndex] : 0;
}

UMediaPlayer* UShineVideoGroupGraphNode::EnsureSegmentPlayer(int32 SegmentIndex)
{
    if (SegmentIndex < 0 || SegmentIndex >= MaxSegments)
    {
        return nullptr;
    }

    const FString File = GetSegmentOutputFile(SegmentIndex);
    if (File.IsEmpty())
    {
        return nullptr;
    }

    const FString Absolute = FPaths::ConvertRelativePathToFull(File);

    // 已经有播放器、而且还是同一个文件：直接复用（复用才谈得上"重建控件不打断播放"）。
    if (SegmentPlayers.IsValidIndex(SegmentIndex) && SegmentPlayers[SegmentIndex]
        && SegmentPlayerPaths.IsValidIndex(SegmentIndex) && SegmentPlayerPaths[SegmentIndex] == Absolute)
    {
        return SegmentPlayers[SegmentIndex];
    }

    if (!FPaths::FileExists(Absolute))
    {
        return nullptr;
    }

    UMediaPlayer* Player = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
    UMediaTexture* Texture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);
    UFileMediaSource* Source = NewObject<UFileMediaSource>(this, NAME_None, RF_Transient);
    if (!Player || !Texture || !Source)
    {
        return nullptr;
    }

    Player->SetLooping(false);
    Source->SetFilePath(Absolute);

    Texture->SetMediaPlayer(Player);
    // SetMediaPlayer 之后必须 UpdateResource，否则贴图是空的（UE 的固定要求）。
    Texture->UpdateResource();

    if (!Player->OpenSource(Source))
    {
        return nullptr;
    }

    const int32 RequiredSize = SegmentIndex + 1;
    if (SegmentPlayers.Num() < RequiredSize)
    {
        SegmentPlayers.SetNum(RequiredSize);
        SegmentTextures.SetNum(RequiredSize);
        SegmentSources.SetNum(RequiredSize);
        SegmentPlayerPaths.SetNum(RequiredSize);
    }

    SegmentPlayers[SegmentIndex] = Player;
    SegmentTextures[SegmentIndex] = Texture;
    SegmentSources[SegmentIndex] = Source;
    SegmentPlayerPaths[SegmentIndex] = Absolute;

    return Player;
}

UMediaTexture* UShineVideoGroupGraphNode::GetSegmentTexture(int32 SegmentIndex) const
{
    return SegmentTextures.IsValidIndex(SegmentIndex) ? SegmentTextures[SegmentIndex] : nullptr;
}

bool UShineVideoGroupGraphNode::IsSegmentPlaying(int32 SegmentIndex) const
{
    return SegmentPlayers.IsValidIndex(SegmentIndex)
        && SegmentPlayers[SegmentIndex]
        && SegmentPlayers[SegmentIndex]->IsPlaying();
}

void UShineVideoGroupGraphNode::ToggleSegmentPlayback(int32 SegmentIndex)
{
    UMediaPlayer* Player = EnsureSegmentPlayer(SegmentIndex);
    if (!Player)
    {
        return;
    }

    if (Player->IsPlaying())
    {
        Player->Pause();
        return;
    }

    // 播完之后再点一次应该从头来，而不是"从结尾继续"（那样看起来毫无反应）。
    if (Player->GetDuration().IsZero() == false
        && Player->GetTime() >= Player->GetDuration() - FTimespan::FromMilliseconds(50))
    {
        Player->Seek(FTimespan::Zero());
    }

    Player->Play();
}

void UShineVideoGroupGraphNode::ReleaseSegmentPlayers()
{
    for (TObjectPtr<UMediaPlayer>& Player : SegmentPlayers)
    {
        if (Player)
        {
            Player->Close();
        }
    }

    SegmentPlayers.Reset();
    SegmentTextures.Reset();
    SegmentSources.Reset();
    SegmentPlayerPaths.Reset();
}
