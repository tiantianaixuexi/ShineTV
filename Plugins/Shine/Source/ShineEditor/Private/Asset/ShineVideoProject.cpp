#include "Asset/ShineVideoProject.h"

#include "Asset/ShineVideoGraph.h"
#include "Graph/ShineVideoGraphSchema.h"

UShineVideoProject::UShineVideoProject()
{
    // 默认值都在头文件里写好了（模型文件名是 P0 实测确认可用的那几份）。
    // 这里只保证"即便有人在别处 NewObject 出来也能用"。
    if (ComfyBaseUrl.IsEmpty())
    {
        ComfyBaseUrl = TEXT("http://127.0.0.1:8188");
    }
}

void UShineVideoProject::PostLoad()
{
    Super::PostLoad();

    // 老资产（第 1 步手工填 Shots 的那些）没有 Graph 字段；这里补一张空画布，
    // 而不是把 Shots 反向"猜"成一张图——猜错比空着更难排查。
    if (Graph)
    {
        Graph->Schema = UShineVideoGraphSchema::StaticClass();
        Graph->bEditable = true;
    }
}

UShineVideoGraph* UShineVideoProject::GetOrCreateGraph()
{
    if (!Graph)
    {
        // RF_Transactional：画布上的一次连线/一次参数修改都要能撤销。
        Graph = NewObject<UShineVideoGraph>(this, TEXT("VideoGraph"), RF_Transactional);
    }

    if (Graph)
    {
        Graph->Schema = UShineVideoGraphSchema::StaticClass();
        Graph->bEditable = true;
    }

    return Graph;
}

bool UShineVideoProject::Sanitize()
{
    bool bChanged = false;

    if (ComfyBaseUrl.TrimStartAndEnd().IsEmpty())
    {
        ComfyBaseUrl = TEXT("http://127.0.0.1:8188");
        bChanged = true;
    }
    else
    {
        // 提交时自己会拼 "/api/..."，这里先把尾斜杠去掉，免得出现 "//api"。
        FString TrimmedUrl = ComfyBaseUrl.TrimStartAndEnd();
        while (TrimmedUrl.EndsWith(TEXT("/")))
        {
            TrimmedUrl.LeftChopInline(1);
            bChanged = true;
        }

        ComfyBaseUrl = TrimmedUrl;
    }

    if (FilenamePrefix.TrimStartAndEnd().IsEmpty())
    {
        FilenamePrefix = TEXT("Shine/H3");
        bChanged = true;
    }

    for (int32 ShotIndex = 0; ShotIndex < Shots.Num(); ++ShotIndex)
    {
        FShineVideoShot& Shot = Shots[ShotIndex];

        // 参考图的硬上限在 H3 侧是 9 张（ref_images.ref_image_* 的 max=9），
        // 超出的会被静默忽略——这里先裁掉，并让 builder 再按"是否链式"复核一次。
        if (Shot.ReferenceImages.Num() > MaxReferenceImagesPerShot)
        {
            Shot.ReferenceImages.SetNum(MaxReferenceImagesPerShot);
            bChanged = true;
        }

        // 运行期字段是"上一次的结果"，提交前必须清掉：否则失败时会让人以为这次也有了产物。
        if (!Shot.LastPromptId.IsEmpty() || Shot.LastOutputFiles.Num() > 0 || !Shot.LastError.IsEmpty())
        {
            Shot.LastPromptId.Reset();
            Shot.LastOutputFiles.Reset();
            Shot.LastError.Reset();
            bChanged = true;
        }
    }

    return bChanged;
}
