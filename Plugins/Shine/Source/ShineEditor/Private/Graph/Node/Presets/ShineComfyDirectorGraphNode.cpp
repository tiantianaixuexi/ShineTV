#include "Graph/Node/Presets/ShineComfyDirectorGraphNode.h"

#include "Comfy/Builders/Nodes/ShineComfyDirectorConfig.h"
#include "Graph/Slate/Presets/SShineComfyDirectorNode.h"

UShineComfyDirectorGraphNode::UShineComfyDirectorGraphNode()
{
    LoadConfig();
}

TSharedPtr<SGraphNode> UShineComfyDirectorGraphNode::CreateVisualWidget()
{
    return SNew(SShineComfyDirectorNode, this);
}

void UShineComfyDirectorGraphNode::BuildNodePins()
{
    const FShineComfyDirectorConfig& Config = FShineComfyDirectorConfig::Get();

    for (const FShineComfyDirectorPinDef& PinDef : Config.InputPins)
    {
        CreateNamedPin(EGPD_Input, FName(*PinDef.Category), FName(*PinDef.Name));
    }

    for (const FShineComfyDirectorPinDef& PinDef : Config.OutputPins)
    {
        CreateNamedPin(EGPD_Output, FName(*PinDef.Category), FName(*PinDef.Name));
    }
}

void UShineComfyDirectorGraphNode::PostLoad()
{
    Super::PostLoad();
    MergeConfigParameters();
}

void UShineComfyDirectorGraphNode::MergeConfigParameters()
{
    const FShineComfyDirectorConfig& Config = FShineComfyDirectorConfig::Get();
    if (!Config.bIsValid || Config.Parameters.Num() == 0)
    {
        return;
    }

    for (const FShineComfyNodeParameter& ConfigParam : Config.Parameters)
    {
        FShineComfyNodeParameter* Existing = Parameters.FindByPredicate(
            [&ConfigParam](const FShineComfyNodeParameter& Candidate)
            {
                return Candidate.Name == ConfigParam.Name;
            });

        if (!Existing)
        {
            // 配置里新增的参数（老资产没有）用配置默认值补上。
            Parameters.Add(ConfigParam);
            continue;
        }

        // 标签 / 类型 / 选项来源以配置为准，节点上的实际取值保持不变。
        if (!Existing->Label.EqualTo(ConfigParam.Label))
        {
            Existing->Label = ConfigParam.Label;
        }

        if (Existing->Type != ConfigParam.Type)
        {
            Existing->Type = ConfigParam.Type;
        }

        if (Existing->OptionsSourceName != ConfigParam.OptionsSourceName)
        {
            Existing->OptionsSourceName = ConfigParam.OptionsSourceName;
        }

        if (Existing->StringOptions.Num() == 0 && ConfigParam.StringOptions.Num() > 0)
        {
            Existing->StringOptions = ConfigParam.StringOptions;
        }
    }
}

void UShineComfyDirectorGraphNode::ApplyModelLibrary(const TArray<FShineComfyModelFolder>& ModelFolders)
{
    if (ModelFolders.Num() == 0 || Parameters.Num() == 0)
    {
        return;
    }

    bool bChanged = false;
    for (FShineComfyNodeParameter& Parameter : Parameters)
    {
        if (Parameter.OptionsSourceName.IsEmpty())
        {
            continue;
        }

        const FShineComfyModelFolder* Folder = ModelFolders.FindByPredicate(
            [&Parameter](const FShineComfyModelFolder& Candidate)
            {
                return Candidate.FolderName.Equals(Parameter.OptionsSourceName, ESearchCase::IgnoreCase);
            });

        if (!Folder || Folder->ModelNames.Num() == 0)
        {
            continue;
        }

        // 有选项就渲染成下拉框；当前值不在列表里时自动落到第一个可用模型，
        // 避免选了个已经删掉的模型却不知道。
        if (Parameter.StringOptions != Folder->ModelNames)
        {
            Parameter.StringOptions = Folder->ModelNames;
            bChanged = true;
        }

        if (!Parameter.StringOptions.Contains(Parameter.StringValue))
        {
            Parameter.StringValue = Parameter.StringOptions[0];
            bChanged = true;
        }
    }

    if (bChanged)
    {
        RefreshNodeAfterParameterChange();
    }
}

void UShineComfyDirectorGraphNode::LoadConfig()
{
    const FShineComfyDirectorConfig& Config = FShineComfyDirectorConfig::Get();

    SetPresetName(FName(*Config.Preset));
    SetNodePresentation(
        FText::FromString(Config.DisplayName),
        FText::FromString(Config.Description),
        Config.AccentColor);

    ResetPresetParameters();
    for (const FShineComfyNodeParameter& Param : Config.Parameters)
    {
        Parameters.Add(Param);
    }
}
