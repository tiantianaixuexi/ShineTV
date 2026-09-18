#include "Comfy/ShineVideoTaskConsole.h"

#include "Asset/ShineVideoGraph.h"
#include "Asset/ShineVideoProject.h"
#include "Asset/ShineVideoTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Comfy/Builders/SceneToImage/ShineSceneToImageWorkflowBuilder.h"
#include "Comfy/ShineComfyPaths.h"
#include "Comfy/ShineImageTaskRunner.h"
#include "Comfy/ShineVideoGraphCompiler.h"
#include "Comfy/ShineVideoTaskRunner.h"
#include "EdGraph/EdGraphPin.h"
#include "FileHelpers.h"
#include "Graph/Node/Presets/ShineComfyDirectorGraphNode.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Node/Video/ShineVideoGroupGraphNode.h"
#include "Graph/Node/Video/ShineVideoImageGraphNode.h"
#include "Graph/Node/Video/ShineVideoScriptGraphNode.h"
#include "Graph/Node/Video/ShineVideoShotGraphNode.h"
#include "Graph/Node/Video/ShineVideoShotImageGraphNode.h"
#include "Graph/ShineComfyGraph.h"
#include "Graph/ShineComfyGraphSchema.h"
#include "Graph/ShineVideoGraphSchema.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogShineH3Console, Log, All);

namespace
{
    TArray<IConsoleCommand*> GConsoleCommands;

    /** 当前那次任务（一次只允许跑一个）。 */
    TSharedPtr<FShineVideoTaskRunner> GActiveRunner;

    FString GLastLoggedDetail;
    int32 GLastLoggedStep = -1;

    /** 面板上的进度是给眼睛看的，控制台日志得节流，否则 25 步刷 25 行。 */
    constexpr int32 StepLogInterval = 5;

    FString NormalizeAssetPath(const FString& InPath)
    {
        FString Path = InPath.TrimStartAndEnd();
        Path.ReplaceInline(TEXT("\\"), TEXT("/"));

        // "/Game/X/Y" 这种没有 "." 的写法 LoadObject 也认，但补全更稳。
        if (!Path.Contains(TEXT(".")))
        {
            Path = FString::Printf(TEXT("%s.%s"), *Path, *FPaths::GetCleanFilename(Path));
        }

        return Path;
    }

    UShineVideoProject* LoadProjectAsset(const FString& InPath)
    {
        return LoadObject<UShineVideoProject>(nullptr, *NormalizeAssetPath(InPath));
    }

