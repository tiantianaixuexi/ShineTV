#include "Graph/Slate/Video/SShineVideoImageNode.h"

#include "Comfy/ShineImageTaskRunner.h"
#include "Comfy/ShineMentionResolver.h"
#include "Comfy/ShineVideoGraphCompiler.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Graph/Node/Video/ShineVideoGraphNodeBase.h"
#include "Graph/Node/Video/ShineVideoImageGraphNode.h"
#include "Graph/Node/Video/ShineVideoShotImageGraphNode.h"
#include "Graph/Slate/ShineImageZoom.h"
#include "Misc/Paths.h"
#include "UI/SShineComfyMediaPreview.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"

void SShineVideoImageNode::Construct(const FArguments& InArgs, UShineComfyGraphNodeBase* InNode)
{
    SShineVideoNode::Construct(SShineVideoNode::FArguments(), InNode);
}

FString SShineVideoImageNode::ResolveDisplayPath() const
{
    FString RawPath;

    if (const UShineVideoShotImageGraphNode* ShotImageNode = Cast<UShineVideoShotImageGraphNode>(GraphNode))
    {
        RawPath = ShotImageNode->GetImagePath();
    }
    else if (const UShineVideoImageGraphNode* ImageNode = Cast<UShineVideoImageGraphNode>(GraphNode))
    {
        RawPath = ImageNode->GetImagePath();
    }

    if (RawPath.IsEmpty())
    {
        return FString();
    }

    // 素材库相对路径要按设置里的素材库根解析，这里统一走 P3 的解析器。
    const FString Resolved = FShineMentionResolver::ResolveLocalPath(RawPath);
    return Resolved.IsEmpty() ? RawPath : Resolved;
}

TSharedRef<SWidget> SShineVideoImageNode::BuildPreviewWidget()
{
    const FString DisplayPath = ResolveDisplayPath();

    TSharedRef<SShineComfyMediaPreview> PreviewWidget = SAssignNew(MediaPreview, SShineComfyMediaPreview)
        .PreviewSize(FVector2D(300.0f, 200.0f))
        .MediaPath(DisplayPath);

    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Center)
        [
            PreviewWidget
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
            [
                // 出图按钮只在「分镜图」节点上出现：它是"分镜 → 视频"之间的那一级，
                // 而「素材图片」节点只是引用一张现成的图。
                SNew(SButton)
                .Text(FText::FromString(TEXT("出图")))
                .ToolTipText(FText::FromString(TEXT(
                    "用上游分镜的提示词 + 这张底图跑一次 SD1.5 出图（底图旁边有 _Depth / _Normal 就自动接两支 ControlNet）")))
                .Visibility(this, &SShineVideoImageNode::GetGenerateButtonVisibility)
                .IsEnabled(this, &SShineVideoImageNode::CanGenerate)
                .OnClicked(FOnClicked::CreateSP(this, &SShineVideoImageNode::HandleGenerateClicked))
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("放大")))
                .ToolTipText(FText::FromString(TEXT("按原图尺寸打开，可拖大窗口看细节")))
                .IsEnabled_Lambda([this]() -> bool { return !ResolveDisplayPath().IsEmpty(); })
                .OnClicked(FOnClicked::CreateSP(this, &SShineVideoImageNode::HandleZoomClicked))
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("重新读图")))
                .ToolTipText(FText::FromString(TEXT("同名文件被新结果覆盖后，点它重读一次")))
                .IsEnabled_Lambda([this]() -> bool { return !ResolveDisplayPath().IsEmpty(); })
                .OnClicked(FOnClicked::CreateSP(this, &SShineVideoImageNode::HandleReloadClicked))
            ]
        ];
}

FReply SShineVideoImageNode::HandleZoomClicked()
{
    const FString DisplayPath = ResolveDisplayPath();
    if (!DisplayPath.IsEmpty())
    {
        ShineImageZoom::Show(DisplayPath);
    }

    return FReply::Handled();
}

