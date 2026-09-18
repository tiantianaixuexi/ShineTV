#pragma once

#include "CoreMinimal.h"

class SWidget;

/**
 * Abstract base for all ShineTools feature tools.
 *
 * Each concrete tool owns its own logic (visualizers, parameters, etc.)
 * and provides a Slate panel via CreatePanelWidget().
 *
 * ShineToolsEdMode owns an array of these; ShineToolsToolkit iterates
 * them to build the Modes panel.
 */
class FShineBaseTool
{
public:
	virtual ~FShineBaseTool() = default;

	/** Called when the editor mode enters. */
	virtual void Enter() {}
	/** Called when the editor mode exits. */
	virtual void Exit() {}
	/** Per-frame update. Default empty. */
	virtual void Tick() {}
	/** Selection changed — each tool decides how to respond. Default empty. */
	virtual void OnSelectionChanged() {}

	/**
	 * Whether this tool is currently active.
	 * Only active tools receive Tick() and OnSelectionChanged().
	 * Default false — each tool decides for itself (e.g. checkbox state).
	 */
	virtual bool IsActive() const { return false; }

	/**
	 * Which section this tool's panel belongs to in the Modes panel.
	 * Sections are top-level SExpandableAreas defined in ShineToolsToolkit.
	 * Return NAME_None for ungrouped / root level.
	 */
	virtual FName GetSectionName() const { return NAME_None; }

	/** Build the Slate panel for this tool. */
	virtual TSharedRef<SWidget> CreatePanelWidget() = 0;
};
