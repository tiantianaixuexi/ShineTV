#include "Comfy/ShineVideoGraphCompiler.h"

#include "Asset/ShineCharacterAsset.h"
#include "Asset/ShineVideoGraph.h"
#include "Asset/ShineVideoProject.h"
#include "Comfy/ShineComfyPaths.h"
#include "Comfy/ShineImageTaskRunner.h"
#include "Comfy/ShineMentionResolver.h"
#include "Comfy/ShineVideoTaskRunner.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Video/ShineVideoCharacterGraphNode.h"
#include "Graph/Node/Video/ShineVideoGroupGraphNode.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "Graph/Node/Video/ShineVideoImageGraphNode.h"
#include "Graph/Node/Video/ShineVideoScriptGraphNode.h"
#include "Graph/Node/Video/ShineVideoShotGraphNode.h"
#include "Graph/Node/Video/ShineVideoShotImageGraphNode.h"
#include "Misc/Paths.h"

namespace
{
    using namespace ShineVideoShotNode;

    /** H3 的参考图硬上限（`ref_images.ref_image_*` 的 max=9）。 */
    constexpr int32 MaxReferenceImages = 9;

    // ---------------------------------------------------------------- 参数读取小工具

    FString GetTextParameter(const UShineComfyGraphNodeBase* Node, const FName& ParameterName)
    {
        const FShineComfyNodeParameter* Parameter = Node ? Node->FindParameter(ParameterName) : nullptr;
        return Parameter ? Parameter->StringValue.TrimStartAndEnd() : FString();
    }

    int32 GetIntParameter(const UShineComfyGraphNodeBase* Node, const FName& ParameterName, int32 DefaultValue)
    {
        const FShineComfyNodeParameter* Parameter = Node ? Node->FindParameter(ParameterName) : nullptr;
        return Parameter ? Parameter->IntValue : DefaultValue;
    }

    double GetFloatParameter(const UShineComfyGraphNodeBase* Node, const FName& ParameterName, double DefaultValue)
    {
        const FShineComfyNodeParameter* Parameter = Node ? Node->FindParameter(ParameterName) : nullptr;
        return Parameter ? Parameter->FloatValue : DefaultValue;
    }

    /** 把素材标识解析成磁盘绝对路径（相对路径按素材库根解析）。不检查存在性。 */
    FString ResolveLocalPath(const FString& Identifier)
    {
        const FString Trimmed = Identifier.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            return FString();
        }

