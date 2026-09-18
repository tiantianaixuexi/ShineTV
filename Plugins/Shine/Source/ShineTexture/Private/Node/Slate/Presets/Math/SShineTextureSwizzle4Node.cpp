#include "Node/Slate/Presets/SShineTextureSwizzle4Node.h"

#include "Node/Presets/Math/ShineTextureSwizzle4Node.h"
#include "Widgets/SBoxPanel.h"

void SShineTextureSwizzle4Node::Construct(const FArguments& InArgs, UShineTextureSwizzle4Node* InNode)
{
    ConstructBase(InNode);
}

TSharedRef<SWidget> SShineTextureSwizzle4Node::CreateNodeContent() const
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
            CreateBodyText(NSLOCTEXT("SShineTextureSwizzle4Node", "ComponentHelpText", "0=X, 1=Y, 2=Z, 3=W"))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureSwizzle4Node", "OutputXYLabel", "Output XY"),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetSwizzleNode() ? GetSwizzleNode()->GetXComponent() : 0; },
                    [this](int32 NewValue)
                    {
                        if (UShineTextureSwizzle4Node* Node = GetSwizzleNode())
                        {
                            Node->SetXComponent(NewValue);
                        }
                    },
                    ComponentOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSwizzle4Node", "OutputXLabel", "X"))),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetSwizzleNode() ? GetSwizzleNode()->GetYComponent() : 1; },
                    [this](int32 NewValue)
                    {
                        if (UShineTextureSwizzle4Node* Node = GetSwizzleNode())
                        {
                            Node->SetYComponent(NewValue);
                        }
                    },
                    ComponentOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSwizzle4Node", "OutputYLabel", "Y"))),
                8.0f)
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            CreateDualValueSection(
                NSLOCTEXT("SShineTextureSwizzle4Node", "OutputZWLabel", "Output ZW"),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetSwizzleNode() ? GetSwizzleNode()->GetZComponent() : 2; },
                    [this](int32 NewValue)
                    {
                        if (UShineTextureSwizzle4Node* Node = GetSwizzleNode())
                        {
                            Node->SetZComponent(NewValue);
                        }
                    },
                    ComponentOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSwizzle4Node", "OutputZLabel", "Z"))),
                CreateIntegerEntryBox(
                    [this]() -> TOptional<int32> { return GetSwizzleNode() ? GetSwizzleNode()->GetWComponent() : 3; },
                    [this](int32 NewValue)
                    {
                        if (UShineTextureSwizzle4Node* Node = GetSwizzleNode())
                        {
                            Node->SetWComponent(NewValue);
                        }
                    },
                    ComponentOptions,
                    CreateSectionLabel(NSLOCTEXT("SShineTextureSwizzle4Node", "OutputWLabel", "W"))),
                6.0f)
        ],
        196.0f);
}

UShineTextureSwizzle4Node* SShineTextureSwizzle4Node::GetSwizzleNode() const
{
    return GraphNode ? CastChecked<UShineTextureSwizzle4Node>(GraphNode) : nullptr;
}
