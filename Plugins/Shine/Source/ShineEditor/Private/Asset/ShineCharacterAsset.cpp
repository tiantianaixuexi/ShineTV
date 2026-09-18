#include "Asset/ShineCharacterAsset.h"

UShineCharacterAsset::UShineCharacterAsset()
{
}

bool UShineCharacterAsset::Sanitize()
{
    bool bChanged = false;

    if (ReferenceImages.Num() > MaxReferenceImages)
    {
        // 从末尾裁：H3 的 <Picture i> 是按连接顺序编号的，从头裁会让所有编号整体错位，
        // 提示词里已经写好的 {{Mixed N}} 全废。
        ReferenceImages.SetNum(MaxReferenceImages);
        bChanged = true;
    }

    if (DisplayName.TrimStartAndEnd().IsEmpty())
    {
        DisplayName = GetName();
        bChanged = true;
    }

    return bChanged;
}
