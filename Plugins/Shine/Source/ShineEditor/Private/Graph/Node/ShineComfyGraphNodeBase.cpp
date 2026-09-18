#include "Graph/Node/ShineComfyGraphNodeBase.h"

#include "Graph/ShineComfyGraphSchema.h"
#include "Graph/Slate/SShineComfyGraphStandardNode.h"
#include "ScopedTransaction.h"

namespace
{
    /**
     * 改参数要进撤销栈。
     *
     * 引擎自带的撤销只覆盖"连线与增删节点"：改一个提示词、拖一个种子是撤不回来的，
     * 而这恰恰是最容易手滑、也最不想重打一遍的操作。事务名带上节点标题与参数名，
     * 撤销列表里才认得出"刚才那一下改了什么"。
     *
     * 每个 Set* 里都要就地构造 FScopedTransaction（它不可拷贝），所以只共用这个描述文本。
     */
    FText MakeParameterTransactionName(const UShineComfyGraphNodeBase& Node, FName ParameterName)
    {
        return FText::Format(
            NSLOCTEXT("ShineComfyGraphNode", "SetParameter", "修改 {0} 的 {1}"),
            Node.GetNodeTitle(ENodeTitleType::ListView), FText::FromName(ParameterName));
    }

    /**
     * 只有会被保存下来的资产才值得进撤销栈。
     *
     * 对拍用的临时图（`Shine.H3.BuildStoryboardProbe` 造的那颗导演台节点）也是走这同一个
     * `Set*`，它一次要灌 20 个参数——不挡住的话撤销列表里会多出 20 条"什么都没改"的记录。
     */
    bool ShouldRecordTransaction(const UObject& Object)
    {
        const UObject* Outermost = Object.GetOutermost();
        return Outermost && !Outermost->HasAnyFlags(RF_Transient);
    }
}

UShineComfyGraphNodeBase::UShineComfyGraphNodeBase()
    : AccentColor(FLinearColor::Gray)
{
}


FName UShineComfyGraphNodeBase::GetNodePreset() const
{
    return NAME_None;
}

FText UShineComfyGraphNodeBase::GetNodeSubtitle() const
{
    return NodeSubtitle;
}

const TArray<FShineComfyNodeParameter>& UShineComfyGraphNodeBase::GetParameters() const
{
    return Parameters;
}

const FShineComfyNodeParameter* UShineComfyGraphNodeBase::FindParameter(FName ParameterName) const
{
    return Parameters.FindByPredicate([ParameterName](const FShineComfyNodeParameter& Parameter)
    {
        return Parameter.Name == ParameterName;
    });
}

bool UShineComfyGraphNodeBase::SetTextParameter(FName ParameterName, const FString& NewValue)
{
    FShineComfyNodeParameter* Parameter = FindParameterMutable(ParameterName);
    if (!Parameter || Parameter->Type != EShineComfyParameterType::Text)
    {
        return false;
    }

    const FScopedTransaction Transaction(MakeParameterTransactionName(*this, ParameterName), ShouldRecordTransaction(*this));
    Modify();

    Parameter->StringValue = NewValue;
    NotifyNodeStateChanged(ShouldRefreshGraphOnParameterChanged(*Parameter));
    return true;
}

bool UShineComfyGraphNodeBase::SetFloatParameter(FName ParameterName, double NewValue)
{
    FShineComfyNodeParameter* Parameter = FindParameterMutable(ParameterName);
    if (!Parameter || Parameter->Type != EShineComfyParameterType::Float)
    {
        return false;
    }

    const FScopedTransaction Transaction(MakeParameterTransactionName(*this, ParameterName), ShouldRecordTransaction(*this));
    Modify();

    Parameter->FloatValue = NewValue;
    NotifyNodeStateChanged(ShouldRefreshGraphOnParameterChanged(*Parameter));
    return true;
}