        const FString Resolved = FShineMentionResolver::ResolveLocalPath(Trimmed);
        return Resolved.IsEmpty() ? Trimmed : Resolved;
    }

    /** 提示"这张图在磁盘上找不到"，但只报一次同样的路径。 */
    void WarnIfMissing(const FString& Path, const FString& Who, TArray<FString>& OutWarnings)
    {
        const FString Absolute = ResolveLocalPath(Path);
        if (Absolute.IsEmpty() || FPaths::FileExists(Absolute))
        {
            return;
        }

        const FString Message = FString::Printf(TEXT("%s 引用的图片在磁盘上找不到：%s"), *Who, *Absolute);
        if (!OutWarnings.Contains(Message))
        {
            OutWarnings.Add(Message);
        }
    }

    /**
     * 节点的可读标签，用于报错定位（"「分镜」开场 的第 3 个参考图槽位…"这种）。
     *
     * 用节点自己的**角色名**（`GetVideoNodeKind`）而不是 UObject 名字：流程页签是给做片子的人看的，
     * `ShineVideoShotGraphNode_2` 对他没有任何意义。
     */
    FString DescribeNode(const UShineComfyGraphNodeBase* Node)
    {
        if (!Node)
        {
            return TEXT("(空节点)");
        }

        const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        if (const UShineVideoGraphNodeBase* VideoNode = Cast<UShineVideoGraphNodeBase>(Node))
        {
            return FString::Printf(TEXT("%s「%s」"), *VideoNode->GetVideoNodeKind().ToString(), *Title);
        }

        return FString::Printf(TEXT("「%s」"), *Title);
    }

    // ---------------------------------------------------------------- 参考图展开

    /**
     * 把一个参考图槽位的上游节点展开成**有序的图片路径**。
     *
     * 图片节点 → 一张图；角色节点 → 该角色资产的整份参考图；分镜图节点 → 它的那一张。
     * 返回值表示"这个槽位确实贡献了参考图"。
     *
     * @param OutReferencedNodes 被用到的节点都记进来，最后用来提示"哪些节点是孤立的"。
     */
    bool ExpandPictureSlotSource(
        UEdGraphNode* SourceNode,
        FString& OutIdentityPrompt,
        TArray<FString>& OutPaths,
        TSet<FString>& OutCharacterAssetPaths,
        TOptional<int32>& OutForcedSeed,
        TSet<const UEdGraphNode*>& OutReferencedNodes,
        TArray<FString>& OutWarnings)
    {
        if (SourceNode)
        {
            OutReferencedNodes.Add(SourceNode);
        }

        if (const UShineVideoCharacterGraphNode* CharacterNode = Cast<UShineVideoCharacterGraphNode>(SourceNode))
        {
            const FShineComfyNodeParameter* AssetParameter = CharacterNode->FindParameter(ShineVideoCharacterNode::CharacterAssetParameter);
            const FString Identifier = AssetParameter ? AssetParameter->StringValue.TrimStartAndEnd() : FString();

            const UShineCharacterAsset* Character = CharacterNode->ResolveCharacterAsset();
            if (!Character)
            {
                OutWarnings.Add(FString::Printf(
                    TEXT("%s 找不到角色资产“%s”：这个槽位不会贡献参考图。填资产路径（/Game/...）或资产名。"),
                    *DescribeNode(CharacterNode), Identifier.IsEmpty() ? TEXT("(空)") : *Identifier));
                return false;
            }

            const int32 PathCountBefore = OutPaths.Num();

            for (const FString& ReferenceImage : Character->ReferenceImages)
            {
                const FString Path = ResolveLocalPath(ReferenceImage);
                if (!Path.IsEmpty())
                {
                    OutPaths.Add(Path);
                }
            }

            if (!Character->IdentityPrompt.TrimStartAndEnd().IsEmpty())
            {
                if (!OutIdentityPrompt.IsEmpty())
                {
                    OutIdentityPrompt += TEXT(", ");
                }

                OutIdentityPrompt += Character->IdentityPrompt.TrimStartAndEnd();
            }

            if (Character->bLockSeed)
            {
                if (OutForcedSeed.IsSet() && OutForcedSeed.GetValue() != Character->LockedSeed)
                {
                    OutWarnings.Add(FString::Printf(
                        TEXT("这一段的角色里有两个都锁了种子（%d 与 %d），只能用一个；实际用先出现的 %d。"),
                        OutForcedSeed.GetValue(), Character->LockedSeed, OutForcedSeed.GetValue()));
                }
                else
                {
                    OutForcedSeed = Character->LockedSeed;
                }
            }

            // 角色资产路径也登记到项目表里：这样提示词里手写的 `@char:<显示名>` 还能按名字匹配到它。
            if (!Character->GetPathName().IsEmpty())
            {
                OutCharacterAssetPaths.Add(Character->GetPathName());
            }

            if (OutPaths.Num() == PathCountBefore)
            {
                OutWarnings.Add(FString::Printf(
                    TEXT("%s 的角色资产一张参考图都没有：这个槽位不贡献参考图。"), *DescribeNode(CharacterNode)));
            }

            return OutPaths.Num() > PathCountBefore;
        }

        if (const UShineVideoImageGraphNode* ImageNode = Cast<UShineVideoImageGraphNode>(SourceNode))
        {
            const FString Path = ImageNode->GetImagePath();
            if (Path.IsEmpty())
            {
                OutWarnings.Add(FString::Printf(TEXT("%s 没填「图片路径」，这个槽位不贡献参考图。"), *DescribeNode(ImageNode)));
                return false;
            }

            OutPaths.Add(ResolveLocalPath(Path));
            WarnIfMissing(Path, DescribeNode(ImageNode), OutWarnings);
            return true;
        }

        if (const UShineVideoShotImageGraphNode* ShotImageNode = Cast<UShineVideoShotImageGraphNode>(SourceNode))
        {
            const FString Path = ShotImageNode->GetImagePath();
            if (Path.IsEmpty())
            {
                OutWarnings.Add(FString::Printf(TEXT("%s 还没有图，这个槽位不贡献参考图。"), *DescribeNode(ShotImageNode)));
                return false;
            }

            OutPaths.Add(ResolveLocalPath(Path));
            WarnIfMissing(Path, DescribeNode(ShotImageNode), OutWarnings);
            return true;
        }

        if (SourceNode)
        {
            OutWarnings.Add(FString::Printf(
                TEXT("%s 不是图片/角色节点，不能当参考图；这个槽位被忽略。"),
                *DescribeNode(Cast<UShineComfyGraphNodeBase>(SourceNode))));
        }

        return false;
    }

    /**
     * 剧本节点 → 提示词的注入。
     *
     * **只注入两样**：`Style` 放到最前面（全局风格基线，保证一集里的镜头看起来是同一部片子），
     * `Period` + `Scene` 拼成一句环境说明放到最后。`Body`（正文）和 `Foreshadow`（视觉伏笔）
     * **不注入**——它们是给人看的创作记录，整段塞进提示词只会稀释真正描述画面的那些词。
     * 这条规则写在剧本节点的头文件注释里，这里是唯一实现。
     */
    void ApplyScriptNode(const UShineVideoScriptGraphNode* ScriptNode, FString& InOutPrompt)
    {
        if (!ScriptNode)
        {
            return;
        }

        const FString Style = GetTextParameter(ScriptNode, ShineVideoScriptNode::StyleParameter);
        const FString Period = GetTextParameter(ScriptNode, ShineVideoScriptNode::PeriodParameter);
        const FString Scene = GetTextParameter(ScriptNode, ShineVideoScriptNode::SceneParameter);

        FString SettingSentence;
        if (!Period.IsEmpty() || !Scene.IsEmpty())
        {
            SettingSentence = Period.IsEmpty() ? Scene : (Scene.IsEmpty() ? Period : FString::Printf(TEXT("%s, %s"), *Period, *Scene));
        }

        FString Combined;
        if (!Style.IsEmpty())
        {
            Combined += Style;
            if (!Style.EndsWith(TEXT(".")) && !Style.EndsWith(TEXT(",")))
            {
                Combined += TEXT(",");
            }
            Combined += TEXT(" ");
        }

        Combined += InOutPrompt;

        if (!SettingSentence.IsEmpty())
        {
            if (!Combined.IsEmpty() && !Combined.EndsWith(TEXT(" ")))
            {
                Combined += TEXT(" ");
            }

            Combined += FString::Printf(TEXT("Setting: %s."), *SettingSentence);
        }

        InOutPrompt = Combined;
    }

    /** 这一段的提示词里还有什么引用记号没被画布表达出来（`@image:` / `@char:` / `{{Mixed N}}`）。 */
    void WarnAboutMentionSyntaxInPrompt(const FString& Prompt, int32 SegmentOrdinal, TArray<FString>& OutWarnings)
    {
        if (!FShineVideoGraphCompiler::PromptHasMentionSyntax(Prompt))
        {
            return;
        }

        OutWarnings.Add(FString::Printf(
            TEXT("第 %d 段的提示词里还有 @image:/@char:/{{Mixed N}}：它们会排在画布槽位**之前**"
                 "（ShineMentionResolver 的既定语义），所以画布上显示的槽位编号会整体后移。"
                 "建议二选一——要么全用画布槽位，要么全写在提示词里。"),
            SegmentOrdinal));
    }

    // ---------------------------------------------------------------- 分镜图出图

    /**
     * 去掉提示词里的 `<Picture N>`。
     *
     * 那是 H3 的参考图标签（tokenizer 按连接顺序自动生成，见 H3-SPEC.md 坑 5），SD1.5
     * 的 CLIP 只会把它当成一串无意义的 token 塞进文本编码，所以出分镜图时必须去掉。
     *
     * @return 是否真的去掉了东西（调用方据此给一条提示）。
     */
    bool StripPictureTags(FString& InOutPrompt)
    {
        bool bStripped = false;
        int32 SearchFrom = 0;

        while (SearchFrom < InOutPrompt.Len())
        {
            const int32 TagStart = InOutPrompt.Find(TEXT("<Picture"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
            if (TagStart == INDEX_NONE)
            {
                break;
            }

            const int32 TagEnd = InOutPrompt.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TagStart);
            if (TagEnd == INDEX_NONE)
            {
                break;
            }

            InOutPrompt.RemoveAt(TagStart, TagEnd - TagStart + 1);
            bStripped = true;
            SearchFrom = TagStart;
        }

        if (!bStripped)
        {
            return false;
        }

        // 去掉标签后常留下 "from  the" 这种双空格，顺手压一下。
        while (InOutPrompt.Contains(TEXT("  ")))
        {
            InOutPrompt.ReplaceInline(TEXT("  "), TEXT(" "));
        }
        InOutPrompt.ReplaceInline(TEXT(" ."), TEXT("."));

        return true;
    }

    /** 把一段名字压成能当文件名用的 ASCII 串（产物前缀的"段名"部分）。 */
    FString SanitizeNameForFilePrefix(const FString& InName)
    {
        FString Result;
        Result.Reserve(InName.Len());

        for (const TCHAR Character : InName)
        {
            // 中文标题会被整段替换成下划线——ComfyUI 写文件时中文名虽然能写，
            // 但在别的工具链里（脚本、命令行）会变成一堆转义问题，不值得。
            Result.AppendChar((Character < 128 && FChar::IsAlnum(Character)) ? Character : TEXT('_'));
        }

        while (Result.Contains(TEXT("__")))
        {
            Result.ReplaceInline(TEXT("__"), TEXT("_"));
        }

        int32 Start = 0;
        while (Start < Result.Len() && Result[Start] == TEXT('_'))
        {
            ++Start;
        }

        int32 End = Result.Len();
        while (End > Start && Result[End - 1] == TEXT('_'))
        {
            --End;
        }

        return Result.Mid(Start, End - Start);
    }
}

