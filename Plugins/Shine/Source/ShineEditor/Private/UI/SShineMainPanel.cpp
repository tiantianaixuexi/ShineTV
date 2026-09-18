#include "UI/SShineMainPanel.h"

#include "Asset/ShineComfyAsset.h"
#include "Comfy/ShineComfyClient.h"
#include "Comfy/ShineComfyPaths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/ShineComfyGraph.h"
#include "Graph/ShineComfySchemaActions.h"
#include "Graph/Node/Comfy/ShineComfyGraphNode.h"
#include "Graph/Node/Display/ShineComfyMultiImageGraphNode.h"
#include "Graph/Node/Presets/ShineComfyDirectorGraphNode.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Slate/ShineImageZoom.h"
#include "Graph/Node/Presets/ShineComfyPreviewGraphNode.h"
#include "Graph/Node/Presets/ShineComfyPromptGraphNode.h"
#include "Graph/Node/Presets/ShineComfySamplerGraphNode.h"
#include "Graph/ShineComfyGraphSchema.h"
#include "Framework/Commands/GenericCommands.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "Http/ShineHttpClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "GraphEditor.h"
#include "GraphEditorDragDropAction.h"
#include "ScopedTransaction.h"
#include "SGraphActionMenu.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Comfy/ShineComfySocket.h"
#include "Serialization/JsonWriter.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "SShineMainPanel"

namespace
{
    /**
     * ShineMCP 服务端地址。
     *
     * "捕获视口" 按钮复用 MCP 工具来做「捕获 → 上传 → 回写图参数」，这里按插件设置
     * （项目设置 → 插件 → Shine MCP）拼地址；读不到就用默认值。
     */
    FString ResolveShineMCPBaseUrl()
    {
        static const TCHAR* SettingsSection = TEXT("/Script/ShineMCP.ShineMCPSettings");

        FString Address;
        int32 Port = 8931;
        GConfig->GetString(SettingsSection, TEXT("ListenAddress"), Address, GEditorPerProjectIni);
        GConfig->GetInt(SettingsSection, TEXT("Port"), Port, GEditorPerProjectIni);

        if (Address.IsEmpty() || Address == TEXT("0.0.0.0") || Address == TEXT("::"))
        {
            Address = TEXT("127.0.0.1");
        }

        return FString::Printf(TEXT("http://%s:%d"), *Address, Port);
    }

    bool ShineJsonGetBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool bDefault)
    {
        if (!Object.IsValid())
        {
            return bDefault;
        }

        bool bValue = bDefault;
        return Object->TryGetBoolField(FieldName, bValue) ? bValue : bDefault;
    }

    bool ShouldDisplayModelFolder(const FString& FolderName)
    {
        return !FolderName.Equals(TEXT("custom_nodes"), ESearchCase::IgnoreCase)
            && !FolderName.Equals(TEXT("configs"), ESearchCase::IgnoreCase);
    }

    int32 GetDisplayedModelFolderCount(const TArray<FShineComfyModelFolder>& ModelFolders)
    {
        int32 VisibleFolderCount = 0;
        for (const FShineComfyModelFolder& Folder : ModelFolders)
        {
            if (ShouldDisplayModelFolder(Folder.FolderName))
            {
                ++VisibleFolderCount;
            }
        }

        return VisibleFolderCount;
    }

    FString QueueStateToText(EShineComfyQueueTaskState State)
    {
        return State == EShineComfyQueueTaskState::Running ? TEXT("运行中") : TEXT("排队中");
    }

    FString JsonValueToDisplayString(const TSharedPtr<FJsonValue>& JsonValue)
    {
        if (!JsonValue.IsValid())
        {
            return FString();
        }

        switch (JsonValue->Type)
        {
        case EJson::String:
        {
            FString Value;
            return JsonValue->TryGetString(Value) ? Value : FString();
        }
        case EJson::Number:
            return FString::SanitizeFloat(JsonValue->AsNumber());
        case EJson::Boolean:
            return JsonValue->AsBool() ? TEXT("true") : TEXT("false");
        case EJson::Null:
            return FString();
        default:
            return FString();
        }
    }

    FString BuildQueueItemTitle(const FShineComfyQueueEntry& Entry)
    {
        const FString ShortPromptId = Entry.PromptId.Left(8);
        return FString::Printf(TEXT("%s · #%d · %s"), *QueueStateToText(Entry.State), FMath::RoundToInt(Entry.Number), *ShortPromptId);
    }

    FString BuildHistoryItemTitle(const FShineComfyHistoryEntry& Entry)
    {
        return FString::Printf(TEXT("历史结果 · %s"), *Entry.PromptId.Left(8));
    }

    float ResolveProgressFraction(const FShineComfyTaskProgressState* ProgressState)
    {
        if (!ProgressState)
        {
            return 0.0f;
        }

        if (ProgressState->bCompleted)
        {
            return 1.0f;
        }

        if (!ProgressState->bHasProgress || ProgressState->ProgressMax <= 0)
        {
            return ProgressState->bIsRunning ? 0.05f : 0.0f;
        }

        return FMath::Clamp(static_cast<float>(ProgressState->ProgressValue) / static_cast<float>(ProgressState->ProgressMax), 0.0f, 1.0f);
    }
}

SShineMainPanel::~SShineMainPanel()
{
    DisconnectComfyWebSocket();
}

