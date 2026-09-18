#include "Asset/ShineTextureAsset.h"

#include "EdGraph/EdGraphPin.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Graph/ShineTextureGraph.h"
#include "Graph/ShineTextureGraphSchema.h"
#include "Node/Presets/ShineTextureBlendNode.h"
#include "Node/Presets/ShineTextureOutputNode.h"
#include "Node/Presets/ShineTextureSolidColorNode.h"

UShineTextureAsset::UShineTextureAsset()
{
}

UShineTextureGraph* UShineTextureAsset::GetOrCreateGraph()
{
    CreateDefaultGraph();
    return Graph;
}

UTexture* UShineTextureAsset::GetPreviewTexture() const
{
    return PreviewTexture;
}

UTextureRenderTarget2D* UShineTextureAsset::GetOrCreatePreviewRenderTarget(FIntPoint Size)
{
    const FIntPoint TextureSize(FMath::Max(16, Size.X), FMath::Max(16, Size.Y));
    if (!PreviewTexture || PreviewTexture->SizeX != TextureSize.X || PreviewTexture->SizeY != TextureSize.Y)
    {
        PreviewTexture = NewObject<UTextureRenderTarget2D>(this, TEXT("ShineTexturePreview"), RF_Transient);
        PreviewTexture->NeverStream = true;
        PreviewTexture->Filter = TF_Bilinear;
        PreviewTexture->ClearColor = FLinearColor::Black;
        PreviewTexture->bAutoGenerateMips = false;
        PreviewTexture->bCanCreateUAV = true;
        PreviewTexture->RenderTargetFormat = RTF_RGBA8_SRGB;
        PreviewTexture->InitCustomFormat(TextureSize.X, TextureSize.Y, PF_B8G8R8A8, true);
        PreviewTexture->UpdateResourceImmediate(true);
    }

    return PreviewTexture;
}

UTextureRenderTarget2D* UShineTextureAsset::GetOrCreatePreviewRenderTarget(int32 Size)
{
    return GetOrCreatePreviewRenderTarget(FIntPoint(Size, Size));
}

void UShineTextureAsset::PostLoad()
{
    Super::PostLoad();
    CreateDefaultGraph();
}

void UShineTextureAsset::CreateDefaultGraph()
{
    const bool bCreateStarterNodes = (Graph == nullptr);
    if (!Graph)
    {
        Graph = NewObject<UShineTextureGraph>(this, TEXT("ShineTextureGraph"), RF_Transactional);
    }

    if (!Graph->Schema)
    {
        Graph->Schema = UShineTextureGraphSchema::StaticClass();
    }

    if (!bCreateStarterNodes)
    {
        return;
    }

    auto CreateStarterNode = [this](UClass* NodeClass, const FVector2D& Position)
    {
        UShineTextureGraphNodeBase* Node = NewObject<UShineTextureGraphNodeBase>(Graph, NodeClass, NAME_None, RF_Transactional);
        Graph->AddNode(Node, true, false);
        Node->CreateNewGuid();
        Node->NodePosX = Position.X;
        Node->NodePosY = Position.Y;
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        return Node;
    };

    UShineTextureSolidColorNode* BaseColorNode = CastChecked<UShineTextureSolidColorNode>(
        CreateStarterNode(UShineTextureSolidColorNode::StaticClass(), FVector2D(-360.0f, -80.0f)));
    BaseColorNode->SetColorValue(FLinearColor(0.920f, 0.310f, 0.180f, 1.0f));

    UShineTextureSolidColorNode* AccentColorNode = CastChecked<UShineTextureSolidColorNode>(
        CreateStarterNode(UShineTextureSolidColorNode::StaticClass(), FVector2D(-360.0f, 180.0f)));
    AccentColorNode->SetColorValue(FLinearColor(0.140f, 0.580f, 0.930f, 1.0f));

    UShineTextureBlendNode* BlendNode = CastChecked<UShineTextureBlendNode>(
        CreateStarterNode(UShineTextureBlendNode::StaticClass(), FVector2D(-20.0f, 40.0f)));
    BlendNode->SetBlendFactor(0.35f);

    UShineTextureOutputNode* OutputNode = CastChecked<UShineTextureOutputNode>(
        CreateStarterNode(UShineTextureOutputNode::StaticClass(), FVector2D(320.0f, 40.0f)));

    if (UEdGraphPin* BaseColorPin = BaseColorNode->FindPin(TEXT("Color")))
    {
        if (UEdGraphPin* BlendAPin = BlendNode->FindPin(TEXT("A")))
        {
            BaseColorPin->MakeLinkTo(BlendAPin);
        }
    }

    if (UEdGraphPin* AccentColorPin = AccentColorNode->FindPin(TEXT("Color")))
    {
        if (UEdGraphPin* BlendBPin = BlendNode->FindPin(TEXT("B")))
        {
            AccentColorPin->MakeLinkTo(BlendBPin);
        }
    }

    if (UEdGraphPin* BlendResultPin = BlendNode->FindPin(TEXT("Result")))
    {
        if (UEdGraphPin* OutputSurfacePin = OutputNode->FindPin(TEXT("Surface")))
        {
            BlendResultPin->MakeLinkTo(OutputSurfacePin);
        }
    }

    Graph->NotifyGraphChanged();
}