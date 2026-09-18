#include "Graph/Pin/SShineComfyGraphStandardPin.h"

void SShineComfyGraphStandardPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
    SShineComfyGraphPinBase::Construct(SShineComfyGraphPinBase::FArguments(), InPin);
}

FSlateColor SShineComfyGraphStandardPin::ResolvePinColor() const
{
    if (!GetPinObj())
    {
        return FLinearColor::Gray;
    }

    const FName Category = GetPinObj()->PinType.PinCategory;
    if (Category == TEXT("Shine.Conditioning"))
    {
        return FLinearColor(0.26f, 0.82f, 0.48f, 1.0f);
    }

    if (Category == TEXT("Shine.Latent"))
    {
        return FLinearColor(0.98f, 0.63f, 0.21f, 1.0f);
    }

    if (Category == TEXT("Shine.Image"))
    {
        return FLinearColor(0.31f, 0.66f, 0.97f, 1.0f);
    }

    if (Category == TEXT("Shine.Integer"))
    {
        return FLinearColor(0.86f, 0.87f, 0.91f, 1.0f);
    }

    // 视频工作台那套 pin（ShineVideoGraphTypes.h 里定义）。颜色是"这个 pin 要什么"的
    // 第一眼线索：图片蓝、角色橙、分镜青、视频紫、剧本灰紫——和 Comfy 图那套刻意不同，
    // 免得一眼看过去分不清自己在哪张图上。
    if (Category == TEXT("Shine.Video.Image"))
    {
        return FLinearColor(0.31f, 0.66f, 0.97f, 1.0f);
    }

    if (Category == TEXT("Shine.Video.Picture"))
    {
        return FLinearColor(0.24f, 0.78f, 0.86f, 1.0f);
    }

    if (Category == TEXT("Shine.Video.Character"))
    {
        return FLinearColor(0.91f, 0.54f, 0.20f, 1.0f);
    }

    if (Category == TEXT("Shine.Video.Shot"))
    {
        return FLinearColor(0.24f, 0.60f, 0.86f, 1.0f);
    }

    if (Category == TEXT("Shine.Video.Video"))
    {
        return FLinearColor(0.68f, 0.44f, 0.90f, 1.0f);
    }

    if (Category == TEXT("Shine.Video.Script"))
    {
        return FLinearColor(0.50f, 0.46f, 0.76f, 1.0f);
    }

    return FLinearColor(0.74f, 0.76f, 0.80f, 1.0f);
}