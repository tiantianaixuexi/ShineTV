#include "Editor/SShineAIPaintEditorPanel.h"

#include "Asset/ShineAIPaintAsset.h"
#include "Editor/ShineAIPaintEditorStyle.h"
#include "Editor/ShineAIPaintEditorWidgets.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Paint/ShineAIPaintService.h"
#include "Paint/ShineAIPaintSession.h"
#include "Paint/ShineAIPaintTextureUtils.h"
#include "UObject/Package.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SShineAIPaintEditorPanel"

using namespace ShineAIPaintUI;

namespace
{
    /** Details 风格的分组：一条浅色标题栏 + 内容。 */
    TSharedRef<SWidget> MakeDetailsGroup(const FText& Title, TSharedRef<SWidget> Content)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(Style::GroupHeaderBrush())
                .Padding(FMargin(8.0f, 4.0f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock).Text(Title).TextStyle(&Style::SectionTextStyle())
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(FMargin(10.0f, 6.0f, 8.0f, 8.0f))
            [
                Content
            ];
    }
}

void SShineAIPaintEditorPanel::Construct(const FArguments& InArgs)
{
    Session = InArgs._Session;

    SizeOptions.Add(MakeShared<FString>(TEXT("256")));
    SizeOptions.Add(MakeShared<FString>(TEXT("512")));
    SizeOptions.Add(MakeShared<FString>(TEXT("1024")));
    SizeOptions.Add(MakeShared<FString>(TEXT("2048")));
    SizeOptions.Add(MakeShared<FString>(TEXT("4096")));

    if (Session.IsValid())
    {
        Session->OnStructureChanged.AddSP(this, &SShineAIPaintEditorPanel::HandleStructureChanged);
    }

    TSharedRef<SScrollBox> Scroll = SNew(SScrollBox);
    Scroll->AddSlot()[ BuildTargetGroup() ];
    Scroll->AddSlot()[ BuildBrushGroup() ];
    Scroll->AddSlot()[ BuildMaskGroup() ];
    Scroll->AddSlot()[ BuildAIGroup() ];

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(Style::PanelBrush())
        .Padding(0.0f)
        [
            Scroll
        ]
    ];

    RefreshTargetList();
}

SShineAIPaintEditorPanel::~SShineAIPaintEditorPanel()
{
    if (Session.IsValid())
    {
        Session->OnStructureChanged.RemoveAll(this);
    }
}

