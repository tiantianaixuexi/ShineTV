#include "Editor/ShineVideoGraphEditor.h"

#include "Asset/ShineVideoGraph.h"
#include "Asset/ShineVideoProject.h"
#include "Comfy/Builders/MiniMaxH3/ShineMiniMaxH3WorkflowBuilder.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "Graph/ShineVideoGraphSchema.h"
#include "GraphEditor.h"
#include "GraphEditorDragDropAction.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SGraphActionMenu.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FShineVideoGraphEditor"

namespace
{
    /** 流程日志只留最后这么多行：一次两段链式要跑 40 分钟，事件是海量的。 */
    constexpr int32 MaxLogLines = 400;

    /** 步进日志的节流：25 步一步一行太吵，每 5 步一条。 */
    constexpr int32 StepLogInterval = 5;

    UShineComfyGraphNodeBase* CastToShineNode(UObject* Object)
    {
        return Cast<UShineComfyGraphNodeBase>(Object);
    }
}

const FName FShineVideoGraphEditor::CanvasTabId(TEXT("ShineVideoGraphEditor.Canvas"));
const FName FShineVideoGraphEditor::DetailsTabId(TEXT("ShineVideoGraphEditor.Details"));
const FName FShineVideoGraphEditor::ProcessTabId(TEXT("ShineVideoGraphEditor.Process"));

FShineVideoGraphEditor::~FShineVideoGraphEditor()
{
    if (EditingAsset)
    {
        if (UShineVideoGraph* Graph = EditingAsset->Graph)
        {
            if (GraphChangedHandle.IsValid())
            {
                Graph->RemoveOnGraphChangedHandler(GraphChangedHandle);
                GraphChangedHandle.Reset();
            }
        }
    }

    // runner 是自持的，编辑器关掉之后它可能还在跑：这里只把回调摘掉，
    // 免得它回头戳到一个已经析构的对象。任务本身继续跑完并写日志（Output Log 里还有）。
    if (Runner.IsValid())
    {
        Runner->SetStatusCallback(nullptr);
        if (FinishedHandle.IsValid())
        {
            Runner->FinishedEvent.Remove(FinishedHandle);
            FinishedHandle.Reset();
        }
    }
}

void FShineVideoGraphEditor::InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UShineVideoProject* InAsset)
{
    EditingAsset = InAsset;

    UShineVideoGraph* Graph = GetGraph();
    if (Graph && !GraphChangedHandle.IsValid())
    {
        // 画布上任何改动（连线、参数、增删节点）都要落到资产上，否则关掉编辑器就丢了。
        GraphChangedHandle = Graph->AddOnGraphChangedHandler(
            FOnGraphChanged::FDelegate::CreateSP(this, &FShineVideoGraphEditor::HandleGraphChanged));
    }

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = false;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.NotifyHook = this;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetObject(EditingAsset);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("ShineVideoGraphEditorLayout_v1"))
        ->AddArea(
            FTabManager::NewPrimaryArea()
            ->SetOrientation(Orient_Vertical)
            ->Split(
                FTabManager::NewStack()
                ->SetHideTabWell(false)
                ->SetSizeCoefficient(0.64f)
                ->AddTab(CanvasTabId, ETabState::OpenedTab))
            ->Split(
                FTabManager::NewStack()
                ->SetSizeCoefficient(0.36f)
                ->AddTab(DetailsTabId, ETabState::OpenedTab)
                ->AddTab(ProcessTabId, ETabState::OpenedTab)));

    FAssetEditorToolkit::InitAssetEditor(
        Mode, InitToolkitHost, TEXT("ShineVideoGraphEditorApp"), Layout, true, true, InAsset);

    AppendLog(TEXT("就绪。先把节点连好，再点「编译到项目」——编译是零 GPU 成本的纯计算，"
        "它会把画布翻译成分镜表并告诉你哪些线没接上。"));
    AppendLog(TEXT("分镜的参考图编号顺序 = 左侧 9 个槽位接了线的先后（空槽位跳过），"
        "画布上每个槽位旁边都标了它最终会是第几张（<Picture N>）。"));
    RefreshSelectionSummary();
    RefreshShotRows(nullptr);

    ExtendToolbar();
    RegenerateMenusAndToolbars();
}

UShineVideoGraph* FShineVideoGraphEditor::GetGraph() const
{
    return EditingAsset ? EditingAsset->GetOrCreateGraph() : nullptr;
}

