#pragma once

#include "Asset/ShineVideoTypes.h"
#include "Comfy/Builders/SceneToImage/ShineSceneToImageWorkflowBuilder.h"
#include "CoreMinimal.h"

class UShineVideoGraph;
class UShineVideoProject;
class UShineVideoShotImageGraphNode;
struct FShineVideoTaskStatus;
struct FShineImageTaskRequest;

/**
 * 一段视频在画布上的来源：哪个视频组节点的哪个槽位。
 *
 * 存在的唯一理由：任务跑起来之后要把 runner 的逐段进度画回**对应的节点**，
 * 而 runner 只认"第几段"（`FShineVideoShotProgress::ShotIndex`，下标进 `Shots`）。
 * 有了这张表，"第 3 段"就能翻译成"视频组 A 的 #3 槽位"。
 */
struct FShineVideoSegmentBinding
{
    /** 视频组节点的 `UEdGraphNode::NodeGuid`。 */
    FGuid VideoGroupNodeId;

    /** 槽位下标（0 基）。 */
    int32 SlotIndex = INDEX_NONE;
};

/**
 * 画布编译的结果。
 *
 * 顺序就是段序：`Shots[i]` 与 `Bindings[i]`、`SegmentCaptions[i]` 一一对应。
 */
struct FShineVideoGraphCompileResult
{
    bool bSuccess = false;
    FString ErrorMessage;

    /** 编译出来的分镜表，顺序 = 段序。直接灌进 `UShineVideoProject::Shots`。 */
    TArray<FShineVideoShot> Shots;

    /** 与 `Shots` 一一对应的节点来源。 */
    TArray<FShineVideoSegmentBinding> Bindings;

    /** 与 `Shots` 一一对应的段标题（画布上逐段显示用）。 */
    TArray<FString> SegmentCaptions;

    /** 画布上用到的角色资产路径（灌进 `UShineVideoProject::CharacterAssetPaths`）。 */
    TArray<FString> CharacterAssetPaths;

    /** 产物前缀（取视频组节点上填的那个；多个组不一致时取第一个并给警告）。 */
    FString FilenamePrefix;

    TArray<FString> Warnings;

    int32 GetShotCount() const { return Shots.Num(); }
};

/**
 * 画布 → 分镜表。
 *
 * ## 为什么只编到"分镜表"就停
 *
 * `FShineMiniMaxH3WorkflowBuilder` 已经与 P0 探针图（`probe_ref2va.json` / `probe_chain2.json`）
 * 逐字段对拍过，它是唯一权威的"分镜表 → ComfyUI API 图"实现。画布这一层再往下编一次，
 * 只会多出一处可能与探针图不一致的代码。所以这里只做画布语义 → `FShineVideoShot` 的翻译：
 * 段序、参考图顺序、链式、提示词与参数，一个不多。
 *
 * ## 纯函数
 *
 * 不读文件内容、不联网、不加载图片（只在需要提示"这张图不存在"时查一次 `FileExists`）。
 * 所以控制台可以随时 `-dryrun` 式地编一遍来核对，零 GPU 成本。
 *
 * ## 顺序规则（唯一权威说明，UI 上展示的编号必须与此一致）
 *
 * 一段的参考图顺序：
 *   1. 如果这一段的上游是**分镜图**节点，分镜图这张排在最前（`<Picture 1>`）——
 *      这就是 PLAN 3.2 第 3 条"先出分镜图，再让 H3 拿分镜图当参考/首帧"；
 *   2. 然后是分镜节点上 9 个槽位**按槽位下标**依次展开，空槽位跳过；
 *      角色槽位展开成该角色资产的整份参考图（顺序 = 资产里的顺序）。
 *
 * 提示词里如果还写着 `@image:` / `@char:`，`FShineMentionResolver` 会把它们排在上面这些之前
 * （那是 P3 定下的语义，`ShineMentionResolver.h` 里有完整说明），编号因此整体后移——
 * 编译器会在这种时候给一条警告，别让"槽位编号"和"实际编号"悄悄对不上。
 */
class SHINEEDITOR_API FShineVideoGraphCompiler
{
public:
    /** 编译整张画布。没接到视频组的分镜、孤立节点都会以警告形式报出来。 */
    static FShineVideoGraphCompileResult Compile(const UShineVideoGraph& Graph);