// ---------------------------------------------------------------------------
// 目标贴图
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SShineAIPaintEditorPanel::BuildTargetGroup()
{
    TSharedRef<SVerticalBox> MaterialBox = SNew(SVerticalBox);
    MaterialBox->AddSlot()
        .AutoHeight()
        [
            MakeButton(
                LOCTEXT("ScanTextures", "扫描材质的贴图"),
                Style::SubtleButton(),
                FOnClicked::CreateSP(this, &SShineAIPaintEditorPanel::HandleScanMaterialTextures),
                LOCTEXT("ScanTexturesTip", "在资产的 SourceMaterial 里找贴图参数，然后在下面的下拉里选一张来画。"))
        ];
    MaterialBox->AddSlot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            SNew(SComboBox<TSharedPtr<FString>>)
            .OptionsSource(&MaterialTextureOptions)
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
            {
                return SNew(STextBlock)
                    .Text(FText::FromString(Item.IsValid() ? *Item : FString()))
                    .TextStyle(&Style::BodyTextStyle());
            })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
            {
                const int32 Index = MaterialTextureOptions.IndexOfByKey(Item);
                if (Index != INDEX_NONE)
                {
                    HandleUseMaterialTexture(Index);
                }
            })
            [
                SNew(STextBlock)
                .TextStyle(&Style::BodyTextStyle())
                .Text_Lambda([this]()
                {
                    return MaterialTextureOptions.Num() > 0
                        ? LOCTEXT("PickMaterialTexture", "选一张材质里的贴图…")
                        : LOCTEXT("NoMaterialTexture", "（先点「扫描材质的贴图」）");
                })
            ]
        ];

    TSharedRef<SWidget> Content = SNew(SVerticalBox)

        // 要画的贴图
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            MakeRow(
                LOCTEXT("TargetTexture", "目标贴图"),
                MakeAssetPicker(
                    UTexture2D::StaticClass(),
                    TAttribute<FString>::CreateLambda([this]()
                    {
                        const UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
                        const UTexture2D* Texture = Asset ? Asset->TargetTexture : nullptr;
                        return Texture ? Texture->GetPathName() : FString();
                    }),
                    FOnSetObject::CreateLambda([this](const FAssetData& AssetData)
                    {
                        HandlePickedTargetTexture(AssetData);
                    })))
        ]

        // 贴图状态
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text_Lambda([this]() { return FText::FromString(GetTargetTextureSummary()); })
            .AutoWrapText(true)
            .TextStyle(&Style::BodyTextStyle())
            .ColorAndOpacity_Lambda([this]()
            {
                if (!Session.IsValid())
                {
                    return FSlateColor(Style::Danger());
                }

                if (!Session->HasPaintTarget())
                {
                    return FSlateColor(Style::Warning());
                }

                return Session->CanUpdateTargetInPlace()
                    ? FSlateColor(Style::TextSecondary())
                    : FSlateColor(Style::Accent());
            })
        ]

        // 新建尺寸
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                LOCTEXT("NewSizeLabel", "新建尺寸"),
                SNew(SBox)
                .WidthOverride(84.0f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&SizeOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                    {
                        return SNew(STextBlock)
                            .Text(FText::FromString(Item.IsValid() ? *Item : FString()))
                            .TextStyle(&Style::BodyTextStyle());
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                    {
                        if (UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr)
                        {
                            if (Item.IsValid())
                            {
                                Asset->NewTextureSize = FCString::Atoi(**Item);
                            }
                        }
                    })
                    [
                        SNew(STextBlock)
                        .TextStyle(&Style::BodyTextStyle())
                        .Text_Lambda([this]()
                        {
                            const UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
                            return FText::FromString(FString::FromInt(Asset ? Asset->NewTextureSize : 1024));
                        })
                    ]
                ])
        ]

        // 贴图动作
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                FText::GetEmpty(),
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
                    MakeButton(
                        LOCTEXT("CreateTexture", "新建一张贴图"),
                        Style::SubtleButton(),
                        FOnClicked::CreateSP(this, &SShineAIPaintEditorPanel::HandleCreateTexture),
                        LOCTEXT("CreateTextureTip", "在资产同目录下新建一张可绘制贴图资产（纯色起步），并设为绘制目标。"))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)[
                    MakeButton(
                        LOCTEXT("ReloadTexture", "重载"),
                        Style::SubtleButton(),
                        FOnClicked::CreateSP(this, &SShineAIPaintEditorPanel::HandleReloadTexture),
                        LOCTEXT("ReloadTextureTip", "丢弃当前未写入的涂改，重新从贴图读一遍像素。"))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[
                    MakeButton(
                        LOCTEXT("WriteTexture", "写入贴图"),
                        Style::SubtleButton(),
                        FOnClicked::CreateSP(this, &SShineAIPaintEditorPanel::HandleWriteTexture),
                        LOCTEXT("WriteTextureTip", "把当前画布像素写回那张贴图。未压缩贴图本来就实时在写，这里只是强制同步一次；压缩贴图必须走这一步才会更新。"))
                ])
        ]

        // 材质
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                LOCTEXT("SourceMaterial", "材质"),
                MakeAssetPicker(
                    UMaterialInterface::StaticClass(),
                    TAttribute<FString>::CreateLambda([this]()
                    {
                        const UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
                        const UMaterialInterface* Material = Asset ? Asset->SourceMaterial : nullptr;
                        return Material ? Material->GetPathName() : FString();
                    }),
                    FOnSetObject::CreateLambda([this](const FAssetData& AssetData)
                    {
                        HandlePickedSourceMaterial(AssetData);
                    })))
        ]

        // 材质贴图
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(LOCTEXT("FromMaterial", "材质贴图"), MaterialBox)
        ]

        // 预览网格
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                LOCTEXT("PreviewMeshes", "预览网格"),
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    MakeValueChip(
                        TAttribute<FText>::CreateLambda([this]() { return FText::AsNumber(Session.IsValid() ? Session->GetTargets().Num() : 0); }),
                        34.0f)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    MakeButton(
                        LOCTEXT("AddMeshSlot", "+ 添加"),
                        Style::SubtleButton(),
                        FOnClicked::CreateSP(this, &SShineAIPaintEditorPanel::HandleAddMeshSlot),
                        LOCTEXT("AddMeshSlotTip", "新增一个空槽位，然后在下面的挑选框里选网格资产。"))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    MakeButton(
                        LOCTEXT("ClearTargets", "清空"),
                        Style::SubtleButton(),
                        FOnClicked::CreateLambda([this]()
                        {
                            if (UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr)
                            {
                                Asset->Modify();
                                Asset->MeshAssets.Reset();
                                ReloadSessionFromAsset();
                            }
                            return FReply::Handled();
                        }))
                ])
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            SNew(SBox)
            .HeightOverride(96.0f)
            [
                SNew(SBorder)
                .BorderImage(Style::WellBrush())
                .Padding(FMargin(5.0f, 4.0f))
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [
                        SAssignNew(TargetListBox, SVerticalBox)
                    ]
                ]
            ]
        ];

    return MakeDetailsGroup(LOCTEXT("GroupTarget", "目标贴图"), Content);
}