    void LogUsage()
    {
        UE_LOG(LogShineH3Console, Display, TEXT("用法："));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.CreateTestProject <资产路径> <图1> [图2 ...]"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.CreateWorkbenchGraph <资产路径> <图1> [图2 ...]   建一张两段链式的工作台画布"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.CompileGraph <资产路径> [-dump]                 画布 → 分镜表（纯计算，零 GPU 成本）"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.RunProject <资产路径> [-url=http://127.0.0.1:8188] [-python=<python.exe>] [-dryrun] [-unet=<文件名>] [-mode=ref|fl] [-segments=<n>] [-tag=<后缀>]"));
        UE_LOG(LogShineH3Console, Display, TEXT("      -unet / -mode / -segments 只作用于这一次运行、不写回资产（三组对照跑用）；-tag 给写出的节点图加后缀，避免三跑互相覆盖。"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.GenerateStoryboards <资产路径> [-dryrun] [-nobrothers] [-url=] [-steps=] [-denoise=] [-cfg=] [-width=] [-height=] [-seed=]"));
        UE_LOG(LogShineH3Console, Display, TEXT("      给画布上每颗「分镜图」节点出一张图（第二级：分镜图 → 视频）。-dryrun 只编译节点图、不提交。"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.BuildStoryboardProbe <资产路径> [-nobrothers] [-out=<目录>] [同样的参数]"));
        UE_LOG(LogShineH3Console, Display, TEXT("      零成本对拍：同一个参数下，图片版 builder 的图 vs Director 导出的图，两份 JSON 都写出来。"));
        UE_LOG(LogShineH3Console, Display, TEXT("例："));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.CreateTestProject /Game/ShineTests/H3Test \"H:/ComfyUI-aki-v3/ComfyUI/input/Scene_20260913_042611_807_Color.png\" \"H:/ComfyUI-aki-v3/ComfyUI/input/Scene_20260913_042600_612_Color.png\""));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.CreateWorkbenchGraph /Game/ShineTests/H3Workbench \"H:/ComfyUI-aki-v3/ComfyUI/input/Scene_20260913_042611_807_Color.png\" \"H:/ComfyUI-aki-v3/ComfyUI/input/Scene_20260913_042600_612_Color.png\""));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.CompileGraph /Game/ShineTests/H3Workbench -dump"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.RunProject /Game/ShineTests/H3Workbench"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.GenerateStoryboards /Game/ShineTests/H3Workbench"));
        UE_LOG(LogShineH3Console, Display, TEXT("  Shine.H3.BuildStoryboardProbe /Game/ShineTests/H3Workbench"));
    }

    /** ComfyUI 的 python_embeded 里自带 PyAV，MediaInfo.py 用它就能查音轨，不用另装 ffmpeg。 */
    FString DeriveEmbeddedPython()
    {
        const FString OutputDirectory = ShineComfyPaths::GetConfiguredOutputDirectory();
        if (OutputDirectory.IsEmpty())
        {
            return FString();
        }

        // .../ComfyUI/output → 上两级 → .../python_embeded/python.exe
        const FString ComfyRoot = FPaths::GetPath(FPaths::GetPath(OutputDirectory));
        const FString Candidate = FPaths::Combine(ComfyRoot, TEXT("python_embeded"), TEXT("python.exe"));
        return FPaths::FileExists(Candidate) ? Candidate : FString();
    }

    void VerifyWithMediaInfo(const FString& PythonPathOverride, const TArray<FString>& OutputFiles)
    {
        if (OutputFiles.Num() == 0)
        {
            return;
        }

        const FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Scripts/H3/MediaInfo.py"));

        FString PythonPath = PythonPathOverride;
        if (PythonPath.IsEmpty() || !FPaths::FileExists(PythonPath))
        {
            const FString KnownPath = TEXT("H:/ComfyUI-aki-v3/python_embeded/python.exe");
            PythonPath = FPaths::FileExists(KnownPath) ? KnownPath : DeriveEmbeddedPython();
        }

        FString QuotedFiles;
        for (const FString& OutputFile : OutputFiles)
        {
            QuotedFiles += FString::Printf(TEXT(" \"%s\""), *OutputFile);
        }

        if (PythonPath.IsEmpty() || !FPaths::FileExists(ScriptPath))
        {
            UE_LOG(LogShineH3Console, Display,
                TEXT("没找到可用的 python 或 %s，请自己执行：%s \"%s\"%s"),
                *ScriptPath,
                PythonPath.IsEmpty() ? TEXT("python") : *PythonPath,
                *ScriptPath,
                *QuotedFiles);
            return;
        }

        // 同步执行：MediaInfo 只是读一下容器头，几百毫秒；这一步跑完任务本身就结束了，
        // 不会卡住正在生成的流程。
        const FString Params = FString::Printf(TEXT("\"%s\"%s"), *ScriptPath, *QuotedFiles);
        int32 ReturnCode = 0;
        FString StdOut;
        UE_LOG(LogShineH3Console, Display, TEXT("调用 \"%s\" %s"), *PythonPath, *Params);

        if (FPlatformProcess::ExecProcess(*PythonPath, *Params, &ReturnCode, &StdOut, nullptr))
        {
            UE_LOG(LogShineH3Console, Display, TEXT("---- MediaInfo 校验 ----\n%s\n------------------------"), *StdOut);
        }
        else
        {
            UE_LOG(LogShineH3Console, Warning, TEXT("MediaInfo.py 执行失败（返回码 %d）。"), ReturnCode);
        }
    }

    void LogStatus(const FShineVideoTaskStatus& Status)
    {
        if (!Status.Detail.IsEmpty() && Status.Detail != GLastLoggedDetail)
        {
            GLastLoggedDetail = Status.Detail;
            UE_LOG(LogShineH3Console, Display, TEXT("[%s] %s"),
                FShineVideoTaskRunner::ToString(Status.State), *Status.Detail);
        }

        // 逐节点进度：只报"当前跑的是谁、跑到第几步"，每 5 步一行。
        if (Status.TotalSteps > 0
            && (Status.CurrentStep - GLastLoggedStep >= StepLogInterval || Status.CurrentStep < GLastLoggedStep))
        {
            GLastLoggedStep = Status.CurrentStep;
            UE_LOG(LogShineH3Console, Display, TEXT("  节点 %s（%s）%d/%d"),
                *Status.CurrentNodeId,
                Status.CurrentNodeClass.IsEmpty() ? TEXT("?") : *Status.CurrentNodeClass,
                Status.CurrentStep, Status.TotalSteps);
        }
    }

    // ---------------------------------------------------------------- 出分镜图（SD1.5）

    FString GLastLoggedImageDetail;
    int32 GLastLoggedImageStep = -1;

    void LogImageStatus(const FShineImageTaskStatus& Status)
    {
        if (!Status.Detail.IsEmpty() && Status.Detail != GLastLoggedImageDetail)
        {
            GLastLoggedImageDetail = Status.Detail;
            UE_LOG(LogShineH3Console, Display, TEXT("[%s] %s"),
                FShineImageTaskRunner::ToString(Status.State), *Status.Detail);
        }

        if (Status.TotalSteps > 0
            && (Status.CurrentStep - GLastLoggedImageStep >= StepLogInterval || Status.CurrentStep < GLastLoggedImageStep))
        {
            GLastLoggedImageStep = Status.CurrentStep;
            UE_LOG(LogShineH3Console, Display, TEXT("  节点 %s（%s）%d/%d"),
                *Status.CurrentNodeId,
                Status.CurrentNodeClass.IsEmpty() ? TEXT("?") : *Status.CurrentNodeClass,
                Status.CurrentStep, Status.TotalSteps);
        }
    }

    /**
     * 出图参数的临时覆盖。
     *
     * 存在的理由：出一张分镜图只要几秒，而项目的图片侧配置是"资产上的定稿值"。
     * 试参数时为了改个步数就去动资产（还要记得改回来）很容易忘，所以这里给一组命令行旋钮，
     * **只影响这一次运行**。控制台的 `BuildStoryboardProbe` 走同一组旋钮，对拍的两边才可能
     * 参数一致。
     */
    struct FImageParamOverride
    {
        TOptional<int32> Steps;
        TOptional<int32> Width;
        TOptional<int32> Height;
        TOptional<int32> Seed;
        TOptional<double> Cfg;
        TOptional<double> Denoise;
    };

    struct FStoryboardArgs
    {
        FString AssetPath;
        FString BaseUrlOverride;
        FString OutputDirectory;
        bool bDryRun = false;
        bool bNoBrothers = false;
        FImageParamOverride Override;
    };

    bool ParseStoryboardArgs(const TArray<FString>& Args, FStoryboardArgs& Out)
    {
        for (const FString& Arg : Args)
        {
            if (Arg.Equals(TEXT("-dryrun"), ESearchCase::IgnoreCase))
            {
                Out.bDryRun = true;
            }
            else if (Arg.Equals(TEXT("-nobrothers"), ESearchCase::IgnoreCase))
            {
                Out.bNoBrothers = true;
            }
            else if (Arg.StartsWith(TEXT("-url=")))
            {
                Out.BaseUrlOverride = Arg.RightChop(5).TrimQuotes();
            }
            else if (Arg.StartsWith(TEXT("-out=")))
            {
                Out.OutputDirectory = Arg.RightChop(5).TrimQuotes();
            }
            else if (Arg.StartsWith(TEXT("-steps=")))
            {
                Out.Override.Steps = FCString::Atoi(*Arg.RightChop(7));
            }
            else if (Arg.StartsWith(TEXT("-width=")))
            {
                Out.Override.Width = FCString::Atoi(*Arg.RightChop(7));
            }
            else if (Arg.StartsWith(TEXT("-height=")))
            {
                Out.Override.Height = FCString::Atoi(*Arg.RightChop(8));
            }
            else if (Arg.StartsWith(TEXT("-seed=")))
            {
                Out.Override.Seed = FCString::Atoi(*Arg.RightChop(6));
            }
            else if (Arg.StartsWith(TEXT("-cfg=")))
            {
                Out.Override.Cfg = FCString::Atof(*Arg.RightChop(5));
            }
            else if (Arg.StartsWith(TEXT("-denoise=")))
            {
                Out.Override.Denoise = FCString::Atof(*Arg.RightChop(9));
            }
            else if (Out.AssetPath.IsEmpty())
            {
                Out.AssetPath = Arg.TrimQuotes();
            }
        }

        return !Out.AssetPath.IsEmpty();
    }

    void ApplyImageOverride(const FImageParamOverride& Override, FShineSceneToImageRequest& InOutWorkflow)
    {
        if (Override.Steps.IsSet())
        {
            InOutWorkflow.Steps = Override.Steps.GetValue();
        }
        if (Override.Width.IsSet())
        {
            InOutWorkflow.Width = Override.Width.GetValue();
        }
        if (Override.Height.IsSet())
        {
            InOutWorkflow.Height = Override.Height.GetValue();
        }
        if (Override.Seed.IsSet())
        {
            InOutWorkflow.Seed = Override.Seed.GetValue();
        }
        if (Override.Cfg.IsSet())
        {
            InOutWorkflow.Cfg = Override.Cfg.GetValue();
        }
        if (Override.Denoise.IsSet())
        {
            InOutWorkflow.Denoise = Override.Denoise.GetValue();
        }
    }

    /** 画布上所有「分镜图」节点，按画布位置排（与视频组的段序同一套规则）。 */
    void GetStoryboardNodesInOrder(const UShineVideoGraph& Graph, TArray<UShineVideoShotImageGraphNode*>& OutNodes)
    {
        OutNodes.Reset();

        for (UEdGraphNode* GraphNode : Graph.Nodes)
        {
            if (UShineVideoShotImageGraphNode* ShotImageNode = Cast<UShineVideoShotImageGraphNode>(GraphNode))
            {
                OutNodes.Add(ShotImageNode);
            }
        }

        OutNodes.Sort([](const UShineVideoShotImageGraphNode& Left, const UShineVideoShotImageGraphNode& Right)
        {
            // 先按 Y 分行（同一行容差 120），行内按 X：并排放在同一行不会因 Y 差几像素乱序。
            constexpr int32 RowTolerance = 120;
            if (FMath::Abs(Left.NodePosY - Right.NodePosY) > RowTolerance)
            {
                return Left.NodePosY < Right.NodePosY;
            }

            return Left.NodePosX < Right.NodePosX;
        });
    }

    bool WritePrettyJson(const FString& Json, const FString& FilePath)
    {
        FString TextToWrite = Json;

        TSharedPtr<FJsonObject> Parsed;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
        {
            FString Pretty;
            const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
                TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Pretty);
            if (FJsonSerializer::Serialize(Parsed.ToSharedRef(), Writer))
            {
                TextToWrite = Pretty;
            }
        }

        IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
        return FFileHelper::SaveStringToFile(TextToWrite, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    /**
     * 对拍的"探针"：同一套参数造一颗 `EnableImageGen=true` 的导演台节点，导出它的 prompt。
     *
     * 探针必须来自**真正被用户用过的那条链路**（`DirectorNode.json` + `ExportComfyPromptToJson`，
     * `ShineSceneToImage.py` 每次出图走的都是它），而不是我手抄一份 JSON——手抄的那份只能证明
     * "我抄得对"，证明不了 builder 和线上链路一致。
     */
    bool ExportDirectorImagePrompt(const FShineSceneToImageRequest& Request, FString& OutJson, FString& OutError)
    {
        UShineComfyGraph* Graph = NewObject<UShineComfyGraph>(GetTransientPackage(), NAME_None, RF_Transient);
        if (!Graph)
        {
            OutError = TEXT("建不出临时 Shine Comfy 图。");
            return false;
        }

        Graph->Schema = UShineComfyGraphSchema::StaticClass();
        Graph->bEditable = true;

        UShineComfyGraphNodeBase* DirectorNode = NewObject<UShineComfyGraphNodeBase>(
            Graph, UShineComfyDirectorGraphNode::StaticClass());
        if (!DirectorNode)
        {
            OutError = TEXT("建不出导演台节点（Resources/DirectorNode.json 读不到？）。");
            return false;
        }

        // 顺序照抄 FShineComfySchemaAction_NewNode::PerformAction。
        Graph->AddNode(DirectorNode, true, false);
        DirectorNode->CreateNewGuid();
        DirectorNode->PostPlacedNewNode();
        DirectorNode->AllocateDefaultPins();

        DirectorNode->SetBoolParameter(FName(TEXT("EnableImageGen")), true);
        DirectorNode->SetBoolParameter(FName(TEXT("EnableVideoGen")), false);
        DirectorNode->SetTextParameter(FName(TEXT("Checkpoint")), Request.Checkpoint);
        DirectorNode->SetTextParameter(FName(TEXT("PositivePrompt")), Request.PositivePrompt);
        DirectorNode->SetTextParameter(FName(TEXT("NegativePrompt")), Request.NegativePrompt);
        DirectorNode->SetTextParameter(FName(TEXT("ColorImage")), Request.ColorImageName);
        DirectorNode->SetTextParameter(FName(TEXT("DepthImage")), Request.DepthImageName);
        DirectorNode->SetTextParameter(FName(TEXT("NormalImage")), Request.NormalImageName);
        DirectorNode->SetTextParameter(FName(TEXT("DepthControlNet")), Request.DepthControlNet);
        DirectorNode->SetTextParameter(FName(TEXT("NormalControlNet")), Request.NormalControlNet);
        DirectorNode->SetIntegerParameter(FName(TEXT("Width")), Request.Width);
        DirectorNode->SetIntegerParameter(FName(TEXT("Height")), Request.Height);
        DirectorNode->SetIntegerParameter(FName(TEXT("Steps")), Request.Steps);
        DirectorNode->SetIntegerParameter(FName(TEXT("Seed")), Request.Seed);
        DirectorNode->SetFloatParameter(FName(TEXT("CfgScale")), Request.Cfg);
        DirectorNode->SetFloatParameter(FName(TEXT("Denoise")), Request.Denoise);
        DirectorNode->SetFloatParameter(FName(TEXT("DepthStrength")), Request.DepthStrength);
        DirectorNode->SetFloatParameter(FName(TEXT("NormalStrength")), Request.NormalStrength);
        DirectorNode->SetFloatParameter(FName(TEXT("ControlEndPercent")), Request.ControlEndPercent);
        DirectorNode->SetTextParameter(FName(TEXT("SamplerName")), Request.SamplerName);
        DirectorNode->SetTextParameter(FName(TEXT("Scheduler")), Request.Scheduler);
        DirectorNode->SetTextParameter(FName(TEXT("OutputPrefix")), Request.OutputPrefix);

        if (!Graph->ExportComfyPromptToJson(OutJson, OutError))
        {
            return false;
        }

        return true;
    }

    void OnRunnerFinished(TSharedPtr<FShineVideoTaskRunner> Runner, const FString& PythonPathOverride, const FString& ProjectName)
    {
        if (!Runner.IsValid())
        {
            return;
        }

        const FShineVideoTaskStatus& Status = Runner->GetStatus();

        for (const FString& Warning : Status.Warnings)
        {
            UE_LOG(LogShineH3Console, Warning, TEXT("提示：%s"), *Warning);
        }

        for (const FShineVideoShotProgress& Shot : Status.Shots)
        {
            UE_LOG(LogShineH3Console, Display, TEXT("分镜 %s：%s，产物 %d 个"),
                *Shot.Title, *Shot.State, Shot.OutputFiles.Num());
            for (const FString& OutputFile : Shot.OutputFiles)
            {
                UE_LOG(LogShineH3Console, Display, TEXT("    %s"), *OutputFile);
            }
        }

        if (Status.State == EShineVideoTaskState::Completed)
        {
            const FString GraphPath = Runner->SaveBuiltGraphJson(ProjectName);
            if (!GraphPath.IsEmpty())
            {
                UE_LOG(LogShineH3Console, Display, TEXT("节点图已写出：%s"), *GraphPath);
            }

            VerifyWithMediaInfo(PythonPathOverride, Status.OutputFiles);
        }
        else
        {
            UE_LOG(LogShineH3Console, Error, TEXT("任务未成功（%s）：%s"),
                FShineVideoTaskRunner::ToString(Status.State), *Status.ErrorMessage);
        }

        GActiveRunner.Reset();
    }

    void HandleCreateTestProject(const TArray<FString>& Args)
    {
        if (Args.Num() < 2)
        {
            LogUsage();
            return;
        }

        const FString PackagePath = Args[0].TrimStartAndEnd();
        const FString AssetName = FPaths::GetCleanFilename(PackagePath);
        if (AssetName.IsEmpty())
        {
            UE_LOG(LogShineH3Console, Error, TEXT("资产路径不合法：%s"), *PackagePath);
            return;
        }

        UShineVideoProject* Project = LoadObject<UShineVideoProject>(nullptr, *NormalizeAssetPath(PackagePath));
        if (!Project)
        {
            UPackage* Package = CreatePackage(*PackagePath);
            Project = NewObject<UShineVideoProject>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Project);
        }

        TArray<FString> ReferenceImages;
        for (int32 ArgIndex = 1; ArgIndex < Args.Num(); ++ArgIndex)
        {
            const FString ImagePath = Args[ArgIndex].TrimStartAndEnd().TrimQuotes();
            if (ImagePath.IsEmpty())
            {
                continue;
            }

            if (!FPaths::FileExists(ImagePath))
            {
                UE_LOG(LogShineH3Console, Error, TEXT("参考图不存在：%s"), *ImagePath);
                return;
            }

            ReferenceImages.Add(FPaths::ConvertRelativePathToFull(ImagePath));
        }

        Project->ProjectName = AssetName;
        Project->FilenamePrefix = TEXT("Shine/H3");
        Project->Shots.Reset();

        // 两段链式，参数与 probe_chain2.json 对齐（1280x704 / 124 帧 / 25 步 / CFG 4.0）。
        // 第二段的提示词里那份「<Picture 3>」就是第一段的末帧——链式只能这样表达。
        FShineVideoShot FirstShot;
        FirstShot.Title = TEXT("Shot 1");
        FirstShot.Prompt = TEXT(
            "Shot 1. Keep the exact identity of the subject from <Picture 1>. "
            "A cinematic live-action medium shot of that subject standing in the environment of <Picture 2>, "
            "camera slowly pushing in, warm daylight drifting across the frame, dust motes in the air, "
            "gentle handheld micro-movement, photorealistic, natural room ambience.");
        FirstShot.ReferenceImages = ReferenceImages;
        FirstShot.Mode = EShineVideoShotMode::Reference;
        FirstShot.bChainFromPrevious = false;
        FirstShot.Seed = 42;

        FShineVideoShot SecondShot = FirstShot;
        SecondShot.Title = TEXT("Shot 2");
        SecondShot.Prompt = TEXT(
            "Shot 2, continuing directly from the previous shot. Keep the exact identity of the subject from <Picture 1>, "
            "still in the environment of <Picture 2>, and carry over the pose, framing and lighting shown in <Picture 3> "
            "so the cut is seamless. The camera keeps pushing in and then slowly orbits to the subject's left, "
            "warm daylight continues drifting, gentle handheld micro-movement, photorealistic, "
            "matching the ambience of the previous shot.");
        // 链式：上一段的末帧会被 builder 接到这一段的第三张参考图上（<Picture 3>）。
        SecondShot.bChainFromPrevious = true;
        SecondShot.Seed = 43;

        Project->Shots.Add(FirstShot);
        Project->Shots.Add(SecondShot);

        UPackage* Package = Cast<UPackage>(Project->GetOutermost());
        if (Package)
        {
            Package->MarkPackageDirty();
        }

        const bool bSaved = Package && UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
        UE_LOG(LogShineH3Console, Display, TEXT("测试项目已%s：%s（%d 段，参考图 %d 张）"),
            bSaved ? TEXT("创建并保存") : TEXT("创建（保存失败）"),
            *PackagePath, Project->Shots.Num(), ReferenceImages.Num());
        UE_LOG(LogShineH3Console, Display, TEXT("接着执行：Shine.H3.RunProject %s"), *PackagePath);
    }

    /**
     * `Shine.H3.RunProject` 的"三组对照跑"覆盖开关。
     *
     * 为什么要它：`UnetName` 只能在 Details 面板改、分镜的 `Mode` 只能在画布节点上改，
     * 而"同一套画布 / 同一套分镜表、只换一处变量"必须能无人值守地跑出来（也要能保证
     * 参数真的换了）。语义与 `Shine.H3.GenerateStoryboards` 上的 `-steps=` / `-denoise=`
     * 完全一致：**只作用于这一次运行，不写回资产**。
     */
    struct FProjectOverride
    {
        FString UnetName;
        TOptional<bool> bFirstLastFrame;
        TOptional<int32> SegmentCount;

        bool IsEmpty() const
        {
            return UnetName.IsEmpty() && !bFirstLastFrame.IsSet() && !SegmentCount.IsSet();
        }
    };

    void ApplyProjectOverride(const FProjectOverride& Override, UShineVideoProject& Project)
    {
        if (!Override.UnetName.IsEmpty())
        {
            Project.UnetName = Override.UnetName;
        }

        if (Override.bFirstLastFrame.IsSet())
        {
            const bool bFirstLast = Override.bFirstLastFrame.GetValue();

            for (FShineVideoShot& Shot : Project.Shots)
            {
                Shot.Mode = bFirstLast ? EShineVideoShotMode::FirstLastFrame : EShineVideoShotMode::Reference;
            }

            // 底座与驱动方式不匹配是这条支线最容易踩的坑：ref2va 的节点**没有** first_frame 输入。
            // builder 也会为此报警告，这里只是让人更早看见。
            const bool bUnetLooksFl = Project.UnetName.Contains(TEXT("fl2va"), ESearchCase::IgnoreCase);
            if (bFirstLast && !bUnetLooksFl)
            {
                UE_LOG(LogShineH3Console, Warning,
                    TEXT("注意：-mode=fl（首尾帧）要配 fl2va 底座，而当前 UnetName 是“%s”。"),
                    *Project.UnetName);
            }
        }

        if (Override.SegmentCount.IsSet())
        {
            const int32 Available = Project.Shots.Num();
            const int32 Requested = Override.SegmentCount.GetValue();
            const int32 Count = FMath::Clamp(Requested, 1, FMath::Max(Available, 1));

            if (Requested != Count)
            {
                UE_LOG(LogShineH3Console, Warning,
                    TEXT("-segments=%d 越界：分镜表只有 %d 段，实际按 %d 段跑。"), Requested, Available, Count);
            }

            if (Count < Available)
            {
                Project.Shots.SetNum(Count);
            }
        }
    }

    void LogProjectOverride(const FProjectOverride& Override, const UShineVideoProject& Project)
    {
        TArray<FString> Parts;
        if (!Override.UnetName.IsEmpty())
        {
            Parts.Add(FString::Printf(TEXT("unet=%s"), *Project.UnetName));
        }
        if (Override.bFirstLastFrame.IsSet())
        {
            Parts.Add(FString::Printf(TEXT("mode=%s"),
                Override.bFirstLastFrame.GetValue() ? TEXT("首尾帧") : TEXT("参考图")));
        }
        if (Override.SegmentCount.IsSet())
        {
            Parts.Add(FString::Printf(TEXT("segments=%d"), Project.Shots.Num()));
        }

        UE_LOG(LogShineH3Console, Display, TEXT("本次运行的覆盖（只影响这一次，不写回资产）：%s"),
            *FString::Join(Parts, TEXT("，")));
    }

    /** 写出的节点图叫什么（`-tag=` 时拼上后缀，三组对照跑因此不会互相覆盖）。 */
    FString MakeGraphStem(const FString& ProjectName, const FString& Tag)
    {
        return Tag.IsEmpty() ? ProjectName : FString::Printf(TEXT("%s_%s"), *ProjectName, *Tag);
    }

    void HandleRunProject(const TArray<FString>& Args)
    {
        if (Args.Num() == 0)
        {
            LogUsage();
            return;
        }

        if (GActiveRunner.IsValid() && GActiveRunner->GetStatus().IsActive())
        {
            UE_LOG(LogShineH3Console, Warning, TEXT("已经有一个任务在跑（%s），先等它结束或调用 Cancel。"),
                FShineVideoTaskRunner::ToString(GActiveRunner->GetStatus().State));
            return;
        }

        FString AssetPath;
        FString BaseUrlOverride;
        FString PythonPathOverride;
        FString Tag;
        FProjectOverride Override;
        bool bDryRun = false;
        for (const FString& Arg : Args)
        {
            if (Arg.StartsWith(TEXT("-url=")))
            {
                BaseUrlOverride = Arg.RightChop(5).TrimQuotes();
            }
            else if (Arg.StartsWith(TEXT("-python=")))
            {
                PythonPathOverride = Arg.RightChop(8).TrimQuotes();
            }
            else if (Arg.StartsWith(TEXT("-unet=")))
            {
                Override.UnetName = Arg.RightChop(6).TrimQuotes();
            }
            else if (Arg.StartsWith(TEXT("-mode=")))
            {
                const FString ModeText = Arg.RightChop(6).TrimQuotes().ToLower();
                if (ModeText == TEXT("fl") || ModeText == TEXT("firstlast"))
                {
                    Override.bFirstLastFrame = true;
                }
                else if (ModeText == TEXT("ref") || ModeText == TEXT("reference"))
                {
                    Override.bFirstLastFrame = false;
                }
                else
                {
                    // 认不出来的 mode 直接拒跑：静默忽略会让"三组对照"变成三组一样的跑。
                    UE_LOG(LogShineH3Console, Error, TEXT("-mode=%s 不认识：只认 ref / fl。"), *ModeText);
                    LogUsage();
                    return;
                }
            }
            else if (Arg.StartsWith(TEXT("-segments=")))
            {
                Override.SegmentCount = FCString::Atoi(*Arg.RightChop(10));
            }
            else if (Arg.StartsWith(TEXT("-tag=")))
            {
                Tag = Arg.RightChop(5).TrimQuotes();
            }
            else if (Arg.Equals(TEXT("-dryrun"), ESearchCase::IgnoreCase))
            {
                bDryRun = true;
            }
            else if (AssetPath.IsEmpty())
            {
                AssetPath = Arg.TrimQuotes();
            }
        }

        UShineVideoProject* Project = LoadProjectAsset(AssetPath);
        if (!Project)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("加载不到项目资产：%s"), *AssetPath);
            return;
        }

        // 覆盖只落在一份瞬态副本上：原资产一行不改（否则三组跑会互相污染，
        // 而且"上一次跑的参数"会悄悄变成下次的起点）。
        TStrongObjectPtr<UShineVideoProject> OverrideOwner;
        const UShineVideoProject* EffectiveProject = Project;
        if (!Override.IsEmpty())
        {
            UShineVideoProject* Copy = DuplicateObject<UShineVideoProject>(Project, GetTransientPackage());
            if (!Copy)
            {
                UE_LOG(LogShineH3Console, Error, TEXT("复制项目失败，覆盖参数没生效，已中止。"));
                return;
            }

            ApplyProjectOverride(Override, *Copy);
            OverrideOwner = TStrongObjectPtr<UShineVideoProject>(Copy);
            EffectiveProject = Copy;
            LogProjectOverride(Override, *Copy);
        }

        if (!EffectiveProject->HasSubmittableShot())
        {
            UE_LOG(LogShineH3Console, Error, TEXT("这个项目一个可提交的分镜都没有（每段至少要有提示词或参考图）。"));
            return;
        }

        GLastLoggedDetail.Reset();
        GLastLoggedStep = -1;

        const TSharedRef<FShineVideoTaskRunner> Runner = MakeShared<FShineVideoTaskRunner>();
        GActiveRunner = Runner;

        Runner->SetStatusCallback([](const FShineVideoTaskStatus& Status)
        {
            LogStatus(Status);
        });

        // 只捕弱引用：FinishedEvent 存在 Runner 自己身上，捕强引用就成了自环。
        Runner->FinishedEvent.AddLambda([WeakRunner = Runner.ToWeakPtr(), PythonPathOverride,
            ProjectName = MakeGraphStem(Project->GetName(), Tag)]()
        {
            OnRunnerFinished(WeakRunner.Pin(), PythonPathOverride, ProjectName);
        });

        UE_LOG(LogShineH3Console, Display, TEXT("开始跑项目：%s（%d 段）%s"),
            *Project->GetName(), EffectiveProject->Shots.Num(), bDryRun ? TEXT("—— dry-run，只编译节点图") : TEXT(""));

        if (!Runner->Run(*EffectiveProject, BaseUrlOverride, bDryRun))
        {
            UE_LOG(LogShineH3Console, Error, TEXT("启动失败：%s"), *Runner->GetStatus().ErrorMessage);
            GActiveRunner.Reset();
        }
    }

    /** 载入或新建一个项目资产（与 CreateTestProject 用的是同一套规则）。 */
    UShineVideoProject* LoadOrCreateProject(const FString& PackagePath, FString& OutAssetName)
    {
        OutAssetName = FPaths::GetCleanFilename(PackagePath);
        if (OutAssetName.IsEmpty())
        {
            UE_LOG(LogShineH3Console, Error, TEXT("资产路径不合法：%s"), *PackagePath);
            return nullptr;
        }

        UShineVideoProject* Project = LoadObject<UShineVideoProject>(nullptr, *NormalizeAssetPath(PackagePath));
        if (!Project)
        {
            UPackage* Package = CreatePackage(*PackagePath);
            Project = NewObject<UShineVideoProject>(Package, *OutAssetName, RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Project);
        }

        return Project;
    }

    bool SaveProject(UShineVideoProject* Project)
    {
        UPackage* Package = Project ? Cast<UPackage>(Project->GetOutermost()) : nullptr;
        if (!Package)
        {
            return false;
        }

        Package->MarkPackageDirty();
        return UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
    }

    /**
     * 建一张两段链式的工作台画布（无 UI 版）。
     *
     * 图的形状与 Scripts/H3/probe_chain2.json 那次的思路一致：共享身份图 + 上一段末帧连贯，
     * 只是这里通过**画布**表达——共享参考图 = 两根线接到同一个图片节点，
     * 连贯 = 视频组上的那根自环线（`VideoOut1 → Chain2`）。
     */
    void HandleCreateWorkbenchGraph(const TArray<FString>& Args)
    {
        if (Args.Num() < 2)
        {
            LogUsage();
            return;
        }

        const FString PackagePath = Args[0].TrimStartAndEnd();
        FString AssetName;
        UShineVideoProject* Project = LoadOrCreateProject(PackagePath, AssetName);
        if (!Project)
        {
            return;
        }

        TArray<FString> Images;
        for (int32 ArgIndex = 1; ArgIndex < Args.Num(); ++ArgIndex)
        {
            const FString ImagePath = Args[ArgIndex].TrimStartAndEnd().TrimQuotes();
            if (ImagePath.IsEmpty())
            {
                continue;
            }

            if (!FPaths::FileExists(ImagePath))
            {
                UE_LOG(LogShineH3Console, Error, TEXT("参考图不存在：%s"), *ImagePath);
                return;
            }

            Images.Add(FPaths::ConvertRelativePathToFull(ImagePath));
        }

        if (Images.Num() == 0)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("至少要给一张参考图（第一张会被当作「分镜图」，其余接进分镜的槽位）。"));
            return;
        }

        Project->ProjectName = AssetName;

        UShineVideoGraph* Graph = Project->GetOrCreateGraph();
        if (!Graph)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("建不出画布（GetOrCreateGraph 失败）。"));
            return;
        }

        // 语义是"重建一张工作台"，不是"往现有图上加东西"。
        TArray<UEdGraphNode*> ExistingNodes;
        ExistingNodes.Reserve(Graph->Nodes.Num());
        for (const TObjectPtr<UEdGraphNode>& NodePtr : Graph->Nodes)
        {
            if (NodePtr)
            {
                ExistingNodes.Add(NodePtr);
            }
        }
        for (UEdGraphNode* Node : ExistingNodes)
        {
            Graph->RemoveNode(Node, true);
        }

        const UEdGraphSchema* Schema = Graph->GetSchema();

        // ---- 剧本（全局风格/设定）
        UShineVideoScriptGraphNode* ScriptNode = Cast<UShineVideoScriptGraphNode>(
            Graph->CreateNodeAt(UShineVideoScriptGraphNode::StaticClass(), FVector2D(-1260.0, -560.0)));
        if (ScriptNode)
        {
            ScriptNode->SetTextParameter(ShineVideoScriptNode::TitleParameter, AssetName);
            ScriptNode->SetTextParameter(ShineVideoScriptNode::BodyParameter,
                TEXT("第一集：主角在老式公寓里等一个不会来的人。"));
            ScriptNode->SetTextParameter(ShineVideoScriptNode::ForeshadowParameter,
                TEXT("桌上的第二只茶杯始终是空的。"));
        }

        // ---- 素材图片节点（第一张留给"分镜图"，其余接进分镜槽位）
        TArray<UShineVideoImageGraphNode*> ImageNodes;
        for (int32 ImageIndex = 0; ImageIndex < Images.Num(); ++ImageIndex)
        {
            UShineVideoImageGraphNode* ImageNode = Cast<UShineVideoImageGraphNode>(
                Graph->CreateNodeAt(UShineVideoImageGraphNode::StaticClass(),
                    FVector2D(-1260.0, -240.0 + ImageIndex * 280.0)));
            if (!ImageNode)
            {
                continue;
            }

            ImageNode->SetTextParameter(ShineVideoImageNode::LabelParameter,
                FPaths::GetBaseFilename(Images[ImageIndex]));
            ImageNode->SetTextParameter(ShineVideoImageNode::ImagePathParameter, Images[ImageIndex]);
            ImageNodes.Add(ImageNode);
        }

        // ---- 视频组（一个，两段）
        UShineVideoGroupGraphNode* GroupNode = Cast<UShineVideoGroupGraphNode>(
            Graph->CreateNodeAt(UShineVideoGroupGraphNode::StaticClass(), FVector2D(620.0, -360.0)));
        if (GroupNode)
        {
            GroupNode->SetTextParameter(ShineVideoGroupNode::TitleParameter, TEXT("视频组"));
        }

        // ---- 两段分镜：共享同一批参考图；第二段把上一段的输出接回「链」
        for (int32 SegmentIndex = 0; SegmentIndex < 2; ++SegmentIndex)
        {
            const double RowY = -420.0 + SegmentIndex * 520.0;

            UShineVideoShotGraphNode* ShotNode = Cast<UShineVideoShotGraphNode>(
                Graph->CreateNodeAt(UShineVideoShotGraphNode::StaticClass(), FVector2D(-600.0, RowY)));
            if (!ShotNode)
            {
                continue;
            }

            ShotNode->SetTextParameter(ShineVideoShotNode::TitleParameter,
                FString::Printf(TEXT("Shot %d"), SegmentIndex + 1));
            // 提示词里的编号必须与画布真会产出的编号一致：这一段的参考图是
            // `<Picture 1>` = 分镜图（由 ImageNodes[0] 那张图当定稿图）、
            // `<Picture 2>` = 分镜槽位里那张素材图。写一个不存在的 `<Picture 3>` 只会让人以为编号算错了。
            ShotNode->SetTextParameter(ShineVideoShotNode::PromptParameter,
                SegmentIndex == 0
                    ? TEXT("A cinematic live-action medium shot continuing from the framing shown in <Picture 1>, "
                           "the subject standing in the environment of <Picture 2>, camera slowly pushing in, "
                           "warm daylight drifting across the frame, gentle handheld micro-movement, photorealistic.")
                    : TEXT("Continuing directly from the previous shot: same framing as <Picture 1>, same environment as <Picture 2>, "
                           "the camera keeps pushing in then slowly orbits to the subject's left, warm daylight continues drifting, "
                           "photorealistic, matching the previous shot's ambience."));
            ShotNode->SetIntegerParameter(ShineVideoShotNode::SeedParameter, 42 + SegmentIndex);

            if (ScriptNode && Schema)
            {
                Schema->TryCreateConnection(
                    ScriptNode->FindPin(ShineVideoScriptNode::OutputPin),
                    ShotNode->FindPin(ShineVideoShotNode::ScriptPin));
            }

            // 槽位从第 1 个开始接：第 0 张图留给下面的"分镜图"，避免同一张图占两个编号。
            for (int32 SlotIndex = 0; SlotIndex + 1 < ImageNodes.Num(); ++SlotIndex)
            {
                if (Schema)
                {
                    Schema->TryCreateConnection(
                        ImageNodes[SlotIndex + 1]->FindPin(ShineVideoImageNode::OutputPin),
                        ShotNode->FindPin(UShineVideoShotGraphNode::MakePicturePinName(SlotIndex)));
                }
            }

            UShineVideoShotImageGraphNode* ShotImageNode = Cast<UShineVideoShotImageGraphNode>(
                Graph->CreateNodeAt(UShineVideoShotImageGraphNode::StaticClass(), FVector2D(-160.0, RowY)));
            if (!ShotImageNode)
            {
                continue;
            }

            ShotImageNode->SetTextParameter(ShineVideoShotImageNode::TitleParameter,
                FString::Printf(TEXT("Shot %d 分镜图"), SegmentIndex + 1));
            // 刻意不填「图片路径」，而是把第 0 个素材图片节点**接进来**：
            // 这样连"分镜图的图从哪来"也是画布上看得见的一根线，而不是藏在参数里。
            ShotImageNode->SetTextParameter(ShineVideoShotImageNode::ImagePathParameter, FString());

            if (Schema)
            {
                Schema->TryCreateConnection(
                    ShotNode->FindPin(ShineVideoShotNode::OutputPin),
                    ShotImageNode->FindPin(ShineVideoShotImageNode::ShotPin));

                if (ImageNodes.Num() > 0)
                {
                    Schema->TryCreateConnection(
                        ImageNodes[0]->FindPin(ShineVideoImageNode::OutputPin),
                        ShotImageNode->FindPin(ShineVideoShotImageNode::ImagePin));
                }

                if (GroupNode)
                {
                    Schema->TryCreateConnection(
                        ShotImageNode->FindPin(ShineVideoShotImageNode::OutputPin),
                        GroupNode->FindPin(UShineVideoGroupGraphNode::MakeSegmentInputPinName(SegmentIndex)));
                }
            }
        }

        // ---- 链式：视频组自己的 VideoOut1 接回本节点的 Chain2（全图唯一放行的同节点连线）
        if (GroupNode && Schema)
        {
            Schema->TryCreateConnection(
                GroupNode->FindPin(UShineVideoGroupGraphNode::MakeSegmentOutputPinName(0)),
                GroupNode->FindPin(UShineVideoGroupGraphNode::MakeChainInputPinName(1)));
        }

        Graph->NotifyGraphChanged();

        const bool bSaved = SaveProject(Project);
        UE_LOG(LogShineH3Console, Display, TEXT("工作台画布已%s：%s（%d 个节点，%d 张参考图）"),
            bSaved ? TEXT("建好并保存") : TEXT("建好（保存失败）"),
            *PackagePath, Graph->Nodes.Num(), Images.Num());
        UE_LOG(LogShineH3Console, Display, TEXT("接着执行：Shine.H3.CompileGraph %s -dump"), *PackagePath);
    }

    /**
     * 把画布编译成分镜表（纯计算，零 GPU 成本）。
     *
     * 这是核对"画布表达的东西"和"真要提交的东西"是否一致的地方：编译只写 `Shots`，
     * 模型/输出设置一概不动；`-dump` 会把整个项目写成 JSON，方便和手写项目逐字段对拍。
     */
    void HandleCompileGraph(const TArray<FString>& Args)
    {
        if (Args.Num() == 0)
        {
            LogUsage();
            return;
        }

        FString AssetPath;
        bool bDump = false;
        for (const FString& Arg : Args)
        {
            if (Arg.Equals(TEXT("-dump"), ESearchCase::IgnoreCase))
            {
                bDump = true;
            }
            else if (AssetPath.IsEmpty())
            {
                AssetPath = Arg.TrimQuotes();
            }
        }

        UShineVideoProject* Project = LoadProjectAsset(AssetPath);
        if (!Project)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("加载不到项目资产：%s"), *AssetPath);
            return;
        }

        UShineVideoGraph* Graph = Project->GetOrCreateGraph();
        if (!Graph)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("这个项目没有画布。"));
            return;
        }

        const FShineVideoGraphCompileResult Result = FShineVideoGraphCompiler::Compile(*Graph);

        for (const FString& Warning : Result.Warnings)
        {
            UE_LOG(LogShineH3Console, Warning, TEXT("提示：%s"), *Warning);
        }

        if (!Result.bSuccess)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("编译失败：%s"), *Result.ErrorMessage);
            return;
        }

        FShineVideoGraphCompiler::ApplyToProject(Result, *Project);
        FShineVideoGraphCompiler::ApplyTaskStatus(*Graph, Result, FShineVideoTaskStatus());

        UE_LOG(LogShineH3Console, Display, TEXT("编译通过：%d 段"), Result.GetShotCount());
        for (int32 ShotIndex = 0; ShotIndex < Result.Shots.Num(); ++ShotIndex)
        {
            const FShineVideoShot& Shot = Result.Shots[ShotIndex];
            UE_LOG(LogShineH3Console, Display,
                TEXT("  第 %d 段  %s  ·  %s  ·  参考图 %d 张%s  ·  %dx%d · %d 帧 · %d 步 · CFG %.1f · 种子 %d"),
                ShotIndex + 1, *Shot.Title,
                Shot.Mode == EShineVideoShotMode::FirstLastFrame ? TEXT("首尾帧") : TEXT("参考图"),
                Shot.ReferenceImages.Num(),
                Shot.bChainFromPrevious ? TEXT("（链上一段末帧）") : TEXT(""),
                Shot.Width, Shot.Height, Shot.Length, Shot.Steps, Shot.Cfg, Shot.Seed);

            for (int32 ImageIndex = 0; ImageIndex < Shot.ReferenceImages.Num(); ++ImageIndex)
            {
                UE_LOG(LogShineH3Console, Display, TEXT("      <Picture %d>  %s"),
                    ImageIndex + 1, *Shot.ReferenceImages[ImageIndex]);
            }
        }

        if (bDump)
        {
            FString Json;
            if (FJsonObjectConverter::UStructToJsonObjectString(*Project, Json))
            {
                const FString DumpPath = FPaths::ConvertRelativePathToFull(
                    FPaths::ProjectSavedDir() / TEXT("ShineH3") / FString::Printf(TEXT("%s_shots.json"), *Project->GetName()));
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(DumpPath), true);

                if (FFileHelper::SaveStringToFile(Json, *DumpPath))
                {
                    UE_LOG(LogShineH3Console, Display, TEXT("分镜表已写出：%s"), *DumpPath);
                }
            }
        }

        const bool bSaved = SaveProject(Project);
        UE_LOG(LogShineH3Console, Display, TEXT("分镜表已写回项目（%s）。接着执行：Shine.H3.RunProject %s"),
            bSaved ? TEXT("已保存") : TEXT("保存失败"), *AssetPath);
    }

    // ---------------------------------------------------------------- 出分镜图

    class FStoryboardJob;

    /** 当前那次"逐个分镜图节点出图"的驱动（一次只允许一个）。 */
    TSharedPtr<FStoryboardJob> GStoryboardJob;

    /**
     * 一次"逐张出图"的驱动：一张跑完再起下一张。
     *
     * 为什么要串行而不是一起提交：ComfyUI 的队列当然能排，但一次提交 N 张 SD1.5 会让
     * 显存门禁、进度、回读全都搅在一起；而一张图只有几秒，串行的代价远小于"哪张动过、
     * 哪张没动过"这件事说不清。
     */
    class FStoryboardJob : public TSharedFromThis<FStoryboardJob>
    {
    public:
        void Start(UShineVideoProject* InProject, const FString& InAssetPath, const TArray<FShineImageTaskRequest>& InRequests)
        {
            Project = TStrongObjectPtr<UShineVideoProject>(InProject);
            AssetPath = InAssetPath;
            Requests = InRequests;
            Index = 0;
            Succeeded = 0;
            Failed = 0;
            RunNext();
        }

    private:
        void RunNext()
        {
            if (!Requests.IsValidIndex(Index))
            {
                FinishJob();
                return;
            }

            UE_LOG(LogShineH3Console, Display, TEXT("出图 %d/%d：%s（%dx%d · %d 步 · denoise %.2f）"),
                Index + 1, Requests.Num(), *Requests[Index].Workflow.OutputPrefix,
                Requests[Index].Workflow.Width, Requests[Index].Workflow.Height,
                Requests[Index].Workflow.Steps, Requests[Index].Workflow.Denoise);

            FString Error;
            Runner = FShineImageTaskRunner::Start(Requests[Index], false, Error);
            if (!Runner.IsValid())
            {
                ++Failed;
                UE_LOG(LogShineH3Console, Error, TEXT("第 %d 张启动失败：%s"), Index + 1, *Error);
                ++Index;
                RunNext();
                return;
            }

            Runner->SetStatusCallback([](const FShineImageTaskStatus& Status) { LogImageStatus(Status); });

            // 只捕弱引用：FinishedEvent 存在 Runner 自己身上，捕强引用就成了自环。
            Runner->FinishedEvent.AddLambda([WeakSelf = AsWeak()]()
            {
                if (const TSharedPtr<FStoryboardJob> Self = WeakSelf.Pin())
                {
                    Self->HandleFinished();
                }
            });
        }

        void HandleFinished()
        {
            const int32 CurrentOrdinal = Index + 1;

            if (Runner.IsValid())
            {
                const FShineImageTaskStatus& Status = Runner->GetStatus();

                for (const FString& Warning : Status.Warnings)
                {
                    UE_LOG(LogShineH3Console, Warning, TEXT("提示：%s"), *Warning);
                }

                if (Status.State == EShineImageTaskState::Completed)
                {
                    ++Succeeded;
                    for (const FString& OutputFile : Status.OutputFiles)
                    {
                        UE_LOG(LogShineH3Console, Display, TEXT("  产物：%s"), *OutputFile);
                    }
                }
                else
                {
                    ++Failed;
                    UE_LOG(LogShineH3Console, Error, TEXT("第 %d 张未成功（%s）：%s"),
                        CurrentOrdinal, FShineImageTaskRunner::ToString(Status.State), *Status.ErrorMessage);
                }

                Runner.Reset();
            }

            ++Index;
            RunNext();
        }

        void FinishJob()
        {
            UE_LOG(LogShineH3Console, Display, TEXT("出图结束：成功 %d 张，失败 %d 张。"), Succeeded, Failed);

            // 产出的图是写进节点、存进资产的：不保存的话下次打开就没了。
            if (Project.IsValid())
            {
                UPackage* Package = Cast<UPackage>(Project->GetOutermost());
                if (Package)
                {
                    Package->MarkPackageDirty();
                    const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
                    UE_LOG(LogShineH3Console, Display, TEXT("项目资产已%s：%s"),
                        bSaved ? TEXT("保存") : TEXT("保存失败"), *AssetPath);
                }

                Project.Reset();
            }

            // 放掉自己：Job 出栈后没人再引用，画布与节点也就跟着释放。
            GStoryboardJob.Reset();
        }

        TStrongObjectPtr<UShineVideoProject> Project;
        FString AssetPath;
        TArray<FShineImageTaskRequest> Requests;
        int32 Index = 0;
        int32 Succeeded = 0;
        int32 Failed = 0;
        TSharedPtr<FShineImageTaskRunner> Runner;
    };

    void HandleGenerateStoryboards(const TArray<FString>& Args)
    {
        FStoryboardArgs Parsed;
        if (!ParseStoryboardArgs(Args, Parsed))
        {
            LogUsage();
            return;
        }

        if (const TSharedPtr<FShineImageTaskRunner> Active = FShineImageTaskRunner::GetActive())
        {
            UE_LOG(LogShineH3Console, Warning, TEXT("已经有一个出图任务在跑（%s），等它结束再来。"),
                FShineImageTaskRunner::ToString(Active->GetStatus().State));
            return;
        }

        UShineVideoProject* Project = LoadProjectAsset(Parsed.AssetPath);
        if (!Project)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("加载不到项目资产：%s"), *Parsed.AssetPath);
            return;
        }

        UShineVideoGraph* Graph = Project->GetOrCreateGraph();
        if (!Graph)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("这个项目没有画布。"));
            return;
        }

        TArray<UShineVideoShotImageGraphNode*> Nodes;
        GetStoryboardNodesInOrder(*Graph, Nodes);
        if (Nodes.Num() == 0)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("画布上没有「分镜图」节点：先右键建一颗，接上分镜与参考图。"));
            return;
        }

        // 先把每一张的请求都组装出来：参数不对的在这里就报出来，别跑到一半才发现。
        TArray<FShineImageTaskRequest> Requests;
        Requests.Reserve(Nodes.Num());
        for (UShineVideoShotImageGraphNode* Node : Nodes)
        {
            const FShineVideoGraphCompiler::FStoryboardRequest Storyboard =
                FShineVideoGraphCompiler::BuildStoryboardRequest(*Node);

            for (const FString& Warning : Storyboard.Warnings)
            {
                UE_LOG(LogShineH3Console, Warning, TEXT("提示：%s"), *Warning);
            }

            if (!Storyboard.bSuccess)
            {
                UE_LOG(LogShineH3Console, Error, TEXT("跳过 %s：%s"), *Storyboard.NodeLabel, *Storyboard.ErrorMessage);
                continue;
            }

            FShineImageTaskRequest TaskRequest;
            FShineVideoGraphCompiler::MakeImageTaskRequest(Storyboard, Node, Parsed.BaseUrlOverride,
                Parsed.bNoBrothers, TaskRequest);
            ApplyImageOverride(Parsed.Override, TaskRequest.Workflow);
            Requests.Add(MoveTemp(TaskRequest));
        }

        if (Requests.Num() == 0)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("没有一张分镜图能出（都缺参考图或提示词）。"));
            return;
        }

        GLastLoggedImageDetail.Reset();
        GLastLoggedImageStep = -1;

        if (Parsed.bDryRun)
        {
            // dry-run 是同步的：只编译节点图，不碰网络，所以这里直接一张张跑完。
            for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
            {
                FString Error;
                const TSharedPtr<FShineImageTaskRunner> Runner =
                    FShineImageTaskRunner::Start(Requests[RequestIndex], true, Error);
                if (!Runner.IsValid())
                {
                    UE_LOG(LogShineH3Console, Error, TEXT("第 %d 张 dry-run 失败：%s"), RequestIndex + 1, *Error);
                    continue;
                }

                for (const FString& Warning : Runner->GetStatus().Warnings)
                {
                    UE_LOG(LogShineH3Console, Warning, TEXT("提示：%s"), *Warning);
                }

                UE_LOG(LogShineH3Console, Display, TEXT("第 %d 张：%s"), RequestIndex + 1, *Runner->GetStatus().Detail);

                const FString GraphPath = Runner->SaveBuiltGraphJson(
                    FString::Printf(TEXT("%s_storyboard%d"), *Project->GetName(), RequestIndex + 1));
                if (!GraphPath.IsEmpty())
                {
                    UE_LOG(LogShineH3Console, Display, TEXT("节点图已写出：%s"), *GraphPath);
                }
            }

            return;
        }

        GStoryboardJob = MakeShared<FStoryboardJob>();
        GStoryboardJob->Start(Project, Parsed.AssetPath, Requests);
    }

    /**
     * 零成本对拍：同一个参数下，图片版 builder 的图 vs 导演台导出的图。
     *
     * 为什么必须有这条命令：`FShineSceneToImageWorkflowBuilder` 的全部价值就是"和线上那条
     * 被用过的图片路线逐字段一致"。两条都不提交、不联网，所以它是零 GPU 成本的——
     * 而"提交成功"什么都证明不了（ComfyUI 对未知输入键是静默忽略的）。
     *
     * 两份 JSON 的差用 `Scripts/H3/DiffProbeGraph.py` 判：不带 `-nobrothers` 时应当**完全一致**；
     * 带 `-nobrothers` 时加 `--ignore-class ControlNetLoader --ignore-class ControlNetApplyAdvanced`
     * 后应当一致（那正是"没有深度/法线就不接 ControlNet"这条规则）。
     */
    void HandleBuildStoryboardProbe(const TArray<FString>& Args)
    {
        FStoryboardArgs Parsed;
        if (!ParseStoryboardArgs(Args, Parsed))
        {
            LogUsage();
            return;
        }

        UShineVideoProject* Project = LoadProjectAsset(Parsed.AssetPath);
        if (!Project)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("加载不到项目资产：%s"), *Parsed.AssetPath);
            return;
        }

        UShineVideoGraph* Graph = Project->GetOrCreateGraph();
        if (!Graph)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("这个项目没有画布。"));
            return;
        }

        TArray<UShineVideoShotImageGraphNode*> Nodes;
        GetStoryboardNodesInOrder(*Graph, Nodes);
        if (Nodes.Num() == 0)
        {
            UE_LOG(LogShineH3Console, Error, TEXT("画布上没有「分镜图」节点。"));
            return;
        }

        const FString OutputDirectory = Parsed.OutputDirectory.IsEmpty()
            ? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ShineH3"))
            : FPaths::ConvertRelativePathToFull(Parsed.OutputDirectory);

        for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
        {
            const FShineVideoGraphCompiler::FStoryboardRequest Storyboard =
                FShineVideoGraphCompiler::BuildStoryboardRequest(*Nodes[NodeIndex]);

            for (const FString& Warning : Storyboard.Warnings)
            {
                UE_LOG(LogShineH3Console, Warning, TEXT("提示：%s"), *Warning);
            }

            if (!Storyboard.bSuccess)
            {
                UE_LOG(LogShineH3Console, Error, TEXT("跳过 %s：%s"), *Storyboard.NodeLabel, *Storyboard.ErrorMessage);
                continue;
            }

            const FString Stem = FString::Printf(TEXT("%s_storyboard%d"), *Project->GetName(), NodeIndex + 1);
            const FString ImageGraphPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_image_graph.json"), *Stem));
            const FString DirectorGraphPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_director_graph.json"), *Stem));

            UE_LOG(LogShineH3Console, Display, TEXT("第 %d 张：%s（底图 %s）"), NodeIndex + 1, *Storyboard.NodeLabel,
                *FPaths::GetCleanFilename(Storyboard.ColorImagePath));

            // ---- builder 侧
            FShineSceneToImageRequest BuilderRequest = Storyboard.Workflow;
            BuilderRequest.ColorImageName = FPaths::GetCleanFilename(Storyboard.ColorImagePath);
            BuilderRequest.DepthImageName = (!Parsed.bNoBrothers && !Storyboard.DepthImagePath.IsEmpty())
                ? FPaths::GetCleanFilename(Storyboard.DepthImagePath) : FString();
            BuilderRequest.NormalImageName = (!Parsed.bNoBrothers && !Storyboard.NormalImagePath.IsEmpty())
                ? FPaths::GetCleanFilename(Storyboard.NormalImagePath) : FString();
            ApplyImageOverride(Parsed.Override, BuilderRequest);

            const FShineSceneToImageWorkflowBuilder::FBuildResult Built =
                FShineSceneToImageWorkflowBuilder::Build(BuilderRequest);

            for (const FString& Warning : Built.Warnings)
            {
                UE_LOG(LogShineH3Console, Warning, TEXT("提示：%s"), *Warning);
            }

            if (!Built.bSuccess)
            {
                UE_LOG(LogShineH3Console, Error, TEXT("builder 编译失败：%s"), *Built.ErrorMessage);
            }
            else if (WritePrettyJson(Built.ToJsonString(), ImageGraphPath))
            {
                UE_LOG(LogShineH3Console, Display, TEXT("builder 图已写出：%s"), *ImageGraphPath);
            }

            // ---- 探针侧：导演台照实吃深度/法线（它就是模板那种形态），
            //      忽略 ControlNet 支这件事交给 DiffProbeGraph.py 的 --ignore-class。
            FShineSceneToImageRequest DirectorRequest = Storyboard.Workflow;
            DirectorRequest.ColorImageName = BuilderRequest.ColorImageName;
            DirectorRequest.DepthImageName = Storyboard.DepthImagePath.IsEmpty()
                ? FString() : FPaths::GetCleanFilename(Storyboard.DepthImagePath);
            DirectorRequest.NormalImageName = Storyboard.NormalImagePath.IsEmpty()
                ? FString() : FPaths::GetCleanFilename(Storyboard.NormalImagePath);
            ApplyImageOverride(Parsed.Override, DirectorRequest);

            FString DirectorJson;
            FString DirectorError;
            if (!ExportDirectorImagePrompt(DirectorRequest, DirectorJson, DirectorError))
            {
                UE_LOG(LogShineH3Console, Error, TEXT("导演台导出失败：%s"), *DirectorError);
            }
            else if (WritePrettyJson(DirectorJson, DirectorGraphPath))
            {
                UE_LOG(LogShineH3Console, Display, TEXT("导演台图已写出：%s"), *DirectorGraphPath);
            }

            if (Parsed.bNoBrothers)
            {
                UE_LOG(LogShineH3Console, Display,
                    TEXT("对拍：python Scripts/H3/DiffProbeGraph.py \"%s\" \"%s\" --ignore-class ControlNetLoader --ignore-class ControlNetApplyAdvanced"),
                    *ImageGraphPath, *DirectorGraphPath);
            }
            else
            {
                UE_LOG(LogShineH3Console, Display,
                    TEXT("对拍：python Scripts/H3/DiffProbeGraph.py \"%s\" \"%s\""),
                    *ImageGraphPath, *DirectorGraphPath);
            }
        }
    }
}