void SShineMainPanel::Construct(const FArguments& InArgs)
{
    if (UShineComfyGraph* ExistingGraph = InArgs._Graph)
    {
        CanvasGraph = ExistingGraph;
    }
    else
    {
        CreateCanvasGraph();
    }

    if (UShineComfyAsset* EditingAsset = GetEditingAsset())
    {
        ComfyBaseUrl = EditingAsset->ComfyBaseUrl;
    }

    BindGraphEditorCommands();
    RebuildModelTreeItems();

    const FSlateBrush* PanelBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
    const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
    const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
    const FSlateFontInfo MutedFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
    constexpr FLinearColor WorkspaceBackground(0.035f, 0.039f, 0.052f, 1.0f);
    constexpr FLinearColor LeftRailBackground(0.062f, 0.067f, 0.082f, 1.0f);
    constexpr FLinearColor CardBackground(0.086f, 0.091f, 0.110f, 1.0f);
    constexpr FLinearColor CardBackgroundStrong(0.098f, 0.103f, 0.122f, 1.0f);
    constexpr FLinearColor AccentColor(0.145f, 0.424f, 0.812f, 1.0f);
    constexpr FLinearColor AccentSoft(0.180f, 0.200f, 0.240f, 1.0f);
    constexpr FLinearColor TextPrimary(0.93f, 0.95f, 0.98f, 1.0f);
    constexpr FLinearColor TextMuted(0.59f, 0.63f, 0.70f, 1.0f);

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(PanelBrush)
        .BorderBackgroundColor(WorkspaceBackground)
        .Padding(10.0f)
        [
            SNew(SSplitter)
            + SSplitter::Slot()
            .Value(0.28f)
            [
                SNew(SBorder)
                .BorderImage(PanelBrush)
                .BorderBackgroundColor(LeftRailBackground)
                .Padding(12.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Font(HeaderFont)
                        .ColorAndOpacity(TextPrimary)
                        .Text(LOCTEXT("ComfyLibraryTitle", "Comfy 资源库"))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 4.0f, 0.0f, 10.0f)
                    [
                        SNew(STextBlock)
                        .Font(MutedFont)
                        .ColorAndOpacity(TextMuted)
                        .AutoWrapText(true)
                        .Text(LOCTEXT("ComfyLibrarySubtitle", "点击切换模型和节点，按需要刷新对应资源。"))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(PanelBrush)
                        .BorderBackgroundColor(CardBackgroundStrong)
                        .Padding(10.0f)
                        [
                            SAssignNew(ComfyBaseUrlTextBox, SEditableTextBox)
                            .Text(FText::FromString(ComfyBaseUrl))
                            .OnTextCommitted(this, &SShineMainPanel::HandleComfyBaseUrlCommitted)
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                        [
                            SNew(SButton)
                            .ButtonColorAndOpacity(AccentColor)
                            .ContentPadding(FMargin(12.0f, 8.0f))
                            .Text(LOCTEXT("RefreshModels", "刷新模型库"))
                            .OnClicked(this, &SShineMainPanel::HandleRefreshComfyModels)
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                        [
                            SNew(SButton)
                            .ButtonColorAndOpacity(AccentSoft)
                            .ContentPadding(FMargin(12.0f, 8.0f))
                            .Text(LOCTEXT("RefreshNodes", "刷新节点库"))
                            .OnClicked(this, &SShineMainPanel::HandleRefreshComfyNodes)
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(PanelBrush)
                        .BorderBackgroundColor(CardBackgroundStrong)
                        .Padding(4.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                            [
                                SNew(SButton)
                                .ButtonColorAndOpacity(this, &SShineMainPanel::GetModelTabColor)
                                .ContentPadding(FMargin(12.0f, 6.0f))
                                .Text(LOCTEXT("ModelTab", "模型"))
                                .OnClicked(this, &SShineMainPanel::HandleShowModelLibrary)
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                            [
                                SNew(SButton)
                                .ButtonColorAndOpacity(this, &SShineMainPanel::GetNodeTabColor)
                                .ContentPadding(FMargin(12.0f, 6.0f))
                                .Text(LOCTEXT("NodeTab", "节点"))
                                .OnClicked(this, &SShineMainPanel::HandleShowNodeLibrary)
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [
                        SAssignNew(ComfyStatusTextBlock, STextBlock)
                        .Font(MutedFont)
                        .ColorAndOpacity(TextMuted)
                        .Text(FText::FromString(TEXT("点击按钮从 ComfyUI 拉取模型和节点。")))
                        .AutoWrapText(true)
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SNew(SOverlay)
                        + SOverlay::Slot()
                        [
                            SNew(SBox)
                            .Visibility(this, &SShineMainPanel::GetModelLibraryVisibility)
                            [
                                SNew(SBorder)
                                .BorderImage(PanelBrush)
                                .BorderBackgroundColor(CardBackground)
                                .Padding(10.0f)
                                [
                                    SNew(SVerticalBox)
                                    + SVerticalBox::Slot()
                                    .AutoHeight()
                                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                    [
                                        SAssignNew(ModelSearchBox, SSearchBox)
                                        .HintText(LOCTEXT("ModelSearchHint", "过滤模型"))
                                        .OnTextChanged(this, &SShineMainPanel::HandleModelFilterTextChanged)
                                    ]
                                    + SVerticalBox::Slot()
                                    .FillHeight(1.0f)
                                    [
                                        SNew(SBorder)
                                        .BorderImage(PanelBrush)
                                        .BorderBackgroundColor(FLinearColor(0.060f, 0.064f, 0.080f, 1.0f))
                                        .Padding(4.0f)
                                        [
                                            SAssignNew(ModelTreeView, STreeView<TSharedPtr<FShineComfyModelTreeItem>>)
                                            .TreeItemsSource(&ModelRootItems)
                                            .OnGenerateRow(this, &SShineMainPanel::HandleGenerateModelRow)
                                            .OnGetChildren(this, &SShineMainPanel::HandleGetModelChildren)
                                            .SelectionMode(ESelectionMode::None)
                                        ]
                                    ]
                                ]
                            ]
                        ]
                        + SOverlay::Slot()
                        [
                            SNew(SBox)
                            .Visibility(this, &SShineMainPanel::GetNodeLibraryVisibility)
                            [
                                SNew(SBorder)
                                .BorderImage(PanelBrush)
                                .BorderBackgroundColor(CardBackground)
                                .Padding(10.0f)
                                [
                                    SNew(SBorder)
                                    .BorderImage(PanelBrush)
                                    .BorderBackgroundColor(FLinearColor(0.060f, 0.064f, 0.080f, 1.0f))
                                    .Padding(4.0f)
                                    [
                                        SAssignNew(NodePalette, SGraphActionMenu)
                                        .OnActionDragged(this, &SShineMainPanel::HandleNodePaletteActionDragged)
                                        .OnCollectAllActions(this, &SShineMainPanel::CollectNodePaletteActions)
                                        .AutoExpandActionMenu(false)
                                        .GraphObj(CanvasGraph.Get())
                                    ]
                                ]
                            ]
                        ]
                    ]
                ]
            ]
            + SSplitter::Slot()
            .Value(0.46f)
            [
                SNew(SBorder)
                .BorderImage(PanelBrush)
                .BorderBackgroundColor(CardBackground)
                .Padding(10.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Font(TitleFont)
                        .ColorAndOpacity(TextPrimary)
                        .Text(LOCTEXT("CanvasPanelTitle", "Graph Workspace"))
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(PanelBrush)
                        .BorderBackgroundColor(FLinearColor(0.055f, 0.059f, 0.073f, 1.0f))
                        .Padding(2.0f)
                        [
                            SAssignNew(GraphEditor, SGraphEditor)
                            .IsEditable(true)
                            .GraphToEdit(CanvasGraph.Get())
                            .AdditionalCommands(GraphEditorCommands)
                            .Appearance(this, &SShineMainPanel::GetGraphAppearance)
                            .ShowGraphStateOverlay(false)
                        ]
                    ]
                ]
            ]
            + SSplitter::Slot()
            .Value(0.26f)
            [
                SNew(SBorder)
                .BorderImage(PanelBrush)
                .BorderBackgroundColor(CardBackground)
                .Padding(10.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Font(TitleFont)
                        .ColorAndOpacity(TextPrimary)
                        .Text(LOCTEXT("QueuePanelTitle", "Comfy 任务队列"))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 4.0f, 0.0f, 10.0f)
                    [
                        SNew(STextBlock)
                        .Font(MutedFont)
                        .ColorAndOpacity(TextMuted)
                        .AutoWrapText(true)
                        .Text(LOCTEXT("QueuePanelSubtitle", "提交当前图到 ComfyUI 队列，并通过 WebSocket 跟踪实时进度。列表下方会混合显示最近历史结果。"))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                        [
                            SNew(SButton)
                            .ButtonColorAndOpacity(AccentColor)
                            .ContentPadding(FMargin(12.0f, 8.0f))
                            .Text(LOCTEXT("StartQueueTask", "开始任务"))
                            .OnClicked(this, &SShineMainPanel::HandleStartComfyTask)
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                        [
                            SNew(SButton)
                            .ButtonColorAndOpacity(AccentSoft)
                            .ContentPadding(FMargin(12.0f, 8.0f))
                            .Text(LOCTEXT("RefreshQueue", "刷新队列"))
                            .OnClicked(this, &SShineMainPanel::HandleRefreshComfyQueue)
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity(FLinearColor(0.16f, 0.42f, 0.34f, 1.0f))
                        .ContentPadding(FMargin(12.0f, 8.0f))
                        .ToolTipText(LOCTEXT("CaptureSceneTip",
                            "用当前视口机位拍下 颜色/深度/法线 三张图，上传给 ComfyUI，"
                            "并把图里节点上的图片参数换成这次的新文件。"
                            "这样点“开始任务”就会用你刚摆好的画面（画面会贴在视口右侧预览）。"))
                        .Text(LOCTEXT("CaptureScene", "捕获视口 → 颜色/深度/法线"))
                        .OnClicked(this, &SShineMainPanel::HandleCaptureScene)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity(FLinearColor(0.32f, 0.20f, 0.20f, 1.0f))
                        .ContentPadding(FMargin(12.0f, 8.0f))
                        .Text(LOCTEXT("DeleteQueueTask", "删除任务"))
                        .OnClicked(this, &SShineMainPanel::HandleDeleteSelectedComfyTask)
                        .IsEnabled(this, &SShineMainPanel::CanDeleteSelectedComfyTask)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [
                        SAssignNew(QueueStatusTextBlock, STextBlock)
                        .Font(MutedFont)
                        .ColorAndOpacity(TextMuted)
                        .AutoWrapText(true)
                        .Text(FText::FromString(TEXT("等待连接 ComfyUI 队列。")))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                    [
                        SNew(STextBlock)
                        .Font(MutedFont)
                        .ColorAndOpacity(TextPrimary)
                        .Text(this, &SShineMainPanel::GetActiveTaskTitle)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                    [
                        SNew(SProgressBar)
                        .Percent(this, &SShineMainPanel::GetActiveTaskProgress)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                    [
                        SNew(STextBlock)
                        .Font(MutedFont)
                        .ColorAndOpacity(TextMuted)
                        .AutoWrapText(true)
                        .Text(this, &SShineMainPanel::GetActiveTaskDetail)
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(PanelBrush)
                        .BorderBackgroundColor(FLinearColor(0.060f, 0.064f, 0.080f, 1.0f))
                        .Padding(4.0f)
                        [
                            SAssignNew(QueueListView, SListView<TSharedPtr<FShineComfyQueueListItem>>)
                            .ListItemsSource(&QueueItems)
                            .OnGenerateRow(this, &SShineMainPanel::HandleGenerateQueueRow)
                            .SelectionMode(ESelectionMode::Single)
                        ]
                    ]
                ]
            ]
        ]
    ];

    if (GraphEditor.IsValid())
    {
        GraphEditor->ZoomToFit(false);
    }

    SetOutputText(TEXT("Run Graph to inspect the execution result, or export the current graph and DAG as JSON."));
    FetchComfyModels(false);
    ConnectComfyWebSocket();
    RefreshComfyQueue();
}

void SShineMainPanel::BindGraphEditorCommands()
{
    if (!GraphEditorCommands.IsValid())
    {
        GraphEditorCommands = MakeShared<FUICommandList>();
    }

    GraphEditorCommands->MapAction(
        FGenericCommands::Get().Delete,
        FExecuteAction::CreateSP(this, &SShineMainPanel::DeleteSelectedNodes),
        FCanExecuteAction::CreateSP(this, &SShineMainPanel::CanDeleteSelectedNodes));
}

bool SShineMainPanel::ExportGraphJsonToOutput()
{
    FString ExportedJson;
    if (CanvasGraph.IsValid() && CanvasGraph->ExportGraphDefinitionToJson(ExportedJson))
    {
        SetOutputText(ExportedJson);
        return true;
    }

    return false;
}

bool SShineMainPanel::ExportExecutionPlanToOutput()
{
    FString ExportedJson;
    if (CanvasGraph.IsValid() && CanvasGraph->ExportExecutionPlanToJson(ExportedJson))
    {
        SetOutputText(ExportedJson);
        return true;
    }

    return false;
}

bool SShineMainPanel::ExecuteGraphToOutput()
{
    if (CanvasGraph.IsValid())
    {
        const FShineComfyExecutionResult ExecutionResult = CanvasGraph->ExecuteGraph();
        SetOutputText(ExecutionResult.Summary);
        return true;
    }

    return false;
}

void SShineMainPanel::ZoomToFitGraph()
{
    if (GraphEditor.IsValid())
    {
        GraphEditor->ZoomToFit(false);
    }
}

bool SShineMainPanel::CanDeleteSelectedNodes() const
{
    if (!GraphEditor.IsValid())
    {
        return false;
    }

    const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
    for (UObject* SelectedObject : SelectedNodes)
    {
        if (const UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(SelectedObject))
        {
            if (SelectedNode->CanUserDeleteNode())
            {
                return true;
            }
        }
    }

    return false;
}

void SShineMainPanel::DeleteSelectedNodes()
{
    if (!GraphEditor.IsValid() || !CanvasGraph.IsValid())
    {
        return;
    }

    const FGraphPanelSelectionSet SelectedNodes = GraphEditor->GetSelectedNodes();
    if (SelectedNodes.IsEmpty())
    {
        return;
    }

    const FScopedTransaction Transaction(NSLOCTEXT("SShineMainPanel", "DeleteSelectedNodes", "Delete Shine Comfy Nodes"));
    CanvasGraph->Modify();
    GraphEditor->ClearSelectionSet();

    for (UObject* SelectedObject : SelectedNodes)
    {
        if (UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(SelectedObject))
        {
            if (!SelectedNode->CanUserDeleteNode())
            {
                continue;
            }

            SelectedNode->Modify();
            SelectedNode->DestroyNode();
        }
    }

    CanvasGraph->NotifyGraphChanged();
}

FReply SShineMainPanel::HandleRefreshComfyModels()
{
    ActiveLibraryTab = EShineComfyLibraryTab::Models;
    FetchComfyModels(true);
    return FReply::Handled();
}

FReply SShineMainPanel::HandleRefreshComfyNodes()
{
    ActiveLibraryTab = EShineComfyLibraryTab::Nodes;
    FetchComfyNodes(true);
    return FReply::Handled();
}

void SShineMainPanel::FetchComfyModels(bool bForceRefresh)
{
    SetComfyStatusText(TEXT("正在从 ComfyUI 拉取模型列表..."));
    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);
    FShineComfyFetchOptions FetchOptions;
    FetchOptions.bForceRefresh = bForceRefresh;
    FetchOptions.bAllowStaleCacheOnError = true;
    FShineComfyClient::FetchModels(ComfyBaseUrl, FetchOptions, [PanelWeakPtr](FShineComfyModelsResult&& Result)
    {
        TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
        if (!Pinned.IsValid())
        {
            return;
        }

        Pinned->ComfyModelFolders = MoveTemp(Result.Folders);
        FShineComfyClient::ApplyModelLibraryToNodeDefinitions(Pinned->ComfyNodeDefinitions, Pinned->ComfyModelFolders);
        ShineComfySchemaActions::SetDynamicNodeDefinitions(Pinned->CanvasGraph.Get(), Pinned->ComfyNodeDefinitions);
        Pinned->ApplyModelLibraryToComfyNodes();
        Pinned->RebuildModelTreeItems();
        if (Pinned->ModelTreeView.IsValid())
        {
            Pinned->ModelTreeView->RequestTreeRefresh();
            for (const TSharedPtr<FShineComfyModelTreeItem>& RootItem : Pinned->ModelRootItems)
            {
                Pinned->ModelTreeView->SetItemExpansion(RootItem, true);
            }
        }

        if (Pinned->NodePalette.IsValid())
        {
            Pinned->NodePalette->RefreshAllActions(true);
        }

        const FString Status = Result.bSuccess
            ? FString::Printf(TEXT("已读取 %d 个模型目录%s。"), GetDisplayedModelFolderCount(Pinned->ComfyModelFolders), Result.bFromCache ? TEXT("（缓存）") : TEXT(""))
            : Result.ErrorMessage;
        Pinned->SetComfyStatusText(Status);
    });
}

void SShineMainPanel::FetchComfyNodes(bool bForceRefresh)
{
    SetComfyStatusText(TEXT("正在从 ComfyUI 拉取节点定义..."));
    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);
    FShineComfyFetchOptions FetchOptions;
    FetchOptions.bForceRefresh = bForceRefresh;
    FetchOptions.bAllowStaleCacheOnError = true;
    FShineComfyClient::FetchNodes(ComfyBaseUrl, FetchOptions, [PanelWeakPtr](FShineComfyNodesResult&& Result)
    {
        TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
        if (!Pinned.IsValid())
        {
            return;
        }

        Pinned->ComfyNodeDefinitions = MoveTemp(Result.Nodes);
        FShineComfyClient::ApplyModelLibraryToNodeDefinitions(Pinned->ComfyNodeDefinitions, Pinned->ComfyModelFolders);
        ShineComfySchemaActions::SetDynamicNodeDefinitions(Pinned->CanvasGraph.Get(), Pinned->ComfyNodeDefinitions);
        if (Pinned->NodePalette.IsValid())
        {
            Pinned->NodePalette->RefreshAllActions(true);
        }

        const FString Status = Result.bSuccess
            ? FString::Printf(TEXT("已读取 %d 个 Comfy 节点，可直接拖到画布%s。"), Pinned->ComfyNodeDefinitions.Num(), Result.bFromCache ? TEXT("（缓存）") : TEXT(""))
            : Result.ErrorMessage;
        Pinned->SetComfyStatusText(Status);
    });
}

void SShineMainPanel::ApplyModelLibraryToComfyNodes()
{
    if (!CanvasGraph.IsValid())
    {
        return;
    }

    for (UEdGraphNode* GraphNode : CanvasGraph->Nodes)
    {
        if (UShineComfyGraphNode* ComfyNode = Cast<UShineComfyGraphNode>(GraphNode))
        {
            ComfyNode->ApplyModelLibrary(ComfyModelFolders);
        }
        else if (UShineComfySamplerGraphNode* SamplerNode = Cast<UShineComfySamplerGraphNode>(GraphNode))
        {
            SamplerNode->ApplyModelLibrary(ComfyModelFolders);
        }
        else if (UShineComfyDirectorGraphNode* DirectorNode = Cast<UShineComfyDirectorGraphNode>(GraphNode))
        {
            // 导演台节点里的模型参数（基础模型 / ControlNet / LTX 等）声明了 optionsSource，
            // 这里用刚拉到的模型库把它们变成下拉框。
            DirectorNode->ApplyModelLibrary(ComfyModelFolders);
        }
    }
}

void SShineMainPanel::RebuildModelTreeItems()
{
    ModelRootItems.Reset();
    const FString LowerFilter = ModelFilterText.ToLower();

    for (const FShineComfyModelFolder& Folder : ComfyModelFolders)
    {
        if (!ShouldDisplayModelFolder(Folder.FolderName))
        {
            continue;
        }

        TSharedPtr<FShineComfyModelTreeItem> FolderItem = MakeShared<FShineComfyModelTreeItem>(
            FString::Printf(TEXT("%s (%d)"), *Folder.FolderName, Folder.ModelNames.Num()),
            true);

        for (const FString& ModelName : Folder.ModelNames)
        {
            if (!LowerFilter.IsEmpty() && !ModelName.ToLower().Contains(LowerFilter) && !Folder.FolderName.ToLower().Contains(LowerFilter))
            {
                continue;
            }

            FolderItem->Children.Add(MakeShared<FShineComfyModelTreeItem>(ModelName, false));
        }

        if (LowerFilter.IsEmpty() || FolderItem->Children.Num() > 0 || Folder.FolderName.ToLower().Contains(LowerFilter))
        {
            ModelRootItems.Add(FolderItem);
        }
    }
}

void SShineMainPanel::CollectNodePaletteActions(FGraphActionListBuilderBase& OutAllActions) const
{
    ShineComfySchemaActions::AppendDynamicNodeActions(CanvasGraph.Get(), OutAllActions);
}

FReply SShineMainPanel::HandleNodePaletteActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent)
{
    if (InActions.Num() > 0 && InActions[0].IsValid())
    {
        return FReply::Handled().BeginDragDrop(FGraphSchemaActionDragDropAction::New(InActions[0]));
    }

    return FReply::Unhandled();
}

TSharedRef<ITableRow> SShineMainPanel::HandleGenerateModelRow(TSharedPtr<FShineComfyModelTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
    const bool bIsCategory = Item.IsValid() && Item->bIsCategory;
    const FSlateFontInfo RowFont = FCoreStyle::GetDefaultFontStyle(bIsCategory ? "Bold" : "Regular", bIsCategory ? 10 : 9);
    const FSlateColor RowColor = bIsCategory
        ? FLinearColor(0.92f, 0.95f, 0.99f, 1.0f)
        : FLinearColor(0.69f, 0.73f, 0.79f, 1.0f);
    const FLinearColor RowBackground = bIsCategory
        ? FLinearColor(0.116f, 0.126f, 0.153f, 1.0f)
        : FLinearColor(0.072f, 0.078f, 0.096f, 0.70f);

    return SNew(STableRow<TSharedPtr<FShineComfyModelTreeItem>>, OwnerTable)
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .BorderBackgroundColor(RowBackground)
            .Padding(FMargin(bIsCategory ? 8.0f : 10.0f, bIsCategory ? 5.0f : 3.0f))
            [
                SNew(STextBlock)
                .Font(RowFont)
                .Text(FText::FromString(Item.IsValid() ? Item->Label : FString()))
                .ColorAndOpacity(RowColor)
            ]
        ];
}

void SShineMainPanel::HandleGetModelChildren(TSharedPtr<FShineComfyModelTreeItem> Item, TArray<TSharedPtr<FShineComfyModelTreeItem>>& OutChildren) const
{
    if (Item.IsValid())
    {
        OutChildren = Item->Children;
    }
}

void SShineMainPanel::HandleModelFilterTextChanged(const FText& NewFilterText)
{
    ModelFilterText = NewFilterText.ToString();
    RebuildModelTreeItems();
    if (ModelTreeView.IsValid())
    {
        ModelTreeView->RequestTreeRefresh();
    }
}

void SShineMainPanel::HandleComfyBaseUrlCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
    ComfyBaseUrl = NewText.ToString().TrimStartAndEnd();

    if (ComfyBaseUrlTextBox.IsValid())
    {
        ComfyBaseUrlTextBox->SetText(FText::FromString(ComfyBaseUrl));
    }

    if (UShineComfyAsset* EditingAsset = GetEditingAsset())
    {
        if (EditingAsset->ComfyBaseUrl != ComfyBaseUrl)
        {
            EditingAsset->Modify();
            EditingAsset->ComfyBaseUrl = ComfyBaseUrl;
            EditingAsset->MarkPackageDirty();
        }
    }

    DisconnectComfyWebSocket();
    ConnectComfyWebSocket();
    FetchComfyModels(false);
    RefreshComfyQueue();
}

FReply SShineMainPanel::HandleStartComfyTask()
{
    if (!CanvasGraph.IsValid())
    {
        SetQueueStatusText(TEXT("当前没有可提交的图。"));
        return FReply::Handled();
    }

    FString PromptJson;
    FString ErrorMessage;
    if (!CanvasGraph->ExportComfyPromptToJson(PromptJson, ErrorMessage))
    {
        SetQueueStatusText(ErrorMessage.IsEmpty() ? TEXT("生成 ComfyUI prompt 失败。") : ErrorMessage);
        return FReply::Handled();
    }

    TSharedPtr<FJsonObject> PromptObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PromptJson);
    if (!FJsonSerializer::Deserialize(Reader, PromptObject) || !PromptObject.IsValid())
    {
        SetQueueStatusText(TEXT("生成的 ComfyUI prompt JSON 解析失败。"));
        return FReply::Handled();
    }

    SetQueueStatusText(TEXT("正在提交 ComfyUI 任务..."));
    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);
    FShineComfyPromptSubmitRequest Request;
    Request.Prompt = PromptObject;
    Request.ClientId = ComfyClientId;
    FShineComfyClient::SubmitPrompt(ComfyBaseUrl, Request, [PanelWeakPtr](FShineComfyPromptSubmitResult&& Result)
    {
        const TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
        if (!Pinned.IsValid())
        {
            return;
        }

        if (!Result.bSuccess)
        {
            Pinned->SetQueueStatusText(Result.ErrorMessage.IsEmpty() ? TEXT("ComfyUI 拒绝了任务提交。") : Result.ErrorMessage);
            return;
        }

        FShineComfyTaskProgressState& ProgressState = Pinned->TaskProgressByPromptId.FindOrAdd(Result.PromptId);
        ProgressState.PromptId = Result.PromptId;
        ProgressState.StatusText = TEXT("已提交，等待执行");
        ProgressState.bIsRunning = false;
        ProgressState.bCompleted = false;
        ProgressState.bFailed = false;
        ProgressState.bHasProgress = false;

        Pinned->SetQueueStatusText(FString::Printf(TEXT("任务已提交到 ComfyUI 队列：%s"), *Result.PromptId.Left(8)));
        Pinned->RefreshComfyQueue();
    });

    return FReply::Handled();
}

FReply SShineMainPanel::HandleCaptureScene()
{
    UShineComfyAsset* EditingAsset = GetEditingAsset();
    const FString AssetPath = EditingAsset && EditingAsset->GetOutermost()
        ? EditingAsset->GetOutermost()->GetName()
        : FString();

    // 捕获分辨率跟图里节点上的 Width/Height 对齐，避免"拍一张 768 的图喂给 1024 的流程"。
    int32 CaptureWidth = 768;
    int32 CaptureHeight = 768;
    if (CanvasGraph.IsValid())
    {
        for (UEdGraphNode* GraphNode : CanvasGraph->Nodes)
        {
            const UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(GraphNode);
            if (!ShineNode)
            {
                continue;
            }

            if (const FShineComfyNodeParameter* WidthParameter = ShineNode->FindParameter(TEXT("Width")))
            {
                if (WidthParameter->Type == EShineComfyParameterType::Integer && WidthParameter->IntValue > 0)
                {
                    CaptureWidth = WidthParameter->IntValue;
                }
            }

            if (const FShineComfyNodeParameter* HeightParameter = ShineNode->FindParameter(TEXT("Height")))
            {
                if (HeightParameter->Type == EShineComfyParameterType::Integer && HeightParameter->IntValue > 0)
                {
                    CaptureHeight = HeightParameter->IntValue;
                }
            }
        }
    }

    TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("assetPath"), AssetPath);
    Args->SetStringField(TEXT("baseUrl"), ComfyBaseUrl);
    Args->SetStringField(TEXT("prefix"), TEXT("Scene"));
    Args->SetNumberField(TEXT("width"), CaptureWidth);
    Args->SetNumberField(TEXT("height"), CaptureHeight);

    FString RequestBody;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(Args.ToSharedRef(), Writer);

    const FString RequestUrl = FString::Printf(TEXT("%s/tools/ue_capture_to_graph"), *ResolveShineMCPBaseUrl());
    SetOutputText(FString::Printf(
        TEXT("正在用当前视口机位捕获 %dx%d 的颜色/深度/法线，并上传到 ComfyUI ...\n（结果会贴在视口右侧预览）"),
        CaptureWidth, CaptureHeight));

    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);
    FShineHttpClient::PostJsonObject(RequestUrl, RequestBody,
        [PanelWeakPtr](FShineHttpResponse&& Response, TSharedPtr<FJsonObject> JsonObject)
        {
            const TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
            if (!Pinned.IsValid())
            {
                return;
            }

            if (!Response.bSuccess || !JsonObject.IsValid())
            {
                Pinned->SetOutputText(FString::Printf(
                    TEXT("捕获失败：%s\n")
                    TEXT("这条链路走的是编辑器内的 ShineMCP 服务（默认 127.0.0.1:8931）。")
                    TEXT("如果刚编译完还没重启编辑器，请重启编辑器后重试。"),
                    *Response.ErrorMessage));
                return;
            }

            if (!ShineJsonGetBool(JsonObject, TEXT("success"), false))
            {
                FString Error;
                JsonObject->TryGetStringField(TEXT("error"), Error);
                Pinned->SetOutputText(FString::Printf(TEXT("捕获失败：%s"), *Error));
                return;
            }

            TArray<FString> Lines;

            FString Directory;
            JsonObject->TryGetStringField(TEXT("directory"), Directory);

            const TSharedPtr<FJsonObject>* Files = nullptr;
            if (JsonObject->TryGetObjectField(TEXT("files"), Files) && Files && Files->IsValid())
            {
                for (const TCHAR* Key : { TEXT("color"), TEXT("depth"), TEXT("normal") })
                {
                    const TSharedPtr<FJsonObject>* Entry = nullptr;
                    if (!(*Files)->TryGetObjectField(Key, Entry) || !Entry || !Entry->IsValid())
                    {
                        continue;
                    }

                    FString Path;
                    (*Entry)->TryGetStringField(TEXT("path"), Path);
                    Lines.Add(FString::Printf(TEXT("  %s: %s"), Key, *Path));
                }
            }

            const TArray<TSharedPtr<FJsonValue>>* UpdatedParameters = nullptr;
            if (JsonObject->TryGetArrayField(TEXT("updatedParameters"), UpdatedParameters) && UpdatedParameters)
            {
                for (const TSharedPtr<FJsonValue>& Value : *UpdatedParameters)
                {
                    FString Text;
                    if (Value.IsValid() && Value->TryGetString(Text))
                    {
                        Lines.Add(FString::Printf(TEXT("  参数 %s"), *Text));
                    }
                }
            }

            Lines.Add(TEXT(""));
            Lines.Add(TEXT("现在点“开始任务”，ComfyUI 就会用这次的新画面（不再是上一次的缓存结果）。"));
            Pinned->SetOutputText(FString::Printf(
                TEXT("捕获完成：%s\n%s"),
                *Directory,
                *FString::Join(Lines, TEXT("\n"))));

            if (Pinned->GraphEditor.IsValid())
            {
                Pinned->GraphEditor->NotifyGraphChanged();
            }
        });

    return FReply::Handled();
}

