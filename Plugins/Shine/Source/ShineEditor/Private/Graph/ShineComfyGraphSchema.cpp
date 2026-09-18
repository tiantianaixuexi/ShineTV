#include "Graph/ShineComfyGraphSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/ShineComfyConnectionDrawingPolicy.h"
#include "Graph/ShineComfyGraph.h"
#include "Graph/ShineComfySchemaActions.h"
#include "ToolMenu.h"

void UShineComfyGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
    ShineComfySchemaActions::AppendDynamicNodeActions(ContextMenuBuilder.CurrentGraph, ContextMenuBuilder);
}

void UShineComfyGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
}

FConnectionDrawingPolicy* UShineComfyGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
    return new FShineComfyConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements);
}

const FPinConnectionResponse UShineComfyGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
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

    if (A->PinType.PinCategory != B->PinType.PinCategory)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Pin categories do not match."));
    }

    return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT("Connect pins."));
}

EGraphType UShineComfyGraphSchema::GetGraphType(const UEdGraph* TestEdGraph) const
{
    return GT_Function;
}

bool UShineComfyGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
    const FPinConnectionResponse Response = CanCreateConnection(A, B);

    if (Response.Response != CONNECT_RESPONSE_MAKE)
    {
        return false;
    }

    A->Modify();
    B->Modify();
    A->MakeLinkTo(B);
    return true;
}