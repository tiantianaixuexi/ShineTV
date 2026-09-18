#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class FShineBaseTool;

class FShineToolsToolkit final : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;

	/**
	 * Build the Modes panel from the given tools.
	 * Each tool's GetSectionName() maps it to a top-level SExpandableArea.
	 * Add new peer sections by extending the section list inside this function.
	 */
	void AddToolPanels(TArray<TUniquePtr<FShineBaseTool>>& Tools);

private:
	TSharedPtr<class SWidget> InlineContentWidget;
};