bool FShineVideoGraphCompiler::PromptHasMentionSyntax(const FString& Prompt)
{
    return Prompt.Contains(ShineVideoMention::ImagePrefix)
        || Prompt.Contains(ShineVideoMention::CharacterPrefix)
        || Prompt.Contains(TEXT("{{Mixed"));
}

FShineVideoGraphCompiler::FStoryboardRequest FShineVideoGraphCompiler::BuildStoryboardRequest(
    const UShineVideoShotImageGraphNode& Node)
{
    FStoryboardRequest Result;

    // 节点的可读名取自它自己的「名称」参数：这一类的 GetNodeTitle 是固定的"分镜图"，
    // 拿它当标签会让日志里所有分镜图长得一模一样。
    const FString NodeTitle = GetTextParameter(&Node, ShineVideoShotImageNode::TitleParameter);
    Result.NodeLabel = NodeTitle.IsEmpty() ? Node.GetName() : NodeTitle;

    // ---------------------------------------------------------------- 1. 图片侧参数

    const UShineVideoProject* Project = nullptr;
    if (const UEdGraph* Graph = Node.GetGraph())
    {
        // 画布是项目的 Instanced 子对象，所以往上找一层就是项目。
        Project = Graph->GetTypedOuter<UShineVideoProject>();
    }

    if (Project)
    {
        Result.Workflow.Checkpoint = Project->ImageCheckpoint;
        Result.Workflow.DepthControlNet = Project->ImageDepthControlNet;
        Result.Workflow.NormalControlNet = Project->ImageNormalControlNet;
        Result.Workflow.Width = Project->ImageWidth;
        Result.Workflow.Height = Project->ImageHeight;
        Result.Workflow.Steps = Project->ImageSteps;
        Result.Workflow.Cfg = Project->ImageCfg;
        Result.Workflow.Denoise = Project->ImageDenoise;
        Result.Workflow.Seed = Project->ImageSeed;
        Result.Workflow.DepthStrength = Project->ImageDepthStrength;
        Result.Workflow.NormalStrength = Project->ImageNormalStrength;
        Result.Workflow.ControlEndPercent = Project->ImageControlEndPercent;
        Result.Workflow.SamplerName = Project->ImageSamplerName;
        Result.Workflow.Scheduler = Project->ImageScheduler;

        if (!Project->NegativePrompt.TrimStartAndEnd().IsEmpty())
        {
            Result.Workflow.NegativePrompt = Project->NegativePrompt;
        }
    }
    else
    {
        Result.Warnings.Add(FString::Printf(
            TEXT("%s 不在任何项目资产里（取不到图片侧配置），本次用 builder 的默认参数出图。"),
            *Result.NodeLabel));
    }

    // ---------------------------------------------------------------- 2. 上游分镜的提示词

    UShineVideoShotGraphNode* ShotNode = nullptr;
    if (const UEdGraphPin* ShotLinkPin = Node.FindPin(ShineVideoShotImageNode::ShotPin))
    {
        if (ShotLinkPin->LinkedTo.Num() > 0)
        {
            ShotNode = Cast<UShineVideoShotGraphNode>(ShotLinkPin->LinkedTo[0]->GetOwningNode());
        }
    }

    FString Prompt;
    if (ShotNode)
    {
        Prompt = ShotNode->GetPromptText();

        // 剧本注入与视频那条线共用同一处实现（ApplyScriptNode）：同一个分镜写的东西，
        // 不该因为"这次是出图还是出视频"而注入得不一样。
        if (const UEdGraphPin* ScriptLinkPin = ShotNode->FindPin(ShineVideoShotNode::ScriptPin))
        {
            if (ScriptLinkPin->LinkedTo.Num() > 0)
            {
                if (const UShineVideoScriptGraphNode* ScriptNode =
                    Cast<UShineVideoScriptGraphNode>(ScriptLinkPin->LinkedTo[0]->GetOwningNode()))
                {
                    ApplyScriptNode(ScriptNode, Prompt);
                }
            }
        }
    }
    else
    {
        Result.Warnings.Add(FString::Printf(
            TEXT("%s 没有接上游「分镜」节点：拿不到提示词，只能用参考图 + 默认负向出图。"),
            *Result.NodeLabel));
    }

    // ---------------------------------------------------------------- 3. 参考图候选

    struct FCandidate
    {
        FString Path;
        FString Source;
    };

    TArray<FCandidate> Candidates;

    auto AddCandidate = [&Candidates](const FString& RawPath, const FString& Source)
    {
        const FString Path = ResolveLocalPath(RawPath);
        if (!Path.IsEmpty())
        {
            Candidates.Add({ Path, Source });
        }
    };

    // 3.1 「图片」输入接线：画布上明确画出来的那个"起点"。
    if (const UEdGraphPin* ImageLinkPin = Node.FindPin(ShineVideoShotImageNode::ImagePin))
    {
        if (ImageLinkPin->LinkedTo.Num() > 0)
        {
            UEdGraphNode* SourceNode = ImageLinkPin->LinkedTo[0]->GetOwningNode();
            if (const UShineVideoImageGraphNode* ImageNode = Cast<UShineVideoImageGraphNode>(SourceNode))
            {
                // 用节点上填的「名称」而不是 GetNodeTitle：这一类的标题是固定的"素材图片"，
                // 报出来等于没说。同理，分镜图用它的「名称」。
                const FString Label = GetTextParameter(ImageNode, ShineVideoImageNode::LabelParameter);
                AddCandidate(ImageNode->GetImagePath(), Label.IsEmpty() ? DescribeNode(ImageNode)
                    : FString::Printf(TEXT("素材图片「%s」"), *Label));
            }
            else if (const UShineVideoShotImageGraphNode* OtherShotImage = Cast<UShineVideoShotImageGraphNode>(SourceNode))
            {
                const FString Label = GetTextParameter(OtherShotImage, ShineVideoShotImageNode::TitleParameter);
                AddCandidate(OtherShotImage->GetImagePath(), Label.IsEmpty() ? DescribeNode(OtherShotImage)
                    : FString::Printf(TEXT("分镜图「%s」"), *Label));
            }
        }
    }

    // 3.2 上游分镜的参考图槽位：只有第一个"真能展开出图"的槽位当候选来源，
    //     但所有槽位都要展开一遍——角色节点的身份描述在后面几步要用。
    FString IdentityPrompt;
    if (ShotNode)
    {
        TArray<UEdGraphPin*> PicturePins;
        ShotNode->GetPicturePins(PicturePins);

        bool bCollectedSlotCandidate = false;
        for (int32 SlotIndex = 0; SlotIndex < PicturePins.Num(); ++SlotIndex)
        {
            const UEdGraphPin* PicturePin = PicturePins[SlotIndex];
            if (!PicturePin || PicturePin->LinkedTo.Num() == 0)
            {
                continue;
            }

            TArray<FString> SlotPaths;
            TSet<FString> SlotCharacterAssetPaths;
            TOptional<int32> SlotForcedSeed;
            TSet<const UEdGraphNode*> SlotReferencedNodes;

            if (!ExpandPictureSlotSource(PicturePin->LinkedTo[0]->GetOwningNode(), IdentityPrompt, SlotPaths,
                SlotCharacterAssetPaths, SlotForcedSeed, SlotReferencedNodes, Result.Warnings))
            {
                continue;
            }

            if (bCollectedSlotCandidate)
            {
                continue;
            }

            for (const FString& SlotPath : SlotPaths)
            {
                Candidates.Add({ SlotPath, FString::Printf(TEXT("%s 的第 %d 个槽位"),
                    *DescribeNode(ShotNode), SlotIndex + 1) });
            }

            bCollectedSlotCandidate = true;
        }
    }

    // 3.3 节点自己的「图片路径」参数：明确手填的那一张。
    const FString ParameterImagePath = GetTextParameter(&Node, ShineVideoShotImageNode::ImagePathParameter);
    if (!ParameterImagePath.IsEmpty())
    {
        AddCandidate(ParameterImagePath, FString::Printf(TEXT("%s 的「图片路径」"), *Result.NodeLabel));
    }

    if (Candidates.Num() == 0)
    {
        Result.ErrorMessage = FString::Printf(
            TEXT("%s 还没有图：给它接一张素材图 / 上游分镜图，或者填上「图片路径」——出图总得有个底图。"),
            *Result.NodeLabel);
        return Result;
    }

    // ---------------------------------------------------------------- 4. 挑一张当底图

    // 优先挑"带 _Depth / _Normal 兄弟文件"的那张：两支 ControlNet 正好吃这两个文件，
    // 而"这张图有没有结构引导"是一次出图里差别最大的那一项。
    int32 ChosenIndex = 0;
    for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
    {
        const bool bHasDepth = !ShineComfyPaths::FindSiblingImage(Candidates[CandidateIndex].Path, TEXT("_Depth")).IsEmpty();
        const bool bHasNormal = !ShineComfyPaths::FindSiblingImage(Candidates[CandidateIndex].Path, TEXT("_Normal")).IsEmpty();
        if (bHasDepth && bHasNormal)
        {
            ChosenIndex = CandidateIndex;
            break;
        }
    }

    const FCandidate& Chosen = Candidates[ChosenIndex];
    Result.ColorImagePath = Chosen.Path;
    Result.DepthImagePath = ShineComfyPaths::FindSiblingImage(Chosen.Path, TEXT("_Depth"));
    Result.NormalImagePath = ShineComfyPaths::FindSiblingImage(Chosen.Path, TEXT("_Normal"));

    if (Candidates.Num() > 1)
    {
        const bool bHasControlNet = !Result.DepthImagePath.IsEmpty() || !Result.NormalImagePath.IsEmpty();
        Result.Warnings.Add(FString::Printf(
            TEXT("%s 有 %d 张候选参考图，底图用的是「%s」（%s）——%s。"),
            *Result.NodeLabel, Candidates.Num(), *Chosen.Source, *FPaths::GetCleanFilename(Chosen.Path),
            bHasControlNet
                ? TEXT("它带着 _Depth / _Normal 兄弟文件，正好驱动 ControlNet")
                : TEXT("候选里没有一张带 _Depth / _Normal 兄弟文件，本次不接 ControlNet")));
    }
    else if (Result.DepthImagePath.IsEmpty() && Result.NormalImagePath.IsEmpty())
    {
        Result.Warnings.Add(FString::Printf(
            TEXT("%s 的底图旁边没有 _Depth / _Normal 兄弟文件，本次不接 ControlNet（结构只靠 img2img 的 Denoise 保）。"),
            *Result.NodeLabel));
    }

    // ---------------------------------------------------------------- 5. 提示词

    if (StripPictureTags(Prompt))
    {
        Result.Warnings.Add(FString::Printf(
            TEXT("%s 的提示词里有 <Picture N>（H3 的参考图标签），出图时已去掉：SD1.5 不认它，"
                 "身份与构图靠参考图 + ControlNet 锚定。"),
            *Result.NodeLabel));
    }

    if (!IdentityPrompt.IsEmpty())
    {
        if (!Prompt.IsEmpty() && !Prompt.EndsWith(TEXT(" ")))
        {
            Prompt += TEXT(" ");
        }

        Prompt += FString::Printf(TEXT("Keep the exact identity of the subject: %s."), *IdentityPrompt);
    }

    Result.Workflow.PositivePrompt = Prompt.TrimStartAndEnd();
    if (Result.Workflow.PositivePrompt.IsEmpty())
    {
        Result.Warnings.Add(FString::Printf(
            TEXT("%s 的正向提示词是空的：只会按参考图重绘（受 Denoise 控制）。"), *Result.NodeLabel));
    }

    // ---------------------------------------------------------------- 6. 产物前缀

    FString Suffix = SanitizeNameForFilePrefix(Result.NodeLabel);
    if (Suffix.IsEmpty())
    {
        Suffix = TEXT("storyboard");
    }

    const FString ProjectPrefix = Project && !Project->ImageOutputPrefix.TrimStartAndEnd().IsEmpty()
        ? Project->ImageOutputPrefix.TrimStartAndEnd()
        : Result.Workflow.OutputPrefix;
    Result.Workflow.OutputPrefix = FString::Printf(TEXT("%s_%s"), *ProjectPrefix, *Suffix);

    Result.bSuccess = true;
    return Result;
}

