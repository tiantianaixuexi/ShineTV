#include "Node/ShineTextureGraphNodeBase.h"

#include "Asset/ShineTextureAsset.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Graph/ShineTextureGraphSchema.h"
#include "Node/ShineTextureNodeConstants.h"
#include "Node/Slate/SShineTextureGraphNodeBase.h"
#include "Render/ShineTexturePreviewRenderer.h"
#include "SGraphNode.h"

const FName UShineTextureGraphNodeBase::ColorPinCategory(TEXT("ShineTexture.Color"));
const FName UShineTextureGraphNodeBase::ScalarPinCategory(TEXT("ShineTexture.Scalar"));
const FName UShineTextureGraphNodeBase::Vector2PinCategory(TEXT("ShineTexture.Vector2"));
const FName UShineTextureGraphNodeBase::BoolPinCategory(TEXT("ShineTexture.Bool"));
const FName UShineTextureGraphNodeBase::IntPinCategory(TEXT("ShineTexture.Int"));

TSharedPtr<SGraphNode> UShineTextureGraphNodeBase::CreateVisualWidget()
{
    return SNew(SShineTextureGraphNodeBase, this);
}

void UShineTextureGraphNodeBase::AllocateDefaultPins()
{
    BuildNodePins();
}

FText UShineTextureGraphNodeBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return NodeTitle;
}

FLinearColor UShineTextureGraphNodeBase::GetNodeTitleColor() const
{
    return AccentColor;
}

FText UShineTextureGraphNodeBase::GetTooltipText() const
{
    return NodeSubtitle;
}

bool UShineTextureGraphNodeBase::CanUserDeleteNode() const
{
    return true;
}

bool UShineTextureGraphNodeBase::CanDuplicateNode() const
{
    return true;
}

bool UShineTextureGraphNodeBase::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const
{
    return DesiredSchema && DesiredSchema->IsA<UShineTextureGraphSchema>();
}

FText UShineTextureGraphNodeBase::GetSubtitle() const
{
    return NodeSubtitle;
}

bool UShineTextureGraphNodeBase::IsInlinePreviewExpanded() const
{
    return bInlinePreviewExpanded;
}

void UShineTextureGraphNodeBase::SetInlinePreviewExpanded(bool bInExpanded)
{
    if (bInlinePreviewExpanded == bInExpanded)
    {
        return;
    }

    Modify();
    bInlinePreviewExpanded = bInExpanded;
    NotifyNodeVisualsChanged();
}

FIntPoint UShineTextureGraphNodeBase::GetInlinePreviewSize() const
{
    return InlinePreviewSize;
}

void UShineTextureGraphNodeBase::SetInlinePreviewSize(FIntPoint InPreviewSize)
{
    const FIntPoint NewPreviewSize(
        FMath::Clamp(InPreviewSize.X, 64, 1024),
        FMath::Clamp(InPreviewSize.Y, 64, 1024));
    if (InlinePreviewSize == NewPreviewSize)
    {
        return;
    }

    Modify();
    InlinePreviewSize = NewPreviewSize;
    NotifyNodeVisualsChanged();
}

int32 UShineTextureGraphNodeBase::GetInlinePreviewResolution() const
{
    return GetInlinePreviewSize().X;
}

void UShineTextureGraphNodeBase::SetInlinePreviewResolution(int32 InPreviewResolution)
{
    SetInlinePreviewSize(FIntPoint(InPreviewResolution, InPreviewResolution));
}

UTexture* UShineTextureGraphNodeBase::GetInlinePreviewTexture() const
{
    return InlinePreviewTexture;
}

UTexture* UShineTextureGraphNodeBase::GetInlinePreviewDisplayTexture() const
{
    if (PreviewDisplayMode == EShineTexturePreviewDisplayMode::Color)
    {
        return GetInlinePreviewTexture();
    }

    return InlinePreviewDisplayTexture ? InlinePreviewDisplayTexture : GetInlinePreviewTexture();
}

UTextureRenderTarget2D* UShineTextureGraphNodeBase::GetOrCreateInlinePreviewRenderTarget(FIntPoint Size)
{
    const FIntPoint TextureSize(FMath::Max(16, Size.X), FMath::Max(16, Size.Y));
    if (!InlinePreviewTexture || InlinePreviewTexture->SizeX != TextureSize.X || InlinePreviewTexture->SizeY != TextureSize.Y)
    {
        InlinePreviewTexture = NewObject<UTextureRenderTarget2D>(this, TEXT("ShineTextureInlinePreview"), RF_Transient);
        InlinePreviewTexture->NeverStream = true;
        InlinePreviewTexture->Filter = TF_Bilinear;
        InlinePreviewTexture->ClearColor = FLinearColor::Black;
        InlinePreviewTexture->bAutoGenerateMips = false;
        InlinePreviewTexture->bCanCreateUAV = true;
        InlinePreviewTexture->RenderTargetFormat = RTF_RGBA8_SRGB;
        InlinePreviewTexture->InitCustomFormat(TextureSize.X, TextureSize.Y, PF_B8G8R8A8, true);
        InlinePreviewTexture->UpdateResourceImmediate(true);
    }

    return InlinePreviewTexture;
}

