#include "Comfy/Builders/MiniMaxH3/ShineMiniMaxH3WorkflowBuilder.h"

#include "Asset/ShineVideoProject.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    /** H3 的 length 网格：向上对齐到 n % 17 == 5（comfy_extras/nodes_minimax_h3.py:33-36）。 */
    constexpr int32 FrameGridStride = 17;
    constexpr int32 FrameGridOffset = 5;

    /** H3 的宽高必须是 32 的倍数。 */
    constexpr int32 SizeMultiple = 32;

    /** 节点 id 分配器：顺序发号，同一份项目每次编译出的 id 完全一致，便于对拍与缓存。 */
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

    /** 往 API 图里塞节点，并顺手记账（节点 id -> class_type，面板要拿它翻译进度）。 */
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

    /** 参考图键名是【带父前缀的 dotted 路径】：ref_images.ref_image_0（见 H3-SPEC.md 坑 2）。 */
    FString MakeReferenceImageKey(int32 Index)
    {
        return FString::Printf(TEXT("ref_images.ref_image_%d"), Index);
    }

    /** SaveVideo 的产物前缀：项目前缀 + 分镜序号，便于回读时按段认领。 */
    FString MakeShotFilenamePrefix(const FString& ProjectPrefix, int32 ShotIndex)
    {
        const FString Prefix = ProjectPrefix.IsEmpty() ? TEXT("Shine/H3") : ProjectPrefix;
        return FString::Printf(TEXT("%s_shot%d"), *Prefix, ShotIndex + 1);
    }

    /**
     * 模型文件名与驱动方式的匹配检查。
     *
     * ref2va / fl2va 是**两份不同的底座**（一份吃参考图、一份吃首尾帧），而项目里只有一个
     * UnetName。两者混在一个项目里时，必然有一段是错的——ComfyUI 不一定会报错，
     * 可能只是出图诡异，所以这里必须显式提示。
     */
    void CheckModelMatchesMode(const FString& UnetName, bool bHasReferenceShots, bool bHasFirstLastShots, TArray<FString>& OutWarnings)
    {
        const bool bLooksRef2Va = UnetName.Contains(TEXT("ref2va"), ESearchCase::IgnoreCase);
        const bool bLooksFl2Va = UnetName.Contains(TEXT("fl2va"), ESearchCase::IgnoreCase);

        if (bHasReferenceShots && bLooksFl2Va)
        {
            OutWarnings.Add(FString::Printf(
                TEXT("有分镜走参考图驱动（ref2va），但底座“%s”看起来是 fl2va（首尾帧）用的；两者不是同一份权重。"),
                *UnetName));
        }

        if (bHasFirstLastShots && bLooksRef2Va)
        {
            OutWarnings.Add(FString::Printf(
                TEXT("有分镜走首尾帧驱动（fl2va），但底座“%s”看起来是 ref2va（参考图）用的；两者不是同一份权重。"),
                *UnetName));
        }

        if (bHasReferenceShots && bHasFirstLastShots)
        {
            OutWarnings.Add(TEXT("同一个项目里混用了 ref2va 与 fl2va 两种驱动方式，而项目只有一份底座权重，必然有一段跑不对。"));
        }
    }
}

FString FShineMiniMaxH3WorkflowBuilder::FBuildResult::ToJsonString() const
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

int32 FShineMiniMaxH3WorkflowBuilder::AlignFrameCount(int32 RequestedLength)
{
    // H3 的 align_frame_count 会把 length 向上对齐到 n % 17 == 5：
    // 124 合法（下一档 141）。不对齐的话实际生成的帧数和填的不一样，
    // 而链式取末帧的 ImageFromBatch 下标会跟着错（见 H3-SPEC.md 坑 7）。
    const int32 Length = FMath::Max(RequestedLength, FrameGridOffset);
    const int32 Remainder = Length % FrameGridStride;
    const int32 Delta = (Remainder <= FrameGridOffset)
        ? (FrameGridOffset - Remainder)
        : (FrameGridStride - Remainder + FrameGridOffset);

    return Length + Delta;
}

bool FShineMiniMaxH3WorkflowBuilder::IsFrameCountOnGrid(int32 Frames)
{
    return Frames > 0 && (Frames % FrameGridStride) == FrameGridOffset;
}