void FShineVideoGraphEditor::MarkAssetDirty()
{
    if (EditingAsset)
    {
        EditingAsset->MarkPackageDirty();
    }
}

FName FShineVideoGraphEditor::GetToolkitFName() const
{
    return TEXT("ShineVideoGraphEditor");
}

FText FShineVideoGraphEditor::GetBaseToolkitName() const
{
    return LOCTEXT("ToolkitName", "Shine 视频工作台");
}

FString FShineVideoGraphEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("ShineVideo");
}

FLinearColor FShineVideoGraphEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.13f, 0.61f, 0.58f, 1.0f);
}

void FShineVideoGraphEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(CanvasTabId, FOnSpawnTab::CreateSP(this, &FShineVideoGraphEditor::SpawnCanvasTab))
        .SetDisplayName(LOCTEXT("CanvasTabLabel", "画布"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FShineVideoGraphEditor::SpawnDetailsTab))
        .SetDisplayName(LOCTEXT("DetailsTabLabel", "项目"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(ProcessTabId, FOnSpawnTab::CreateSP(this, &FShineVideoGraphEditor::SpawnProcessTab))
        .SetDisplayName(LOCTEXT("ProcessTabLabel", "流程"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FShineVideoGraphEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(CanvasTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
    InTabManager->UnregisterTabSpawner(ProcessTabId);
}

void FShineVideoGraphEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(EditingAsset);
}

FString FShineVideoGraphEditor::GetReferencerName() const
{
    return TEXT("FShineVideoGraphEditor");
}

void FShineVideoGraphEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
    RefreshShotRows(Runner.IsValid() && Runner->GetStatus().IsActive() ? &Runner->GetStatus() : nullptr);
    MarkAssetDirty();
}

// ---------------------------------------------------------------------------- 页签

TSharedRef<SDockTab> FShineVideoGraphEditor::SpawnCanvasTab(const FSpawnTabArgs& Args)
{
    UShineVideoGraph* Graph = GetGraph();

    if (!GraphEditorCommands.IsValid())
    {
        // 只要"有"一个命令表，SGraphEditor 就会把引擎自带的 撤销/重做/删除/复制/粘贴/框选
        // 全部绑上（见 SGraphEditorImpl::Construct 里对 AdditionalCommands 的处理），
        // 所以这里刻意留空：多绑一遍只会和引擎自带的打架。
        GraphEditorCommands = MakeShared<FUICommandList>();
    }

    // FGraphEditorEvents / FOnSelectionChanged 都是 SGraphEditor 的**嵌套**类型，
    // 不是全局名字——写成全局名字在 UE 5.8 里编译不过。
    SGraphEditor::FGraphEditorEvents GraphEvents;
    GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FShineVideoGraphEditor::HandleSelectionChanged);

    return SNew(SDockTab)
        .Label(LOCTEXT("CanvasTabTitle", "画布"))
        [
            SNew(SSplitter)
            + SSplitter::Slot()
            .Value(0.22f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                .Padding(4.0f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(6.0f, 6.0f, 6.0f, 2.0f)
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text(LOCTEXT("PaletteHint",
                            "拖下面任一类节点到画布上。连线顺序有意义的地方（分镜的参考图槽位、"
                            "视频组的段输入）都在节点上标了编号。"))
                    ]

                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    .Padding(2.0f)
                    [
                        SAssignNew(NodePalette, SGraphActionMenu)
                        .OnActionDragged(this, &FShineVideoGraphEditor::HandleNodePaletteActionDragged)
                        .OnCollectAllActions(this, &FShineVideoGraphEditor::CollectNodePaletteActions)
                        .AutoExpandActionMenu(true)
                        .GraphObj(Graph)
                    ]
                ]
            ]
            + SSplitter::Slot()
            .Value(0.78f)
            [
                SAssignNew(GraphEditor, SGraphEditor)
                .IsEditable(true)
                .GraphToEdit(Graph)
                .GraphEvents(GraphEvents)
                .AdditionalCommands(GraphEditorCommands)
                .Appearance(this, &FShineVideoGraphEditor::GetGraphAppearance)
                .ShowGraphStateOverlay(false)
            ]
        ];
}