FReply SShineVideoImageNode::HandleReloadClicked()
{
    if (MediaPreview.IsValid())
    {
        // 先清掉缓存路径，否则 SetMediaPath 会因为"路径没变"直接返回、不重读。
        MediaPreview->Reload();
    }

    return FReply::Handled();
}

EVisibility SShineVideoImageNode::GetGenerateButtonVisibility() const
{
    return GraphNode && GraphNode->IsA<UShineVideoShotImageGraphNode>()
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

bool SShineVideoImageNode::CanGenerate() const
{
    // 一次只允许一个出图任务（FShineImageTaskRunner::Start 也守着这条），
    // 所以有任务在跑时按钮直接变灰，而不是点了才告诉你"已经有任务在跑"。
    return !FShineImageTaskRunner::GetActive().IsValid();
}

FReply SShineVideoImageNode::HandleGenerateClicked()
{
    UShineVideoShotImageGraphNode* ShotImageNode = Cast<UShineVideoShotImageGraphNode>(GraphNode);
    if (!ShotImageNode)
    {
        return FReply::Handled();
    }

    // 画布 → 出图请求：与无 UI 控制台走的是同一处（FShineVideoGraphCompiler），
    // 所以"UI 上出的图"和"命令行出的图"不可能各按各的规则拼参数。
    const FShineVideoGraphCompiler::FStoryboardRequest Storyboard =
        FShineVideoGraphCompiler::BuildStoryboardRequest(*ShotImageNode);

    if (!Storyboard.bSuccess)
    {
        ShowNotification(Storyboard.ErrorMessage, false);
        return FReply::Handled();
    }

    FShineImageTaskRequest TaskRequest;
    FShineVideoGraphCompiler::MakeImageTaskRequest(Storyboard, ShotImageNode, FString(), false, TaskRequest);

    FString Error;
    if (!FShineImageTaskRunner::Start(TaskRequest, false, Error))
    {
        ShowNotification(Error, false);
        return FReply::Handled();
    }

    // 底图是哪一张、接没接 ControlNet 这类信息不能只进日志：它就是"为什么出成这样"的答案。
    if (Storyboard.Warnings.Num() > 0)
    {
        ShowNotification(FString::Join(Storyboard.Warnings, TEXT("\n")), true);
    }

    return FReply::Handled();
}

void SShineVideoImageNode::ShowNotification(const FString& Message, bool bSuccess) const
{
    if (Message.IsEmpty())
    {
        return;
    }

    FNotificationInfo Info(FText::FromString(Message));
    Info.ExpireDuration = bSuccess ? 6.0f : 10.0f;
    Info.bUseSuccessFailIcons = true;

    const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
    if (Item.IsValid())
    {
        Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
    }
}

FText SShineVideoImageNode::GetExtraStatusText() const
{
    // 出图在跑的时候，节点上最该看见的是"当前这一步在干什么"，而不是上一次的文件名。
    if (const UShineVideoGraphNodeBase* VideoNode = GetVideoNode())
    {
        const EShineVideoNodeRuntimeState State = VideoNode->GetRuntimeState();
        const bool bInFlight = State == EShineVideoNodeRuntimeState::Pending
            || State == EShineVideoNodeRuntimeState::Running;
        if (bInFlight && !VideoNode->GetRuntimeDetail().IsEmpty())
        {
            return FText::FromString(VideoNode->GetRuntimeDetail());
        }
    }

    const FString DisplayPath = ResolveDisplayPath();
    if (DisplayPath.IsEmpty())
    {
        return FText::FromString(TEXT("还没有图"));
    }

    return FText::FromString(FPaths::GetCleanFilename(DisplayPath));
}

bool SShineVideoImageNode::HasExtraBody() const
{
    // 图片节点永远显示身体：缩略图本身就是要看的东西，哪怕还没跑过任务。
    return true;
}