FReply SShineMainPanel::HandleRefreshComfyQueue()
{
    RefreshComfyQueue();
    return FReply::Handled();
}

FReply SShineMainPanel::HandleDeleteSelectedComfyTask()
{
    if (!QueueListView.IsValid())
    {
        return FReply::Handled();
    }

    const TArray<TSharedPtr<FShineComfyQueueListItem>> SelectedItems = QueueListView->GetSelectedItems();
    if (SelectedItems.Num() == 0 || !SelectedItems[0].IsValid())
    {
        SetQueueStatusText(TEXT("先在右侧选择一个任务。"));
        return FReply::Handled();
    }

    const TSharedPtr<FShineComfyQueueListItem> SelectedItem = SelectedItems[0];
    SetQueueStatusText(SelectedItem->Entry.State == EShineComfyQueueTaskState::Running ? TEXT("正在停止运行中的 ComfyUI 任务...") : TEXT("正在删除排队中的 ComfyUI 任务..."));

    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);
    if (SelectedItem->Entry.State == EShineComfyQueueTaskState::Running)
    {
        FShineComfyClient::InterruptPrompt(ComfyBaseUrl, SelectedItem->Entry.PromptId, [PanelWeakPtr](FShineComfyQueueOperationResult&& Result)
        {
            const TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
            if (!Pinned.IsValid())
            {
                return;
            }

            Pinned->SetQueueStatusText(Result.bSuccess ? TEXT("已请求停止运行中的任务。") : Result.ErrorMessage);
            Pinned->RefreshComfyQueue();
        });
    }
    else
    {
        TArray<FString> PromptIds;
        PromptIds.Add(SelectedItem->Entry.PromptId);
        FShineComfyClient::DeleteQueueItems(ComfyBaseUrl, PromptIds, [PanelWeakPtr](FShineComfyQueueOperationResult&& Result)
        {
            const TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
            if (!Pinned.IsValid())
            {
                return;
            }

            Pinned->SetQueueStatusText(Result.bSuccess ? TEXT("已删除排队任务。") : Result.ErrorMessage);
            Pinned->RefreshComfyQueue();
        });
    }

    return FReply::Handled();
}