TSharedRef<SDockTab> FShineVideoGraphEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(LOCTEXT("DetailsTabTitle", "项目"))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f, 6.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                .Padding(8.0f)
                [
                    SAssignNew(SelectionText, STextBlock)
                    .AutoWrapText(true)
                    .Text(LOCTEXT("NothingSelected", "画布上没有选中节点。点一个节点看它的参数摘要——"
                        "参数是**就地**在节点上改的，不在这里重复一份表单。"))
                ]
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                DetailsView.ToSharedRef()
            ]
        ];
}

TSharedRef<SDockTab> FShineVideoGraphEditor::SpawnProcessTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .Label(LOCTEXT("ProcessTabTitle", "流程"))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f, 6.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                .Padding(8.0f)
                [
                    SAssignNew(StateText, STextBlock)
                    .AutoWrapText(true)
                    .Text(LOCTEXT("IdleState", "空闲"))
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(10.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("ShotsHeader", "分镜概览（编译产物；画布是唯一入口）"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(4.0f, 0.0f)
            [
                SAssignNew(ShotRowsBox, SVerticalBox)
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(8.0f, 6.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                .Padding(4.0f)
                [
                    SAssignNew(LogTextBox, SMultiLineEditableTextBox)
                    .IsReadOnly(true)
                    .AutoWrapText(true)
                    .AlwaysShowScrollbars(true)
                ]
            ]
        ];
}

// ---------------------------------------------------------------------------- 工具栏

void FShineVideoGraphEditor::ExtendToolbar()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(GetToolMenuToolbarName()))
    {
        FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("ShineVideoGraph"));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineVideoCompileGraph"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineVideoGraphEditor::HandleCompileClicked),
                FCanExecuteAction::CreateSP(this, &FShineVideoGraphEditor::CanCompile)),
            LOCTEXT("CompileLabel", "编译到项目"),
            LOCTEXT("CompileTooltip", "把画布翻译成分镜表（写进资产）。纯计算、零 GPU 成本，用来看接线和编号对不对——提交成功证明不了任何事。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Export"))));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineVideoGenerate"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineVideoGraphEditor::HandleGenerateClicked),
                FCanExecuteAction::CreateSP(this, &FShineVideoGraphEditor::CanStartTask)),
            LOCTEXT("GenerateLabel", "生成"),
            LOCTEXT("GenerateTooltip", "先编译，再走完整流程：显存门禁 → 上传参考图 → 提交 → 逐节点进度 → 回读 → 落盘。25 步 CFG 4.0 实测约 21 分钟/段。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Play"))));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineVideoCancel"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineVideoGraphEditor::HandleCancelClicked),
                FCanExecuteAction::CreateSP(this, &FShineVideoGraphEditor::CanCancel)),
            LOCTEXT("CancelLabel", "取消"),
            LOCTEXT("CancelTooltip", "中断 ComfyUI 上正在跑的任务。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Delete"))));

        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("ShineVideoExportGraph"),
            FUIAction(
                FExecuteAction::CreateSP(this, &FShineVideoGraphEditor::HandleExportGraphClicked),
                FCanExecuteAction::CreateSP(this, &FShineVideoGraphEditor::CanExportGraph)),
            LOCTEXT("ExportGraphLabel", "导出节点图"),
            LOCTEXT("ExportGraphTooltip", "把最近一次编译出的 ComfyUI API 图写到 Saved/ShineH3/ 下，便于跟 Scripts/H3/probe_*.json 对拍。"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Save"))));
    }
}

bool FShineVideoGraphEditor::IsTaskActive() const
{
    return Runner.IsValid() && Runner->GetStatus().IsActive();
}

bool FShineVideoGraphEditor::CanCompile() const
{
    return EditingAsset != nullptr && GetGraph() != nullptr && !IsTaskActive();
}

bool FShineVideoGraphEditor::CanStartTask() const
{
    return CanCompile() && !IsTaskActive();
}

bool FShineVideoGraphEditor::CanCancel() const
{
    return IsTaskActive();
}

bool FShineVideoGraphEditor::CanExportGraph() const
{
    return Runner.IsValid() && !Runner->GetLastBuiltGraphJson().IsEmpty();
}

// ---------------------------------------------------------------------------- 编译与生成

