#include "Graph/Node/Video/ShineVideoCharacterGraphNode.h"

#include "Asset/ShineCharacterAsset.h"
#include "Comfy/ShineMentionResolver.h"
#include "Graph/ShineVideoGraphTypes.h"
#include "Graph/Slate/Video/SShineVideoNode.h"

using namespace ShineVideoCharacterNode;

UShineVideoCharacterGraphNode::UShineVideoCharacterGraphNode()
{
    SetNodePresentation(
        FText::FromString(TEXT("角色")),
        FText::FromString(TEXT("引用一个角色资产：它的整份参考图 + 身份描述会一起进分镜")),
        FLinearColor(0.91f, 0.54f, 0.20f, 1.0f));

    Parameters.Reset();

    AddTextParameter(CharacterAssetParameter, TEXT("角色资产"), FString());
}

FText UShineVideoCharacterGraphNode::GetVideoNodeKind() const
{
    return FText::FromString(TEXT("角色"));
}

FName UShineVideoCharacterGraphNode::GetNodePreset() const
{
    return Preset;
}

TSharedPtr<SGraphNode> UShineVideoCharacterGraphNode::CreateVisualWidget()
{
    return SNew(SShineVideoNode, this);
}

void UShineVideoCharacterGraphNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ShineVideoPin::Character, OutputPin);
}

const UShineCharacterAsset* UShineVideoCharacterGraphNode::ResolveCharacterAsset() const
{
    const FShineComfyNodeParameter* Parameter = FindParameter(CharacterAssetParameter);
    if (!Parameter)
    {
        return nullptr;
    }

    const FString Identifier = Parameter->StringValue.TrimStartAndEnd();
    if (Identifier.IsEmpty())
    {
        return nullptr;
    }

    // 复用 P3 的解析器：它既认 "/Game/xxx" 这种对象路径，也认显示名/资产名。
    return FShineMentionResolver::FindCharacterAsset(Identifier, TArray<FString>());
}
