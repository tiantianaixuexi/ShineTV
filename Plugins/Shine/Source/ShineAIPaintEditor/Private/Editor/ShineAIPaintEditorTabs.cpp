#include "Editor/ShineAIPaintEditorTabs.h"

#include "Asset/ShineAIPaintAsset.h"
#include "Editor/ShineAIPaintEditorStyle.h"
#include "Editor/ShineAIPaintEditorWidgets.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "Paint/ShineAIPaintService.h"
#include "Paint/ShineAIPaintSession.h"
#include "Paint/ShineAIPaintTextureUtils.h"
#include "Paint/SShineAIPaintCanvas.h"
#include "Paint/SShineAIPaintPreviewViewport.h"

using namespace ShineAIPaintUI;

// ===========================================================================
// 视口页签
// ===========================================================================

void SShineAIPaintViewportTab::Construct(const FArguments& InArgs)
{
    Session = InArgs._Session;

    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(Style::WellBrush())
            .Padding(0.0f)
            .Clipping(EWidgetClipping::ClipToBounds)
            [
                SNew(SShineAIPaintPreviewViewport)
                .Session(Session)
            ]
        ]
        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(FMargin(8.0f))
        [
            BuildToolStrip()
        ]
    ];
}

TSharedRef<SWidget> SShineAIPaintViewportTab::BuildToolStrip()
{
    auto MakeTool = [this](EShineAIPaintTool Tool, const FText& Label, bool bCool) -> TSharedRef<SWidget>
    {
        return SNew(SShineAIPaintToolButton)
            .Session(Session)
            .Tool(Tool)
            .bCool(bCool)
            .Label(Label);
    };

    return SNew(SBorder)
        .BorderImage(Style::CardBrush())
        .Padding(FMargin(6.0f, 4.0f))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 3.0f, 0.0f)[ MakeTool(EShineAIPaintTool::Paint, NSLOCTEXT("ShineAIPaint", "ToolPaint", "画底色"), false) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 0.0f)[ MakeTool(EShineAIPaintTool::Erase, NSLOCTEXT("ShineAIPaint", "ToolErase", "擦除"), false) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 0.0f)[ MakeTool(EShineAIPaintTool::MaskPaint, NSLOCTEXT("ShineAIPaint", "ToolMask", "画遮罩"), true) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3.0f, 0.0f)[ MakeTool(EShineAIPaintTool::MaskErase, NSLOCTEXT("ShineAIPaint", "ToolMaskErase", "擦遮罩"), true) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ MakeToolbarSeparator() ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
                SNew(SCheckBox)
                .IsChecked_Lambda([this]()
                {
                    return (Session.IsValid() && Session->bPaintOnModel) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                {
                    if (Session.IsValid())
                    {
                        Session->bPaintOnModel = (State == ECheckBoxState::Checked);
                    }
                })
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("ShineAIPaint", "PaintOnModel", "在模型上涂画"))
                    .TextStyle(&Style::BodyTextStyle())
                ]
            ]
        ];
}

// ===========================================================================
// 画布页签
// ===========================================================================

void SShineAIPaintCanvasTab::Construct(const FArguments& InArgs)
{
    Session = InArgs._Session;

    TSharedRef<SWidget> Canvas = SNew(SShineAIPaintCanvasHost)
        .OnAreaResized(TFunction<void(const FVector2D&)>(
            [this](const FVector2D& NewSize)
            {
                HandleCanvasAreaResized(NewSize);
            }))
        [
            SNew(SBox)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .Padding(FMargin(4.0f))
            [
                SAssignNew(CanvasSizeBox, SBox)
                .WidthOverride_Lambda([this]()
                {
                    return Session.IsValid() && Session->HasPaintTarget() ? Session->GetWidth() * Session->ViewZoom : 200.0f;
                })
                .HeightOverride_Lambda([this]()
                {
                    return Session.IsValid() && Session->HasPaintTarget() ? Session->GetHeight() * Session->ViewZoom : 200.0f;
                })
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SNew(SShineAIPaintCanvas)
                        .Session(Session)
                    ]
                    + SOverlay::Slot()
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("ShineAIPaint", "NoTargetInCanvas", "还没有贴图 —— 在「属性」里新建或指认一张"))
                        .TextStyle(&Style::MutedTextStyle())
                        .Visibility_Lambda([this]()
                        {
                            return Session.IsValid() && Session->HasPaintTarget() ? EVisibility::Collapsed : EVisibility::Visible;
                        })
                    ]
                ]
            ]
        ];

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(Style::PanelBrush())
        .Padding(0.0f)
        .Clipping(EWidgetClipping::ClipToBounds)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[ BuildHeader() ]
            + SVerticalBox::Slot().FillHeight(1.0f)[ Canvas ]
        ]
    ];

    RefreshCanvasLayout();
}