    /**
     * 把编译结果写进项目资产：`Shots` / `CharacterAssetPaths` / `FilenamePrefix`。
     * 模型、URL、输出设置一概不动——那些是项目级配置，画布上不重复一份。
     */
    static void ApplyToProject(const FShineVideoGraphCompileResult& Result, UShineVideoProject& Project);

    /**
     * 把任务进度画回画布：每段的运行期状态、步进、产物文件。
     *
     * 只在"确实变了"的时候才调 `NotifyGraphChanged()`——那个调用会把所有节点控件
     * 拆掉重建（`SGraphPanel::PurgeVisualRepresentation`），一次两段链式有几百条
     * 进度事件，不做变更判断会一直重建。
     *
     * @return true 表示节点状态有改动（调用方据此决定要不要重绘画布）。
     */
    static bool ApplyTaskStatus(UShineVideoGraph& Graph, const FShineVideoGraphCompileResult& Result, const FShineVideoTaskStatus& Status);

    /** 提示词里是否还留着引用记号（`@image:` / `@char:` / `{{Mixed N}}`）。 */
    static bool PromptHasMentionSyntax(const FString& Prompt);

    // ---------------------------------------------------------------- 分镜图出图

    /**
     * 画布上一颗「分镜图」节点的一次出图请求。
     *
     * 提示词取**上游分镜**（含剧本注入），参考图取这一级真正要用的那一张颜色图。
     */
    struct FStoryboardRequest
    {
        bool bSuccess = false;
        FString ErrorMessage;

        /** 节点的可读名（日志 / 通知用）。 */
        FString NodeLabel;

        /**
         * builder 的输入：提示词 + 采样参数 + 产物前缀。
         * `ColorImageName` / `DepthImageName` / `NormalImageName` **故意留空**——
         * 那是"上传之后 ComfyUI 那边的名字"，由 `FShineImageTaskRunner` 填。
         */
        FShineSceneToImageRequest Workflow;

        /** 颜色参考图的本地绝对路径。 */
        FString ColorImagePath;

        /** 深度 / 法线的本地绝对路径；空 = 不接那一支 ControlNet。 */
        FString DepthImagePath;
        FString NormalImagePath;

        TArray<FString> Warnings;
    };

    /**
     * 组装一颗分镜图节点的出图请求（纯函数：只在"这张图在不在磁盘上"时查一次 `FileExists`）。
     *
     * ## 颜色参考图怎么选
     *
     * 候选按画布上的优先级排：**「图片」输入接线 → 上游分镜的第一个已接线槽位 →
     * 节点自己的「图片路径」参数**；然后把候选里**第一张带 `_Depth` / `_Normal` 兄弟文件**的
     * 挑出来用（一个都没有时用第一个）。之所以优先挑"带兄弟文件"的那张：深度 / 法线正好驱动
     * 两支 ControlNet，而"这张图有没有结构引导"是一次出图里差别最大的那一项。选中的那张会
     * 通过 `Warnings` 说明白，不是暗箱。
     *
     * ## 提示词
     *
     * 上游分镜的提示词 + 剧本注入（与视频那条线同一处实现），但**去掉 `<Picture N>`**：
     * 那是 H3 的参考图标签（tokenizer 按连接顺序生成），SD1.5 只会把它当成乱码 token。
     * 身份/构图在这种模式下靠参考图 + ControlNet 锚定。
     */
    static FStoryboardRequest BuildStoryboardRequest(const UShineVideoShotImageGraphNode& Node);

    /**
     * 把 `FStoryboardRequest` 转成一次出图任务请求（UI 上的「出图」按钮与控制台走同一处）。
     *
     * 图名故意留空：那是"上传之后 ComfyUI 那边叫什么"，由 `FShineImageTaskRunner` 填。
     *
     * @param bNoBrothers true = 不接深度/法线，也不去找兄弟文件（零成本对拍"不接 ControlNet"
     *                    那一种形态时用；UI 上恒为 false）。
     */
    static void MakeImageTaskRequest(const FStoryboardRequest& Storyboard, UShineVideoShotImageGraphNode* TargetNode,
        const FString& BaseUrlOverride, bool bNoBrothers, FShineImageTaskRequest& OutRequest);
};
