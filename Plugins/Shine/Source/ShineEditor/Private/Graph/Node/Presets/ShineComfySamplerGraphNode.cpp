#include "Graph/Node/Presets/ShineComfySamplerGraphNode.h"

#include "Comfy/ShineComfyTypes.h"
#include "Graph/Slate/Presets/SShineComfySamplerNode.h"

namespace ShineComfySamplerNode
{
    const FName Preset(TEXT("Sampler"));
    const FName ConditioningPin(TEXT("Conditioning"));
    const FName NegativePin(TEXT("Negative"));
    const FName LatentPin(TEXT("Latent"));
    const FName SeedPin(TEXT("Seed"));
    const FName ConditioningCategory(TEXT("Shine.Conditioning"));
    const FName LatentCategory(TEXT("Shine.Latent"));
    const FName IntegerCategory(TEXT("Shine.Integer"));
    const FName CheckpointParameter(TEXT("Checkpoint"));
    const FName WidthParameter(TEXT("Width"));
    const FName HeightParameter(TEXT("Height"));
    const FName SamplerNameParameter(TEXT("SamplerName"));
    const FName SchedulerParameter(TEXT("Scheduler"));
    const FName DenoiseParameter(TEXT("Denoise"));

    FShineComfyNodeParameter MakeTextChoiceParameter(const FName& Name, const TCHAR* Label, const TCHAR* DefaultValue, TArray<FString> Options)
    {
        FShineComfyNodeParameter Parameter;
        Parameter.Name = Name;
        Parameter.Label = FText::FromString(Label);
        Parameter.Type = EShineComfyParameterType::Text;
        Parameter.StringValue = DefaultValue;
        Parameter.StringOptions = MoveTemp(Options);
        return Parameter;
    }
}

UShineComfySamplerGraphNode::UShineComfySamplerGraphNode()
{
    SetPresetName(ShineComfySamplerNode::Preset);
    SetNodePresentation(
        FText::FromString(TEXT("KSampler")),
        FText::FromString(TEXT("Denoise latent with prompt conditioning")),
        FLinearColor(0.94f, 0.47f, 0.17f, 1.0f));

    ResetPresetParameters();
    Parameters.Add(ShineComfySamplerNode::MakeTextChoiceParameter(ShineComfySamplerNode::CheckpointParameter, TEXT("Checkpoint"), TEXT(""), TArray<FString>()));
    AddIntegerParameter(ShineComfySamplerNode::WidthParameter, TEXT("Width"), 1024);
    AddIntegerParameter(ShineComfySamplerNode::HeightParameter, TEXT("Height"), 1024);
    AddIntegerParameter(TEXT("Steps"), TEXT("Steps"), 20);
    AddFloatParameter(TEXT("CfgScale"), TEXT("CFG Scale"), 7.0);
    AddIntegerParameter(TEXT("Seed"), TEXT("Seed"), 42);
    Parameters.Add(ShineComfySamplerNode::MakeTextChoiceParameter(ShineComfySamplerNode::SamplerNameParameter, TEXT("Sampler"), TEXT("euler"), TArray<FString>{ TEXT("euler"), TEXT("euler_ancestral"), TEXT("dpmpp_2m"), TEXT("dpmpp_sde"), TEXT("uni_pc") }));
    Parameters.Add(ShineComfySamplerNode::MakeTextChoiceParameter(ShineComfySamplerNode::SchedulerParameter, TEXT("Scheduler"), TEXT("normal"), TArray<FString>{ TEXT("normal"), TEXT("karras"), TEXT("exponential"), TEXT("sgm_uniform") }));
    AddFloatParameter(ShineComfySamplerNode::DenoiseParameter, TEXT("Denoise"), 1.0);
    AddBoolParameter(TEXT("UseFixedSeed"), TEXT("Use Fixed Seed"), true);
}

TSharedPtr<SGraphNode> UShineComfySamplerGraphNode::CreateVisualWidget()
{
    return SNew(SShineComfySamplerNode, this);
}

void UShineComfySamplerGraphNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ShineComfySamplerNode::ConditioningCategory, ShineComfySamplerNode::ConditioningPin);
    CreateNamedPin(EGPD_Input, ShineComfySamplerNode::ConditioningCategory, ShineComfySamplerNode::NegativePin);
    CreateNamedPin(EGPD_Input, ShineComfySamplerNode::IntegerCategory, ShineComfySamplerNode::SeedPin);
    CreateNamedPin(EGPD_Output, ShineComfySamplerNode::LatentCategory, ShineComfySamplerNode::LatentPin);
}

void UShineComfySamplerGraphNode::ApplyModelLibrary(const TArray<FShineComfyModelFolder>& ModelFolders)
{
    FShineComfyNodeParameter* CheckpointParameter = Parameters.FindByPredicate([](const FShineComfyNodeParameter& Parameter)
    {
        return Parameter.Name == ShineComfySamplerNode::CheckpointParameter;
    });
    if (!CheckpointParameter)
    {
        return;
    }

    const FShineComfyModelFolder* CheckpointFolder = ModelFolders.FindByPredicate([](const FShineComfyModelFolder& Folder)
    {
        return Folder.FolderName.Equals(TEXT("checkpoints"), ESearchCase::IgnoreCase);
    });
    if (!CheckpointFolder)
    {
        return;
    }

    CheckpointParameter->StringOptions = CheckpointFolder->ModelNames;
    if (CheckpointParameter->StringOptions.Num() > 0 && !CheckpointParameter->StringOptions.Contains(CheckpointParameter->StringValue))
    {
        CheckpointParameter->StringValue = CheckpointParameter->StringOptions[0];
        this->RefreshNodeAfterParameterChange();
    }
}