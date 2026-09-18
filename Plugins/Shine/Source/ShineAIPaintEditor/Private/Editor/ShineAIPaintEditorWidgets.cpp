#include "Editor/ShineAIPaintEditorWidgets.h"

#include "Paint/ShineAIPaintSession.h"

namespace ShineAIPaintUI
{
    void SShineAIPaintToolButton::Construct(const FArguments& InArgs)
    {
        Session = InArgs._Session;
        Tool = InArgs._Tool;
        bCool = InArgs._bCool;

        SButton::Construct(SButton::FArguments()
            .ContentPadding(FMargin(0.0f))
            .OnClicked(this, &SShineAIPaintToolButton::HandleClicked)
            [
                SNew(STextBlock)
                .Text(InArgs._Label)
                .Font(Style::BodyFont())
                .Justification(ETextJustify::Center)
                .ColorAndOpacity_Lambda([this]() { return GetTextColor(); })
            ]);

        SetCanTick(true);
        CurrentStyle = SelectStyle();
        SetButtonStyle(CurrentStyle);
    }

    void SShineAIPaintToolButton::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
    {
        SButton::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

        const FButtonStyle* Desired = SelectStyle();
        if (Desired != CurrentStyle)
        {
            CurrentStyle = Desired;
            SetButtonStyle(Desired);
        }
    }

    bool SShineAIPaintToolButton::IsActive() const
    {
        return Session.IsValid() && Session->CurrentTool == Tool;
    }

    const FButtonStyle* SShineAIPaintToolButton::SelectStyle() const
    {
        if (!IsActive())
        {
            return &Style::ToolButton();
        }

        return bCool ? &Style::ToolButtonActiveCool() : &Style::ToolButtonActive();
    }

    FSlateColor SShineAIPaintToolButton::GetTextColor() const
    {
        if (!IsActive())
        {
            return FSlateColor(Style::TextSecondary());
        }

        return bCool ? FSlateColor(Style::OnAccentCool()) : FSlateColor(Style::OnAccent());
    }

    FReply SShineAIPaintToolButton::HandleClicked()
    {
        if (Session.IsValid())
        {
            Session->CurrentTool = Tool;
            Session->SetLastStatus(GetToolHint(Tool).ToString());
        }

        return FReply::Handled();
    }

    void SShineAIPaintCanvasHost::Construct(const FArguments& InArgs)
    {
        OnAreaResized = InArgs._OnAreaResized;

        SBorder::Construct(SBorder::FArguments()
            .Padding(0.0f)
            [
                InArgs._Content.Widget
            ]);

        SetCanTick(true);
    }

    void SShineAIPaintCanvasHost::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
    {
        SBorder::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

        const FVector2D NewSize = AllottedGeometry.GetLocalSize();
        if (OnAreaResized && !NewSize.Equals(LastReportedSize, 0.5f))
        {
            LastReportedSize = NewSize;
            OnAreaResized(NewSize);
        }
    }

    // -----------------------------------------------------------------------
    // 可选中列表行
    // -----------------------------------------------------------------------

    void SShineAIPaintSelectableRow::Construct(const FArguments& InArgs)
    {
        IsSelected = InArgs._IsSelected;
        AccentColor = InArgs._AccentColor;
        OnClicked = InArgs._OnClicked;

        const FSimpleDelegate RemoveDelegate = InArgs._OnRemove;
        const FText RemoveTooltip = InArgs._RemoveTooltip;

        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

        // 左侧颜色条：选中时才出现，颜色就是这盏灯的灯色。
        Row->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Fill)
            .Padding(FMargin(2.0f, 3.0f))
            [
                SNew(SBox)
                .WidthOverride(3.0f)
                [
                    SNew(SBorder)
                    .BorderImage(Style::RoundedFillBrush())
                    .BorderBackgroundColor_Lambda([this]()
                    {
                        return FSlateColor(AccentColor.Get(FLinearColor::White));
                    })
                    .Padding(0.0f)
                    .Visibility_Lambda([this]()
                    {
                        return IsSelected.Get(false) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                ]
            ];

        Row->AddSlot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            .Padding(FMargin(4.0f, 0.0f))
            [
                InArgs._Content.Widget
            ];

        if (RemoveDelegate.IsBound())
        {
            Row->AddSlot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(FMargin(2.0f, 0.0f))
                [
                    MakeButton(
                        FText::FromString(TEXT("×")),
                        Style::MiniButton(),
                        FOnClicked::CreateLambda([RemoveDelegate]()
                        {
                            RemoveDelegate.ExecuteIfBound();
                            return FReply::Handled();
                        }),
                        RemoveTooltip,
                        FSlateColor(Style::TextMuted()),
                        Style::BodyFont())
                ];
        }

        SBorder::Construct(SBorder::FArguments()
            .Padding(FMargin(0.0f))
            .BorderImage_Lambda([this]() { return GetRowBrush(); })
            [
                Row
            ]);
    }

    const FSlateBrush* SShineAIPaintSelectableRow::GetRowBrush() const
    {
        if (IsSelected.Get(false))
        {
            return Style::ListRowSelectedBrush();
        }

        return bHovered ? Style::ListRowHoveredBrush() : Style::ListRowBrush();
    }

    void SShineAIPaintSelectableRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
    {
        SBorder::OnMouseEnter(MyGeometry, MouseEvent);

        if (!bHovered)
        {
            bHovered = true;
            Invalidate(EInvalidateWidgetReason::Paint);
        }
    }

    void SShineAIPaintSelectableRow::OnMouseLeave(const FPointerEvent& MouseEvent)
    {
        SBorder::OnMouseLeave(MouseEvent);

        if (bHovered)
        {
            bHovered = false;
            Invalidate(EInvalidateWidgetReason::Paint);
        }
    }

    FReply SShineAIPaintSelectableRow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
    {
        const FReply Reply = SBorder::OnMouseButtonUp(MyGeometry, MouseEvent);
        if (Reply.IsEventHandled())
        {
            return Reply;
        }

        if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
        {
            OnClicked.ExecuteIfBound();
            return FReply::Handled();
        }

        return Reply;
    }
}