bool FShineVideoGraphEditor::CompileGraph(bool bApplyToProject)
{
    UShineVideoGraph* Graph = GetGraph();
    if (!Graph || !EditingAsset)
    {
        return false;
    }

    const FShineVideoGraphCompileResult Result = FShineVideoGraphCompiler::Compile(*Graph);
    LastCompileResult = Result;

    for (const FString& Warning : Result.Warnings)
    {
        AppendLog(FString::Printf(TEXT("提示：%s"), *Warning));
    }

    if (!Result.bSuccess)
    {
        AppendLog(FString::Printf(TEXT("编译失败：%s"), *Result.ErrorMessage));
        ShowNotification(FText::FromString(Result.ErrorMessage), false);
        return false;
    }

    // 段标题与"第几段"的归属先写回节点：画布上的逐段状态要靠这张表。
    FShineVideoGraphCompiler::ApplyTaskStatus(*Graph, LastCompileResult, FShineVideoTaskStatus());

    if (bApplyToProject)
    {
        FShineVideoGraphCompiler::ApplyToProject(LastCompileResult, *EditingAsset);
        RefreshShotRows(nullptr);
        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
        MarkAssetDirty();
    }

    // 编译摘要里逐段写出"这一段最后会带几张参考图、是不是链式"——这是提交前最该核对的数字。
    AppendLog(FString::Printf(TEXT("编译通过：%d 段。"), Result.GetShotCount()));
    for (int32 ShotIndex = 0; ShotIndex < Result.Shots.Num(); ++ShotIndex)
    {
        const FShineVideoShot& Shot = Result.Shots[ShotIndex];
        AppendLog(FString::Printf(TEXT("  第 %d 段  %s  ·  %s  ·  参考图 %d 张%s  ·  %dx%d · %d 帧 · %d 步 · CFG %.1f"),
            ShotIndex + 1,
            *Shot.Title,
            Shot.Mode == EShineVideoShotMode::FirstLastFrame ? TEXT("首尾帧") : TEXT("参考图"),
            Shot.ReferenceImages.Num(),
            Shot.bChainFromPrevious ? TEXT("（链上一段末帧）") : TEXT(""),
            Shot.Width, Shot.Height, Shot.Length, Shot.Steps, Shot.Cfg));
    }

    Graph->NotifyGraphChanged();
    ShowNotification(FText::FromString(FString::Printf(TEXT("编译通过：%d 段%s"),
        Result.GetShotCount(), bApplyToProject ? TEXT("，已写入项目") : TEXT(""))), true);

    return true;
}

void FShineVideoGraphEditor::HandleCompileClicked()
{
    LastLoggedDetail.Reset();
    LastLoggedStep = -1;
    LastLoggedWarningCount = 0;

    AppendLog(TEXT("———— 编译到项目 ————"));
    CompileGraph(/*bApplyToProject=*/true);
    RegenerateMenusAndToolbars();
}

void FShineVideoGraphEditor::HandleGenerateClicked()
{
    if (!EditingAsset)
    {
        return;
    }

    LastLoggedDetail.Reset();
    LastLoggedStep = -1;
    LastLoggedWarningCount = 0;

    AppendLog(TEXT("———— 生成 ————"));

    // 先编译：拿着一张和画布不一致的分镜表去生成是最难查的一类问题。
    if (!CompileGraph(/*bApplyToProject=*/true))
    {
        return;
    }

    if (!EditingAsset->HasSubmittableShot())
    {
        AppendLog(TEXT("没有可提交的分镜：每段至少要有提示词或一张图。"));
        ShowNotification(LOCTEXT("NoSubmittableShot", "没有可提交的分镜。"), false);
        return;
    }

    AppendLog(FString::Printf(TEXT("开始生成（%d 段，25 步实测约 21 分钟/段）"), EditingAsset->Shots.Num()));

    FShineVideoTaskRunner& TaskRunner = EnsureRunner();
    if (!TaskRunner.Run(*EditingAsset))
    {
        AppendLog(FString::Printf(TEXT("启动失败：%s"), *TaskRunner.GetStatus().ErrorMessage));
    }

    RegenerateMenusAndToolbars();
}

void FShineVideoGraphEditor::HandleCancelClicked()
{
    if (Runner.IsValid())
    {
        Runner->Cancel();
        RegenerateMenusAndToolbars();
    }
}