UTextureRenderTarget2D* UShineTextureGraphNodeBase::GetOrCreateInlinePreviewDisplayRenderTarget(FIntPoint Size)
{
    const FIntPoint TextureSize(FMath::Max(16, Size.X), FMath::Max(16, Size.Y));
    if (!InlinePreviewDisplayTexture || InlinePreviewDisplayTexture->SizeX != TextureSize.X || InlinePreviewDisplayTexture->SizeY != TextureSize.Y)
    {
        InlinePreviewDisplayTexture = NewObject<UTextureRenderTarget2D>(this, TEXT("ShineTextureInlinePreviewDisplay"), RF_Transient);
        InlinePreviewDisplayTexture->NeverStream = true;
        InlinePreviewDisplayTexture->Filter = TF_Bilinear;
        InlinePreviewDisplayTexture->ClearColor = FLinearColor::Black;
        InlinePreviewDisplayTexture->bAutoGenerateMips = false;
        InlinePreviewDisplayTexture->bCanCreateUAV = true;
        InlinePreviewDisplayTexture->RenderTargetFormat = RTF_RGBA8_SRGB;
        InlinePreviewDisplayTexture->InitCustomFormat(TextureSize.X, TextureSize.Y, PF_B8G8R8A8, true);
        InlinePreviewDisplayTexture->UpdateResourceImmediate(true);
    }

    return InlinePreviewDisplayTexture;
}

EShineTexturePreviewDisplayMode UShineTextureGraphNodeBase::GetPreviewDisplayMode() const
{
    return PreviewDisplayMode;
}

void UShineTextureGraphNodeBase::SetPreviewDisplayMode(EShineTexturePreviewDisplayMode InPreviewDisplayMode)
{
    if (PreviewDisplayMode == InPreviewDisplayMode)
    {
        return;
    }

    Modify();
    PreviewDisplayMode = InPreviewDisplayMode;

    if (UShineTextureAsset* Asset = GetGraph() ? GetGraph()->GetTypedOuter<UShineTextureAsset>() : nullptr)
    {
        ShineTexturePreviewRenderer::RefreshAssetPreview(Asset);
    }

    if (UEdGraph* Graph = GetGraph())
    {
        Graph->NotifyGraphChanged();
    }
}

void UShineTextureGraphNodeBase::BeginInteractivePreviewChange()
{
    bInteractivePreviewChange = true;
}

void UShineTextureGraphNodeBase::EndInteractivePreviewChange()
{
    if (!bInteractivePreviewChange)
    {
        return;
    }

    bInteractivePreviewChange = false;
    NotifyNodeVisualsChanged();
}

bool UShineTextureGraphNodeBase::IsNumericPinCategory(const FName& PinCategory)
{
    return PinCategory == ScalarPinCategory || PinCategory == Vector2PinCategory || PinCategory == ColorPinCategory || PinCategory == IntPinCategory;
}

bool UShineTextureGraphNodeBase::CanConnectPinCategories(const FName& OutputPinCategory, const FName& InputPinCategory)
{
    if (OutputPinCategory == InputPinCategory)
    {
        return true;
    }

    if (IsNumericPinCategory(OutputPinCategory) && IsNumericPinCategory(InputPinCategory))
    {
        return true;
    }

    if (OutputPinCategory == BoolPinCategory && IsNumericPinCategory(InputPinCategory))
    {
        return true;
    }

    return false;
}

void UShineTextureGraphNodeBase::SetNodePresentation(const FText& InNodeTitle, const FText& InNodeSubtitle, const FLinearColor& InAccentColor)
{
    NodeTitle = InNodeTitle;
    NodeSubtitle = InNodeSubtitle;
    AccentColor = InAccentColor;
}

void UShineTextureGraphNodeBase::CreateNamedPin(EEdGraphPinDirection Direction, const FName& PinCategory, const FName& PinName)
{
    CreatePin(Direction, PinCategory, PinName);
}

void UShineTextureGraphNodeBase::NotifyNodeVisualsChanged()
{
    if (bInteractivePreviewChange)
    {
        if (UShineTextureAsset* Asset = GetGraph() ? GetGraph()->GetTypedOuter<UShineTextureAsset>() : nullptr)
        {
            ShineTexturePreviewRenderer::RefreshAssetPreview(Asset);
        }
        return;
    }

    if (UEdGraph* Graph = GetGraph())
    {
        Graph->NotifyGraphChanged();
    }
}

void UShineTextureGraphNodeBase::BuildNodePins()
{
}