FReply SShineMainPanel::HandleShowModelLibrary()
{
    ActiveLibraryTab = EShineComfyLibraryTab::Models;
    return FReply::Handled();
}

FReply SShineMainPanel::HandleShowNodeLibrary()
{
    ActiveLibraryTab = EShineComfyLibraryTab::Nodes;
    return FReply::Handled();
}

EVisibility SShineMainPanel::GetModelLibraryVisibility() const
{
    return ActiveLibraryTab == EShineComfyLibraryTab::Models ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SShineMainPanel::GetNodeLibraryVisibility() const
{
    return ActiveLibraryTab == EShineComfyLibraryTab::Nodes ? EVisibility::Visible : EVisibility::Collapsed;
}

FSlateColor SShineMainPanel::GetModelTabColor() const
{
    return ActiveLibraryTab == EShineComfyLibraryTab::Models
        ? FLinearColor(0.145f, 0.424f, 0.812f, 1.0f)
        : FLinearColor(0.180f, 0.200f, 0.240f, 1.0f);
}

FSlateColor SShineMainPanel::GetNodeTabColor() const
{
    return ActiveLibraryTab == EShineComfyLibraryTab::Nodes
        ? FLinearColor(0.145f, 0.424f, 0.812f, 1.0f)
        : FLinearColor(0.180f, 0.200f, 0.240f, 1.0f);
}

void SShineMainPanel::SetQueueStatusText(const FString& NewStatus)
{
    if (QueueStatusTextBlock.IsValid())
    {
        QueueStatusTextBlock->SetText(FText::FromString(NewStatus));
    }

    SetOutputText(NewStatus);
}

void SShineMainPanel::RefreshComfyQueue()
{
    SetQueueStatusText(TEXT("正在读取 ComfyUI 队列..."));
    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);
    FShineComfyClient::FetchQueue(ComfyBaseUrl, [PanelWeakPtr](FShineComfyQueueResult&& Result)
    {
        const TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
        if (!Pinned.IsValid())
        {
            return;
        }

        if (!Result.bSuccess)
        {
            Pinned->SetQueueStatusText(Result.ErrorMessage.IsEmpty() ? TEXT("读取 ComfyUI 队列失败。") : Result.ErrorMessage);
            return;
        }

        Pinned->CurrentQueueResult = Result;
        Pinned->RebuildQueueItems(Result);
        const FString QueueStatus = FString::Printf(TEXT("当前队列：运行中 %d，排队中 %d。"), Result.Running.Num(), Result.Pending.Num());
        Pinned->SetQueueStatusText(QueueStatus);
        Pinned->RefreshComfyHistory();
    });
}

