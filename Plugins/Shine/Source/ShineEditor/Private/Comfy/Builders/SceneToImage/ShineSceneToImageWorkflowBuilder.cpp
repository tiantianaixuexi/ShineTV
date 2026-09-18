#include "Comfy/Builders/SceneToImage/ShineSceneToImageWorkflowBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    /**
     * 节点 id 分配器：顺序发号。
     *
     * 与 DirectorNode.json 那支导出的 id 不同（那边是按模板顺序分配的），但这不影响对拍：
     * `DiffProbeGraph.py` 比的是"图的形状与接线"（节点指纹 + 连线指纹），不是 id。
     */
    class FNodeIdPool
    {
    public:
        FString Take()
        {
            return FString::FromInt(++NextId);
        }

    private:
        int32 NextId = 0;
    };

    /** 往 API 图里塞节点，并顺手记账（节点 id -> class_type，用来翻译进度）。 */
    class FGraphWriter
    {
    public:
        TSharedPtr<FJsonObject> AddNode(const FString& NodeId, const FString& ClassType)
        {
            TSharedPtr<FJsonObject> Inputs = MakeShared<FJsonObject>();
            TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
            Node->SetStringField(TEXT("class_type"), ClassType);
            Node->SetObjectField(TEXT("inputs"), Inputs);
            Root->SetObjectField(NodeId, Node);
            ClassTypes.Add(NodeId, ClassType);
            return Inputs;
        }

        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        TMap<FString, FString> ClassTypes;
    };

    /** 输出连线：[节点 id, 输出槽]。ComfyUI 的 API 图里连线就是这个二元数组。 */
    TArray<TSharedPtr<FJsonValue>> MakeLink(const FString& NodeId, int32 OutputSlot)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(2);
        Values.Add(MakeShared<FJsonValueString>(NodeId));
        Values.Add(MakeShared<FJsonValueNumber>(static_cast<double>(OutputSlot)));
        return Values;
    }
}

FString FShineSceneToImageWorkflowBuilder::FBuildResult::ToJsonString() const
{
    FString JsonText;
    if (!Prompt.IsValid())
    {
        return JsonText;
    }

    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
    FJsonSerializer::Serialize(Prompt.ToSharedRef(), Writer);
    return JsonText;
}

int32 FShineSceneToImageWorkflowBuilder::AlignToMultiple(int32 Value, int32 Multiple)
{
    if (Multiple <= 0 || Value <= 0)
    {
        return Value;
    }

    return ((Value + Multiple - 1) / Multiple) * Multiple;
}