// ---------------------------------------------------------------------------
// 画笔
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SShineAIPaintEditorPanel::BuildBrushGroup()
{
    TSharedRef<SWidget> Content = SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight()
        [
            MakeSliderRow(
                LOCTEXT("BrushSize", "笔刷大小"),
                MakeSlider(
                    0.004f,
                    0.25f,
                    TAttribute<float>::CreateLambda([this]() { return Session.IsValid() ? Session->BrushRadiusUV : 0.035f; }),
                    FOnFloatValueChanged::CreateLambda([this](float Value)
                    {
                        if (Session.IsValid())
                        {
                            Session->BrushRadiusUV = Value;
                        }
                    })),
                TAttribute<FText>::CreateLambda([this]()
                {
                    return FText::FromString(FString::Printf(TEXT("%.1f%%"), (Session.IsValid() ? Session->BrushRadiusUV : 0.0f) * 100.0f));
                }))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                LOCTEXT("BrushColor", "笔刷颜色"),
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    MakeColorPreviewBox(TAttribute<FLinearColor>::CreateLambda([this]()
                    {
                        return Session.IsValid() ? Session->BrushColor : FLinearColor::White;
                    }))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    MakeVectorBox(
                        [this]() -> FVector3f
                        {
                            if (!Session.IsValid())
                            {
                                return FVector3f::ZeroVector;
                            }

                            return FVector3f(Session->BrushColor.R, Session->BrushColor.G, Session->BrushColor.B);
                        },
                        [this](const FVector3f& Value)
                        {
                            if (Session.IsValid())
                            {
                                Session->BrushColor = FLinearColor(
                                    FMath::Clamp(Value.X, 0.0f, 1.0f),
                                    FMath::Clamp(Value.Y, 0.0f, 1.0f),
                                    FMath::Clamp(Value.Z, 0.0f, 1.0f));
                            }
                        },
                        0.01f,
                        LOCTEXT("ChannelR", "红（R）"),
                        LOCTEXT("ChannelG", "绿（G）"),
                        LOCTEXT("ChannelB", "蓝（B）"))
                ])
        ];

    return MakeDetailsGroup(LOCTEXT("GroupBrush", "画笔"), Content);
}

// ---------------------------------------------------------------------------
// 遮罩
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SShineAIPaintEditorPanel::BuildMaskGroup()
{
    TSharedRef<SWidget> Content = SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            MakeRow(
                LOCTEXT("MaskMode", "遮罩语义"),
                SNew(SCheckBox)
                .IsChecked_Lambda([this]()
                {
                    return (Session.IsValid() && Session->Settings.MaskMode == EShineAIPaintMaskMode::Editable)
                        ? ECheckBoxState::Checked
                        : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                {
                    if (Session.IsValid())
                    {
                        Session->Settings.MaskMode = (State == ECheckBoxState::Checked)
                            ? EShineAIPaintMaskMode::Editable
                            : EShineAIPaintMaskMode::Protect;
                    }
                })
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("MaskModeEditable", "遮罩区 = 只更新这里"))
                    .TextStyle(&Style::BodyTextStyle())
                ])
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                LOCTEXT("MaskState", "遮罩状态"),
                SNew(STextBlock)
                .AutoWrapText(true)
                .TextStyle(&Style::BodyTextStyle())
                .Text_Lambda([this]()
                {
                    if (!Session.IsValid())
                    {
                        return FText::GetEmpty();
                    }

                    const bool bEmpty = Session->IsMaskEmpty();
                    const bool bEditable = Session->Settings.MaskMode == EShineAIPaintMaskMode::Editable;
                    return FText::FromString(bEmpty
                        ? TEXT("空 —— AI 会更新整张图")
                        : (bEditable
                            ? TEXT("已启用 · 只更新遮罩区")
                            : TEXT("已启用 · 遮罩区被保护")));
                })
                .ColorAndOpacity_Lambda([this]()
                {
                    if (Session.IsValid() && !Session->IsMaskEmpty())
                    {
                        return FSlateColor(Session->Settings.MaskMode == EShineAIPaintMaskMode::Editable
                            ? Style::AccentCool()
                            : Style::Accent());
                    }

                    return FSlateColor(Style::TextMuted());
                }))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeHintText(LOCTEXT("MaskHint", "不勾选时遮罩区 = AI 不更新（保护）；勾选时遮罩区 = 只更新这里。"))
        ];

    return MakeDetailsGroup(LOCTEXT("GroupMask", "遮罩"), Content);
}

