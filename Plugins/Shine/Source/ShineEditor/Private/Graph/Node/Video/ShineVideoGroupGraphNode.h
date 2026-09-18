#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "ShineVideoGroupGraphNode.generated.h"

/**
 * 视频组节点：一次提交 N 段，节点身上直接播 mp4。
 *
 * ## 三条语义都落在这个节点的 pin 上，不藏在代码里
 *
 * 1. `#1..#N` 的**连接顺序就是段序**（不是节点内部的一个列表），
 *    所以拖动节点位置不会改变段序，改顺序只能改连线。
 * 2. **链式是一根线**，不是勾选框：把 `VideoOut(i-1)` 接回本节点的 `Chain(i)`，
 *    就是"第 i 段接第 i-1 段的末帧"。这条自环是 schema 里唯一放行的同节点连线
 *    （见 `UShineVideoGraphSchema::CanCreateConnection`）。
 * 3. 节点身体里每一段都显示自己的状态与步进（由 `FShineVideoGraphCompiler::ApplyTaskStatus`
 *    从 runner 的 `FShineVideoTaskStatus::Shots` 回填），所以"跑到第几段、第几步"是看得见
 *    N 个独立进度，而不是一根总进度条。
 *
 * ## 段槽位为什么用按钮增删，而不是连线时自动长
 *
 * 自动增长要在 `PinConnectionListChanged` 里改 pin 表，而那个回调正处在引擎遍历 pin 的
 * 循环里，同时 `NotifyGraphChanged()` 会**立刻**销毁并重建所有节点控件——拖线拖到一半
 * 把控件拆掉是崩溃的经典写法。按钮点击是普通的 Slate 事件，在这个时机增删 pin 是安全的。
 */
UCLASS()
class UShineVideoGroupGraphNode : public UShineVideoGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineVideoGroupGraphNode();

    virtual FText GetVideoNodeKind() const override;
    virtual FName GetNodePreset() const override;
    virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

    /** 一个项目里的段数上限（再多就该拆成多个视频组了）。 */
    static constexpr int32 MaxSegments = 16;

    /** 新建时的默认槽位数：够一个"单段"或"两段链式"直接用。 */
    static constexpr int32 DefaultSegmentSlots = 2;

    /** 段下标（0 基）→ pin 名。 */
    static FName MakeSegmentInputPinName(int32 SegmentIndex);
    static FName MakeChainInputPinName(int32 SegmentIndex);
    static FName MakeSegmentOutputPinName(int32 SegmentIndex);

    static int32 ParseSegmentInputPinIndex(const FName& PinName);
    static int32 ParseChainInputPinIndex(const FName& PinName);
    static int32 ParseSegmentOutputPinIndex(const FName& PinName);

    /**
     * 这根线是不是"链式"自环：本节点的 `VideoOut(k)` → 本节点的 `Chain(k+1)`。
     * schema 的 `CanCreateConnection` 和编译器都用它，规则只有这一处。
     */
    static bool IsChainLink(const class UEdGraphPin* OutputPin, const class UEdGraphPin* InputPin);

    /** 按段序取 `#i` 输入 pin（含未接线的空槽位，长度 = 槽位数）。 */
    void GetSegmentInputPins(TArray<class UEdGraphPin*>& OutPins) const;

    const class UEdGraphPin* GetChainInputPin(int32 SegmentIndex) const;
    const class UEdGraphPin* GetSegmentOutputPin(int32 SegmentIndex) const;

    /** 当前槽位数（节点身体里会为每一段画一行）。 */
    int32 GetSegmentSlotCount() const;

    /** 在某一段之后是否需要"链式"输入（第 1 段没有上一段，所以下标 0 没有）。 */
    static bool HasChainInput(int32 SegmentIndex) { return SegmentIndex >= 1; }

    /** 槽位增删（由节点上的按钮调用，不在连线回调里调用）。 */
    bool CanAddSegmentSlot() const;
    bool CanRemoveLastSegmentSlot() const;
    bool AddSegmentSlot();
    bool RemoveLastSegmentSlot();

    // ------------------------------------------------------------ 运行期显示（Transient）

    /**
     * 编译时把段标题与已有产物写进节点，节点身体据此显示"1. 开场  已完成  a.mp4"。
     * 不写进资产（Transient），因为它只是上一次编译/上一次任务的痕迹。
     */
    void SetSegmentCaptions(const TArray<FString>& InCaptions);
    void SetSegmentOutputFile(int32 SegmentIndex, const FString& InFile);

    const FString& GetSegmentCaption(int32 SegmentIndex) const;
    const FString& GetSegmentOutputFile(int32 SegmentIndex) const;

    /** 逐段进度回填（由 `ApplyTaskStatus` 调用）。 */
    void SetSegmentProgress(int32 SegmentIndex, EShineVideoNodeRuntimeState InState, int32 InStep, int32 InStepMax, const FString& InDetail);
    EShineVideoNodeRuntimeState GetSegmentState(int32 SegmentIndex) const;
    void GetSegmentStep(int32 SegmentIndex, int32& OutStep, int32& OutStepMax) const;

    // ------------------------------------------------------------ 段内播放
    //
    // 播放器归**节点**持有，不归节点控件持有：一次任务里节点控件会被反复重建
    // （`NotifyGraphChanged()` 会 PurgeVisualRepresentation），放在控件里每重建一次
    // 就把正在播的片子掐掉。

    /** 取（必要时创建）某一段的播放器；产物文件换了会重建。 */
    class UMediaPlayer* EnsureSegmentPlayer(int32 SegmentIndex);

    /** 某一段的贴图（没建过时返回 nullptr）。 */
    class UMediaTexture* GetSegmentTexture(int32 SegmentIndex) const;

    bool IsSegmentPlaying(int32 SegmentIndex) const;

    /** 播放 / 暂停切换（没有产物文件时什么都不做）。 */
    void ToggleSegmentPlayback(int32 SegmentIndex);

    /** 放掉全部解码器（节点被删、编辑器关闭时调）。 */
    void ReleaseSegmentPlayers();

protected:
    virtual void BuildNodePins() override;

private:
    /** 建一个段槽位需要的三个 pin（输入 / 链式输入 / 输出）。 */
    void CreateSegmentPins(int32 SegmentIndex);

    UPROPERTY(Transient)
    TArray<FString> SegmentCaptions;

    UPROPERTY(Transient)
    TArray<FString> SegmentOutputFiles;

    UPROPERTY(Transient)
    TArray<EShineVideoNodeRuntimeState> SegmentStates;

    UPROPERTY(Transient)
    TArray<int32> SegmentStepValues;

    UPROPERTY(Transient)
    TArray<int32> SegmentStepMaxes;

    /** 逐段的播放器 / 贴图 / 媒体源；下标与段槽位对齐。 */
    UPROPERTY(Transient)
    TArray<TObjectPtr<class UMediaPlayer>> SegmentPlayers;

    UPROPERTY(Transient)
    TArray<TObjectPtr<class UMediaTexture>> SegmentTextures;

    UPROPERTY(Transient)
    TArray<TObjectPtr<class UFileMediaSource>> SegmentSources;

    /** 建播放器时用的是哪个文件：产物文件换了就要重建。 */
    TArray<FString> SegmentPlayerPaths;
};

namespace ShineVideoGroupNode
{
    const FName Preset(TEXT("VideoGroup"));

    const FName TitleParameter(TEXT("Title"));
    const FName PrefixParameter(TEXT("Prefix"));
}