void SShineMainPanel::RefreshComfyHistory()
{
    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);
    FShineComfyClient::FetchHistory(ComfyBaseUrl, 12, [PanelWeakPtr](FShineComfyHistoryResult&& Result)
    {
        const TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin();
        if (!Pinned.IsValid())
        {
            return;
        }

        if (!Result.bSuccess)
        {
            return;
        }

        Pinned->HistoryEntries = MoveTemp(Result.Entries);
        Pinned->RebuildQueueItems(Pinned->CurrentQueueResult);

        // 生成完把结果抓到本地，直接画到图里的 Preview / Gallery 节点上。
        Pinned->PushLatestResultToDisplayNodes();
    });
}

namespace
{
    /** 从 ComfyUI 的 /view 接口抓一张图存到本地（二进制，所以直接走 FHttpModule）。 */
    void DownloadComfyImageToDisk(
        const FString& BaseUrl,
        const FString& Filename,
        const FString& Subfolder,
        const FString& Type,
        const FString& SavePath,
        TFunction<void(bool)> OnDone)
    {
        const FString Url = FString::Printf(TEXT("%s/view?filename=%s&subfolder=%s&type=%s"),
            *BaseUrl.TrimEnd(),
            *FGenericPlatformHttp::UrlEncode(Filename),
            *FGenericPlatformHttp::UrlEncode(Subfolder),
            *FGenericPlatformHttp::UrlEncode(Type));

        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb(TEXT("GET"));
        Request->OnProcessRequestComplete().BindLambda(
            [SavePath, OnDone = MoveTemp(OnDone)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk) mutable
            {
                bool bSaved = false;
                if (bConnectedOk && Response.IsValid() && Response.Get()->GetResponseCode() == 200)
                {
                    IFileManager::Get().MakeDirectory(*FPaths::GetPath(SavePath), true);
                    bSaved = FFileHelper::SaveArrayToFile(Response.Get()->GetContent(), *SavePath);
                }
                OnDone(bSaved);
            });
        Request->ProcessRequest();
    }
}

