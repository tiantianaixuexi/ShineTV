#pragma once

#include "Comfy/ShineVideoGraphCompiler.h"
#include "Comfy/ShineVideoTaskRunner.h"
#include "CoreMinimal.h"
#include "Misc/NotifyHook.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

// FGraphPanelSelectionSet / FGraphEditorEvents 都定义在这里，而且前者是 typedef，
// 不能前向声明（前向声明会造出一个同名的空结构，和引擎那个不是同一个类型）。
#include "GraphEditor.h"

class UShineVideoProject;
class UShineVideoGraph;
class IDetailsView;
class SGraphActionMenu;
class SGraphEditor;
class SMultiLineEditableTextBox;
class STextBlock;
class SVerticalBox;
class FUICommandList;
struct FEdGraphEditAction;

/**
 * 视频工作台：**画布 + 项目属性 + 流程**。
 *
 * ## 三个页签各自负责什么
 *
 * - **画布**：六类节点（剧本 / 角色 / 素材图片 / 分镜 / 分镜图 / 视频组）与它们之间的连线。
 *   所有节点参数就地编辑（`SShineVideoNode` 系列），不需要另一个表单。
 * - **项目**：资产自己的属性（ComfyUI 地址、四个模型文件名、产物前缀、Turbo LoRA…）
 *   加上"当前选中节点"的可读摘要。分镜表（`Shots`）是**编译产物**，只读地列在旁边——
 *   手改它没有意义，下一次编译就会被覆盖。
 * - **流程**：沿用第 1 步的状态机。一次任务要经过 显存门禁 → 上传 → 提交 →
 *   25 步 × N 段 → 回读 → 落盘，这中间每一步都得看得见，否则出问题只能翻 Output Log。
 *
 * ## 工具栏的顺序就是推荐的操作顺序
 *
 * 「编译到项目」→「生成」→「取消」，最后才是「导出节点图」。
 * 编译是零成本（纯函数、不联网）的，所以它放在最前面，鼓励先编译一遍看接线对不对——
 * ComfyUI 对未知输入键是静默忽略的，"提交成功"什么都证明不了。
 */
class FShineVideoGraphEditor : public FAssetEditorToolkit, public FGCObject, public FNotifyHook
{
public:
    virtual ~FShineVideoGraphEditor() override;

    void InitAssetEditor(EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UShineVideoProject* InAsset);

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

    virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override;

    /** 项目属性改了：立刻重排分镜概览 + 标脏。 */
    virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

private:
    UShineVideoGraph* GetGraph() const;
    void MarkAssetDirty();

    void ExtendToolbar();

    bool CanCompile() const;
    bool CanStartTask() const;
    bool CanCancel() const;
    bool CanExportGraph() const;

    /** 有没有一次任务正在跑。 */
    bool IsTaskActive() const;

    /**
     * 编译一遍画布。
     *
     * @param bApplyToProject true = 把结果写进资产的 `Shots`（"编译到项目"就是这么用的）；
     *                        false = 只试算，用来核对（"生成"之前也会先跑这个）。
     * @return 编译是否成功（失败原因已经写进流程页签）。
     */
    bool CompileGraph(bool bApplyToProject);

    void HandleCompileClicked();
    void HandleGenerateClicked();
    void HandleCancelClicked();
    void HandleExportGraphClicked();

    FShineVideoTaskRunner& EnsureRunner();

    void HandleGraphChanged(const FEdGraphEditAction& Action);
    void HandleSelectionChanged(const FGraphPanelSelectionSet& Selection);
    void RefreshSelectionSummary();

    void HandleStatusChanged(const FShineVideoTaskStatus& Status);
    void HandleFinished();

    void AppendLog(const FString& Line);
    void RefreshShotRows(const FShineVideoTaskStatus* Status);
    void ShowNotification(const FText& Message, bool bSuccess);

    struct FGraphAppearanceInfo GetGraphAppearance() const;
    void CollectNodePaletteActions(struct FGraphActionListBuilderBase& OutAllActions) const;
    FReply HandleNodePaletteActionDragged(const TArray<TSharedPtr<struct FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent);

    TSharedRef<class SDockTab> SpawnCanvasTab(const class FSpawnTabArgs& Args);
    TSharedRef<class SDockTab> SpawnDetailsTab(const class FSpawnTabArgs& Args);
    TSharedRef<class SDockTab> SpawnProcessTab(const class FSpawnTabArgs& Args);

    static const FName CanvasTabId;
    static const FName DetailsTabId;
    static const FName ProcessTabId;

    TObjectPtr<UShineVideoProject> EditingAsset;

    TSharedPtr<SGraphEditor> GraphEditor;
    TSharedPtr<SGraphActionMenu> NodePalette;
    TSharedPtr<FUICommandList> GraphEditorCommands;
    TSharedPtr<IDetailsView> DetailsView;

    TSharedPtr<STextBlock> SelectionText;
    TSharedPtr<STextBlock> StateText;
    TSharedPtr<SVerticalBox> ShotRowsBox;
    TSharedPtr<SMultiLineEditableTextBox> LogTextBox;

    TSharedPtr<FShineVideoTaskRunner> Runner;
    FDelegateHandle FinishedHandle;

    /** 最近一次编译的结果：把 runner 的"第几段"翻译回画布节点就靠它。 */
    FShineVideoGraphCompileResult LastCompileResult;

    FDelegateHandle GraphChangedHandle;

    /** 画布重建的节流：逐段进度一秒能来好几条，不该每条都重建全部节点控件。 */
    double LastCanvasRefreshSeconds = 0.0;
    static constexpr double CanvasRefreshIntervalSeconds = 1.5;

    FString LogText;
    FString LastLoggedDetail;
    int32 LastLoggedStep = -1;
    int32 LastLoggedWarningCount = 0;
};