TSharedRef<SWidget> SShineAIPaintCanvasTab::BuildHeader()
{
    return SNew(SBorder)
        .BorderImage(Style::GroupHeaderBrush())
        .Padding(FMargin(8.0f, 4.0f))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
                SNew(STextBlock).Text(NSLOCTEXT("ShineAIPaint", "CanvasTitle", "贴图画布")).TextStyle(&Style::SectionTextStyle())
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 4.0f, 0.0f)[
                SNew(STextBlock).Text(NSLOCTEXT("ShineAIPaint", "Zoom", "缩放")).TextStyle(&Style::BodyTextStyle()).ColorAndOpacity(FSlateColor(Style::TextMuted()))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)[
                SNew(SBox).WidthOverride(120.0f)[
                    MakeSlider(
                        0.05f, 1.5f,
                        TAttribute<float>::CreateLambda([this]() { return Session.IsValid() ? Session->ViewZoom : 0.5f; }),
                        FOnFloatValueChanged::CreateLambda([this](float Value)
                        {
                            if (Session.IsValid())
                            {
                                Session->ViewZoom = Value;
                                RefreshCanvasLayout();
                            }
                        }))
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
                MakeValueChip(TAttribute<FText>::CreateLambda([this]()
                {
                    return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt((Session.IsValid() ? Session->ViewZoom : 0.0f) * 100.0f)));
                }))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)[
                MakeButton(
                    NSLOCTEXT("ShineAIPaint", "FitCanvas", "适应"),
                    Style::SubtleButton(),
                    FOnClicked::CreateLambda([this]()
                    {
                        FitCanvasToArea();
                        return FReply::Handled();
                    }),
                    NSLOCTEXT("ShineAIPaint", "FitCanvasTip", "把缩放调成整张贴图都看得见。"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)[ SNew(SSpacer) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)[
                MakeButton(NSLOCTEXT("ShineAIPaint", "FillBase", "填充底色"), Style::SubtleButton(),
                    FOnClicked::CreateSP(this, &SShineAIPaintCanvasTab::HandleFillBase),
                    NSLOCTEXT("ShineAIPaint", "FillBaseTip", "用当前笔刷颜色把整张底色填满。"))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)[
                MakeButton(NSLOCTEXT("ShineAIPaint", "ClearMask", "清空遮罩"), Style::SubtleButton(),
                    FOnClicked::CreateSP(this, &SShineAIPaintCanvasTab::HandleClearMask))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)[
                MakeButton(NSLOCTEXT("ShineAIPaint", "ResetCanvas", "重置画布"), Style::SubtleButton(),
                    FOnClicked::CreateSP(this, &SShineAIPaintCanvasTab::HandleResetCanvas),
                    NSLOCTEXT("ShineAIPaint", "ResetCanvasTip", "把底色恢复到初始纯色，并清空遮罩。"))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)[
                MakeButton(NSLOCTEXT("ShineAIPaint", "ExportPng", "导出 PNG"), Style::SubtleButton(),
                    FOnClicked::CreateSP(this, &SShineAIPaintCanvasTab::HandleExportPng),
                    NSLOCTEXT("ShineAIPaint", "ExportPngTip", "把当前画布与遮罩导出成 PNG 到 Saved/ShineAIPaint。"))
            ]
        ];
}