void SShineMainPanel::PushLatestResultToDisplayNodes()
{
    if (!CanvasGraph.IsValid())
    {
        return;
    }

    // 图里没有显示节点就不用折腾了。
    bool bHasDisplayNode = false;
    for (UEdGraphNode* GraphNode : CanvasGraph->Nodes)
    {
        if (Cast<UShineComfyPreviewGraphNode>(GraphNode) || Cast<UShineComfyMultiImageGraphNode>(GraphNode))
        {
            bHasDisplayNode = true;
            break;
        }
    }

    if (!bHasDisplayNode)
    {
        return;
    }

    const FShineComfyHistoryEntry* LatestEntry = nullptr;
    for (const FShineComfyHistoryEntry& Entry : HistoryEntries)
    {
        if (!Entry.bFailed && Entry.Images.Num() > 0)
        {
            LatestEntry = &Entry;
            break;
        }
    }

    if (!LatestEntry || PushedResultPromptIds.Contains(LatestEntry->PromptId))
    {
        return;
    }

    const FString PromptId = LatestEntry->PromptId;
    const TArray<FShineComfyHistoryImage> Images = LatestEntry->Images;
    PushedResultPromptIds.Add(PromptId);

    // 优先直接指向 ComfyUI 写出的原图：不复制、不重新下载、不导入资产。
    TArray<FString> LocalPaths;
    for (const FShineComfyHistoryImage& Image : Images)
    {
        const FString LocalPath = ShineComfyPaths::MakeLocalImagePath(Image.Subfolder, Image.FileName);
        if (!LocalPath.IsEmpty() && FPaths::FileExists(LocalPath))
        {
            LocalPaths.Add(LocalPath);
        }
    }

    if (LocalPaths.Num() > 0)
    {
        ApplyResultImagesToDisplayNodes(LocalPaths);
    }

    if (!ShineComfyPaths::GetConfiguredOutputDirectory().IsEmpty())
    {
        // 配了 output 目录就完全不做复制；找不到的只提示，不偷偷下副本。
        if (LocalPaths.Num() < Images.Num())
        {
            SetOutputText(FString::Printf(
                TEXT("有 %d/%d 张结果图在配置的 ComfyUI output 目录里没找到，检查 项目设置 → 插件 → Shine MCP/Shine Comfy 里的路径。"),
                Images.Num() - LocalPaths.Num(),
                Images.Num()));
        }
        return;
    }

    // 没配目录（例如 ComfyUI 跑在别的机器上）：才退回抓一份到本地。
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShineComfy"), TEXT("Results"));

    TSharedPtr<TArray<FString>> DownloadedPaths = MakeShared<TArray<FString>>();
    TSharedPtr<int32> Remaining = MakeShared<int32>(Images.Num());
    TWeakPtr<SShineMainPanel> PanelWeakPtr = SharedThis(this);

    for (int32 Index = 0; Index < Images.Num(); ++Index)
    {
        const FShineComfyHistoryImage& Image = Images[Index];
        const FString SavePath = FPaths::Combine(
            Directory,
            FString::Printf(TEXT("%s_%d%s"),
                *PromptId.Left(8),
                Index,
                *FPaths::GetExtension(Image.FileName, true)));

        DownloadComfyImageToDisk(ComfyBaseUrl, Image.FileName, Image.Subfolder, Image.Type, SavePath,
            [PanelWeakPtr, DownloadedPaths, Remaining, SavePath](bool bSaved)
            {
                if (bSaved)
                {
                    DownloadedPaths->Add(SavePath);
                }

                --(*Remaining);
                if (*Remaining <= 0)
                {
                    if (const TSharedPtr<SShineMainPanel> Pinned = PanelWeakPtr.Pin())
                    {
                        Pinned->ApplyResultImagesToDisplayNodes(*DownloadedPaths);
                    }
                }
            });
    }
}

void SShineMainPanel::ApplyResultImagesToDisplayNodes(const TArray<FString>& ImagePaths)
{
    if (!CanvasGraph.IsValid() || ImagePaths.Num() == 0)
    {
        return;
    }

    int32 UpdatedNodeCount = 0;
    for (UEdGraphNode* GraphNode : CanvasGraph->Nodes)
    {
        UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(GraphNode);
        if (!ShineNode)
        {
            continue;
        }

        if (Cast<UShineComfyPreviewGraphNode>(ShineNode) || Cast<UShineComfyMultiImageGraphNode>(ShineNode))
        {
            ShineNode->SetResultImagePaths(ImagePaths);
            ++UpdatedNodeCount;
        }
    }

    if (UpdatedNodeCount == 0)
    {
        return;
    }

    // 已经打开的"放大"窗口跟着一起刷新。
    ShineImageZoom::RefreshOpenWindows(ImagePaths);

    if (UShineComfyAsset* EditingAsset = GetEditingAsset())
    {
        EditingAsset->MarkPackageDirty();
    }

    SetOutputText(FString::Printf(
        TEXT("生成结果已画进图里的显示节点（%d 个节点，%d 张图）：\n%s"),
        UpdatedNodeCount,
        ImagePaths.Num(),
        *FString::Join(ImagePaths, TEXT("\n"))));

    if (GraphEditor.IsValid())
    {
        GraphEditor->NotifyGraphChanged();
    }
}

void SShineMainPanel::RebuildQueueItems(const FShineComfyQueueResult& QueueResult)
{
    QueueItems.Reset();

    auto AppendEntries = [this](const TArray<FShineComfyQueueEntry>& Entries)
    {
        for (const FShineComfyQueueEntry& Entry : Entries)
        {
            TSharedPtr<FShineComfyQueueListItem> Item = MakeShared<FShineComfyQueueListItem>();
            Item->Entry = Entry;
            Item->TitleText = BuildQueueItemTitle(Entry);
            QueueItems.Add(MoveTemp(Item));
        }
    };

    AppendEntries(QueueResult.Running);
    AppendEntries(QueueResult.Pending);

    for (const FShineComfyHistoryEntry& HistoryEntry : HistoryEntries)
    {
        const bool bAlreadyVisible = QueueResult.Running.ContainsByPredicate([&HistoryEntry](const FShineComfyQueueEntry& Entry)
        {
            return Entry.PromptId == HistoryEntry.PromptId;
        }) || QueueResult.Pending.ContainsByPredicate([&HistoryEntry](const FShineComfyQueueEntry& Entry)
        {
            return Entry.PromptId == HistoryEntry.PromptId;
        });
        if (bAlreadyVisible)
        {
            continue;
        }

        TSharedPtr<FShineComfyQueueListItem> Item = MakeShared<FShineComfyQueueListItem>();
        Item->bIsHistory = true;
        Item->HistoryEntry = HistoryEntry;
        Item->TitleText = BuildHistoryItemTitle(HistoryEntry);
        QueueItems.Add(MoveTemp(Item));
    }

    RefreshVisibleQueueItems();
}