void FShineVideoGraphEditor::HandleExportGraphClicked()
{
    if (!Runner.IsValid() || !EditingAsset)
    {
        return;
    }

    const FString FilePath = Runner->SaveBuiltGraphJson(EditingAsset->GetName());
    if (FilePath.IsEmpty())
    {
        AppendLog(TEXT("导出失败：还没有编译出的 ComfyUI 节点图（要先跑一次「生成」或控制台的 -dryrun）。"));
        return;
    }

    AppendLog(FString::Printf(TEXT("节点图已导出：%s"), *FilePath));
}

FShineVideoTaskRunner& FShineVideoGraphEditor::EnsureRunner()
{
    if (!Runner.IsValid())
    {
        Runner = MakeShared<FShineVideoTaskRunner>();
        Runner->SetStatusCallback([this](const FShineVideoTaskStatus& Status) { HandleStatusChanged(Status); });
        FinishedHandle = Runner->FinishedEvent.AddRaw(this, &FShineVideoGraphEditor::HandleFinished);
    }

    return *Runner;
}

// ---------------------------------------------------------------------------- 画布事件

void FShineVideoGraphEditor::HandleGraphChanged(const FEdGraphEditAction& Action)
{
    MarkAssetDirty();
}

void FShineVideoGraphEditor::HandleSelectionChanged(const FGraphPanelSelectionSet& Selection)
{
    RefreshSelectionSummary();
}

void FShineVideoGraphEditor::RefreshSelectionSummary()
{
    if (!SelectionText.IsValid())
    {
        return;
    }

    if (!GraphEditor.IsValid())
    {
        SelectionText->SetText(FText::GetEmpty());
        return;
    }

    const FGraphPanelSelectionSet& Selection = GraphEditor->GetSelectedNodes();
    TArray<UObject*> SelectedNodes;
    for (UObject* Object : Selection)
    {
        if (CastToShineNode(Object))
        {
            SelectedNodes.Add(Object);
        }
    }

    if (SelectedNodes.Num() == 0)
    {
        SelectionText->SetText(LOCTEXT("NothingSelected", "画布上没有选中节点。点一个节点看它的参数摘要——"
            "参数是**就地**在节点上改的，不在这里重复一份表单。"));
        return;
    }

    if (SelectedNodes.Num() > 1)
    {
        SelectionText->SetText(FText::FromString(FString::Printf(TEXT("选中了 %d 个节点。"), SelectedNodes.Num())));
        return;
    }

    const UShineComfyGraphNodeBase* Node = CastToShineNode(SelectedNodes[0]);

    // 角色名（"分镜" / "视频组"…）比 UObject 名字有用得多，先把它写出来。
    FString KindLabel;
    if (const UShineVideoGraphNodeBase* VideoNode = Cast<UShineVideoGraphNodeBase>(Node))
    {
        KindLabel = FString::Printf(TEXT("%s · "), *VideoNode->GetVideoNodeKind().ToString());
    }

    FString Summary = FString::Printf(TEXT("%s%s    %s\n"),
        *KindLabel,
        *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
        *Node->GetNodeSubtitle().ToString());

    for (const FShineComfyNodeParameter& Parameter : Node->GetParameters())
    {
        FString Value = Parameter.ExportValueAsString();
        Value.ReplaceInline(TEXT("\n"), TEXT(" "));
        if (Value.Len() > 120)
        {
            Value = Value.Left(117) + TEXT("...");
        }

        Summary += FString::Printf(TEXT("  %s = %s\n"), *Parameter.Label.ToString(), *Value);
    }

    SelectionText->SetText(FText::FromString(Summary));
}

struct FGraphAppearanceInfo FShineVideoGraphEditor::GetGraphAppearance() const
{
    FGraphAppearanceInfo Appearance;
    Appearance.CornerText = LOCTEXT("CornerText", "Shine 视频");
    Appearance.InstructionText = LOCTEXT("InstructionText",
        "剧本/角色/图片 → 分镜 → 分镜图 → 视频组。连线顺序有意义的地方都在节点上标了编号。");
    return Appearance;
}

void FShineVideoGraphEditor::CollectNodePaletteActions(FGraphActionListBuilderBase& OutAllActions) const
{
    ShineVideoSchemaActions::AppendNodeActions(OutAllActions);
}

FReply FShineVideoGraphEditor::HandleNodePaletteActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent)
{
    if (InActions.Num() > 0 && InActions[0].IsValid())
    {
        return FReply::Handled().BeginDragDrop(FGraphSchemaActionDragDropAction::New(InActions[0]));
    }

    return FReply::Unhandled();
}

