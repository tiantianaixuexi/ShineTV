#include "Graph/ShineTextureGraphSchema.h"

#include "ConnectionDrawingPolicy.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "Node/ShineTextureNodeRegistry.h"
#include "Node/Slate/ShineTextureSlateTheme.h"
#include "ToolMenu.h"

namespace
{
    class FShineTextureConnectionDrawingPolicy final : public FConnectionDrawingPolicy
    {
    public:
        FShineTextureConnectionDrawingPolicy(
            int32 InBackLayerID,
            int32 InFrontLayerID,
            float InZoomFactor,
            const FSlateRect& InClippingRect,
            FSlateWindowElementList& InDrawElements)
            : FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
        {
        }

        virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params) override
        {
            FConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);
            Params.WireColor = ShineTextureSlateTheme::ResolvePinColor(OutputPin).CopyWithNewOpacity(0.95f);
            Params.WireThickness = (OutputPin && InputPin) ? 2.35f : 1.9f;
            Params.bDrawBubbles = false;
            Params.AssociatedPin1 = OutputPin;
            Params.AssociatedPin2 = InputPin;
        }
    };

    class FShineTextureSchemaAction_NewNode final : public FEdGraphSchemaAction
    {
    public:
        FShineTextureSchemaAction_NewNode(
            UClass* InNodeClass,
            const FText& InCategory,
            const FText& InMenuDescription,
            const FText& InTooltip)
            : FEdGraphSchemaAction(InCategory, InMenuDescription, InTooltip, 0)
            , NodeClass(InNodeClass)
        {
        }

        virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, FVector2D Location, bool bSelectNewNode) override
        {
            if (!ParentGraph)
            {
                return nullptr;
            }

            UShineTextureGraphNodeBase* NewNode = NewObject<UShineTextureGraphNodeBase>(ParentGraph, NodeClass, NAME_None, RF_Transactional);
            ParentGraph->Modify();
            ParentGraph->AddNode(NewNode, true, bSelectNewNode);
            NewNode->SetFlags(RF_Transactional);
            NewNode->CreateNewGuid();
            NewNode->NodePosX = Location.X;
            NewNode->NodePosY = Location.Y;
            NewNode->PostPlacedNewNode();
            NewNode->AllocateDefaultPins();
            NewNode->AutowireNewNode(FromPin);
            return NewNode;
        }

    private:
        UClass* NodeClass = nullptr;
    };
}

void UShineTextureGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
    for (const FShineTextureNodeRegistration& Registration : ShineTextureNodeRegistry::GetNodeRegistrations())
    {
        ContextMenuBuilder.AddAction(MakeShared<FShineTextureSchemaAction_NewNode>(
            Registration.NodeClass.Get(),
            Registration.Category,
            Registration.MenuLabel,
            Registration.Tooltip));
    }
}

void UShineTextureGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
}

const FPinConnectionResponse UShineTextureGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
    if (!A || !B)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Invalid pin."));
    }

    if (A == B)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Cannot connect a pin to itself."));
    }

    if (A->GetOwningNode() == B->GetOwningNode())
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Cannot connect pins from the same node."));
    }

    if (A->Direction == B->Direction)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Connect an output pin to an input pin."));
    }

    const UEdGraphPin* InputPin = (A->Direction == EGPD_Input) ? A : B;
    const UEdGraphPin* OutputPin = (A->Direction == EGPD_Output) ? A : B;

    if (!InputPin || !OutputPin)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Invalid pin directions."));
    }

    if (!UShineTextureGraphNodeBase::CanConnectPinCategories(OutputPin->PinType.PinCategory, InputPin->PinType.PinCategory))
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Pin categories do not match."));
    }

    if (InputPin->LinkedTo.Num() > 0)
    {
        return FPinConnectionResponse(
            (InputPin == A) ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B,
            TEXT("Replace the existing input connection."));
    }

    return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT("Connect pins."));
}

FConnectionDrawingPolicy* UShineTextureGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
    return new FShineTextureConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements);
}

EGraphType UShineTextureGraphSchema::GetGraphType(const UEdGraph* TestEdGraph) const
{
    return GT_Function;
}

bool UShineTextureGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
    const FPinConnectionResponse Response = CanCreateConnection(A, B);
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        return false;
    }

    UEdGraphPin* InputPin = (A && A->Direction == EGPD_Input) ? A : B;
    UEdGraphPin* OutputPin = (A && A->Direction == EGPD_Output) ? A : B;
    if (!InputPin || !OutputPin)
    {
        return false;
    }

    if (Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_A || Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B)
    {
        InputPin->BreakAllPinLinks();
    }

    OutputPin->MakeLinkTo(InputPin);
    if (UEdGraph* Graph = InputPin->GetOwningNodeUnchecked() ? InputPin->GetOwningNodeUnchecked()->GetGraph() : nullptr)
    {
        Graph->NotifyGraphChanged();
    }

    return true;
}