bool UShineComfyGraphNodeBase::SetIntegerParameter(FName ParameterName, int32 NewValue)
{
    FShineComfyNodeParameter* Parameter = FindParameterMutable(ParameterName);
    if (!Parameter || Parameter->Type != EShineComfyParameterType::Integer)
    {
        return false;
    }

    const FScopedTransaction Transaction(MakeParameterTransactionName(*this, ParameterName), ShouldRecordTransaction(*this));
    Modify();

    Parameter->IntValue = NewValue;
    NotifyNodeStateChanged(ShouldRefreshGraphOnParameterChanged(*Parameter));
    return true;
}

bool UShineComfyGraphNodeBase::SetBoolParameter(FName ParameterName, bool bNewValue)
{
    FShineComfyNodeParameter* Parameter = FindParameterMutable(ParameterName);
    if (!Parameter || Parameter->Type != EShineComfyParameterType::Boolean)
    {
        return false;
    }

    const FScopedTransaction Transaction(MakeParameterTransactionName(*this, ParameterName), ShouldRecordTransaction(*this));
    Modify();

    Parameter->bBoolValue = bNewValue;
    NotifyNodeStateChanged(ShouldRefreshGraphOnParameterChanged(*Parameter));
    return true;
}

void UShineComfyGraphNodeBase::RefreshNodeAfterParameterChange()
{
    NotifyNodeStateChanged(true);
}

void UShineComfyGraphNodeBase::SetResultImagePaths(const TArray<FString>& InPaths)
{
    ResultImagePaths = InPaths;
    // 显示节点靠这个通知重建节点身体里的缩略图。
    NotifyNodeStateChanged(true);
}

void UShineComfyGraphNodeBase::AllocateDefaultPins()
{
    BuildNodePins();
}

FText UShineComfyGraphNodeBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return NodeTitle;
}

FLinearColor UShineComfyGraphNodeBase::GetNodeTitleColor() const
{
    return AccentColor;
}

FText UShineComfyGraphNodeBase::GetTooltipText() const
{
    return NodeSubtitle;
}

bool UShineComfyGraphNodeBase::CanUserDeleteNode() const
{
    return true;
}

bool UShineComfyGraphNodeBase::CanDuplicateNode() const
{
    return true;
}

bool UShineComfyGraphNodeBase::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const
{
    return DesiredSchema && DesiredSchema->IsA<UShineComfyGraphSchema>();
}

void UShineComfyGraphNodeBase::SetNodePresentation(const FText& InNodeTitle, const FText& InNodeSubtitle, const FLinearColor& InAccentColor)
{
    NodeTitle = InNodeTitle;
    NodeSubtitle = InNodeSubtitle;
    AccentColor = InAccentColor;
}

void UShineComfyGraphNodeBase::CreateNamedPin(EEdGraphPinDirection Direction, const FName& PinCategory, const FName& PinName)
{
    CreatePin(Direction, PinCategory, PinName);
}

void UShineComfyGraphNodeBase::NotifyNodeStateChanged(bool bRefreshGraph)
{
    Modify();

    if (bRefreshGraph)
    {
        if (UEdGraph* Graph = GetGraph())
        {
            Graph->NotifyGraphChanged();
        }
    }
}

TSharedPtr<SGraphNode> UShineComfyGraphNodeBase::CreateVisualWidget()
{
    return SNew(SShineComfyGraphStandardNode, this);
}

void UShineComfyGraphNodeBase::BuildNodePins()
{
}

bool UShineComfyGraphNodeBase::ShouldRefreshGraphOnParameterChanged(const FShineComfyNodeParameter& Parameter) const
{
    return Parameter.Type == EShineComfyParameterType::Text || Parameter.Type == EShineComfyParameterType::Boolean;
}

FShineComfyNodeParameter* UShineComfyGraphNodeBase::FindParameterMutable(FName ParameterName)
{
    return Parameters.FindByPredicate([ParameterName](const FShineComfyNodeParameter& Parameter)
    {
        return Parameter.Name == ParameterName;
    });
}