// ---------------------------------------------------------------------------- 任务进度

void FShineVideoGraphEditor::HandleStatusChanged(const FShineVideoTaskStatus& Status)
{
    if (StateText.IsValid())
    {
        FString StateLine = FString::Printf(TEXT("[%s] %s"),
            FShineVideoTaskRunner::ToString(Status.State),
            Status.Detail.IsEmpty() ? TEXT("…") : *Status.Detail);

        if (Status.State == EShineVideoTaskState::WaitingForVram)
        {
            StateLine += FString::Printf(TEXT("    空闲显存 %.2f / 要求 %.1f GB"), Status.VramFreeGb, Status.VramRequiredGb);
        }
        else if (Status.TotalSteps > 0)
        {
            StateLine += FString::Printf(TEXT("    节点 %s（%s）%d/%d"),
                *Status.CurrentNodeId,
                Status.CurrentNodeClass.IsEmpty() ? TEXT("?") : *Status.CurrentNodeClass,
                Status.CurrentStep, Status.TotalSteps);
        }

        StateText->SetText(FText::FromString(StateLine));
    }

    if (Status.Detail != LastLoggedDetail)
    {
        LastLoggedDetail = Status.Detail;
        AppendLog(FString::Printf(TEXT("[%s] %s"), FShineVideoTaskRunner::ToString(Status.State), *Status.Detail));
    }

    if (Status.TotalSteps > 0
        && (Status.CurrentStep - LastLoggedStep >= StepLogInterval || Status.CurrentStep < LastLoggedStep))
    {
        LastLoggedStep = Status.CurrentStep;
        AppendLog(FString::Printf(TEXT("    节点 %s（%s）%d/%d"),
            *Status.CurrentNodeId,
            Status.CurrentNodeClass.IsEmpty() ? TEXT("?") : *Status.CurrentNodeClass,
            Status.CurrentStep, Status.TotalSteps));
    }

    for (int32 WarningIndex = LastLoggedWarningCount; WarningIndex < Status.Warnings.Num(); ++WarningIndex)
    {
        AppendLog(FString::Printf(TEXT("提示：%s"), *Status.Warnings[WarningIndex]));
    }
    LastLoggedWarningCount = Status.Warnings.Num();

    RefreshShotRows(&Status);

    // 把逐段进度画回画布。节流是必需的：进度事件一秒能来好几条，而每一次刷新都会
    // 让 SGraphPanel 把所有节点控件拆掉重建（PurgeVisualRepresentation）。
    if (UShineVideoGraph* Graph = GetGraph())
    {
        const double Now = FPlatformTime::Seconds();
        const bool bThrottleExpired = (Now - LastCanvasRefreshSeconds) >= CanvasRefreshIntervalSeconds;

        if (FShineVideoGraphCompiler::ApplyTaskStatus(*Graph, LastCompileResult, Status) && bThrottleExpired)
        {
            LastCanvasRefreshSeconds = Now;
            Graph->NotifyGraphChanged();
        }
    }
}

void FShineVideoGraphEditor::HandleFinished()
{
    if (!Runner.IsValid())
    {
        return;
    }

    const FShineVideoTaskStatus& Status = Runner->GetStatus();

    if (Status.State == EShineVideoTaskState::Completed)
    {
        const FString GraphPath = EditingAsset ? Runner->SaveBuiltGraphJson(EditingAsset->GetName()) : FString();
        if (!GraphPath.IsEmpty())
        {
            AppendLog(FString::Printf(TEXT("节点图：%s"), *GraphPath));
        }

        ShowNotification(FText::FromString(Status.Detail), true);
    }
    else
    {
        ShowNotification(FText::FromString(FString::Printf(TEXT("%s：%s"),
            FShineVideoTaskRunner::ToString(Status.State), *Status.ErrorMessage)), false);
    }

    if (DetailsView.IsValid())
    {
        // 运行期字段（LastPromptId / LastOutputFiles / LastError）刚被回填，刷新一下。
        DetailsView->ForceRefresh();
    }

    // 收尾时必须刷一次画布：最后几段的"完成/失败"不能因为节流被吞掉。
    if (UShineVideoGraph* Graph = GetGraph())
    {
        FShineVideoGraphCompiler::ApplyTaskStatus(*Graph, LastCompileResult, Status);
        Graph->NotifyGraphChanged();
    }

    RefreshShotRows(&Status);
    RegenerateMenusAndToolbars();
}

