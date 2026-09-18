#pragma once

#include "CoreMinimal.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"

class UShineTextureSolidColorNode;

class SShineTextureSolidColorNode : public SShineTextureGraphNodeBase
{
public:
    SLATE_BEGIN_ARGS(SShineTextureSolidColorNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineTextureSolidColorNode* InNode);

protected:
    virtual TSharedRef<SWidget> CreateNodeContent() const override;

private:
    TOptional<int32> GetColorChannelValue(int32 ChannelIndex) const;
    void SetColorChannelValue(int32 ChannelIndex, int32 ChannelValue) const;
    FReply HandleColorBlockClicked() const;
    void HandleColorPicked(FLinearColor NewColor) const;
    void HandleColorPickerCancelled(FLinearColor OriginalColor) const;
    void HandleColorPickerClosed(const TSharedRef<SWindow>& Window) const;
    UShineTextureSolidColorNode* GetSolidColorNode() const;

    mutable TOptional<FLinearColor> PendingColorPickerOriginalColor;
};
