#pragma once

#include "Comfy/ShineComfyClient.h"
#include "CoreMinimal.h"
#include "Graph/ShineComfyGraph.h"
#include "Layout/Visibility.h"
#include "Styling/SlateColor.h"
#include "UObject/StrongObjectPtr.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SCompoundWidget.h"

struct FEdGraphSchemaAction;
struct FGraphAppearanceInfo;
struct FGraphActionListBuilderBase;
struct FPointerEvent;

class ITableRow;
class SGraphEditor;
class SGraphActionMenu;
class SEditableTextBox;
template <typename ItemType> class SListView;
class SMultiLineEditableTextBox;
class SSearchBox;
class STableViewBase;
class STextBlock;
class UShineComfyAsset;
template <typename ItemType> class STreeView;
class UShineComfyGraphNodeBase;
class FUICommandList;

enum class EShineComfyLibraryTab : uint8
{
    Models,
    Nodes
};

struct FShineComfyModelTreeItem
{
    explicit FShineComfyModelTreeItem(const FString& InLabel = FString(), bool bInIsCategory = false)
        : Label(InLabel)
        , bIsCategory(bInIsCategory)
    {
    }

    FString Label;
    bool bIsCategory = false;
    TArray<TSharedPtr<FShineComfyModelTreeItem>> Children;
};

struct FShineComfyTaskProgressState
{
    FString PromptId;
    FString StatusText;
    FString CurrentNodeId;
    int32 ProgressValue = 0;
    int32 ProgressMax = 0;
    bool bHasProgress = false;
    bool bIsRunning = false;
    bool bCompleted = false;
    bool bFailed = false;
};

struct FShineComfyQueueListItem
{
    bool bIsHistory = false;
    FShineComfyQueueEntry Entry;
    FShineComfyHistoryEntry HistoryEntry;
    FString TitleText;
    FString StatusText;
    FString ProgressText;
    float ProgressFraction = 0.0f;
};

class SShineMainPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SShineMainPanel) {}
        SLATE_ARGUMENT(UShineComfyGraph*, Graph)
    SLATE_END_ARGS()

    virtual ~SShineMainPanel() override;

    void Construct(const FArguments& InArgs);
    bool ExportGraphJsonToOutput();
    bool ExportExecutionPlanToOutput();
    bool ExecuteGraphToOutput();
    void ZoomToFitGraph();
    bool CanDeleteSelectedNodes() const;
    void DeleteSelectedNodes();

private:
    void BindGraphEditorCommands();
    FReply HandleRefreshComfyModels();
    FReply HandleRefreshComfyNodes();
    FGraphAppearanceInfo GetGraphAppearance() const;
    void CreateCanvasGraph();
    void PopulateDemoGraph();
    void SetOutputText(const FString& NewOutput);
    void SetComfyStatusText(const FString& NewStatus);
    void FetchComfyModels(bool bForceRefresh);
    void FetchComfyNodes(bool bForceRefresh);
    void ApplyModelLibraryToComfyNodes();
    void RebuildModelTreeItems();
    void CollectNodePaletteActions(FGraphActionListBuilderBase& OutAllActions) const;
    FReply HandleNodePaletteActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent);
    TSharedRef<ITableRow> HandleGenerateModelRow(TSharedPtr<FShineComfyModelTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const;
    void HandleGetModelChildren(TSharedPtr<FShineComfyModelTreeItem> Item, TArray<TSharedPtr<FShineComfyModelTreeItem>>& OutChildren) const;
    void HandleModelFilterTextChanged(const FText& NewFilterText);
    void HandleComfyBaseUrlCommitted(const FText& NewText, ETextCommit::Type CommitType);
    FReply HandleStartComfyTask();
    FReply HandleCaptureScene();
    FReply HandleRefreshComfyQueue();
    FReply HandleDeleteSelectedComfyTask();
    FReply HandleShowModelLibrary();
    FReply HandleShowNodeLibrary();
    EVisibility GetModelLibraryVisibility() const;
    EVisibility GetNodeLibraryVisibility() const;
    FSlateColor GetModelTabColor() const;
    FSlateColor GetNodeTabColor() const;
    void SetQueueStatusText(const FString& NewStatus);
    void RefreshComfyQueue();
    void RefreshComfyHistory();

    /** 把最近一次生成结果抓到本地并画进图里的显示节点（Preview / MultiImageGallery）。 */
    void PushLatestResultToDisplayNodes();
    void ApplyResultImagesToDisplayNodes(const TArray<FString>& ImagePaths);
    TSet<FString> PushedResultPromptIds;
    void RebuildQueueItems(const FShineComfyQueueResult& QueueResult);
    void RefreshVisibleQueueItems();
    void ConnectComfyWebSocket();
    void DisconnectComfyWebSocket();
    void HandleComfyWebSocketConnected();
    void HandleComfyWebSocketConnectionError(const FString& ErrorMessage);
    void HandleComfyWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void HandleComfyWebSocketMessage(const FString& Message);
    TSharedRef<ITableRow> HandleGenerateQueueRow(TSharedPtr<FShineComfyQueueListItem> Item, const TSharedRef<STableViewBase>& OwnerTable) const;
    TSharedPtr<FShineComfyQueueListItem> GetActiveQueueItem() const;
    TOptional<float> GetActiveTaskProgress() const;
    FText GetActiveTaskTitle() const;
    FText GetActiveTaskDetail() const;
    bool CanDeleteSelectedComfyTask() const;
    UShineComfyAsset* GetEditingAsset() const;
    UShineComfyGraphNodeBase* AddDemoNode(const FVector2D& Position, TSubclassOf<UShineComfyGraphNodeBase> NodeClass);

    FString ComfyBaseUrl = TEXT("http://127.0.0.1:8188");
    FString ComfyClientId;
    FString ModelFilterText;
    TArray<FShineComfyModelFolder> ComfyModelFolders;
    TArray<FShineComfyNodeDefinition> ComfyNodeDefinitions;
    TArray<TSharedPtr<FShineComfyModelTreeItem>> ModelRootItems;
    TArray<TSharedPtr<FShineComfyQueueListItem>> QueueItems;
    TArray<FShineComfyHistoryEntry> HistoryEntries;
    FShineComfyQueueResult CurrentQueueResult;
    TMap<FString, FShineComfyTaskProgressState> TaskProgressByPromptId;
    EShineComfyLibraryTab ActiveLibraryTab = EShineComfyLibraryTab::Models;
    bool bComfyWebSocketConnected = false;

    TSharedPtr<SEditableTextBox> ComfyBaseUrlTextBox;
    TSharedPtr<FUICommandList> GraphEditorCommands;
    TSharedPtr<SGraphActionMenu> NodePalette;
    TSharedPtr<SGraphEditor> GraphEditor;
    TSharedPtr<SListView<TSharedPtr<FShineComfyQueueListItem>>> QueueListView;
    TSharedPtr<SSearchBox> ModelSearchBox;
    TSharedPtr<STextBlock> ComfyStatusTextBlock;
    TSharedPtr<STextBlock> QueueStatusTextBlock;
    TSharedPtr<STreeView<TSharedPtr<FShineComfyModelTreeItem>>> ModelTreeView;
    TSharedPtr<SMultiLineEditableTextBox> OutputTextBox;
    // ComfyUI 的 WebSocket 不再由本面板持有：它现在是全项目共用的一条常连
    // （FShineComfySocket），本面板只订阅它的事件。
    TWeakObjectPtr<UShineComfyGraph> CanvasGraph;
    TStrongObjectPtr<UShineComfyGraph> OwnedCanvasGraph;
};