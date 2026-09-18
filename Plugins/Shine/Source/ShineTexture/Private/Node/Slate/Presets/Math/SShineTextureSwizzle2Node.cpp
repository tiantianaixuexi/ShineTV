#include "Node/Slate/Presets/SShineTextureSwizzle2Node.h"

#include "Node/Presets/Math/ShineTextureSwizzle2Node.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureSwizzle2Node::Construct(const FArguments& InArgs, UShineTextureSwizzle2Node* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureSwizzle2Node::CreateNodeContent() const
{
    FShineTextureIntegerEntryOptions ComponentOptions;
    ComponentOptions.MinValue = 0;
    ComponentOptions.MaxValue = 3;
    ComponentOptions.MinSliderValue = 0;
    ComponentOptions.MaxSliderValue = 3;

    return WrapNodeContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateBodyText(NSLOCTEXT("SShineTextureSwizzle2Node", "ComponentHelpText", "0=X, 1=Y, 2=Z, 3=W"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureSwizzle2Node", "OutputLabel", "Output"),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetSwizzleNode() ? GetSwizzleNode()->GetFirstComponent() : 0; },
                    [this](int32 NewValue)
                    {
                        if (UShineTextureSwizzle2Node* Node = GetSwizzleNode())
                        {
                            Node->SetFirstComponent(NewValue);
                        }
                    },
                    ComponentOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSwizzle2Node", "OutputXLabel", "X"))),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetSwizzleNode() ? GetSwizzleNode()->GetSecondComponent() : 1; },
                    [this](int32 NewValue)
                    {
                        if (UShineTextureSwizzle2Node* Node = GetSwizzleNode())
                        {
                            Node->SetSecondComponent(NewValue);
                        }
                    },
                    ComponentOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSwizzle2Node", "OutputYLabel", "Y"))),
                8.0f)
        ],
        196.0f);
}

UShineTextureSwizzle2Node* SShineTextureSwizzle2Node::GetSwizzleNode() const
{
    return GraphNode ? CastChecked<UShineTextureSwizzle2Node>(GraphNode) : nullptr;
}