// ---------------------------------------------------------------------------
// AI 更新
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SShineAIPaintEditorPanel::BuildAIGroup()
{
    TSharedRef<SWidget> Content = SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            MakeRow(
                LOCTEXT("ServiceUrl", "服务地址"),
                SNew(SEditableTextBox)
                .Text_Lambda([this]() { return FText::FromString(Session.IsValid() ? Session->Settings.ServiceUrl : FString()); })
                .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
                {
                    if (Session.IsValid())
                    {
                        Session->Settings.ServiceUrl = Text.ToString();
                    }
                })
                .Font(Style::BodyFont()))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, GRowSpacing)
        [
            MakeHintText(TAttribute<FText>::CreateLambda([this]()
            {
                const UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
                const bool bFromComfyAsset = Asset && Asset->IsUsingComfyAssetUrl();
                return FText::FromString(FString::Printf(TEXT("生效：%s%s"),
                    *GetEffectiveServiceUrl(),
                    bFromComfyAsset ? TEXT("（跟随 ShineComfy 资产）") : TEXT("（本资产自己的地址）")));
            }))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            MakeRow(
                LOCTEXT("Checkpoint", "模型"),
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&CheckpointOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                    {
                        return SNew(STextBlock)
                            .Text(FText::FromString(Item.IsValid() ? *Item : FString()))
                            .TextStyle(&Style::BodyTextStyle());
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                    {
                        if (Session.IsValid() && Item.IsValid())
                        {
                            Session->Settings.CheckpointName = *Item;
                        }
                    })
                    [
                        SNew(STextBlock)
                        .TextStyle(&Style::BodyTextStyle())
                        .Text_Lambda([this]()
                        {
                            if (!Session.IsValid())
                            {
                                return FText::GetEmpty();
                            }
                            return FText::FromString(Session->Settings.CheckpointName.IsEmpty()
                                ? TEXT("（点「拉取列表」）")
                                : Session->Settings.CheckpointName);
                        })
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    MakeButton(
                        LOCTEXT("FetchCheckpoints", "拉取列表"),
                        Style::SubtleButton(),
                        FOnClicked::CreateSP(this, &SShineAIPaintEditorPanel::HandleFetchCheckpoints))
                ])
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                LOCTEXT("Prompt", "提示词"),
                SNew(SBox)
                .HeightOverride(64.0f)
                [
                    SNew(SBorder)
                    .BorderImage(Style::NumericFieldBrush())
                    .Padding(FMargin(4.0f, 3.0f))
                    [
                        SNew(SMultiLineEditableTextBox)
                        .Style(&Style::NumericTextBox())
                        .HintText(LOCTEXT("PromptHint", "想让 AI 画成什么样；可以换行，Ctrl+Enter 提交"))
                        .Text_Lambda([this]() { return FText::FromString(Session.IsValid() ? Session->Settings.Prompt : FString()); })
                        .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
                        {
                            if (Session.IsValid())
                            {
                                Session->Settings.Prompt = Text.ToString();
                            }
                        })
                        .Font(Style::BodyFont())
                        .AutoWrapText(true)
                        .SelectAllTextWhenFocused(true)
                        .ClearKeyboardFocusOnCommit(false)
                        .AllowContextMenu(true)
                    ]
                ])
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeRow(
                LOCTEXT("NegativePrompt", "负向提示词"),
                SNew(SBox)
                .HeightOverride(52.0f)
                [
                    SNew(SBorder)
                    .BorderImage(Style::NumericFieldBrush())
                    .Padding(FMargin(4.0f, 3.0f))
                    [
                        SNew(SMultiLineEditableTextBox)
                        .Style(&Style::NumericTextBox())
                        .HintText(LOCTEXT("NegativeHint", "不想出现的东西；可留空"))
                        .Text_Lambda([this]() { return FText::FromString(Session.IsValid() ? Session->Settings.NegativePrompt : FString()); })
                        .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
                        {
                            if (Session.IsValid())
                            {
                                Session->Settings.NegativePrompt = Text.ToString();
                            }
                        })
                        .Font(Style::BodyFont())
                        .AutoWrapText(true)
                        .SelectAllTextWhenFocused(true)
                        .ClearKeyboardFocusOnCommit(false)
                        .AllowContextMenu(true)
                    ]
                ])
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeSliderRow(
                LOCTEXT("Steps", "步数"),
                MakeSlider(
                    1.0f, 80.0f,
                    TAttribute<float>::CreateLambda([this]() { return Session.IsValid() ? static_cast<float>(Session->Settings.Steps) : 20.0f; }),
                    FOnFloatValueChanged::CreateLambda([this](float Value) { if (Session.IsValid()) { Session->Settings.Steps = FMath::RoundToInt(Value); } })),
                TAttribute<FText>::CreateLambda([this]() { return FText::AsNumber(Session.IsValid() ? Session->Settings.Steps : 0); }))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeSliderRow(
                LOCTEXT("Cfg", "CFG"),
                MakeSlider(
                    1.0f, 20.0f,
                    TAttribute<float>::CreateLambda([this]() { return Session.IsValid() ? Session->Settings.CFG : 7.0f; }),
                    FOnFloatValueChanged::CreateLambda([this](float Value) { if (Session.IsValid()) { Session->Settings.CFG = Value; } })),
                TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FString::Printf(TEXT("%.1f"), Session.IsValid() ? Session->Settings.CFG : 0.0f)); }))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeSliderRow(
                LOCTEXT("Denoise", "重绘强度"),
                MakeSlider(
                    0.05f, 1.0f,
                    TAttribute<float>::CreateLambda([this]() { return Session.IsValid() ? Session->Settings.Denoise : 0.85f; }),
                    FOnFloatValueChanged::CreateLambda([this](float Value) { if (Session.IsValid()) { Session->Settings.Denoise = Value; } })),
                TAttribute<FText>::CreateLambda([this]() { return FText::FromString(FString::Printf(TEXT("%.2f"), Session.IsValid() ? Session->Settings.Denoise : 0.0f)); }))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return (Session.IsValid() && Session->IsLiveAIUpdateEnabled())
                    ? ECheckBoxState::Checked
                    : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
            {
                if (Session.IsValid())
                {
                    Session->SetLiveAIUpdateEnabled(State == ECheckBoxState::Checked);
                }
            })
            [
                SNew(STextBlock)
                .Text(LOCTEXT("LiveAI", "动态更新（每画一笔自动重生成）"))
                .TextStyle(&Style::BodyTextStyle())
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            MakeHintText(LOCTEXT("LiveAIHint", "抬笔时只有这一笔真的改了像素才发请求；结果没回来之前又画了新的，会先把这一版应用上去，再按最新画布补发一次。请求一个一个传，不会堆一堆。"))
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 10.0f, 0.0f, 0.0f)
        [
            SNew(SButton)
            .ButtonStyle(&Style::PrimaryButton())
            .ContentPadding(FMargin(0.0f))
            .HAlign(HAlign_Center)
            .IsEnabled_Lambda([this]()
            {
                return Session.IsValid() && !Session->IsBusy() && Session->HasPaintTarget();
            })
            .OnClicked(this, &SShineAIPaintEditorPanel::HandleRequestAI)
            [
                SNew(STextBlock)
                .Font(Style::SectionFont())
                .ColorAndOpacity_Lambda([this]()
                {
                    return (Session.IsValid() && Session->IsBusy())
                        ? FSlateColor(FLinearColor(0.09f, 0.07f, 0.03f, 0.65f))
                        : FSlateColor(Style::OnAccent());
                })
                .Text_Lambda([this]()
                {
                    return (Session.IsValid() && Session->IsBusy())
                        ? LOCTEXT("AIBusy", "AI 生成中…")
                        : LOCTEXT("AIUpdate", "AI 更新贴图");
                })
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, GRowSpacing, 0.0f, 0.0f)
        [
            MakeHintText(TAttribute<FText>::CreateLambda([this]()
            {
                // 跑的时候显示 WebSocket 推回来的实时进度（拉取式，不广播）。
                if (Session.IsValid() && Session->IsBusy())
                {
                    const FString& LiveProgress = Session->GetLiveProgress();
                    return FText::FromString(LiveProgress.IsEmpty() ? TEXT("AI 生成中…") : LiveProgress);
                }

                return LOCTEXT("AIUpdateHint", "把当前画布 + 遮罩发给 ComfyUI，结果回来后覆盖底色图层。");
            }))
        ];

    return MakeDetailsGroup(LOCTEXT("GroupAI", "AI 更新"), Content);
}