int32 FShineMiniMaxH3WorkflowBuilder::AlignToMultiple(int32 Value, int32 Multiple)
{
    if (Multiple <= 0 || Value <= 0)
    {
        return Value;
    }

    return ((Value + Multiple - 1) / Multiple) * Multiple;
}

FShineMiniMaxH3WorkflowBuilder::FBuildResult FShineMiniMaxH3WorkflowBuilder::Build(const UShineVideoProject& Project)
{
    FBuildResult Result;

    if (Project.UnetName.IsEmpty() || Project.ClipName.IsEmpty() || Project.VideoVaeName.IsEmpty() || Project.AudioVaeName.IsEmpty())
    {
        Result.ErrorMessage = TEXT("项目缺少模型配置：unet / 文本编码器 / 视频 VAE / 音频 VAE 四个文件名都必须填。");
        return Result;
    }

    // 只编译"可提交"的分镜：没有提示词也没有任何参考图/首帧的分镜跑不出东西，
    // 硬塞进去只会白占一次采样时间。
    TArray<int32> CompiledShotIndices;
    CompiledShotIndices.Reserve(Project.Shots.Num());
    bool bHasReferenceShots = false;
    bool bHasFirstLastShots = false;
    for (int32 ShotIndex = 0; ShotIndex < Project.Shots.Num(); ++ShotIndex)
    {
        const FShineVideoShot& Shot = Project.Shots[ShotIndex];
        if (!Shot.IsSubmittable())
        {
            continue;
        }

        CompiledShotIndices.Add(ShotIndex);
        if (Shot.Mode == EShineVideoShotMode::FirstLastFrame)
        {
            bHasFirstLastShots = true;
        }
        else
        {
            bHasReferenceShots = true;
        }
    }

    if (CompiledShotIndices.Num() == 0)
    {
        Result.ErrorMessage = TEXT("项目里没有可提交的分镜：每个分镜至少要有一段提示词或一张参考图。");
        return Result;
    }

    for (int32 ShotIndex = 0; ShotIndex < Project.Shots.Num(); ++ShotIndex)
    {
        if (!Project.Shots[ShotIndex].IsSubmittable())
        {
            Result.Warnings.Add(FString::Printf(
                TEXT("第 %d 个分镜既没有提示词也没有参考图，已跳过。"),
                ShotIndex + 1));
        }
    }

    CheckModelMatchesMode(Project.UnetName, bHasReferenceShots, bHasFirstLastShots, Result.Warnings);

    if (!Project.NegativePrompt.IsEmpty())
    {
        // H3 的负向不是"第二个提示词"：它必须是 ConditioningZeroOut(H3 正向)，
        // 否则会缺 minimax_frame_count / minimax_refs / minimax_token_tags 这些扩展键
        // （见 H3-SPEC.md 坑 4）。所以这个字段现在只是创作记录，别让用户以为它生效了。
        Result.Warnings.Add(TEXT("项目里的负面提示词暂未接入图：H3 的负向必须是 ConditioningZeroOut(正向 conditioning)，带文本的负向待 P6 实测后再决定。"));
    }

    FGraphWriter Graph;
    FNodeIdPool Ids;

    // ------------------------------------------------------------------ 全局节点

    const FString UnetId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(UnetId, TEXT("UNETLoader"));
        Inputs->SetStringField(TEXT("unet_name"), Project.UnetName);
        Inputs->SetStringField(TEXT("weight_dtype"), TEXT("default"));
    }

    const FString ClipId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ClipId, TEXT("CLIPLoader"));
        Inputs->SetStringField(TEXT("clip_name"), Project.ClipName);
        // type 必须是 minimax：换成别的会走错文本编码器分支，出图直接废掉。
        Inputs->SetStringField(TEXT("type"), TEXT("minimax"));
        Inputs->SetStringField(TEXT("device"), TEXT("default"));
    }

    const FString VideoVaeId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(VideoVaeId, TEXT("VAELoader"));
        Inputs->SetStringField(TEXT("vae_name"), Project.VideoVaeName);
    }

    const FString AudioVaeId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(AudioVaeId, TEXT("VAELoader"));
        Inputs->SetStringField(TEXT("vae_name"), Project.AudioVaeName);
    }

    // Turbo LoRA 是可选项：不叠的时候这一层不存在，图与 P0 基线逐字段一致。
    FString ModelSourceId = UnetId;
    FString ClipSourceId = ClipId;
    if (!Project.TurboLoraName.IsEmpty())
    {
        const FString LoraId = Ids.Take();
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(LoraId, TEXT("LoraLoader"));
        Inputs->SetArrayField(TEXT("model"), MakeLink(UnetId, 0));
        Inputs->SetArrayField(TEXT("clip"), MakeLink(ClipId, 0));
        Inputs->SetStringField(TEXT("lora_name"), Project.TurboLoraName);
        Inputs->SetNumberField(TEXT("strength_model"), Project.TurboLoraStrength);
        Inputs->SetNumberField(TEXT("strength_clip"), Project.TurboLoraStrength);

        ModelSourceId = LoraId;
        ClipSourceId = LoraId;
    }

    // shift 是"模型级"的旋钮，一个项目只有一个。分镜上那两个字段只是给 P6 调档位留的入口，
    // 取值不一致时取第一段的，并提示用户。
    const FShineVideoShot& FirstShot = Project.Shots[CompiledShotIndices[0]];
    double ShiftVideo = FirstShot.ShiftVideo;
    double ShiftAudio = FirstShot.ShiftAudio;
    for (const int32 ShotIndex : CompiledShotIndices)
    {
        const FShineVideoShot& Shot = Project.Shots[ShotIndex];
        if (!FMath::IsNearlyEqual(Shot.ShiftVideo, ShiftVideo) || !FMath::IsNearlyEqual(Shot.ShiftAudio, ShiftAudio))
        {
            Result.Warnings.Add(FString::Printf(
                TEXT("各分镜的 shift 取值不一致（第 %d 段是 %g/%g），MiniMaxH3SigmaShift 是模型级节点，只能取第一段的 %g/%g。"),
                ShotIndex + 1, Shot.ShiftVideo, Shot.ShiftAudio, ShiftVideo, ShiftAudio));
            break;
        }
    }

    const FString ShiftId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ShiftId, TEXT("MiniMaxH3SigmaShift"));
        Inputs->SetArrayField(TEXT("model"), MakeLink(ModelSourceId, 0));
        Inputs->SetNumberField(TEXT("shift_video"), ShiftVideo);
        Inputs->SetNumberField(TEXT("shift_audio"), ShiftAudio);
    }

    // 采样器名是项目级常量（H3 定稿用 res_multistep），所有分镜共用同一个节点，
    // 与 P0 的两段链式基线一致。
    const FString SamplerSelectId = Ids.Take();
    {
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(SamplerSelectId, TEXT("KSamplerSelect"));
        Inputs->SetStringField(TEXT("sampler_name"), TEXT("res_multistep"));
    }

    // 采样调度：所有分镜的 steps / denoise 一致时共用一个节点（这正是 P0 两段链式基线的形态）；
    // 一旦有分镜的取值不同就退回"每分镜一个"，免得改一段把全片都改了。
    FString SharedSchedulerId;
    {
        const FShineVideoShot& FirstCompiledShot = Project.Shots[CompiledShotIndices[0]];
        bool bAllSameSampling = true;
        for (const int32 ShotIndex : CompiledShotIndices)
        {
            const FShineVideoShot& Shot = Project.Shots[ShotIndex];
            if (Shot.Steps != FirstCompiledShot.Steps || !FMath::IsNearlyEqual(Shot.Denoise, FirstCompiledShot.Denoise))
            {
                bAllSameSampling = false;
                break;
            }
        }

        if (bAllSameSampling)
        {
            SharedSchedulerId = Ids.Take();
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(SharedSchedulerId, TEXT("BasicScheduler"));
            Inputs->SetArrayField(TEXT("model"), MakeLink(ShiftId, 0));
            Inputs->SetStringField(TEXT("scheduler"), TEXT("simple"));
            Inputs->SetNumberField(TEXT("steps"), FirstCompiledShot.Steps);
            Inputs->SetNumberField(TEXT("denoise"), FirstCompiledShot.Denoise);
        }
    }

    // ------------------------------------------------------------------ 逐分镜

    // 链式：下一段要用"上一段 VAEDecode 的末帧"，所以要把上一段的解码节点和帧数记下来。
    FString PreviousDecodeNodeId;
    int32 PreviousAlignedFrames = 0;

    // 同一张图在多个分镜里出现是常态（角色参考图就是），按文件名复用同一个 LoadImage 节点：
    // 图上少一堆重复节点，ComfyUI 那边也省一次读盘。
    TMap<FString, FString> LoadImageNodeIds;
    auto GetOrCreateLoadImage = [&Graph, &Ids, &LoadImageNodeIds](const FString& ImageName) -> FString
    {
        if (const FString* ExistingNodeId = LoadImageNodeIds.Find(ImageName))
        {
            return *ExistingNodeId;
        }

        const FString NodeId = Ids.Take();
        const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(NodeId, TEXT("LoadImage"));
        Inputs->SetStringField(TEXT("image"), ImageName);
        LoadImageNodeIds.Add(ImageName, NodeId);
        return NodeId;
    };

    for (int32 Order = 0; Order < CompiledShotIndices.Num(); ++Order)
    {
        const int32 ShotIndex = CompiledShotIndices[Order];
        const FShineVideoShot& Shot = Project.Shots[ShotIndex];

        FShotPlan Plan;
        Plan.ShotIndex = ShotIndex;

        const int32 AlignedFrames = AlignFrameCount(Shot.Length);
        if (AlignedFrames != Shot.Length)
        {
            Result.Warnings.Add(FString::Printf(
                TEXT("第 %d 段的帧数 %d 不在 H3 的 17k+5 网格上，已对齐到 %d 帧（%.2f 秒 @24fps）。"),
                ShotIndex + 1, Shot.Length, AlignedFrames, static_cast<double>(AlignedFrames) / VideoFps));
        }

        const int32 AlignedWidth = AlignToMultiple(Shot.Width, SizeMultiple);
        const int32 AlignedHeight = AlignToMultiple(Shot.Height, SizeMultiple);
        if (AlignedWidth != Shot.Width || AlignedHeight != Shot.Height)
        {
            Result.Warnings.Add(FString::Printf(
                TEXT("第 %d 段的分辨率 %dx%d 不是 %d 的倍数，已对齐到 %dx%d。"),
                ShotIndex + 1, Shot.Width, Shot.Height, SizeMultiple, AlignedWidth, AlignedHeight));
        }

        FString RefImageSize = Shot.RefImageSize.TrimStartAndEnd().ToLower();
        if (RefImageSize != TEXT("match") && RefImageSize != TEXT("max"))
        {
            Result.Warnings.Add(FString::Printf(
                TEXT("第 %d 段的 ref_image_size“%s”不是 match/max，已按 match 处理。"),
                ShotIndex + 1, *Shot.RefImageSize));
            RefImageSize = TEXT("match");
        }
        if (RefImageSize == TEXT("max"))
        {
            Result.Warnings.Add(FString::Printf(
                TEXT("第 %d 段用了 ref_image_size=max：身份更准，但参考 token 会贯穿每个采样步，可能慢好几倍。"),
                ShotIndex + 1));
        }

        // 能接上一段的前提是"真的有过上一段"。第一个分镜写 bChainFromPrevious 只能忽略。
        bool bChain = Shot.bChainFromPrevious;
        if (bChain && PreviousDecodeNodeId.IsEmpty())
        {
            Result.Warnings.Add(FString::Printf(
                TEXT("第 %d 段要求接上一段末帧，但它前面没有已生成的分镜，已忽略。"),
                ShotIndex + 1));
            bChain = false;
        }
        Plan.bChainedFromPrevious = bChain;

        const bool bReferenceMode = Shot.Mode != EShineVideoShotMode::FirstLastFrame;

        // 已解析好的参考图标识（素材已换成 ComfyUI input 里的文件名，由任务执行器负责）。
        TArray<FString> ReferenceImages;
        ReferenceImages.Reserve(Shot.ReferenceImages.Num());
        for (const FString& ReferenceImage : Shot.ReferenceImages)
        {
            if (!ReferenceImage.TrimStartAndEnd().IsEmpty())
            {
                ReferenceImages.Add(ReferenceImage.TrimStartAndEnd());
            }
        }

        if (bReferenceMode)
        {
            // ref2va 的参考图上限是 9（ref_images.ref_image_* 的 max=9），超了会被静默忽略
            // （见 H3-SPEC.md 坑 2），所以这里宁可显式裁掉并提示。
            const int32 ChainReserve = bChain ? 1 : 0;
            const int32 AllowedCount = MaxReferenceImages - ChainReserve;
            if (ReferenceImages.Num() > AllowedCount)
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("第 %d 段有 %d 张参考图，加上链式末帧超过 H3 的 %d 张上限，已从末尾裁到 %d 张（<Picture 编号会跟着变）。"),
                    ShotIndex + 1, ReferenceImages.Num(), MaxReferenceImages, FMath::Max(AllowedCount, 0)));
                ReferenceImages.SetNum(FMath::Max(AllowedCount, 0));
            }
        }
        else if (ReferenceImages.Num() > 1)
        {
            // fl2va（MiniMaxH3ImageToVideo）【没有 ref_images 输入】，只有 first_frame / last_frame：
            // 第一张会被当首帧（见下面的兜底），第 2 张起用不上。
            Result.Warnings.Add(FString::Printf(
                TEXT("第 %d 段走首尾帧驱动（fl2va），第 2 张起的 %d 张参考图不会被使用（该节点只有首帧 / 末帧两个图像输入）。"),
                ShotIndex + 1, ReferenceImages.Num() - 1));
        }

        // 参考图的 LoadImage 节点。顺序即 <Picture 1..N> —— tokenizer 是按连接顺序
        // 自动生成标签的（见 H3-SPEC.md 坑 5），没法靠提示词改。
        TArray<FString> ReferenceImageNodeIds;
        if (bReferenceMode)
        {
            ReferenceImageNodeIds.Reserve(ReferenceImages.Num());
            for (const FString& ReferenceImage : ReferenceImages)
            {
                ReferenceImageNodeIds.Add(GetOrCreateLoadImage(ReferenceImage));
            }
        }
        Plan.ReferenceImageNodeIds = ReferenceImageNodeIds;

        // 链式末帧：ImageFromBatch(image=上一段 VAEDecode, batch_index=上一段帧数-1, length=1)。
        // 下标按【对齐后的帧数】算，不能写死 123 —— 否则改了 length 就会取错帧。
        FString ChainNodeId;
        if (bChain)
        {
            ChainNodeId = Ids.Take();
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ChainNodeId, TEXT("ImageFromBatch"));
            Inputs->SetArrayField(TEXT("image"), MakeLink(PreviousDecodeNodeId, 0));
            Inputs->SetNumberField(TEXT("batch_index"), PreviousAlignedFrames - 1);
            Inputs->SetNumberField(TEXT("length"), 1);
        }

        // -------------------------------------------------- conditioning 分支

        const FString ConditioningNodeId = Ids.Take();
        if (bReferenceMode)
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ConditioningNodeId, TEXT("MiniMaxH3ReferenceToVideo"));
            Inputs->SetArrayField(TEXT("clip"), MakeLink(ClipSourceId, 0));
            Inputs->SetArrayField(TEXT("vae"), MakeLink(VideoVaeId, 0));
            // ref2va 有 audio_vae，fl2va 没有 —— 这两个节点的入参不一样，别抄错。
            Inputs->SetArrayField(TEXT("audio_vae"), MakeLink(AudioVaeId, 0));
            Inputs->SetStringField(TEXT("prompt"), Shot.Prompt);
            Inputs->SetNumberField(TEXT("width"), AlignedWidth);
            Inputs->SetNumberField(TEXT("height"), AlignedHeight);
            Inputs->SetNumberField(TEXT("length"), AlignedFrames);
            Inputs->SetStringField(TEXT("ref_image_size"), RefImageSize);

            int32 ReferenceIndex = 0;
            for (const FString& ReferenceImageNodeId : ReferenceImageNodeIds)
            {
                Inputs->SetArrayField(MakeReferenceImageKey(ReferenceIndex), MakeLink(ReferenceImageNodeId, 0));
                ++ReferenceIndex;
            }

            if (bChain)
            {
                // 末帧只能作为【参考图】软锚定：ref2va 节点没有 first_frame/last_frame 输入。
                Inputs->SetArrayField(MakeReferenceImageKey(ReferenceIndex), MakeLink(ChainNodeId, 0));
                ++ReferenceIndex;
            }

            if (ReferenceIndex == 0)
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("第 %d 段走参考图驱动，但一张参考图也没有，H3 只会按提示词自由生成（身份无法锚定）。"),
                    ShotIndex + 1));
            }
        }
        else
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ConditioningNodeId, TEXT("MiniMaxH3ImageToVideo"));
            Inputs->SetArrayField(TEXT("clip"), MakeLink(ClipSourceId, 0));
            Inputs->SetArrayField(TEXT("vae"), MakeLink(VideoVaeId, 0));
            Inputs->SetStringField(TEXT("prompt"), Shot.Prompt);
            Inputs->SetNumberField(TEXT("width"), AlignedWidth);
            Inputs->SetNumberField(TEXT("height"), AlignedHeight);
            Inputs->SetNumberField(TEXT("length"), AlignedFrames);

            if (bChain)
            {
                // 链式在 fl2va 下是【硬锚定】：把上一段末帧当首帧。
                Inputs->SetArrayField(TEXT("first_frame"), MakeLink(ChainNodeId, 0));
                Plan.FirstFrameNodeId = ChainNodeId;
            }
            else if (!Shot.FirstFrameImage.TrimStartAndEnd().IsEmpty())
            {
                const FString FirstFrameId = GetOrCreateLoadImage(Shot.FirstFrameImage.TrimStartAndEnd());
                Inputs->SetArrayField(TEXT("first_frame"), MakeLink(FirstFrameId, 0));
                Plan.FirstFrameNodeId = FirstFrameId;
            }
            else if (ReferenceImages.Num() > 0)
            {
                // fl2va 没有参考图输入，所以把**第一张参考图**当首帧用。在画布上它就是这一段的
                // 锚图（分镜图的产物）；提示词里 `@image:` 解析出的第一张也落在这个列表里
                // （见 ShineMentionResolver 的顺序规则）。执行器会把这张图一起上传，LoadImage 找得到。
                //
                // ⚠️ 这里原来判的是 `ReferenceImageNodeIds`——那个数组只在**参考图模式**下才填，
                // fl2va 下永远是空的，所以这段兜底从来没生效过：一旦既没有显式首帧也没有链式，
                // fl2va 节点连一个图像输入都没有，直接退化成纯文本出视频（分镜图白出）。
                const FString FirstFrameId = GetOrCreateLoadImage(ReferenceImages[0]);
                Inputs->SetArrayField(TEXT("first_frame"), MakeLink(FirstFrameId, 0));
                Plan.FirstFrameNodeId = FirstFrameId;
                Result.Warnings.Add(FString::Printf(
                    TEXT("第 %d 段走首尾帧驱动但没给显式首帧：已把第一张参考图当首帧（%s）。"),
                    ShotIndex + 1, *ReferenceImages[0]));
            }
            else
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("第 %d 段走首尾帧驱动，但既没有首帧也没有参考图，H3 会退化成纯文本驱动。"),
                    ShotIndex + 1));
            }
        }

        Plan.ConditioningNodeId = ConditioningNodeId;

        // -------------------------------------------------- 负向 / 采样 / 解码

        // 负向必须作用在 H3 自家的正向 conditioning 上：ConditioningZeroOut 会 copy 全部
        // 扩展键（minimax_frame_count / minimax_refs / minimax_token_tags）只清零张量，
        // 结构一致且省掉第二次 32B 文本编码器前向（见 H3-SPEC.md 坑 4）。
        const FString ZeroOutId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(ZeroOutId, TEXT("ConditioningZeroOut"));
            Inputs->SetArrayField(TEXT("conditioning"), MakeLink(ConditioningNodeId, 0));
        }

        // 采样调度：参数全片一致时用共用节点（与 P0 基线同形）；不一致才给这一段单独建一个
        // ——steps / denoise 是分镜级参数，共享会让改一段影响全片。
        FString SchedulerId = SharedSchedulerId;
        if (SchedulerId.IsEmpty())
        {
            SchedulerId = Ids.Take();
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(SchedulerId, TEXT("BasicScheduler"));
            Inputs->SetArrayField(TEXT("model"), MakeLink(ShiftId, 0));
            Inputs->SetStringField(TEXT("scheduler"), TEXT("simple"));
            Inputs->SetNumberField(TEXT("steps"), Shot.Steps);
            Inputs->SetNumberField(TEXT("denoise"), Shot.Denoise);
        }

        const FString GuiderId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(GuiderId, TEXT("CFGGuider"));
            Inputs->SetArrayField(TEXT("model"), MakeLink(ShiftId, 0));
            Inputs->SetArrayField(TEXT("positive"), MakeLink(ConditioningNodeId, 0));
            Inputs->SetArrayField(TEXT("negative"), MakeLink(ZeroOutId, 0));
            Inputs->SetNumberField(TEXT("cfg"), Shot.Cfg);
        }

        const FString NoiseId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(NoiseId, TEXT("RandomNoise"));
            Inputs->SetNumberField(TEXT("noise_seed"), Shot.Seed);
        }

        const FString SamplerNodeId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(SamplerNodeId, TEXT("SamplerCustomAdvanced"));
            Inputs->SetArrayField(TEXT("noise"), MakeLink(NoiseId, 0));
            Inputs->SetArrayField(TEXT("guider"), MakeLink(GuiderId, 0));
            Inputs->SetArrayField(TEXT("sampler"), MakeLink(SamplerSelectId, 0));
            Inputs->SetArrayField(TEXT("sigmas"), MakeLink(SchedulerId, 0));
            // latent 是 conditioning 节点的第 1 个输出（第 0 个是 CONDITIONING）。
            Inputs->SetArrayField(TEXT("latent_image"), MakeLink(ConditioningNodeId, 1));
        }
        Plan.SamplerNodeId = SamplerNodeId;

        // AV latent 解两路：VAEDecode 自动取嵌套 latent 的第 0 路（视频），
        // VAEDecodeAudio 取第 -1 路（音频）。少接一路 mp4 依然能播，只是没声音。
        const FString DecodeId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(DecodeId, TEXT("VAEDecode"));
            Inputs->SetArrayField(TEXT("samples"), MakeLink(SamplerNodeId, 0));
            Inputs->SetArrayField(TEXT("vae"), MakeLink(VideoVaeId, 0));
        }

        const FString AudioDecodeId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(AudioDecodeId, TEXT("VAEDecodeAudio"));
            Inputs->SetArrayField(TEXT("samples"), MakeLink(SamplerNodeId, 0));
            Inputs->SetArrayField(TEXT("vae"), MakeLink(AudioVaeId, 0));
        }

        const FString CreateVideoId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(CreateVideoId, TEXT("CreateVideo"));
            Inputs->SetArrayField(TEXT("images"), MakeLink(DecodeId, 0));
            Inputs->SetArrayField(TEXT("audio"), MakeLink(AudioDecodeId, 0));
            // fps 必须 24：H3 是 24fps 原生，默认 30 会让片段比实际生成的速度快。
            Inputs->SetNumberField(TEXT("fps"), VideoFps);
            Inputs->SetNumberField(TEXT("bit_depth"), 8);
        }

        const FString SaveVideoId = Ids.Take();
        {
            const TSharedPtr<FJsonObject> Inputs = Graph.AddNode(SaveVideoId, TEXT("SaveVideo"));
            Inputs->SetArrayField(TEXT("video"), MakeLink(CreateVideoId, 0));
            Inputs->SetStringField(TEXT("filename_prefix"), MakeShotFilenamePrefix(Project.FilenamePrefix, ShotIndex));
            Inputs->SetStringField(TEXT("format"), TEXT("auto"));
            Inputs->SetStringField(TEXT("codec"), TEXT("auto"));
        }
        Plan.SaveVideoNodeId = SaveVideoId;

        Result.ShotPlans.Add(MoveTemp(Plan));

        PreviousDecodeNodeId = DecodeId;
        PreviousAlignedFrames = AlignedFrames;
    }

    Result.Prompt = Graph.Root;
    Result.NodeClassTypes = MoveTemp(Graph.ClassTypes);
    Result.bSuccess = true;
    return Result;
}
