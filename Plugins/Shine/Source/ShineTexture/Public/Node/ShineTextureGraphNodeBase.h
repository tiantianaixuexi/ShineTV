#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Node/ShineTextureNodeConstants.h"
#include "ShineTextureGraphNodeBase.generated.h"

class SGraphNode;
class UEdGraphSchema;
class UTexture;
class UTextureRenderTarget2D;

UENUM()
enum class EShineTexturePreviewDisplayMode : uint8
{
    Color UMETA(DisplayName = "RGBA"),
    Grayscale UMETA(DisplayName = "Grayscale"),
    Red UMETA(DisplayName = "Red"),
    Green UMETA(DisplayName = "Green"),
    Blue UMETA(DisplayName = "Blue"),
    Alpha UMETA(DisplayName = "Alpha"),
};

UCLASS(Abstract)
class SHINETEXTUREEDITOR_API UShineTextureGraphNodeBase : public UEdGraphNode
{
    GENERATED_BODY()

public:
    virtual TSharedPtr<SGraphNode> CreateVisualWidget();
    virtual void AllocateDefaultPins() override;
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FLinearColor GetNodeTitleColor() const override;
    virtual FText GetTooltipText() const override;
    virtual bool CanUserDeleteNode() const override;
    virtual bool CanDuplicateNode() const override;
    virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const override;

    FText GetSubtitle() const;
    bool IsInlinePreviewExpanded() const;
    void SetInlinePreviewExpanded(bool bInExpanded);

    virtual FIntPoint GetInlinePreviewSize() const;
    virtual void SetInlinePreviewSize(FIntPoint InPreviewSize);
    virtual int32 GetInlinePreviewResolution() const;
    virtual void SetInlinePreviewResolution(int32 InPreviewResolution);
    virtual UTexture* GetInlinePreviewTexture() const;
    virtual UTexture* GetInlinePreviewDisplayTexture() const;
    virtual UTextureRenderTarget2D* GetOrCreateInlinePreviewRenderTarget(FIntPoint Size);
    UTextureRenderTarget2D* GetOrCreateInlinePreviewDisplayRenderTarget(FIntPoint Size);

    EShineTexturePreviewDisplayMode GetPreviewDisplayMode() const;
    void SetPreviewDisplayMode(EShineTexturePreviewDisplayMode InPreviewDisplayMode);

    void BeginInteractivePreviewChange();
    void EndInteractivePreviewChange();

    static const FName ColorPinCategory;
    static const FName ScalarPinCategory;
    static const FName Vector2PinCategory;
    static const FName BoolPinCategory;
    static const FName IntPinCategory;

    static bool IsNumericPinCategory(const FName& PinCategory);
    static bool CanConnectPinCategories(const FName& OutputPinCategory, const FName& InputPinCategory);

protected:
    void SetNodePresentation(const FText& InNodeTitle, const FText& InNodeSubtitle, const FLinearColor& InAccentColor);
    void CreateNamedPin(EEdGraphPinDirection Direction, const FName& PinCategory, const FName& PinName);
    void NotifyNodeVisualsChanged();

    virtual void BuildNodePins();

    UPROPERTY()
    FText NodeTitle;

    UPROPERTY()
    FText NodeSubtitle;

    UPROPERTY()
    FLinearColor AccentColor = FLinearColor::Gray;

    UPROPERTY(EditAnywhere, Category = "Preview", meta = (ClampMin = "64", ClampMax = "1024", UIMin = "64", UIMax = "1024"))
    FIntPoint InlinePreviewSize = ShineTextureNodeConstants::DefaultInlinePreviewSize();

    UPROPERTY(EditAnywhere, Category = "Preview")
    bool bInlinePreviewExpanded = false;

    UPROPERTY(EditAnywhere, Category = "Preview")
    EShineTexturePreviewDisplayMode PreviewDisplayMode = EShineTexturePreviewDisplayMode::Color;

    UPROPERTY(VisibleAnywhere, Transient, Category = "Preview")
    TObjectPtr<UTextureRenderTarget2D> InlinePreviewTexture;

    UPROPERTY(VisibleAnywhere, Transient, Category = "Preview")
    TObjectPtr<UTextureRenderTarget2D> InlinePreviewDisplayTexture;

    UPROPERTY(Transient)
    bool bInteractivePreviewChange = false;
};
