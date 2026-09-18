// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "ShineToolsBpFunction.generated.h"

class UDynamicMesh;
class AActor;
class UWorld;

/**
 * 
 */
UCLASS()
class SHINETOOLS_API UShineToolsBpFunction : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	public:
	UFUNCTION(BlueprintCallable,BlueprintPure,Category="ShineTools|BitMask")
	static bool ShineBitMaskAnd(int A, int B);

	/**
	 * Duplicate an editor actor through the engine duplication path so component instances
	 * and editable instance data are preserved.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShineTools|Editor|Actor", meta = (AdvancedDisplay = "ToWorld,LocationOffset"))
	static AActor* DuplicateEditorActor(AActor* SourceActor, UWorld* ToWorld = nullptr, FVector LocationOffset = FVector::ZeroVector)
	{
		if (!SourceActor || !GEditor)
		{
			return nullptr;
		}

		UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (!ActorSubsystem)
		{
			return nullptr;
		}

		UWorld* TargetWorld = ToWorld ? ToWorld : SourceActor->GetWorld();
		if (!TargetWorld)
		{
			return nullptr;
		}

		return ActorSubsystem->DuplicateActor(SourceActor, TargetWorld, LocationOffset);
	}

};

