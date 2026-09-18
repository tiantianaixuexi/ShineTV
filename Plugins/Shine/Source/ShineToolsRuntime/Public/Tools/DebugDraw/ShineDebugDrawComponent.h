#pragma once

#include "CoreMinimal.h"
#include "Debug/DebugDrawComponent.h"

#include "ShineDebugDrawComponent.generated.h"

/** Name of the custom engine show flag that gates this component's drawing. */
namespace ShineDebugDraw
{
	inline const TCHAR* ShowFlagName = TEXT("ShineDebugDraw");
}

USTRUCT(BlueprintType)
struct FShineDebugBox
{
	GENERATED_BODY()

	/** Center, in component-local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector Extent = FVector(50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FLinearColor Color = FLinearColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	float Thickness = 1.0f;
};

USTRUCT(BlueprintType)
struct FShineDebugSphere
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	float Radius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FLinearColor Color = FLinearColor(1.0f, 0.5f, 0.0f, 1.0f);
};

USTRUCT(BlueprintType)
struct FShineDebugLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector End = FVector(100.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FLinearColor Color = FLinearColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	float Thickness = 1.0f;
};

USTRUCT(BlueprintType)
struct FShineDebugArrow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector End = FVector(100.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	float ArrowSize = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FLinearColor Color = FLinearColor::Gray;
};

USTRUCT(BlueprintType)
struct FShineDebugPoint
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shine Debug Draw")
	float Radius = 30.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shine Debug Draw")
	FVector Location = FVector::ZeroVector;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FLinearColor Color = FLinearColor::Gray;
};



USTRUCT(BlueprintType)
struct FShineDebugText
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FVector Location = FVector(0.0f, 0.0f, 50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FLinearColor Color = FLinearColor::White;
};

/**
 * A named, independently toggleable bucket of debug primitives.
 *
 * Mirrors the "group" concept from the old static debug-draw library: one component
 * can hold many groups, each addressed by GroupName, drawn or hidden via bVisible,
 * and cleared/replaced independently for easy reuse.
 */
USTRUCT(BlueprintType)
struct FShineDebugGroup
{
	GENERATED_BODY()

	/** Identifier used by the per-group Blueprint API (AddBoxToGroup, ClearGroup, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	FName GroupName = NAME_None;

	/** When false the whole group is skipped during drawing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	bool bVisible = true;

	UPROPERTY(BlueprintReadWrite, Category = "Shine Debug Draw")
	TArray<FShineDebugBox> Boxes;

	UPROPERTY(BlueprintReadWrite, Category = "Shine Debug Draw")
	TArray<FShineDebugSphere> Spheres;

	UPROPERTY(BlueprintReadWrite, Category = "Shine Debug Draw")
	TArray<FShineDebugLine> Lines;

	UPROPERTY(BlueprintReadWrite, Category = "Shine Debug Draw")
	TArray<FShineDebugArrow> Arrows;
	
	UPROPERTY(BlueprintReadWrite, Category = "Shine Debug Draw")
	TArray<FShineDebugPoint> Points;
	
	UPROPERTY(BlueprintReadWrite, Category = "Shine Debug Draw")
	TArray<FShineDebugText> Texts;
};

/**
 * Per-actor debug visualization component.
 *
 * Attach to any Actor and add one or more named groups; each group draws wire
 * boxes/spheres/lines/arrows and 3D text in the editor (and game) viewport.
 * Groups can be toggled and replaced independently for reuse. Visibility is gated
 * by the custom "ShineDebugDraw" show flag, available in the viewport Show menu.
 *
 * All coordinates are component-local and follow the owning Actor's transform.
 */
UCLASS(ClassGroup = (Shine), meta = (BlueprintSpawnableComponent), HideCategories = (Physics, Collision))
class SHINETOOLSRUNTIME_API UShineDebugDrawComponent : public UDebugDrawComponent
{
	GENERATED_BODY()

public:
	UShineDebugDrawComponent();

	/** All debug groups owned by this component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shine Debug Draw")
	TArray<FShineDebugGroup> Groups;

	// -- Per-group authoring API (creates the group on first use) --

	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	void AddBoxToGroup(FName GroupName, const FShineDebugBox& Box);

	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	void AddSphereToGroup(FName GroupName, const FShineDebugSphere& Sphere);

	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	void AddLineToGroup(FName GroupName, const FShineDebugLine& Line);

	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	void AddArrowToGroup(FName GroupName, const FShineDebugArrow& Arrow);
	
	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	void AddTextToGroup(FName GroupName, const FShineDebugText& Text);

	/** Replace an entire group's contents in one call (creates it if missing). */
	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	void SetGroup(FName GroupName, const FShineDebugGroup& GroupData);

	/** Empty a group's primitives but keep the group entry. */
	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	bool ClearGroup(FName GroupName);

	/** Remove a group entirely. */
	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	bool RemoveGroup(FName GroupName);

	/** Remove every group. */
	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	void ClearAllGroups();

	/** Toggle a single group's visibility. */
	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw|Groups")
	bool SetGroupVisible(FName GroupName, bool _bVisible);

	UFUNCTION(BlueprintPure, Category = "Shine Debug Draw|Groups")
	bool HasGroup(FName GroupName) const;

	/** Rebuild the scene proxy after changing groups at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Shine Debug Draw")
	void RefreshDebugDraw();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual FDebugRenderSceneProxy* CreateDebugSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	/** Find an existing group by name, or create and return a new one. */
	FShineDebugGroup& FindOrAddGroup(FName GroupName);
	FShineDebugGroup* FindGroup(FName GroupName);
};