void FShineVideoTaskConsole::Register()
{
    if (GConsoleCommands.Num() > 0)
    {
        return;
    }

    IConsoleManager& ConsoleManager = IConsoleManager::Get();

    GConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
        TEXT("Shine.H3.CreateTestProject"),
        TEXT("建一个两段链式的 H3 视频测试项目。用法：Shine.H3.CreateTestProject <资产路径> <图1> [图2 ...]"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleCreateTestProject),
        ECVF_Default));

    GConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
        TEXT("Shine.H3.CreateWorkbenchGraph"),
        TEXT("建一张两段链式的工作台画布（无 UI）。用法：Shine.H3.CreateWorkbenchGraph <资产路径> <图1> [图2 ...]"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleCreateWorkbenchGraph),
        ECVF_Default));

    GConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
        TEXT("Shine.H3.CompileGraph"),
        TEXT("把工作台画布编译成分镜表（纯计算、零 GPU 成本）。用法：Shine.H3.CompileGraph <资产路径> [-dump]；-dump 会把整个项目写成 Saved/ShineH3/<名字>_shots.json。"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleCompileGraph),
        ECVF_Default));

    GConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
        TEXT("Shine.H3.RunProject"),
        TEXT("跑一个 H3 视频项目（无 UI）。用法：Shine.H3.RunProject <资产路径> [-url=http://127.0.0.1:8188] [-python=<python.exe>] [-dryrun] [-unet=<文件名>] [-mode=ref|fl] [-segments=<n>] [-tag=<后缀>]；"
             "加 -dryrun 只编译节点图、不提交；-unet / -mode / -segments 只影响这一次运行、不写回资产。"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleRunProject),
        ECVF_Default));

    GConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
        TEXT("Shine.H3.GenerateStoryboards"),
        TEXT("给画布上每颗「分镜图」节点出一张图（SD1.5 + 可选双 ControlNet）。"
             "用法：Shine.H3.GenerateStoryboards <资产路径> [-dryrun] [-nobrothers] [-url=] "
             "[-steps=] [-denoise=] [-cfg=] [-width=] [-height=] [-seed=]；"
             "-dryrun 只编译节点图、不提交，并把图写到 Saved/ShineH3。"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleGenerateStoryboards),
        ECVF_Default));

    GConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
        TEXT("Shine.H3.BuildStoryboardProbe"),
        TEXT("零成本对拍：同一套参数下，图片版 builder 的图 vs 导演台（Director 节点）导出的图，两份 JSON 都写出来。"
             "用法：Shine.H3.BuildStoryboardProbe <资产路径> [-nobrothers] [-out=<目录>] [其余参数同 GenerateStoryboards]。"),
        FConsoleCommandWithArgsDelegate::CreateStatic(&HandleBuildStoryboardProbe),
        ECVF_Default));
}

void FShineVideoTaskConsole::Unregister()
{
    IConsoleManager& ConsoleManager = IConsoleManager::Get();
    for (IConsoleCommand* Command : GConsoleCommands)
    {
        if (Command)
        {
            ConsoleManager.UnregisterConsoleObject(Command);
        }
    }

    GConsoleCommands.Reset();
    GActiveRunner.Reset();
    GStoryboardJob.Reset();
}