void SShineMainPanel::RefreshVisibleQueueItems()
{
    for (const TSharedPtr<FShineComfyQueueListItem>& Item : QueueItems)
    {
        if (!Item.IsValid())
        {
            continue;
        }

        if (Item->bIsHistory)
        {
            Item->TitleText = BuildHistoryItemTitle(Item->HistoryEntry);
            Item->StatusText = Item->HistoryEntry.StatusText;
            Item->ProgressText = FString::Printf(TEXT("%s\n%s"), *Item->HistoryEntry.SummaryText, *Item->HistoryEntry.RawJson.Left(320));
            Item->ProgressFraction = Item->HistoryEntry.bFailed ? 0.0f : 1.0f;
            continue;
        }

        Item->TitleText = BuildQueueItemTitle(Item->Entry);
        Item->StatusText = QueueStateToText(Item->Entry.State);
        Item->ProgressText = Item->Entry.State == EShineComfyQueueTaskState::Pending
            ? FString::Printf(TEXT("队列位置 %d"), Item->Entry.QueueIndex + 1)
            : TEXT("已进入执行阶段");
        Item->ProgressFraction = Item->Entry.State == EShineComfyQueueTaskState::Running ? 0.05f : 0.0f;

        if (const FShineComfyTaskProgressState* ProgressState = TaskProgressByPromptId.Find(Item->Entry.PromptId))
        {
            if (!ProgressState->StatusText.IsEmpty())
            {
                Item->StatusText = ProgressState->StatusText;
            }

            if (ProgressState->bHasProgress && ProgressState->ProgressMax > 0)
            {
                Item->ProgressText = FString::Printf(TEXT("节点 %s · %d / %d"), *ProgressState->CurrentNodeId, ProgressState->ProgressValue, ProgressState->ProgressMax);
            }
            else if (!ProgressState->CurrentNodeId.IsEmpty())
            {
                Item->ProgressText = FString::Printf(TEXT("当前节点 %s"), *ProgressState->CurrentNodeId);
            }

            Item->ProgressFraction = ResolveProgressFraction(ProgressState);
        }
    }

    if (QueueListView.IsValid())
    {
        QueueListView->RequestListRefresh();
    }
}

void SShineMainPanel::ConnectComfyWebSocket()
{
    DisconnectComfyWebSocket();

    // 全项目共用一条 ComfyUI 常连（AI 贴图那边也订阅同一条）。
    // 提交任务和收事件必须是同一个 client_id，所以这里统一从 socket 取。
    FShineComfySocket& ComfySocket = FShineComfySocket::Get();
    ComfyClientId = ComfySocket.GetClientId();

    // 订阅前先记一下当前状态：连接可能早就建好了，那样 ConnectedEvent 不会再补发。
    bComfyWebSocketConnected = ComfySocket.IsConnected();

    ComfySocket.ConnectedEvent.AddSP(this, &SShineMainPanel::HandleComfyWebSocketConnected);
    ComfySocket.ConnectionErrorEvent.AddSP(this, &SShineMainPanel::HandleComfyWebSocketConnectionError);
    ComfySocket.ClosedEvent.AddSP(this, &SShineMainPanel::HandleComfyWebSocketClosed);
    ComfySocket.MessageEvent.AddSP(this, &SShineMainPanel::HandleComfyWebSocketMessage);

    if (ComfyBaseUrl.IsEmpty())
    {
        return;
    }

    ComfySocket.EnsureConnected(ComfyBaseUrl);
    SetQueueStatusText(bComfyWebSocketConnected
        ? TEXT("ComfyUI 队列 WebSocket 已连接（共用常连）。")
        : TEXT("正在连接 ComfyUI 队列 WebSocket..."));
}

void SShineMainPanel::DisconnectComfyWebSocket()
{
    bComfyWebSocketConnected = false;

    // 只退订，不关连接：这条连接是共用的，AI 贴图那边可能还在用。
    // 真正的关闭由 FShineEditorModule::ShutdownModule 负责。
    FShineComfySocket& ComfySocket = FShineComfySocket::Get();
    ComfySocket.ConnectedEvent.RemoveAll(this);
    ComfySocket.ConnectionErrorEvent.RemoveAll(this);
    ComfySocket.ClosedEvent.RemoveAll(this);
    ComfySocket.MessageEvent.RemoveAll(this);
}

void SShineMainPanel::HandleComfyWebSocketConnected()
{
    bComfyWebSocketConnected = true;
    SetQueueStatusText(TEXT("ComfyUI 队列 WebSocket 已连接。"));
    RefreshComfyQueue();
}

void SShineMainPanel::HandleComfyWebSocketConnectionError(const FString& ErrorMessage)
{
    bComfyWebSocketConnected = false;
    SetQueueStatusText(FString::Printf(TEXT("ComfyUI 队列 WebSocket 连接失败：%s"), *ErrorMessage));
}

void SShineMainPanel::HandleComfyWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    bComfyWebSocketConnected = false;
    SetQueueStatusText(FString::Printf(TEXT("ComfyUI 队列 WebSocket 已断开（%d，%s）。"), StatusCode, *Reason));
}

void SShineMainPanel::HandleComfyWebSocketMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> MessageObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, MessageObject) || !MessageObject.IsValid())
    {
        return;
    }

    FString MessageType;
    if (!MessageObject->TryGetStringField(TEXT("type"), MessageType))
    {
        return;
    }

    const TSharedPtr<FJsonObject>* DataObjectPtr = nullptr;
    if (!MessageObject->TryGetObjectField(TEXT("data"), DataObjectPtr) || !DataObjectPtr || !DataObjectPtr->IsValid())
    {
        if (MessageType == TEXT("status"))
        {
            RefreshComfyQueue();
        }
        return;
    }

    const TSharedPtr<FJsonObject>& DataObject = *DataObjectPtr;

    if (MessageType == TEXT("status"))
    {
        RefreshComfyQueue();
        return;
    }

    FString PromptId;
    if (!DataObject->TryGetStringField(TEXT("prompt_id"), PromptId) || PromptId.IsEmpty())
    {
        return;
    }

    FShineComfyTaskProgressState& ProgressState = TaskProgressByPromptId.FindOrAdd(PromptId);
    ProgressState.PromptId = PromptId;

    if (MessageType == TEXT("execution_start"))
    {
        ProgressState.StatusText = TEXT("开始执行");
        ProgressState.bIsRunning = true;
        ProgressState.bCompleted = false;
        ProgressState.bFailed = false;
        ProgressState.bHasProgress = false;
        RefreshComfyQueue();
    }
    else if (MessageType == TEXT("executing"))
    {
        ProgressState.bIsRunning = true;
        const TSharedPtr<FJsonValue> NodeValue = DataObject->TryGetField(TEXT("node"));
        const FString NodeId = JsonValueToDisplayString(NodeValue);
        if (!NodeId.IsEmpty())
        {
            ProgressState.CurrentNodeId = NodeId;
            ProgressState.StatusText = FString::Printf(TEXT("执行节点 %s"), *NodeId);
        }
    }
    else if (MessageType == TEXT("progress"))
    {
        ProgressState.bIsRunning = true;
        ProgressState.bHasProgress = true;
        ProgressState.ProgressValue = DataObject->HasTypedField<EJson::Number>(TEXT("value")) ? static_cast<int32>(DataObject->GetNumberField(TEXT("value"))) : ProgressState.ProgressValue;
        ProgressState.ProgressMax = DataObject->HasTypedField<EJson::Number>(TEXT("max")) ? static_cast<int32>(DataObject->GetNumberField(TEXT("max"))) : ProgressState.ProgressMax;
        const FString NodeId = JsonValueToDisplayString(DataObject->TryGetField(TEXT("node")));
        if (!NodeId.IsEmpty())
        {
            ProgressState.CurrentNodeId = NodeId;
        }
        ProgressState.StatusText = TEXT("执行中");
    }
    else if (MessageType == TEXT("execution_success"))
    {
        ProgressState.StatusText = TEXT("执行完成");
        ProgressState.bCompleted = true;
        ProgressState.bIsRunning = false;
        ProgressState.bHasProgress = true;
        ProgressState.ProgressValue = 1;
        ProgressState.ProgressMax = 1;
        RefreshComfyQueue();
        RefreshComfyHistory();
    }
    else if (MessageType == TEXT("execution_error"))
    {
        ProgressState.StatusText = TEXT("执行失败");
        ProgressState.bFailed = true;
        ProgressState.bIsRunning = false;
        RefreshComfyQueue();
        RefreshComfyHistory();
    }
    else if (MessageType == TEXT("execution_interrupted"))
    {
        ProgressState.StatusText = TEXT("已中断");
        ProgressState.bFailed = true;
        ProgressState.bIsRunning = false;
        RefreshComfyQueue();
        RefreshComfyHistory();
    }

    RefreshVisibleQueueItems();
}