void FShineVideoGraphCompiler::MakeImageTaskRequest(const FStoryboardRequest& Storyboard,
    UShineVideoShotImageGraphNode* TargetNode, const FString& BaseUrlOverride, bool bNoBrothers,
    FShineImageTaskRequest& OutRequest)
{
    OutRequest = FShineImageTaskRequest();

    OutRequest.Workflow = Storyboard.Workflow;

    // 图名是"上传之后 ComfyUI 那边叫什么"，在这里一律留空：执行器上传完才知道。
    OutRequest.Workflow.ColorImageName.Reset();
    OutRequest.Workflow.DepthImageName.Reset();
    OutRequest.Workflow.NormalImageName.Reset();

    OutRequest.ColorImagePath = Storyboard.ColorImagePath;
    OutRequest.DepthImagePath = bNoBrothers ? FString() : Storyboard.DepthImagePath;
    OutRequest.NormalImagePath = bNoBrothers ? FString() : Storyboard.NormalImagePath;
    OutRequest.bAutoFindSiblings = !bNoBrothers;

    OutRequest.BaseUrl = BaseUrlOverride;

    if (TargetNode)
    {
        OutRequest.TargetNode = TargetNode;

        // 下载兜底时的本地目录名：项目名。取不到就退回节点名。
        const UShineVideoProject* Project = TargetNode->GetGraph()
            ? TargetNode->GetGraph()->GetTypedOuter<UShineVideoProject>()
            : nullptr;
        OutRequest.OutputDirectoryName = Project ? Project->GetName() : Storyboard.NodeLabel;
    }
    else
    {
        OutRequest.OutputDirectoryName = Storyboard.NodeLabel;
    }
}

