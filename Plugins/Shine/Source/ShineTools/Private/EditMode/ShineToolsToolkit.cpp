#include "EditMode/ShineToolsToolkit.h"

#include "Tools/ShineBaseTool.h"
#include "EditMode/ShineToolsEdMode.h"
#include "EditorModeManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShineToolsToolkit"

namespace
{
	struct FShineSectionSlot
	{
		FName  Name;
		FText  Title;
		bool   bInitiallyCollapsed;
		TSharedPtr<SVerticalBox> ContentBox;
	};
}

void FShineToolsToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FModeToolkit::Init(InitToolkitHost);
}

void FShineToolsToolkit::AddToolPanels(TArray<TUniquePtr<FShineBaseTool>>& Tools)
{
	// ----------------------------------------------------------------
	// 1. Define sections in desired order — add new peer sections here
	// ----------------------------------------------------------------
	TArray<FShineSectionSlot> Sections;
	Sections.Add({ FName(TEXT("ComfyUI")), LOCTEXT("ComfyUI", "ComfyUI功能"), true,  SNew(SVerticalBox) });
	// Future peer sections:
	// Sections.Add({ FName(TEXT("Modeling")), LOCTEXT("Modeling",  "模型工具"),  false, SNew(SVerticalBox) });

	// ----------------------------------------------------------------
	// 2. Distribute tool panels into their declared sections
	// ----------------------------------------------------------------
	for (const auto& Tool : Tools)
	{
		const FName TargetSection = Tool->GetSectionName();
		for (FShineSectionSlot& Slot : Sections)
		{
			if (Slot.Name == TargetSection)
			{
				Slot.ContentBox->AddSlot()
					.AutoHeight()
					.Padding(2.0f)
					[
						Tool->CreatePanelWidget()
					];
				break;
			}
		}
	}

	// ----------------------------------------------------------------
	// 3. Build layout
	// ----------------------------------------------------------------
	TSharedRef<SVerticalBox> Layout = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("实用工具")))
			.AutoWrapText(true)
		];

	for (const FShineSectionSlot& Slot : Sections)
	{
		Layout->AddSlot()
			.AutoHeight()
			.Padding(4.0f, 2.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(Slot.bInitiallyCollapsed)
				.AreaTitle(Slot.Title)
				.BodyContent()
				[
					SNew(SBorder)
					.Padding(6.0f)
					[
						Slot.ContentBox.ToSharedRef()
					]
				]
			];
	}

	Layout->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 4.0f)
		[
			SNew(SSeparator)
		];

	InlineContentWidget = Layout;
}

FName FShineToolsToolkit::GetToolkitFName() const
{
	return FName(TEXT("ShineToolsActorBoundsModeToolkit"));
}

FText FShineToolsToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "ShineTools");
}

FEdMode* FShineToolsToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FShineToolsEdMode::EM_ShineTools);
}

TSharedPtr<SWidget> FShineToolsToolkit::GetInlineContent() const
{
	return InlineContentWidget;
}

#undef LOCTEXT_NAMESPACE