// ---------------------------------------------------------------------------
// 刷新
// ---------------------------------------------------------------------------

void SShineAIPaintEditorPanel::HandleStructureChanged()
{
    RefreshTargetList();
}

void SShineAIPaintEditorPanel::RefreshTargetList()
{
    if (!TargetListBox.IsValid())
    {
        return;
    }

    TargetListBox->ClearChildren();

    UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
    if (!Asset)
    {
        TargetListBox->AddSlot()
            .AutoHeight()
            .Padding(2.0f)
            [
                MakeHintText(LOCTEXT("NoAsset", "（会话未初始化）"))
            ];
        return;
    }

    if (Asset->MeshAssets.Num() == 0)
    {
        TargetListBox->AddSlot()
            .AutoHeight()
            .Padding(2.0f)
            [
                MakeHintText(LOCTEXT("NoTargets", "还没有预览网格：点「+ 添加」再挑网格资产。"))
            ];
        return;
    }

    for (int32 Index = 0; Index < Asset->MeshAssets.Num(); ++Index)
    {
        TargetListBox->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 1.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    MakeMeshPicker(
                        TAttribute<FString>::CreateLambda([this, Index]()
                        {
                            const UShineAIPaintAsset* CurrentAsset = Session.IsValid() ? Session->GetAsset() : nullptr;
                            return (CurrentAsset && CurrentAsset->MeshAssets.IsValidIndex(Index))
                                ? CurrentAsset->MeshAssets[Index].ToString()
                                : FString();
                        }),
                        FOnSetObject::CreateLambda([this, Index](const FAssetData& AssetData)
                        {
                            HandlePickedMesh(Index, AssetData);
                        }))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                [
                    MakeButton(
                        FText::FromString(TEXT("×")),
                        Style::MiniButton(),
                        FOnClicked::CreateLambda([this, Index]() { return HandleRemoveMeshSlot(Index); }),
                        LOCTEXT("RemoveMeshTip", "把这个网格从预览列表里去掉。"),
                        FSlateColor(Style::TextMuted()),
                        Style::BodyFont())
                ]
            ];
    }
}