FShineVideoGraphCompileResult FShineVideoGraphCompiler::Compile(const UShineVideoGraph& Graph)
{
    FShineVideoGraphCompileResult Result;

    TArray<UShineVideoGroupGraphNode*> Groups;
    Graph.GetVideoGroupNodesInOrder(Groups);

    if (Groups.Num() == 0)
    {
        Result.ErrorMessage = TEXT("画布上还没有「视频组」节点。"
            "分镜要接进视频组的 #1..#N，才知道一次要出几段、按什么顺序出——先右键建一个视频组。");
        return Result;
    }

    /** 已经被编译用到的节点，用来最后提示"孤立节点"。 */
    TSet<const UEdGraphNode*> ReferencedNodes;
    TSet<FString> CharacterAssetPaths;

    int32 SegmentOrdinal = 0;

    for (UShineVideoGroupGraphNode* Group : Groups)
    {
        // 产物前缀：项目级只有一个字段，多个视频组给不同前缀时以第一个为准并明确报警。
        const FString GroupPrefix = GetTextParameter(Group, ShineVideoGroupNode::PrefixParameter);
        if (!GroupPrefix.IsEmpty())
        {
            if (Result.FilenamePrefix.IsEmpty())
            {
                Result.FilenamePrefix = GroupPrefix;
            }
            else if (Result.FilenamePrefix != GroupPrefix)
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("视频组「%s」填的产物前缀“%s”与前面的不一致；产物前缀是项目级的，实际用“%s”。"),
                    *Group->GetNodeTitle(ENodeTitleType::ListView).ToString(), *GroupPrefix, *Result.FilenamePrefix));
            }
        }

        TArray<UEdGraphPin*> SegmentPins;
        Group->GetSegmentInputPins(SegmentPins);

        for (int32 SlotIndex = 0; SlotIndex < SegmentPins.Num(); ++SlotIndex)
        {
            const UEdGraphPin* SegmentPin = SegmentPins[SlotIndex];
            if (!SegmentPin || SegmentPin->LinkedTo.Num() == 0)
            {
                continue;
            }

            UEdGraphNode* SourceNode = SegmentPin->LinkedTo[0]->GetOwningNode();
            ++SegmentOrdinal;

            const FString SegmentLabel = FString::Printf(TEXT("视频组「%s」的 #%d"),
                *Group->GetNodeTitle(ENodeTitleType::ListView).ToString(), SlotIndex + 1);

            // ---- 1. 定出"锚图"（分镜图 / 素材图）与上游分镜节点

            UShineVideoShotGraphNode* ShotNode = nullptr;
            UShineVideoShotImageGraphNode* ShotImageNode = Cast<UShineVideoShotImageGraphNode>(SourceNode);
            UShineVideoImageGraphNode* PlainImageNode = Cast<UShineVideoImageGraphNode>(SourceNode);

            FString AnchorImagePath;
            FString Caption;

            if (ShotImageNode)
            {
                ReferencedNodes.Add(ShotImageNode);

                AnchorImagePath = ShotImageNode->GetImagePath();
                if (AnchorImagePath.IsEmpty())
                {
                    Result.ErrorMessage = FString::Printf(
                        TEXT("%s 接的是「分镜图」节点，而那张图还没有着落："
                             "给它填一个「图片路径」（引用素材），或让上游出图链路把结果写进这个节点。"),
                        *SegmentLabel);
                    return Result;
                }

                WarnIfMissing(AnchorImagePath, DescribeNode(ShotImageNode), Result.Warnings);
                Caption = GetTextParameter(ShotImageNode, ShineVideoShotImageNode::TitleParameter);

                if (const UEdGraphPin* ShotPin = ShotImageNode->FindPin(ShineVideoShotImageNode::ShotPin))
                {
                    if (ShotPin->LinkedTo.Num() > 0)
                    {
                        UEdGraphNode* UpstreamNode = ShotPin->LinkedTo[0]->GetOwningNode();
                        ReferencedNodes.Add(UpstreamNode);
                        ShotNode = Cast<UShineVideoShotGraphNode>(UpstreamNode);
                    }
                }

                // 分镜图的「图片」输入如果接了线，那张图（以及它上游的线）也算用到了。
                if (const UEdGraphPin* ImageSourcePin = ShotImageNode->FindPin(ShineVideoShotImageNode::ImagePin))
                {
                    if (ImageSourcePin->LinkedTo.Num() > 0)
                    {
                        UEdGraphNode* ImageSourceNode = ImageSourcePin->LinkedTo[0]->GetOwningNode();
                        ReferencedNodes.Add(ImageSourceNode);

                        if (Cast<UShineVideoShotGraphNode>(ImageSourceNode))
                        {
                            Result.Warnings.Add(FString::Printf(
                                TEXT("%s 的「图片」输入接的是分镜节点：分镜节点给的是「提示词 + 采样参数」，"
                                 "不是一张图，这根线会被忽略。"), *SegmentLabel));
                        }
                    }
                }
            }
            else if (PlainImageNode)
            {
                ReferencedNodes.Add(PlainImageNode);

                AnchorImagePath = PlainImageNode->GetImagePath();
                if (AnchorImagePath.IsEmpty())
                {
                    Result.ErrorMessage = FString::Printf(
                        TEXT("%s 接的是「素材图片」节点，但它没填「图片路径」。"), *SegmentLabel);
                    return Result;
                }

                WarnIfMissing(AnchorImagePath, DescribeNode(PlainImageNode), Result.Warnings);
                Caption = GetTextParameter(PlainImageNode, ShineVideoImageNode::LabelParameter);
            }
            else if (UShineVideoShotGraphNode* DirectShotNode = Cast<UShineVideoShotGraphNode>(SourceNode))
            {
                // 也允许分镜直接进视频组（跳过"先出分镜图"这一级）：
                // 这时候槽位就是 H3 的参考图，编号从 1 起、没有偏移。
                ShotNode = DirectShotNode;
            }
            else
            {
                Result.ErrorMessage = FString::Printf(
                    TEXT("%s 接的是 %s，视频组只收「分镜图」「素材图片」或「分镜」。"),
                    *SegmentLabel, *DescribeNode(Cast<UShineComfyGraphNodeBase>(SourceNode)));
                return Result;
            }

            // ---- 2. 采样参数与提示词

            FShineVideoShot Shot;
            FString Prompt;
            FString IdentityPrompt;
            TOptional<int32> ForcedSeed;
            TArray<FString> OrderedImages;

            /** 分镜图占到 Picture 1 时，槽位编号整体后移一位。 */
            int32 PictureNumberOffset = 0;

            if (ShotNode)
            {
                ReferencedNodes.Add(ShotNode);

                Prompt = ShotNode->GetPromptText();
                PictureNumberOffset = ShotNode->GetPictureNumberOffset();

                // 剧本节点：把全局风格/场景注入这一段。**只有真的连了线才注入**——
                // 图上多写几个剧本草稿不该互相污染，而且"谁影响了谁"必须一眼能从线上看出来。
                // 局部变量名带 Link 后缀：本文件有 `using namespace ShineVideoShotNode;`，
                // 同名局部变量会触发 C4459（UE 当错误）。
                if (const UEdGraphPin* ScriptLinkPin = ShotNode->FindPin(ShineVideoShotNode::ScriptPin))
                {
                    if (ScriptLinkPin->LinkedTo.Num() > 0)
                    {
                        UEdGraphNode* ScriptSourceNode = ScriptLinkPin->LinkedTo[0]->GetOwningNode();
                        ReferencedNodes.Add(ScriptSourceNode);

                        if (const UShineVideoScriptGraphNode* ScriptNode = Cast<UShineVideoScriptGraphNode>(ScriptSourceNode))
                        {
                            ApplyScriptNode(ScriptNode, Prompt);
                        }
                        else
                        {
                            Result.Warnings.Add(FString::Printf(
                                TEXT("第 %d 段的「剧本」输入接的不是剧本节点（%s），这一段不会被注入风格/场景。"),
                                SegmentOrdinal, *DescribeNode(Cast<UShineComfyGraphNodeBase>(ScriptSourceNode))));
                        }
                    }
                }

                const FString ShotTitle = ShotNode->GetShotTitle();
                if (!ShotTitle.IsEmpty())
                {
                    Caption = ShotTitle;
                }

                const FString ModeText = GetTextParameter(ShotNode, ModeParameter);
                Shot.Mode = ModeText.Equals(ModeFirstLastFrame) ? EShineVideoShotMode::FirstLastFrame : EShineVideoShotMode::Reference;

                const FString RefSize = GetTextParameter(ShotNode, RefImageSizeParameter);
                Shot.RefImageSize = RefSize.Equals(RefSizeMax) ? RefSizeMax : RefSizeMatch;

                Shot.Width = GetIntParameter(ShotNode, WidthParameter, Shot.Width);
                Shot.Height = GetIntParameter(ShotNode, HeightParameter, Shot.Height);
                Shot.Length = GetIntParameter(ShotNode, LengthParameter, Shot.Length);
                Shot.Steps = GetIntParameter(ShotNode, StepsParameter, Shot.Steps);
                Shot.Cfg = GetFloatParameter(ShotNode, CfgParameter, Shot.Cfg);
                Shot.Seed = GetIntParameter(ShotNode, SeedParameter, Shot.Seed);
                Shot.Denoise = GetFloatParameter(ShotNode, DenoiseParameter, Shot.Denoise);
                Shot.ShiftVideo = GetFloatParameter(ShotNode, ShiftVideoParameter, Shot.ShiftVideo);
                Shot.ShiftAudio = GetFloatParameter(ShotNode, ShiftAudioParameter, Shot.ShiftAudio);

                // ---- 3. 槽位展开（按槽位下标，空槽位跳过）

                TArray<UEdGraphPin*> PicturePins;
                ShotNode->GetPicturePins(PicturePins);

                bool bSeenFreeSlot = false;
                for (int32 PictureSlot = 0; PictureSlot < PicturePins.Num(); ++PictureSlot)
                {
                    const UEdGraphPin* PicturePin = PicturePins[PictureSlot];
                    const bool bLinked = PicturePin && PicturePin->LinkedTo.Num() > 0;

                    if (!bLinked)
                    {
                        bSeenFreeSlot = true;
                        continue;
                    }

                    if (bSeenFreeSlot)
                    {
                        Result.Warnings.Add(FString::Printf(
                            TEXT("第 %d 段的分镜槽位有空档（前面的槽位空着、后面的接了线）："
                                 "空槽位会被跳过，所以后面这些线的编号会往前挤——"
                                 "要么把空档补上，要么把线挪到前面的槽位，画布上的编号徽标才是提交后真实生效的编号。"),
                            SegmentOrdinal));
                        bSeenFreeSlot = false;
                    }

                    ExpandPictureSlotSource(
                        PicturePin->LinkedTo[0]->GetOwningNode(),
                        IdentityPrompt,
                        OrderedImages,
                        CharacterAssetPaths,
                        ForcedSeed,
                        ReferencedNodes,
                        Result.Warnings);
                }
            }
            else
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("第 %d 段没有上游「分镜」节点（只有一张图）：这段会拿默认采样参数、不带提示词地跑一遍，"
                         "只适合当占位测试。要写提示词就把「分镜」节点接进分镜图。"),
                    SegmentOrdinal));
            }

            // ---- 4. 组装参考图与首帧

            // 分镜图（锚图）占 `<Picture 1>`；首尾帧模式下它是首帧，不进参考图列表
            //（fl2va 节点根本没有参考图输入，放进去只会误导）。
            if (!AnchorImagePath.IsEmpty() && Shot.Mode == EShineVideoShotMode::Reference)
            {
                OrderedImages.Insert(ResolveLocalPath(AnchorImagePath), 0);
            }

            if (Shot.Mode == EShineVideoShotMode::FirstLastFrame)
            {
                Shot.FirstFrameImage = !AnchorImagePath.IsEmpty()
                    ? ResolveLocalPath(AnchorImagePath)
                    : (OrderedImages.Num() > 0 ? OrderedImages[0] : FString());

                if (Shot.FirstFrameImage.IsEmpty())
                {
                    Result.Warnings.Add(FString::Printf(
                        TEXT("第 %d 段走首尾帧驱动，但没有首帧：H3 会退化成纯文本驱动。"), SegmentOrdinal));
                }

                // 首帧已经用掉一张，剩下的还能当参考图（builder 会提示它们不会被使用）。
            }

            if (OrderedImages.Num() > MaxReferenceImages)
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("第 %d 段有 %d 张参考图，超过 H3 的 %d 张上限；多出来的会被静默忽略，这里先裁掉。"),
                    SegmentOrdinal, OrderedImages.Num(), MaxReferenceImages));
                OrderedImages.SetNum(MaxReferenceImages);
            }

            Shot.ReferenceImages = OrderedImages;

            // ---- 5. 提示词：追加角色身份描述 + 编号偏移提醒

            if (!IdentityPrompt.IsEmpty())
            {
                if (!Prompt.IsEmpty() && !Prompt.EndsWith(TEXT(" ")))
                {
                    Prompt += TEXT(" ");
                }

                Prompt += FString::Printf(TEXT("Keep the exact identity of the subject: %s."), *IdentityPrompt);
            }

            Shot.Prompt = Prompt;
            Shot.Title = Caption.IsEmpty() ? FString::Printf(TEXT("第 %d 段"), SegmentOrdinal) : Caption;

            if (ForcedSeed.IsSet())
            {
                Shot.Seed = ForcedSeed.GetValue();
            }

            if (PictureNumberOffset > 0 && !Result.Warnings.ContainsByPredicate(
                [SegmentOrdinal](const FString& Warning) { return Warning.Contains(FString::Printf(TEXT("第 %d 段"), SegmentOrdinal)) && Warning.Contains(TEXT("编号")); }))
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("第 %d 段有上游分镜图，它会占掉 <Picture 1>，所以分镜槽位的编号从 <Picture 2> 起算"
                         "（画布上的徽标已经按这个显示）。"),
                    SegmentOrdinal));
            }

            WarnAboutMentionSyntaxInPrompt(Prompt, SegmentOrdinal, Result.Warnings);

            // ---- 6. 链式：视频组的自环线

            if (const UEdGraphPin* ChainPin = Group->GetChainInputPin(SlotIndex))
            {
                if (ChainPin->LinkedTo.Num() > 0)
                {
                    const UEdGraphPin* SourceOutputPin = ChainPin->LinkedTo[0];
                    if (UShineVideoGroupGraphNode::IsChainLink(SourceOutputPin, ChainPin))
                    {
                        Shot.bChainFromPrevious = true;
                    }
                    else
                    {
                        Result.Warnings.Add(FString::Printf(
                            TEXT("%s 的「链」输入接的不是上一段的输出，这一段的链式被忽略。"), *SegmentLabel));
                    }
                }
            }

            if (Shot.bChainFromPrevious && SegmentOrdinal == 1)
            {
                // 第 1 段没有上一段可链；接线本身表达不了这个（IsChainLink 已经要求 i-1>=0），
                // 所以这里只可能在"输出接到了自己"这种畸形连线上出现。
                Shot.bChainFromPrevious = false;
            }

            Result.Shots.Add(Shot);

            FShineVideoSegmentBinding& Binding = Result.Bindings.AddDefaulted_GetRef();
            Binding.VideoGroupNodeId = Group->NodeGuid;
            Binding.SlotIndex = SlotIndex;

            Result.SegmentCaptions.Add(Shot.Title);
        }
    }

    if (Result.Shots.Num() == 0)
    {
        Result.ErrorMessage = TEXT("视频组上一个输入都没接：至少把一张「分镜图」或「素材图片」接进 #1。");
        return Result;
    }

    // ---- 7. 孤立节点提示：画布上写了但没接进去的东西，不提示的话用户会以为已经生效了

    for (UEdGraphNode* GraphNode : Graph.Nodes)
    {
        if (!GraphNode || GraphNode->IsA<UShineVideoGroupGraphNode>() || ReferencedNodes.Contains(GraphNode))
        {
            continue;
        }

        const UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(GraphNode);
        if (!ShineNode)
        {
            continue;
        }

        Result.Warnings.Add(FString::Printf(
            TEXT("节点 %s 没有接到视频组的链路上，本次生成不会用到它。"),
            *ShineNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
    }

    for (const FString& CharacterPath : CharacterAssetPaths)
    {
        Result.CharacterAssetPaths.Add(CharacterPath);
    }

    Result.bSuccess = true;
    return Result;
}

void FShineVideoGraphCompiler::ApplyToProject(const FShineVideoGraphCompileResult& Result, UShineVideoProject& Project)
{
    Project.Shots = Result.Shots;
    Project.CharacterAssetPaths = Result.CharacterAssetPaths;

    if (!Result.FilenamePrefix.IsEmpty())
    {
        Project.FilenamePrefix = Result.FilenamePrefix;
    }

    // 上一次运行留下的产物/错误是"运行期字段"，编译出新分镜表之后它们已经对不上新的一组段了，
    // 留着只会让人以为这次已经跑出了东西（runner 提交前也会清一遍，这里先清更直观）。
    Project.Sanitize();
}

bool FShineVideoGraphCompiler::ApplyTaskStatus(UShineVideoGraph& Graph, const FShineVideoGraphCompileResult& Result, const FShineVideoTaskStatus& Status)
{
    bool bChanged = false;

    // 把"这次任务里第几段"翻译成"哪个视频组节点的哪个槽位"。
    TMap<FGuid, UShineVideoGroupGraphNode*> GroupNodeById;
    for (UEdGraphNode* GraphNode : Graph.Nodes)
    {
        if (UShineVideoGroupGraphNode* GroupNode = Cast<UShineVideoGroupGraphNode>(GraphNode))
        {
            GroupNodeById.Add(GroupNode->NodeGuid, GroupNode);
        }
    }

    auto ResolveNode = [&GroupNodeById](const FShineVideoSegmentBinding& Binding) -> UShineVideoGroupGraphNode*
    {
        UShineVideoGroupGraphNode** Found = GroupNodeById.Find(Binding.VideoGroupNodeId);
        return Found ? *Found : nullptr;
    };

    // 先把每一段重置成"本次任务还没轮到它"。任务不活跃时保留上一次的痕迹（那是用户想看到的结果）。
    if (Status.IsActive())
    {
        for (int32 ShotIndex = 0; ShotIndex < Result.Bindings.Num(); ++ShotIndex)
        {
            if (UShineVideoGroupGraphNode* GroupNode = ResolveNode(Result.Bindings[ShotIndex]))
            {
                if (GroupNode->GetSegmentState(Result.Bindings[ShotIndex].SlotIndex) != EShineVideoNodeRuntimeState::Pending)
                {
                    GroupNode->SetSegmentProgress(Result.Bindings[ShotIndex].SlotIndex,
                        EShineVideoNodeRuntimeState::Pending, 0, 0, FString());
                    bChanged = true;
                }
            }
        }
    }

    for (const FShineVideoShotProgress& ShotProgress : Status.Shots)
    {
        if (!Result.Bindings.IsValidIndex(ShotProgress.ShotIndex))
        {
            continue;
        }

        UShineVideoGroupGraphNode* GroupNode = ResolveNode(Result.Bindings[ShotProgress.ShotIndex]);
        if (!GroupNode)
        {
            continue;
        }

        const int32 SlotIndex = Result.Bindings[ShotProgress.ShotIndex].SlotIndex;

        EShineVideoNodeRuntimeState NewState = EShineVideoNodeRuntimeState::Idle;
        if (ShotProgress.IsFinished())
        {
            NewState = EShineVideoNodeRuntimeState::Finished;
        }
        else if (ShotProgress.IsFailed())
        {
            NewState = EShineVideoNodeRuntimeState::Failed;
        }
        else if (ShotProgress.IsRunning())
        {
            NewState = EShineVideoNodeRuntimeState::Running;
        }
        else if (Status.IsActive())
        {
            NewState = EShineVideoNodeRuntimeState::Pending;
        }

        int32 OldStep = 0;
        int32 OldStepMax = 0;
        GroupNode->GetSegmentStep(SlotIndex, OldStep, OldStepMax);

        // 只在"看得见的东西变了"的时候才算改动：进度事件一次两段链式有几百条，
        // 每一条都触发 NotifyGraphChanged 会把所有节点控件反复拆建。
        const bool bStateChanged = GroupNode->GetSegmentState(SlotIndex) != NewState;
        const bool bStepChanged = OldStep != ShotProgress.StepValue || OldStepMax != ShotProgress.StepMax;

        if (bStateChanged || bStepChanged)
        {
            GroupNode->SetSegmentProgress(SlotIndex, NewState,
                ShotProgress.StepValue, ShotProgress.StepMax, ShotProgress.State);
            bChanged = true;
        }

        if (ShotProgress.OutputFiles.Num() > 0)
        {
            const FString& LastFile = ShotProgress.OutputFiles.Last();
            if (GroupNode->GetSegmentOutputFile(SlotIndex) != LastFile)
            {
                GroupNode->SetSegmentOutputFile(SlotIndex, LastFile);
                bChanged = true;
            }
        }
    }

    if (!Result.SegmentCaptions.IsEmpty())
    {
        // 段标题只在编译后变化，每次都写一遍没有副作用（它只是数组赋值）。
        for (int32 ShotIndex = 0; ShotIndex < Result.Bindings.Num(); ++ShotIndex)
        {
            if (UShineVideoGroupGraphNode* GroupNode = ResolveNode(Result.Bindings[ShotIndex]))
            {
                GroupNode->SetSegmentCaptions(Result.SegmentCaptions);
            }
        }
    }

    return bChanged;
}