void FShineVideoGraphEditor::AppendLog(const FString& Line)
{
    LogText += FString::Printf(TEXT("[%s] %s\n"), *FDateTime::Now().ToString(TEXT("%H:%M:%S")), *Line);

    int32 LineCount = 0;
    for (const TCHAR Character : LogText)
    {
        if (Character == TEXT('\n'))
        {
            ++LineCount;
        }
    }

    if (LineCount > MaxLogLines)
    {
        int32 CutIndex = 0;
        int32 SeenLines = 0;
        for (int32 Index = 0; Index < LogText.Len(); ++Index)
        {
            if (LogText[Index] == TEXT('\n') && ++SeenLines == (LineCount - MaxLogLines))
            {
                CutIndex = Index + 1;
                break;
            }
        }

        LogText.RightChopInline(CutIndex);
    }

    if (LogTextBox.IsValid())
    {
        LogTextBox->SetText(FText::FromString(LogText));
    }
}

void FShineVideoGraphEditor::RefreshShotRows(const FShineVideoTaskStatus* Status)
{
    if (!ShotRowsBox.IsValid())
    {
        return;
    }

    ShotRowsBox->ClearChildren();

    if (!EditingAsset)
    {
        return;
    }

    for (int32 ShotIndex = 0; ShotIndex < EditingAsset->Shots.Num(); ++ShotIndex)
    {
        const FShineVideoShot& Shot = EditingAsset->Shots[ShotIndex];

        const FShineVideoShotProgress* Progress = nullptr;
        if (Status)
        {
            Progress = Status->Shots.FindByPredicate(
                [ShotIndex](const FShineVideoShotProgress& Candidate) { return Candidate.ShotIndex == ShotIndex; });
        }

        const int32 AlignedFrames = FShineMiniMaxH3WorkflowBuilder::AlignFrameCount(Shot.Length);
        const bool bUnalignedFrames = AlignedFrames != Shot.Length;

        FString Line = FString::Printf(TEXT("%d. %s"), ShotIndex + 1,
            Shot.Title.IsEmpty() ? TEXT("(未命名)") : *Shot.Title);

        Line += FString::Printf(TEXT("   %s · %dx%d · %d 帧%s · %d 步 · CFG %.1f · %s · 参考图 %d 张"),
            Shot.Mode == EShineVideoShotMode::FirstLastFrame ? TEXT("首尾帧") : TEXT("参考图"),
            Shot.Width, Shot.Height, Shot.Length,
            bUnalignedFrames ? *FString::Printf(TEXT("(将对齐到 %d)"), AlignedFrames) : TEXT(""),
            Shot.Steps, Shot.Cfg,
            *Shot.RefImageSize,
            Shot.ReferenceImages.Num());

        if (Shot.bChainFromPrevious)
        {
            Line += TEXT("   链上一段末帧");
        }

        if (!Shot.IsSubmittable())
        {
            Line += TEXT("   ⚠ 没有提示词也没有参考图，提交时会被跳过");
        }

        if (!Shot.LastError.IsEmpty())
        {
            Line += FString::Printf(TEXT("   ⚠ %s"), *Shot.LastError);
        }

        if (Progress)
        {
            Line += FString::Printf(TEXT("   [%s%s%s]"),
                *Progress->State,
                Progress->StepMax > 0 ? *FString::Printf(TEXT(" %d/%d"), Progress->StepValue, Progress->StepMax) : TEXT(""),
                Progress->OutputFiles.Num() > 0 ? *FString::Printf(TEXT("，产物 %d"), Progress->OutputFiles.Num()) : TEXT(""));
        }

        ShotRowsBox->AddSlot()
            .AutoHeight()
            .Padding(6.0f, 2.0f)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Text(FText::FromString(Line))
            ];
    }
}

void FShineVideoGraphEditor::ShowNotification(const FText& Message, bool bSuccess)
{
    FNotificationInfo Info(Message);
    Info.ExpireDuration = bSuccess ? 6.0f : 12.0f;
    Info.bUseSuccessFailIcons = true;

    if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
    {
        Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
    }
}

#undef LOCTEXT_NAMESPACE