FShineSceneToImageWorkflowBuilder::FBuildResult FShineSceneToImageWorkflowBuilder::Build(
    const FShineSceneToImageRequest& Request)
{
    FBuildResult Result;

    const FString ColorImage = Request.ColorImageName.TrimStartAndEnd();
    if (ColorImage.IsEmpty())
    {
        Result.ErrorMessage = TEXT("没有颜色参考图：SD1.5 这条路线是 img2img，必须给一张底图。");
        return Result;
    }

    if (Request.Checkpoint.TrimStartAndEnd().IsEmpty())
    {
        Result.ErrorMessage = TEXT("没填 Checkpoint（SD1.5 底座文件名）。");
        return Result;
    }

    if (Request.Steps <= 0)
    {
        Result.ErrorMessage = TEXT("采样步数必须大于 0。");
        return Result;
    }

    const int32 AlignedWidth = AlignToMultiple(Request.Width);
    const int32 AlignedHeight = AlignToMultiple(Request.Height);
    if (AlignedWidth != Request.Width || AlignedHeight != Request.Height)
    {
        Result.Warnings.Add(FString::Printf(
            TEXT("分辨率 %dx%d 不是 %d 的倍数，已对齐到 %dx%d（SD1.5 只在这些桶上出好图）。"),
            Request.Width, Request.Height, SizeMultiple, AlignedWidth, AlignedHeight));
    }

    if (AlignedWidth <= 0 || AlignedHeight <= 0)
    {
        Result.ErrorMessage = FString::Printf(TEXT("分辨率不合法：%dx%d。"), Request.Width, Request.Height);
        return Result;
    }

    const FString DepthImage = Request.DepthImageName.TrimStartAndEnd();
    const FString NormalImage = Request.NormalImageName.TrimStartAndEnd();
    const bool bUseDepth = !DepthImage.IsEmpty();
    const bool bUseNormal = !NormalImage.IsEmpty();

    if (!bUseDepth && !bUseNormal)
    {
        Result.Warnings.Add(TEXT("没有深度 / 法线图：本次不接 ControlNet，"
            "画面结构只靠颜色图的 img2img 重绘强度（Denoise）维持。"));
    }
    else if (!bUseDepth || !bUseNormal)
    {
        Result.Warnings.Add(FString::Printf(
            TEXT("只接了%s一支 ControlNet（另一支的图名为空）：结构引导会比双 ControlNet 弱。"),
            bUseDepth ? TEXT("深度") : TEXT("法线")));
    }

    FGraphWriter Graph;
    FNodeIdPool Ids;

    // ---------------------------------------------------------------- 模型与提示词

    const FString CheckpointId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(CheckpointId, TEXT("CheckpointLoaderSimple"));
        Inputs->SetStringField(TEXT("ckpt_name"), Request.Checkpoint.TrimStartAndEnd());
    }

    const FString PositiveId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(PositiveId, TEXT("CLIPTextEncode"));
        Inputs->SetStringField(TEXT("text"), Request.PositivePrompt);
        Inputs->SetArrayField(TEXT("clip"), MakeLink(CheckpointId, 1));
    }

    const FString NegativeId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(NegativeId, TEXT("CLIPTextEncode"));
        Inputs->SetStringField(TEXT("text"), Request.NegativePrompt);
        Inputs->SetArrayField(TEXT("clip"), MakeLink(CheckpointId, 1));
    }

    // ---------------------------------------------------------------- 颜色图 → latent

    const FString ColorId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ColorId, TEXT("LoadImage"));
        Inputs->SetStringField(TEXT("image"), ColorImage);
    }

    // 颜色图必须显式缩到目标尺寸：ControlNet 那两支吃的是原始图（它们内部自己缩），
    // 只有 latent 这一路要靠 ImageScale 对齐，否则 VAEEncode 出来的尺寸和采样目标不一致。
    const FString ScaledId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ScaledId, TEXT("ImageScale"));
        Inputs->SetArrayField(TEXT("image"), MakeLink(ColorId, 0));
        Inputs->SetStringField(TEXT("upscale_method"), TEXT("lanczos"));
        Inputs->SetNumberField(TEXT("width"), AlignedWidth);
        Inputs->SetNumberField(TEXT("height"), AlignedHeight);
        Inputs->SetStringField(TEXT("crop"), TEXT("disabled"));
    }

    const FString EncodeId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(EncodeId, TEXT("VAEEncode"));
        Inputs->SetArrayField(TEXT("pixels"), MakeLink(ScaledId, 0));
        Inputs->SetArrayField(TEXT("vae"), MakeLink(CheckpointId, 2));
    }

    // ---------------------------------------------------------------- ControlNet（可选）

    // conditioning 是"串起来"的：第一支 ControlNet 的 positive/negative 来自 CLIPTextEncode，
    // 第二支再接第一支的两个输出。少一支时另一支直接接 CLIPTextEncode，与 Director 模板一致。
    FString PositiveConditioning = PositiveId;
    int32 PositiveConditioningSlot = 0;
    FString NegativeConditioning = NegativeId;
    int32 NegativeConditioningSlot = 0;

    auto ApplyControlNet = [&](const FString& ImageName, const FString& ControlNetName, double Strength)
    {
        const FString ControlImageId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ControlImageId, TEXT("LoadImage"));
            Inputs->SetStringField(TEXT("image"), ImageName);
        }

        const FString ControlNetLoaderId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ControlNetLoaderId, TEXT("ControlNetLoader"));
            Inputs->SetStringField(TEXT("control_net_name"), ControlNetName.TrimStartAndEnd());
        }

        const FString ApplyId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ApplyId, TEXT("ControlNetApplyAdvanced"));
            Inputs->SetArrayField(TEXT("positive"), MakeLink(PositiveConditioning, PositiveConditioningSlot));
            Inputs->SetArrayField(TEXT("negative"), MakeLink(NegativeConditioning, NegativeConditioningSlot));
            Inputs->SetArrayField(TEXT("control_net"), MakeLink(ControlNetLoaderId, 0));
            Inputs->SetArrayField(TEXT("image"), MakeLink(ControlImageId, 0));
            Inputs->SetNumberField(TEXT("strength"), Strength);
            Inputs->SetNumberField(TEXT("start_percent"), 0.0);
            Inputs->SetNumberField(TEXT("end_percent"), Request.ControlEndPercent);
        }

        // 这一支成了新的 conditioning 源：positive 是 0 号输出，negative 是 1 号。
        PositiveConditioning = ApplyId;
        PositiveConditioningSlot = 0;
        NegativeConditioning = ApplyId;
        NegativeConditioningSlot = 1;
    };

    if (bUseDepth)
    {
        ApplyControlNet(DepthImage, Request.DepthControlNet, Request.DepthStrength);
    }

    if (bUseNormal)
    {
        ApplyControlNet(NormalImage, Request.NormalControlNet, Request.NormalStrength);
    }

    // ---------------------------------------------------------------- 采样 → 解码 → 落盘

    const FString SamplerId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(SamplerId, TEXT("KSampler"));
        Inputs->SetArrayField(TEXT("model"), MakeLink(CheckpointId, 0));
        Inputs->SetNumberField(TEXT("seed"), Request.Seed);
        Inputs->SetNumberField(TEXT("steps"), Request.Steps);
        Inputs->SetNumberField(TEXT("cfg"), Request.Cfg);
        Inputs->SetStringField(TEXT("sampler_name"), Request.SamplerName);
        Inputs->SetStringField(TEXT("scheduler"), Request.Scheduler);
        Inputs->SetArrayField(TEXT("positive"), MakeLink(PositiveConditioning, PositiveConditioningSlot));
        Inputs->SetArrayField(TEXT("negative"), MakeLink(NegativeConditioning, NegativeConditioningSlot));
        Inputs->SetArrayField(TEXT("latent_image"), MakeLink(EncodeId, 0));
        Inputs->SetNumberField(TEXT("denoise"), Request.Denoise);
    }

    const FString DecodeId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(DecodeId, TEXT("VAEDecode"));
        Inputs->SetArrayField(TEXT("samples"), MakeLink(SamplerId, 0));
        Inputs->SetArrayField(TEXT("vae"), MakeLink(CheckpointId, 2));
    }

    const FString SaveImageId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(SaveImageId, TEXT("SaveImage"));
        Inputs->SetArrayField(TEXT("images"), MakeLink(DecodeId, 0));
        Inputs->SetStringField(TEXT("filename_prefix"), Request.OutputPrefix);
    }

    // PreviewImage 保留：它在 ComfyUI 网页端会立刻显示结果，且 Director 模板里也有它
    // （少了它对拍就会多出一条差异）。回读时靠 SaveImageNodeId 把它的产物排除掉。
    const FString PreviewId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(PreviewId, TEXT("PreviewImage"));
        Inputs->SetArrayField(TEXT("images"), MakeLink(DecodeId, 0));
    }

    Result.Prompt = Graph.Root;
    Result.NodeClassTypes = MoveTemp(Graph.ClassTypes);
    Result.SamplerNodeId = SamplerId;
    Result.SaveImageNodeId = SaveImageId;
    Result.bSuccess = true;
    return Result;
}