TSharedRef<ITableRow> SShineMainPanel::HandleGenerateQueueRow(TSharedPtr<FShineComfyQueueListItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
    return SNew(STableRow<TSharedPtr<FShineComfyQueueListItem>>, OwnerTable)
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .BorderBackgroundColor(FLinearColor(0.072f, 0.078f, 0.096f, 0.85f))
            .Padding(FMargin(8.0f, 6.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.93f, 0.95f, 0.98f, 1.0f))
                    .Text(FText::FromString(Item.IsValid() ? Item->TitleText : FString()))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 2.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.78f, 0.82f, 0.90f, 1.0f))
                    .Text(FText::FromString(Item.IsValid() ? Item->StatusText : FString()))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 1.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FLinearColor(0.59f, 0.63f, 0.70f, 1.0f))
                    .Text(FText::FromString(Item.IsValid() ? Item->ProgressText : FString()))
                ]
            ]
        ];
}

TSharedPtr<FShineComfyQueueListItem> SShineMainPanel::GetActiveQueueItem() const
{
    if (QueueListView.IsValid())
    {
        const TArray<TSharedPtr<FShineComfyQueueListItem>> SelectedItems = QueueListView->GetSelectedItems();
        if (SelectedItems.Num() > 0 && SelectedItems[0].IsValid())
        {
            return SelectedItems[0];
        }
    }

    for (const TSharedPtr<FShineComfyQueueListItem>& Item : QueueItems)
    {
        if (Item.IsValid() && !Item->bIsHistory && Item->Entry.State == EShineComfyQueueTaskState::Running)
        {
            return Item;
        }
    }

    return QueueItems.Num() > 0 ? QueueItems[0] : nullptr;
}

TOptional<float> SShineMainPanel::GetActiveTaskProgress() const
{
    const TSharedPtr<FShineComfyQueueListItem> ActiveItem = GetActiveQueueItem();
    return ActiveItem.IsValid() ? TOptional<float>(ActiveItem->ProgressFraction) : TOptional<float>(0.0f);
}

FText SShineMainPanel::GetActiveTaskTitle() const
{
    const TSharedPtr<FShineComfyQueueListItem> ActiveItem = GetActiveQueueItem();
    return FText::FromString(ActiveItem.IsValid() ? ActiveItem->TitleText : TEXT("暂无队列任务或历史结果"));
}

FText SShineMainPanel::GetActiveTaskDetail() const
{
    const TSharedPtr<FShineComfyQueueListItem> ActiveItem = GetActiveQueueItem();
    return FText::FromString(ActiveItem.IsValid() ? ActiveItem->ProgressText : TEXT("提交任务后，这里会显示当前节点进度；完成后的任务会在这里显示历史输出摘要。"));
}

bool SShineMainPanel::CanDeleteSelectedComfyTask() const
{
    return QueueListView.IsValid() && QueueListView->GetSelectedItems().Num() > 0 && QueueListView->GetSelectedItems()[0].IsValid() && !QueueListView->GetSelectedItems()[0]->bIsHistory;
}

UShineComfyAsset* SShineMainPanel::GetEditingAsset() const
{
    if (!CanvasGraph.IsValid())
    {
        return nullptr;
    }

    return CanvasGraph->GetTypedOuter<UShineComfyAsset>();
}

FGraphAppearanceInfo SShineMainPanel::GetGraphAppearance() const
{
    FGraphAppearanceInfo Appearance;
    Appearance.CornerText = LOCTEXT("CornerText", "Shine Graph");
    Appearance.InstructionText = LOCTEXT("InstructionText", "Prototype node canvas for ComfyUI-style graph workflows.");
    return Appearance;
}

void SShineMainPanel::CreateCanvasGraph()
{
    if (CanvasGraph.IsValid())
    {
        return;
    }

    UShineComfyGraph* Graph = NewObject<UShineComfyGraph>(GetTransientPackage(), NAME_None, RF_Transient);
    Graph->Schema = UShineComfyGraphSchema::StaticClass();
    Graph->bEditable = true;
    OwnedCanvasGraph.Reset(Graph);
    CanvasGraph = Graph;

    PopulateDemoGraph();
}

void SShineMainPanel::PopulateDemoGraph()
{
    if (!CanvasGraph.IsValid() || CanvasGraph->Nodes.Num() > 0)
    {
        return;
    }

    UShineComfyGraphNodeBase* PromptNode = AddDemoNode(FVector2D(-340.0f, -40.0f), UShineComfyPromptGraphNode::StaticClass());
    UShineComfyGraphNodeBase* SamplerNode = AddDemoNode(FVector2D(10.0f, -10.0f), UShineComfySamplerGraphNode::StaticClass());
    UShineComfyGraphNodeBase* PreviewNode = AddDemoNode(FVector2D(360.0f, 40.0f), UShineComfyPreviewGraphNode::StaticClass());
    UShineComfyGraphNodeBase* GalleryNode = AddDemoNode(FVector2D(360.0f, 250.0f), UShineComfyMultiImageGraphNode::StaticClass());

    if (!PromptNode || !SamplerNode || !PreviewNode || !GalleryNode)
    {
        return;
    }

    if (const UEdGraphSchema* Schema = CanvasGraph->GetSchema())
    {
        Schema->TryCreateConnection(PromptNode->FindPin(TEXT("Conditioning")), SamplerNode->FindPin(TEXT("Conditioning")));
        Schema->TryCreateConnection(PromptNode->FindPin(TEXT("Negative")), SamplerNode->FindPin(TEXT("Negative")));
        Schema->TryCreateConnection(SamplerNode->FindPin(TEXT("Latent")), PreviewNode->FindPin(TEXT("Latent")));
        Schema->TryCreateConnection(SamplerNode->FindPin(TEXT("Latent")), GalleryNode->FindPin(TEXT("Latent")));
    }

    CanvasGraph->NotifyGraphChanged();
}

UShineComfyGraphNodeBase* SShineMainPanel::AddDemoNode(const FVector2D& Position, TSubclassOf<UShineComfyGraphNodeBase> NodeClass)
{
    if (!CanvasGraph.IsValid() || !NodeClass)
    {
        return nullptr;
    }

    UShineComfyGraphNodeBase* NewNode = NewObject<UShineComfyGraphNodeBase>(CanvasGraph.Get(), NodeClass);
    CanvasGraph->AddNode(NewNode, true, false);

    NewNode->SetFlags(RF_Transactional);
    NewNode->CreateNewGuid();
    NewNode->PostPlacedNewNode();
    NewNode->NodePosX = FMath::RoundToInt(Position.X);
    NewNode->NodePosY = FMath::RoundToInt(Position.Y);
    NewNode->AllocateDefaultPins();

    return NewNode;
}

void SShineMainPanel::SetOutputText(const FString& NewOutput)
{
    if (OutputTextBox.IsValid())
    {
        OutputTextBox->SetText(FText::FromString(NewOutput));
    }
}

void SShineMainPanel::SetComfyStatusText(const FString& NewStatus)
{
    if (ComfyStatusTextBlock.IsValid())
    {
        ComfyStatusTextBlock->SetText(FText::FromString(NewStatus));
    }

    SetOutputText(NewStatus);
}

#undef LOCTEXT_NAMESPACE