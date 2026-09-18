#include "Tools/DebugDraw/ShineDebugDrawComponent.h"

#include "DebugRenderSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneView.h"
#include "ShowFlags.h"

namespace
{
	/**
	 * Scene proxy that renders all visible groups, gated by the custom
	 * "ShineDebugDraw" show flag. The base FDebugRenderSceneProxy draws the
	 * Lines/ArrowLines/Boxes/Spheres/Texts arrays; we only add show-flag-based
	 * view relevance, which the base class does not provide.
	 */
	class FShineDebugSceneProxy final : public FDebugRenderSceneProxy
	{
	public:
		explicit FShineDebugSceneProxy(const UShineDebugDrawComponent* InComponent)
			: FDebugRenderSceneProxy(InComponent)
		{
			DrawType = EDrawType::WireMesh;
			ViewFlagName = ShineDebugDraw::ShowFlagName;
			ViewFlagIndex = static_cast<uint32>(FEngineShowFlags::FindIndexByName(ShineDebugDraw::ShowFlagName));

			// FDebugRenderSceneProxy draws every primitive in absolute world space,
			// so coordinates are used as-is (world-space, independent of the actor).
			for (const FShineDebugGroup& Group : InComponent->Groups)
			{
				if (!Group.bVisible)
				{
					continue;
				}

				for (const FShineDebugBox& Box : Group.Boxes)
				{
					const FBox LocalBox(-Box.Extent, Box.Extent);
					const FTransform BoxTransform(Box.Rotation, Box.Center);
					Boxes.Emplace(LocalBox, Box.Color.ToFColor(true), BoxTransform, EDrawType::WireMesh, Box.Thickness);
				}

				for (const FShineDebugSphere& Sphere : Group.Spheres)
				{
					Spheres.Emplace(Sphere.Radius, Sphere.Center, Sphere.Color, EDrawType::WireMesh);
				}

				for (const FShineDebugLine& Line : Group.Lines)
				{
					Lines.Emplace(Line.Start, Line.End, Line.Color.ToFColor(true), Line.Thickness);
				}

				for (const FShineDebugArrow& Arrow : Group.Arrows)
				{
					ArrowLines.Emplace(Arrow.Start, Arrow.End, Arrow.Color.ToFColor(true), Arrow.ArrowSize);
				}
				

				for (const FShineDebugText& Text : Group.Texts)
				{
					if (Text.Text.IsEmpty())
					{
						continue;
					}

					Texts.Emplace(Text.Text, Text.Location, Text.Color);
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			const bool bShowFlagEnabled = View && View->Family
				? View->Family->EngineShowFlags.GetSingleFlag(ViewFlagIndex)
				: false;

			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View) && bShowFlagEnabled;
			Result.bDynamicRelevance = true;
			Result.bSeparateTranslucency = Result.bNormalTranslucency = true;
			return Result;
		}
	};
}

UShineDebugDrawComponent::UShineDebugDrawComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bIsEditorOnly = false;
	SetGenerateOverlapEvents(false);
}

FShineDebugGroup& UShineDebugDrawComponent::FindOrAddGroup(FName GroupName)
{
	for (FShineDebugGroup& Group : Groups)
	{
		if (Group.GroupName == GroupName)
		{
			return Group;
		}
	}

	FShineDebugGroup& NewGroup = Groups.AddDefaulted_GetRef();
	NewGroup.GroupName = GroupName;
	return NewGroup;
}

FShineDebugGroup* UShineDebugDrawComponent::FindGroup(FName GroupName)
{
	return Groups.FindByPredicate(
		[GroupName](const FShineDebugGroup& Group) { return Group.GroupName == GroupName; });
}

void UShineDebugDrawComponent::AddBoxToGroup(FName GroupName, const FShineDebugBox& Box)
{
	FindOrAddGroup(GroupName).Boxes.Add(Box);
	MarkRenderStateDirty();
}

void UShineDebugDrawComponent::AddSphereToGroup(FName GroupName, const FShineDebugSphere& Sphere)
{
	FindOrAddGroup(GroupName).Spheres.Add(Sphere);
	MarkRenderStateDirty();
}

void UShineDebugDrawComponent::AddLineToGroup(FName GroupName, const FShineDebugLine& Line)
{
	FindOrAddGroup(GroupName).Lines.Add(Line);
	MarkRenderStateDirty();
}

void UShineDebugDrawComponent::AddArrowToGroup(FName GroupName, const FShineDebugArrow& Arrow)
{
	FindOrAddGroup(GroupName).Arrows.Add(Arrow);
	MarkRenderStateDirty();
}



void UShineDebugDrawComponent::AddTextToGroup(FName GroupName, const FShineDebugText& Text)
{
	FindOrAddGroup(GroupName).Texts.Add(Text);
	MarkRenderStateDirty();
}

void UShineDebugDrawComponent::SetGroup(FName GroupName, const FShineDebugGroup& GroupData)
{
	FShineDebugGroup& Group = FindOrAddGroup(GroupName);
	Group = GroupData;
	Group.GroupName = GroupName;
	MarkRenderStateDirty();
}

bool UShineDebugDrawComponent::ClearGroup(FName GroupName)
{
	FShineDebugGroup* Group = FindGroup(GroupName);
	if (!Group)
	{
		return false;
	}

	Group->Boxes.Reset();
	Group->Spheres.Reset();
	Group->Lines.Reset();
	Group->Arrows.Reset();
	Group->Texts.Reset();
	MarkRenderStateDirty();
	return true;
}

bool UShineDebugDrawComponent::RemoveGroup(FName GroupName)
{
	const int32 Removed = Groups.RemoveAll(
		[GroupName](const FShineDebugGroup& Group) { return Group.GroupName == GroupName; });
	if (Removed == 0)
	{
		return false;
	}

	MarkRenderStateDirty();
	return true;
}

void UShineDebugDrawComponent::ClearAllGroups()
{
	if (Groups.IsEmpty())
	{
		return;
	}

	Groups.Reset();
	MarkRenderStateDirty();
}

bool UShineDebugDrawComponent::SetGroupVisible(FName GroupName, bool _bVisible)
{
	FShineDebugGroup* Group = FindGroup(GroupName);
	if (!Group)
	{
		return false;
	}

	if (Group->bVisible != _bVisible)
	{
		Group->bVisible = _bVisible;
		MarkRenderStateDirty();
	}
	return true;
}

bool UShineDebugDrawComponent::HasGroup(FName GroupName) const
{
	return Groups.ContainsByPredicate(
		[GroupName](const FShineDebugGroup& Group) { return Group.GroupName == GroupName; });
}

void UShineDebugDrawComponent::RefreshDebugDraw()
{
	MarkRenderStateDirty();
}

#if WITH_EDITOR
void UShineDebugDrawComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderStateDirty();
}
#endif

FDebugRenderSceneProxy* UShineDebugDrawComponent::CreateDebugSceneProxy()
{
	return new FShineDebugSceneProxy(this);
}

FBoxSphereBounds UShineDebugDrawComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Use effectively infinite bounds so the proxy always passes frustum culling
	// and the debug primitives are drawn from every camera angle/distance.
	// FDebugRenderSceneProxy draws in absolute world space, so the bounds origin
	// does not affect where shapes appear — only whether they get culled.
	return FBoxSphereBounds(FVector::ZeroVector, FVector(HALF_WORLD_MAX), HALF_WORLD_MAX);
}