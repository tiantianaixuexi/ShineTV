#include "Node/Slate/Presets/SShineTextureTextureInputNode.h"

#include "AssetRegistry/AssetData.h"
#include "Engine/Texture2D.h"
#include "Node/Presets/ShineTextureTextureInputNode.h"
#include "Node/ShineTextureNodeConstants.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

namespace
{
    constexpr float TextureInputPreviewDisplaySize = 96.0f;
}

void SShineTextureTextureInputNode::Construct(const FArguments& InArgs, UShineTextureTextureInputNode* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureTextureInputNode::CreateNodeContent() const
{
    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateBodyText(NSLOCTEXT("SShineTextureTextureInputNode", "TextureInputHelp", "选择一张纹理资源作为颜色输入源。"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(ShineTextureSlateTheme::GetBodySpacingPadding())
        [
            CreatePreviewFrame(
                SNew(SBox)
                .WidthOverride(TextureInputPreviewDisplaySize)
                .HeightOverride(TextureInputPreviewDisplaySize)
                [
                    SNew(SImage)
                    .Image(this, &SShineTextureTextureInputNode::GetTexturePreviewBrush)
                ],
                1.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateSingleValueSection(
                NSLOCTEXT("SShineTextureTextureInputNode", "TextureLabel", "纹理 (Texture)"),
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(UTexture2D::StaticClass())
                .ObjectPath(this, &SShineTextureTextureInputNode::GetTextureObjectPath)
                .OnObjectChanged(this, &SShineTextureTextureInputNode::HandleTextureChanged)
                .DisplayThumbnail(false)
                .DisplayUseSelected(true)
                .AllowClear(true),
                8.0f)
        ],
        152.0f);
}

const FSlateBrush* SShineTextureTextureInputNode::GetTexturePreviewBrush() const
{
    TexturePreviewBrush = FSlateBrush();
    TexturePreviewBrush.ImageSize = FVector2D(
        static_cast<float>(ShineTextureNodeConstants::DefaultInternalPreviewResolution),
        static_cast<float>(ShineTextureNodeConstants::DefaultInternalPreviewResolution));

    if (const UShineTextureTextureInputNode* TextureNode = GetTextureInputNode())
    {
        if (UTexture2D* SourceTexture = TextureNode->GetSourceTexture())
        {
            TexturePreviewBrush.SetResourceObject(SourceTexture);
            TexturePreviewBrush.DrawAs = ESlateBrushDrawType::Image;
            return &TexturePreviewBrush;
        }
    }

    TexturePreviewBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
    return &TexturePreviewBrush;
}

FString SShineTextureTextureInputNode::GetTextureObjectPath() const
{
    if (const UShineTextureTextureInputNode* TextureNode = GetTextureInputNode())
    {
        if (const UTexture2D* SourceTexture = TextureNode->GetSourceTexture())
        {
            return SourceTexture->GetPathName();
        }
    }

    return FString();
}

void SShineTextureTextureInputNode::HandleTextureChanged(const FAssetData& AssetData) const
{
    if (UShineTextureTextureInputNode* TextureNode = GetTextureInputNode())
    {
        TextureNode->SetSourceTexture(Cast<UTexture2D>(AssetData.GetAsset()));
    }
}

UShineTextureTextureInputNode* SShineTextureTextureInputNode::GetTextureInputNode() const
{
    return GraphNode ? CastChecked<UShineTextureTextureInputNode>(GraphNode) : nullptr;
}