void SShineAIPaintCanvasTab::RefreshCanvasLayout()
{
    if (CanvasSizeBox.IsValid())
    {
        CanvasSizeBox->Invalidate(EInvalidateWidgetReason::Layout);
    }
}

void SShineAIPaintCanvasTab::HandleCanvasAreaResized(const FVector2D& NewSize)
{
    CanvasAreaSize = NewSize;

    if (!Session.IsValid() || !Session->HasPaintTarget())
    {
        return;
    }

    // 第一次拿到区域尺寸、或换了目标贴图时自动适应一次；之后用户自己的缩放不会被覆盖。
    const UTexture2D* Texture = Session->GetTargetTexture();
    if (AutoFittedTexture != Texture)
    {
        AutoFittedTexture = Texture;
        FitCanvasToArea();
        return;
    }

    // 区域变小（面板被拖矮）时不主动改用户的缩放，但必须保证画布放得下，
    // 否则会被裁掉一截，看起来就像图片被压扁了。
    ClampCanvasZoomToArea();
}

float SShineAIPaintCanvasTab::ComputeFitZoom() const
{
    if (!Session.IsValid() || !Session->HasPaintTarget())
    {
        return 0.5f;
    }

    const float TextureWidth = static_cast<float>(FMath::Max(Session->GetWidth(), 1));
    const float TextureHeight = static_cast<float>(FMath::Max(Session->GetHeight(), 1));

    const float AvailableWidth = FMath::Max(CanvasAreaSize.X - 24.0f, 64.0f);
    const float AvailableHeight = FMath::Max(CanvasAreaSize.Y - 24.0f, 64.0f);

    return FMath::Clamp(
        FMath::Min(AvailableWidth / TextureWidth, AvailableHeight / TextureHeight),
        0.05f,
        1.5f);
}

void SShineAIPaintCanvasTab::ClampCanvasZoomToArea()
{
    if (!Session.IsValid() || !Session->HasPaintTarget())
    {
        return;
    }

    const float FitZoom = ComputeFitZoom();
    if (Session->ViewZoom > FitZoom)
    {
        Session->ViewZoom = FitZoom;
        RefreshCanvasLayout();
    }
}

void SShineAIPaintCanvasTab::FitCanvasToArea()
{
    if (!Session.IsValid() || !Session->HasPaintTarget())
    {
        return;
    }

    Session->ViewZoom = ComputeFitZoom();
    RefreshCanvasLayout();
}

FReply SShineAIPaintCanvasTab::HandleFillBase()
{
    if (Session.IsValid())
    {
        Session->FillAll(Session->BrushColor);
        Session->MaybeRequestLiveAIUpdate();
    }
    return FReply::Handled();
}

FReply SShineAIPaintCanvasTab::HandleClearMask()
{
    if (Session.IsValid())
    {
        Session->ClearMask();
        Session->MaybeRequestLiveAIUpdate();
    }
    return FReply::Handled();
}

FReply SShineAIPaintCanvasTab::HandleResetCanvas()
{
    if (Session.IsValid())
    {
        Session->ResetBasePixels();
        Session->ClearMask();
        Session->SetLastStatus(TEXT("画布已重置。"));
        Session->MaybeRequestLiveAIUpdate();
    }
    return FReply::Handled();
}