// ---------------------------------------------------------------------------
// 资产挑选
// ---------------------------------------------------------------------------

void SShineAIPaintEditorPanel::ReloadSessionFromAsset()
{
    if (Session.IsValid())
    {
        Session->LoadFromAsset();
    }

    RefreshTargetList();
}

void SShineAIPaintEditorPanel::HandlePickedTargetTexture(const FAssetData& AssetData)
{
    UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
    if (!Asset)
    {
        return;
    }

    UTexture2D* Texture = AssetData.IsValid() ? Cast<UTexture2D>(AssetData.GetAsset()) : nullptr;

    Asset->Modify();
    Asset->TargetTexture = Texture;
    if (Texture)
    {
        Asset->EnsureMaskBuffer(Texture->GetSizeX(), Texture->GetSizeY());
    }

    if (Session.IsValid())
    {
        Session->SetLastStatus(Texture
            ? FString::Printf(TEXT("已指认贴图 %s（%dx%d）"), *Texture->GetName(), Texture->GetSizeX(), Texture->GetSizeY())
            : TEXT("已清空目标贴图。"));
    }

    ReloadSessionFromAsset();
}

void SShineAIPaintEditorPanel::HandlePickedSourceMaterial(const FAssetData& AssetData)
{
    UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
    if (!Asset)
    {
        return;
    }

    Asset->Modify();
    Asset->SourceMaterial = AssetData.IsValid() ? Cast<UMaterialInterface>(AssetData.GetAsset()) : nullptr;

    if (Session.IsValid())
    {
        Session->SetLastStatus(Asset->SourceMaterial
            ? TEXT("已指定材质，点「扫描材质的贴图」列出它的贴图参数。")
            : TEXT("已清空材质。"));
    }
}

