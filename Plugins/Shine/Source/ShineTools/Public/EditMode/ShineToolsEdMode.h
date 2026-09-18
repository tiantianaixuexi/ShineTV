#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class FShineBaseTool;

class FShineToolsEdMode final : public FEdMode
{
public:
	static const FEditorModeID EM_ShineTools;

	FShineToolsEdMode();
	virtual ~FShineToolsEdMode() override;

	static FShineToolsEdMode* Get();

	/** Typed accessor for a registered tool. */
	template<typename T>
	T* GetTool() const
	{
		for (const auto& Tool : Tools)
		{
			if (T* Casted = static_cast<T*>(Tool.Get()))
			{
				return Casted;
			}
		}
		return nullptr;
	}

	// -- FEdMode overrides --
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual bool UsesToolkits() const override { return true; }

private:
	void OnSelectionChanged(UObject* NewSelection);

	TArray<TUniquePtr<FShineBaseTool>> Tools;
	FDelegateHandle SelectionChangedHandle;
};