FReply SShineAIPaintCanvasTab::HandleExportPng()
{
    if (!Session.IsValid() || !Session->HasPaintTarget())
    {
        return FReply::Handled();
    }

    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShineAIPaint")));
    const FString TimeStamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const UTexture2D* TargetTexture = Session->GetTargetTexture();
    const FString BaseName = TargetTexture ? TargetTexture->GetName() : TEXT("ShinePaint");

    TArray<uint8> BasePng;
    TArray<uint8> MaskPng;
    const bool bBaseOk = ShineAIPaintImage::EncodePng(Session->GetBasePixels(), Session->GetWidth(), Session->GetHeight(), BasePng);
    const bool bMaskOk = ShineAIPaintImage::EncodeMaskPng(
        Session->GetMaskPixels(),
        Session->GetWidth(),
        Session->GetHeight(),
        Session->Settings.MaskMode == EShineAIPaintMaskMode::Protect,
        MaskPng);

    FString Message;
    if (bBaseOk)
    {
        const FString BasePath = FPaths::Combine(Directory, FString::Printf(TEXT("%s_%s.png"), *BaseName, *TimeStamp));
        if (ShineAIPaintImage::SavePngToFile(BasePng, BasePath))
        {
            Message = FString::Printf(TEXT("已导出贴图：%s"), *BasePath);
        }
    }

    if (bMaskOk && !Session->IsMaskEmpty())
    {
        const FString MaskPath = FPaths::Combine(Directory, FString::Printf(TEXT("%s_mask_%s.png"), *BaseName, *TimeStamp));
        if (ShineAIPaintImage::SavePngToFile(MaskPng, MaskPath))
        {
            Message += FString::Printf(TEXT("\n已导出遮罩（白=可重绘）：%s"), *MaskPath);
        }
    }

    Session->SetLastStatus(Message.IsEmpty() ? TEXT("导出失败。") : Message);
    return FReply::Handled();
}

// ===========================================================================
// 放置（灯光）页签
// ===========================================================================

void SShineAIPaintPlaceActorsTab::Construct(const FArguments& InArgs)
{
    Session = InArgs._Session;

    if (Session.IsValid())
    {
        Session->EnsurePreviewLightsInitialized();
        SelectedLight = Session->PreviewLights.Num() > 0 ? Session->PreviewLights[0] : nullptr;
    }

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(Style::PanelBrush())
        .Padding(0.0f)
        [
            SNew(SVerticalBox)

            // ---- 预览场景灯光 ----
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSectionHeader(
                    NSLOCTEXT("ShineAIPaint", "PlaceSectionLights", "预览场景灯光"),
                    MakeButton(
                        NSLOCTEXT("ShineAIPaint", "RemoveLight", "删除"),
                        Style::MiniButton(),
                        FOnClicked::CreateSP(this, &SShineAIPaintPlaceActorsTab::HandleRemoveSelectedLight),
                        NSLOCTEXT("ShineAIPaint", "RemoveLightTip", "把选中的这盏灯从预览场景里删掉。")))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(FMargin(8.0f, 7.0f, 8.0f, 6.0f))
            [
                BuildToolbar()
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(FMargin(8.0f, 0.0f, 8.0f, 8.0f))
            [
                SNew(SBox)
                .HeightOverride(128.0f)
                [
                    SNew(SBorder)
                    .BorderImage(Style::WellBrush())
                    .Padding(FMargin(3.0f))
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SAssignNew(LightListBox, SVerticalBox)
                        ]
                    ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight()[ MakeRule() ]

            // ---- 灯光属性 ----
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSectionHeader(NSLOCTEXT("ShineAIPaint", "PlaceSectionProps", "灯光属性"))
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                .Padding(FMargin(8.0f, 7.0f, 8.0f, 10.0f))
                [
                    SAssignNew(LightPropertiesBox, SVerticalBox)
                ]
            ]
        ]
    ];

    RefreshLightList();
}

TSharedRef<SWidget> SShineAIPaintPlaceActorsTab::BuildToolbar()
{
    // 加灯的三个按钮等宽排一行，像分段控件。
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

    auto AddLightButton = [this, Row](const FText& Label, const FText& Tooltip, EShineAIPaintLightType Type)
    {
        Row->AddSlot()
            .FillWidth(1.0f)
            .Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
            [
                MakeButton(
                    Label,
                    Style::SubtleButton(),
                    FOnClicked::CreateLambda([this, Type]() { return HandleAddLight(Type); }),
                    Tooltip)
            ];
    };

    AddLightButton(
        NSLOCTEXT("ShineAIPaint", "AddLightDir", "＋ 方向光"),
        NSLOCTEXT("ShineAIPaint", "AddLightDirTip", "加一盏方向光（太阳）。"),
        EShineAIPaintLightType::Directional);

    AddLightButton(
        NSLOCTEXT("ShineAIPaint", "AddLightPoint", "＋ 点光"),
        NSLOCTEXT("ShineAIPaint", "AddLightPointTip", "加一盏点光（灯泡）。"),
        EShineAIPaintLightType::Point);

    Row->AddSlot()
        .FillWidth(1.0f)
        [
            MakeButton(
                NSLOCTEXT("ShineAIPaint", "AddLightSpot", "＋ 聚光"),
                Style::SubtleButton(),
                FOnClicked::CreateLambda([this]() { return HandleAddLight(EShineAIPaintLightType::Spot); }),
                NSLOCTEXT("ShineAIPaint", "AddLightSpotTip", "加一盏聚光灯。"))
        ];

    return Row;
}