void SShineAIPaintEditorPanel::HandlePickedMesh(int32 Index, const FAssetData& AssetData)
{
    UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
    if (!Asset || !Asset->MeshAssets.IsValidIndex(Index))
    {
        return;
    }

    UObject* Mesh = AssetData.IsValid() ? AssetData.GetAsset() : nullptr;

    Asset->Modify();
    Asset->MeshAssets[Index] = TSoftObjectPtr<UObject>(Mesh);

    // 重建预览与拾取三角形。
    ReloadSessionFromAsset();
}

FReply SShineAIPaintEditorPanel::HandleAddMeshSlot()
{
    if (UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr)
    {
        Asset->Modify();
        Asset->MeshAssets.Add(TSoftObjectPtr<UObject>());
        RefreshTargetList();
    }

    return FReply::Handled();
}

FReply SShineAIPaintEditorPanel::HandleRemoveMeshSlot(int32 Index)
{
    if (UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr)
    {
        if (Asset->MeshAssets.IsValidIndex(Index))
        {
            Asset->Modify();
            Asset->MeshAssets.RemoveAt(Index);
            ReloadSessionFromAsset();
        }
    }

    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// 贴图相关动作
// ---------------------------------------------------------------------------

FReply SShineAIPaintEditorPanel::HandleCreateTexture()
{
    UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
    if (!Asset)
    {
        return FReply::Handled();
    }

    const FString PackagePath = FPaths::GetPath(Asset->GetOutermost()->GetName());
    const FString BaseName = FString::Printf(TEXT("%s_Tex"), *Asset->GetName());

    FString ErrorMessage;
    UTexture2D* NewTexture = ShineAIPaintTextureUtils::CreatePaintableTextureAsset(
        PackagePath,
        BaseName,
        Asset->NewTextureSize,
        FColor(190, 190, 196, 255),
        ErrorMessage);

    if (!NewTexture)
    {
        Session->SetLastStatus(FString::Printf(TEXT("新建贴图失败：%s"), *ErrorMessage));
        return FReply::Handled();
    }

    Asset->Modify();
    Asset->TargetTexture = NewTexture;
    Asset->EnsureMaskBuffer(NewTexture->GetSizeX(), NewTexture->GetSizeY());

    Session->SetLastStatus(FString::Printf(TEXT("已新建贴图 %s（%dx%d）"), *NewTexture->GetName(), NewTexture->GetSizeX(), NewTexture->GetSizeY()));
    ReloadSessionFromAsset();
    return FReply::Handled();
}

FReply SShineAIPaintEditorPanel::HandleScanMaterialTextures()
{
    UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
    if (!Asset)
    {
        return FReply::Handled();
    }

    TArray<FName> ParameterNames;
    TArray<UTexture2D*> Textures;
    ShineAIPaintTextureUtils::CollectMaterialTextures(Asset->SourceMaterial, ParameterNames, Textures);

    MaterialTextureCandidates = Textures;
    MaterialTextureOptions.Reset();
    for (int32 Index = 0; Index < Textures.Num(); ++Index)
    {
        MaterialTextureOptions.Add(MakeShared<FString>(FString::Printf(
            TEXT("%s  →  %s"),
            *ParameterNames[Index].ToString(),
            *Textures[Index]->GetName())));
    }

    if (Textures.Num() == 0)
    {
        Session->SetLastStatus(Asset->SourceMaterial
            ? TEXT("这个材质里没有贴图参数。")
            : TEXT("请先在上面指定材质，再点「扫描材质的贴图」。"));
    }
    else
    {
        Session->SetLastStatus(FString::Printf(TEXT("从材质里找到 %d 张贴图，在下拉里选一张开始画。"), Textures.Num()));
    }

    return FReply::Handled();
}

FReply SShineAIPaintEditorPanel::HandleUseMaterialTexture(int32 CandidateIndex)
{
    UShineAIPaintAsset* Asset = Session.IsValid() ? Session->GetAsset() : nullptr;
    if (!Asset || !MaterialTextureCandidates.IsValidIndex(CandidateIndex))
    {
        return FReply::Handled();
    }

    UTexture2D* Texture = MaterialTextureCandidates[CandidateIndex];

    FString Reason;
    if (!ShineAIPaintTextureUtils::IsPaintable(Texture, Reason))
    {
        Session->SetLastStatus(FString::Printf(TEXT("这张贴图不能画：%s"), *Reason));
        return FReply::Handled();
    }

    Asset->Modify();
    Asset->TargetTexture = Texture;
    Asset->EnsureMaskBuffer(Texture->GetSizeX(), Texture->GetSizeY());

    Session->SetLastStatus(FString::Printf(TEXT("已指认贴图 %s（%dx%d）"), *Texture->GetName(), Texture->GetSizeX(), Texture->GetSizeY()));
    ReloadSessionFromAsset();
    return FReply::Handled();
}

FReply SShineAIPaintEditorPanel::HandleReloadTexture()
{
    if (Session.IsValid())
    {
        Session->SetLastStatus(TEXT("已从贴图重新读入像素。"));
    }

    ReloadSessionFromAsset();
    return FReply::Handled();
}

FReply SShineAIPaintEditorPanel::HandleWriteTexture()
{
    if (Session.IsValid())
    {
        Session->SaveToAsset();
        Session->SetLastStatus(TEXT("已把画布写入贴图（贴图重新构建完成）。"));
        Session->OnPixelsChanged.Broadcast();
    }

    return FReply::Handled();
}

// ---------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------

FReply SShineAIPaintEditorPanel::HandleRequestAI()
{
    if (!Session.IsValid())
    {
        return FReply::Handled();
    }

    FString ErrorMessage;
    if (!Session->RequestAIUpdate(ErrorMessage))
    {
        Session->SetLastStatus(ErrorMessage);
    }

    return FReply::Handled();
}

FReply SShineAIPaintEditorPanel::HandleFetchCheckpoints()
{
    if (!Session.IsValid())
    {
        return FReply::Handled();
    }

    const FString ServiceUrl = GetEffectiveServiceUrl();
    Session->SetLastStatus(FString::Printf(TEXT("正在拉取 checkpoint 列表：%s"), *ServiceUrl));

    TWeakPtr<SShineAIPaintEditorPanel> WeakSelf = SharedThis(this);
    FShineAIPaintService::FetchCheckpoints(ServiceUrl, [WeakSelf](bool bSuccess, const TArray<FString>& Checkpoints, const FString& ErrorMessage)
    {
        const TSharedPtr<SShineAIPaintEditorPanel> Pinned = WeakSelf.Pin();
        if (!Pinned.IsValid() || !Pinned->Session.IsValid())
        {
            return;
        }

        if (!bSuccess)
        {
            Pinned->Session->SetLastStatus(FString::Printf(TEXT("拉取 checkpoint 失败：%s"), *ErrorMessage));
            return;
        }

        Pinned->FetchedCheckpointNames = Checkpoints;
        Pinned->CheckpointOptions.Reset();
        for (const FString& Name : Checkpoints)
        {
            Pinned->CheckpointOptions.Add(MakeShared<FString>(Name));
        }

        Pinned->Session->SetLastStatus(FString::Printf(TEXT("已拉取 %d 个 checkpoint。"), Checkpoints.Num()));
    });

    return FReply::Handled();
}

FString SShineAIPaintEditorPanel::GetEffectiveServiceUrl() const
{
    if (Session.IsValid())
    {
        if (const UShineAIPaintAsset* Asset = Session->GetAsset())
        {
            return Asset->ResolveServiceUrl();
        }
    }

    return TEXT("http://127.0.0.1:8188");
}

FString SShineAIPaintEditorPanel::GetTargetTextureSummary() const
{
    if (!Session.IsValid())
    {
        return TEXT("（会话未初始化）");
    }

    const UTexture2D* Texture = Session->GetTargetTexture();
    if (!Texture)
    {
        return Session->GetTargetTextureIssue().IsEmpty()
            ? TEXT("还没指定贴图 —— 新建一张，或从材质里扫描并选一张。")
            : FString::Printf(TEXT("%s  —— 请换一张贴图"), *Session->GetTargetTextureIssue());
    }

    if (!Session->HasPaintTarget())
    {
        return FString::Printf(TEXT("%s（%dx%d）—— %s"), *Texture->GetName(), Texture->GetSizeX(), Texture->GetSizeY(), *Session->GetTargetTextureIssue());
    }

    const FString UpdateHint = Session->CanUpdateTargetInPlace()
        ? TEXT(" · 涂改实时写进贴图")
        : TEXT(" · 压缩格式：需要「写入贴图」");

    return FString::Printf(TEXT("%s（%dx%d）%s"), *Texture->GetName(), Session->GetWidth(), Session->GetHeight(), *UpdateHint);
}

#undef LOCTEXT_NAMESPACE