TSharedRef<SWidget> SShineAIPaintPlaceActorsTab::BuildLightProperties()
{
    // 方向光用的是 lux 量级的强度，和点/聚光的坎德拉差好几个数量级，滑条上限分开给。
    auto GetIntensityMax = [this]() -> float
    {
        return (SelectedLight.IsValid() && SelectedLight->Type == EShineAIPaintLightType::Directional) ? 20.0f : 50000.0f;
    };

    return SNew(SVerticalBox)

        // 名称
        + SVerticalBox::Slot().AutoHeight()[
            MakeRow(
                NSLOCTEXT("ShineAIPaint", "LightName", "名称"),
                MakeFieldFrame(
                    SNew(SEditableTextBox)
                    .Style(&Style::NumericTextBox())
                    .Text_Lambda([this]()
                    {
                        return FText::FromString(SelectedLight.IsValid() ? SelectedLight->Name : FString());
                    })
                    .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
                    {
                        if (SelectedLight.IsValid())
                        {
                            SelectedLight->Name = Text.ToString();
                            RefreshLightList();
                        }
                    }),
                    GAxisFieldHeight))
        ]

        // 类型 + 启用
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, GRowSpacing, 0.0f, 0.0f)[
            MakeRow(
                NSLOCTEXT("ShineAIPaint", "LightType", "类型"),
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
                    SNew(SBorder)
                    .BorderImage(Style::AccentBadgeBrush())
                    .Padding(FMargin(7.0f, 2.0f))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return FText::FromString(SelectedLight.IsValid() ? SelectedLight->GetTypeLabel() : TEXT("—"));
                        })
                        .TextStyle(&Style::BadgeTextStyle())
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f)[ SNew(SSpacer) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]()
                    {
                        return (SelectedLight.IsValid() && SelectedLight->bEnabled) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    {
                        if (SelectedLight.IsValid())
                        {
                            SelectedLight->bEnabled = (State == ECheckBoxState::Checked);
                        }
                    })
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("ShineAIPaint", "LightEnabled", "启用"))
                        .TextStyle(&Style::BodyTextStyle())
                    ]
                ])
        ]

        // 强度
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, GRowSpacing, 0.0f, 0.0f)[
            MakeSliderRow(
                NSLOCTEXT("ShineAIPaint", "LightIntensity", "强度"),
                MakeSlider(
                    0.0f, 1.0f,
                    TAttribute<float>::CreateLambda([this, GetIntensityMax]()
                    {
                        if (!SelectedLight.IsValid()) { return 0.0f; }
                        const float Max = GetIntensityMax();
                        return Max > 0.0f ? FMath::Clamp(SelectedLight->Intensity / Max, 0.0f, 1.0f) : 0.0f;
                    }),
                    FOnFloatValueChanged::CreateLambda([this, GetIntensityMax](float Normalized)
                    {
                        if (SelectedLight.IsValid())
                        {
                            SelectedLight->Intensity = Normalized * GetIntensityMax();
                        }
                    })),
                TAttribute<FText>::CreateLambda([this]()
                {
                    const float Intensity = SelectedLight.IsValid() ? SelectedLight->Intensity : 0.0f;
                    return FText::FromString(Intensity >= 1000.0f
                        ? FString::Printf(TEXT("%.0f"), Intensity)
                        : FString::Printf(TEXT("%.1f"), Intensity));
                }))
        ]

        // 颜色：色板 + R/G/B（和位置那行用同一套数值框）
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, GRowSpacing, 0.0f, 0.0f)[
            MakeRow(
                NSLOCTEXT("ShineAIPaint", "LightColor", "颜色"),
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[
                    MakeColorPreviewBox(TAttribute<FLinearColor>::CreateLambda([this]()
                    {
                        return SelectedLight.IsValid() ? SelectedLight->Color : FLinearColor::Black;
                    }))
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[
                    MakeVectorBox(
                        [this]() -> FVector3f
                        {
                            if (!SelectedLight.IsValid()) { return FVector3f::ZeroVector; }
                            return FVector3f(SelectedLight->Color.R, SelectedLight->Color.G, SelectedLight->Color.B);
                        },
                        [this](const FVector3f& Value)
                        {
                            if (!SelectedLight.IsValid())
                            {
                                return;
                            }

                            SelectedLight->Color = FLinearColor(
                                FMath::Clamp(Value.X, 0.0f, 1.0f),
                                FMath::Clamp(Value.Y, 0.0f, 1.0f),
                                FMath::Clamp(Value.Z, 0.0f, 1.0f));
                        },
                        0.01f,
                        NSLOCTEXT("ShineAIPaint", "LightR", "红（R）"),
                        NSLOCTEXT("ShineAIPaint", "LightG", "绿（G）"),
                        NSLOCTEXT("ShineAIPaint", "LightB", "蓝（B）"))
                ])
        ]

        // ---- 变换 ----
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 5.0f)[
            SNew(STextBlock)
            .Text(NSLOCTEXT("ShineAIPaint", "LightTransform", "变换"))
            .TextStyle(&Style::SectionTextStyle())
        ]
        + SVerticalBox::Slot().AutoHeight()[
            MakeVectorRow(
                NSLOCTEXT("ShineAIPaint", "LightLocation", "位置"),
                [this]() -> FVector3f
                {
                    return SelectedLight.IsValid() ? FVector3f(SelectedLight->Location) : FVector3f::ZeroVector;
                },
                [this](const FVector3f& Value)
                {
                    if (SelectedLight.IsValid())
                    {
                        SelectedLight->Location = FVector(Value);
                    }
                },
                1.0f,
                NSLOCTEXT("ShineAIPaint", "LightXTip", "X：左右。"),
                NSLOCTEXT("ShineAIPaint", "LightYTip", "Y：前后。"),
                NSLOCTEXT("ShineAIPaint", "LightZTip", "Z：上下。"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[
            MakeVectorRow(
                NSLOCTEXT("ShineAIPaint", "LightRotation", "旋转"),
                [this]() -> FVector3f
                {
                    return SelectedLight.IsValid()
                        ? FVector3f(SelectedLight->Rotation.Roll, SelectedLight->Rotation.Pitch, SelectedLight->Rotation.Yaw)
                        : FVector3f::ZeroVector;
                },
                [this](const FVector3f& Value)
                {
                    if (SelectedLight.IsValid())
                    {
                        // FRotator 的构造顺序是（Pitch, Yaw, Roll）。
                        SelectedLight->Rotation = FRotator(Value.Y, Value.Z, Value.X);
                    }
                },
                1.0f,
                NSLOCTEXT("ShineAIPaint", "LightRollTip", "滚转 Roll：绕灯光自身轴，对灯光外观影响很小。"),
                NSLOCTEXT("ShineAIPaint", "LightPitchTip", "俯仰 Pitch：上下抬头 / 低头。"),
                NSLOCTEXT("ShineAIPaint", "LightYawTip", "水平 Yaw：左右转头。"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)[
            SNew(STextBlock)
            .Text(NSLOCTEXT("ShineAIPaint", "LightTransformHint", "方向光只用旋转：方向决定它照到哪，位置不影响画面。"))
            .AutoWrapText(true)
            .TextStyle(&Style::MutedTextStyle())
            .Visibility_Lambda([this]()
            {
                const bool bDirectional = SelectedLight.IsValid() && SelectedLight->Type == EShineAIPaintLightType::Directional;
                return bDirectional ? EVisibility::Visible : EVisibility::Collapsed;
            })
        ]

        // ---- 环境光 ----
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 5.0f)[
            SNew(STextBlock)
            .Text(NSLOCTEXT("ShineAIPaint", "LightSky", "环境光"))
            .TextStyle(&Style::SectionTextStyle())
        ]
        + SVerticalBox::Slot().AutoHeight()[
            MakeSliderRow(
                NSLOCTEXT("ShineAIPaint", "SkyIntensity", "天光强度"),
                MakeSlider(
                    0.0f, 4.0f,
                    TAttribute<float>::CreateLambda([this]() { return Session.IsValid() ? Session->PreviewSkyIntensity : 1.6f; }),
                    FOnFloatValueChanged::CreateLambda([this](float Value)
                    {
                        if (Session.IsValid())
                        {
                            Session->PreviewSkyIntensity = Value;
                        }
                    })),
                TAttribute<FText>::CreateLambda([this]()
                {
                    return FText::FromString(FString::Printf(TEXT("%.1f"), Session.IsValid() ? Session->PreviewSkyIntensity : 0.0f));
                }))
        ];
}

void SShineAIPaintPlaceActorsTab::RefreshLightList()
{
    if (LightListBox.IsValid())
    {
        LightListBox->ClearChildren();

        if (Session.IsValid())
        {
            Session->EnsurePreviewLightsInitialized();
        }

        const int32 LightCount = Session.IsValid() ? Session->PreviewLights.Num() : 0;

        if (LightCount == 0)
        {
            LightListBox->AddSlot()
                .AutoHeight()
                [
                    MakeEmptyState(NSLOCTEXT("ShineAIPaint", "NoLights", "预览场景里还没有灯。\n用上面的按钮加一盏。"))
                ];
        }
        else
        {
            for (const TSharedPtr<FShineAIPaintPreviewLight>& Light : Session->PreviewLights)
            {
                if (!Light.IsValid())
                {
                    continue;
                }

                TWeakPtr<FShineAIPaintPreviewLight> WeakLight = Light;

                LightListBox->AddSlot()
                    .AutoHeight()
                    .Padding(FMargin(0.0f, 1.0f))
                    [
                        SNew(SShineAIPaintSelectableRow)

                        // 选中态：跟着当前选中的灯走。
                        .IsSelected_Lambda([this, WeakLight]()
                        {
                            return SelectedLight.IsValid() && SelectedLight == WeakLight.Pin();
                        })

                        // 左侧色条 / 色板：直接用这盏灯的灯色。
                        .AccentColor_Lambda([WeakLight]()
                        {
                            const TSharedPtr<FShineAIPaintPreviewLight> Pinned = WeakLight.Pin();
                            return Pinned.IsValid() ? Pinned->Color : FLinearColor::White;
                        })

                        .OnClicked(FSimpleDelegate::CreateLambda([this, WeakLight]()
                        {
                            SelectedLight = WeakLight.Pin();
                            RefreshLightList();
                        }))

                        .OnRemove(FSimpleDelegate::CreateLambda([this, WeakLight]()
                        {
                            if (!Session.IsValid())
                            {
                                return;
                            }

                            TSharedPtr<FShineAIPaintPreviewLight> Target = WeakLight.Pin();
                            Session->PreviewLights.Remove(Target);

                            if (SelectedLight == Target)
                            {
                                SelectedLight = Session->PreviewLights.Num() > 0 ? Session->PreviewLights[0] : nullptr;
                            }

                            RefreshLightList();
                        }))

                        .RemoveTooltip(NSLOCTEXT("ShineAIPaint", "RemoveLightRowTip", "删掉这盏灯。"))

                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            .Padding(0.0f, 0.0f, 7.0f, 0.0f)
                            [
                                MakeColorSwatch(TAttribute<FLinearColor>::CreateLambda([WeakLight]()
                                {
                                    const TSharedPtr<FShineAIPaintPreviewLight> Pinned = WeakLight.Pin();
                                    return Pinned.IsValid() ? Pinned->Color : FLinearColor::White;
                                }))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text_Lambda([WeakLight]()
                                {
                                    const TSharedPtr<FShineAIPaintPreviewLight> Pinned = WeakLight.Pin();
                                    if (!Pinned.IsValid())
                                    {
                                        return FText::GetEmpty();
                                    }

                                    return FText::FromString(Pinned->Name.IsEmpty() ? Pinned->GetTypeLabel() : Pinned->Name);
                                })
                                .TextStyle(&Style::BodyTextStyle())
                                .ColorAndOpacity_Lambda([this, WeakLight]()
                                {
                                    const bool bSelected = SelectedLight.IsValid() && SelectedLight == WeakLight.Pin();
                                    return FSlateColor(bSelected ? Style::TextPrimary() : Style::TextSecondary());
                                })
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                            [
                                SNew(SBorder)
                                .BorderImage(Style::AccentBadgeBrush())
                                .Padding(FMargin(6.0f, 1.0f))
                                [
                                    SNew(STextBlock)
                                    .Text_Lambda([WeakLight]()
                                    {
                                        const TSharedPtr<FShineAIPaintPreviewLight> Pinned = WeakLight.Pin();
                                        return FText::FromString(Pinned.IsValid() ? Pinned->GetTypeLabel() : FString());
                                    })
                                    .TextStyle(&Style::BadgeTextStyle())
                                ]
                            ]
                        ]
                    ];
            }
        }
    }

    RefreshLightProperties();
}

void SShineAIPaintPlaceActorsTab::RefreshLightProperties()
{
    if (!LightPropertiesBox.IsValid())
    {
        return;
    }

    LightPropertiesBox->ClearChildren();

    if (!SelectedLight.IsValid())
    {
        LightPropertiesBox->AddSlot()
            .AutoHeight()
            [
                MakeEmptyState(NSLOCTEXT("ShineAIPaint", "NoLightSelected", "上面还没有可调的灯。\n先加一盏，再点它来调属性。"))
            ];
        return;
    }

    LightPropertiesBox->AddSlot()
        .AutoHeight()
        [
            BuildLightProperties()
        ];
}

FReply SShineAIPaintPlaceActorsTab::HandleAddLight(EShineAIPaintLightType Type)
{
    if (!Session.IsValid())
    {
        return FReply::Handled();
    }

    Session->EnsurePreviewLightsInitialized();

    TSharedPtr<FShineAIPaintPreviewLight> Light = MakeShared<FShineAIPaintPreviewLight>();
    Light->Type = Type;
    Light->Name = FString::Printf(TEXT("%s %d"), *Light->GetTypeLabel(), Session->PreviewLights.Num() + 1);

    switch (Type)
    {
    case EShineAIPaintLightType::Directional:
        Light->Location = FVector(0.0f, 0.0f, 400.0f);
        Light->Rotation = FRotator(-30.0f, 60.0f, 0.0f);
        Light->Intensity = 5.0f;
        break;

    case EShineAIPaintLightType::Spot:
        Light->Location = FVector(300.0f, 0.0f, 400.0f);
        Light->Rotation = FRotator(-45.0f, 0.0f, 0.0f);
        Light->Intensity = 30000.0f;
        break;

    default:
        Light->Location = FVector(300.0f, 0.0f, 300.0f);
        Light->Intensity = 8000.0f;
        break;
    }

    Session->PreviewLights.Add(Light);
    SelectedLight = Light;
    RefreshLightList();

    return FReply::Handled();
}

FReply SShineAIPaintPlaceActorsTab::HandleRemoveSelectedLight()
{
    if (Session.IsValid() && SelectedLight.IsValid())
    {
        Session->PreviewLights.Remove(SelectedLight);
        SelectedLight = Session->PreviewLights.Num() > 0 ? Session->PreviewLights[0] : nullptr;
        RefreshLightList();
    }

    return FReply::Handled();
}
