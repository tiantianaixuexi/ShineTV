#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Editor/ShineAIPaintEditorStyle.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Paint/ShineAIPaintTypes.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

/**
 * 编辑器各页签共用的小控件工厂。
 *
 * 全部 inline，属性页签 / 视口页签 / 画布页签 / 放置页签都从这里取，
 * 保证四处的行距、标签列宽度、按钮样式完全一致。
 */
class FShineAIPaintSession;

namespace ShineAIPaintUI
{
    namespace Style = ShineAIPaintStyle;

    /** 所有页签共用一列标签宽度：控件左边缘对齐靠它。 */
    constexpr float GLabelWidth = 70.0f;
    constexpr float GValueWidth = 46.0f;
    constexpr float GRowSpacing = 6.0f;
    /** 属性行里输入 / 色板的高度，让它们看起来一样高。 */
    constexpr float GAxisFieldHeight = 22.0f;

    inline FText GetToolHint(EShineAIPaintTool Tool)
    {
        switch (Tool)
        {
        case EShineAIPaintTool::Paint:     return NSLOCTEXT("ShineAIPaint", "ToolHintPaint", "在底色图层上画；底色就是 AI 的输入图。");
        case EShineAIPaintTool::Erase:     return NSLOCTEXT("ShineAIPaint", "ToolHintErase", "擦回初始底色（AI 出图前的样子）。");
        case EShineAIPaintTool::MaskPaint: return NSLOCTEXT("ShineAIPaint", "ToolHintMaskPaint", "往遮罩图层上画：被涂到的地方按遮罩语义处理。");
        default:                           return NSLOCTEXT("ShineAIPaint", "ToolHintMaskErase", "把已画的遮罩擦掉。");
        }
    }

    inline TSharedRef<SWidget> MakeValueChip(TAttribute<FText> Text, float Width = GValueWidth)
    {
        return SNew(SBox)
            .WidthOverride(Width)
            .VAlign(VAlign_Center)
            [
                SNew(SBorder)
                .BorderImage(Style::WellBrush())
                .Padding(FMargin(4.0f, 1.0f))
                .HAlign(HAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(MoveTemp(Text))
                    .Font(Style::ValueFont())
                    .ColorAndOpacity(FSlateColor(Style::TextSecondary()))
                    .Justification(ETextJustify::Center)
                ]
            ];
    }

    inline TSharedRef<SWidget> MakeLabel(const FText& Text, float Width = GLabelWidth)
    {
        return SNew(SBox)
            .WidthOverride(Width)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(Text)
                .TextStyle(&Style::BodyTextStyle())
                .ColorAndOpacity(FSlateColor(Style::TextMuted()))
            ];
    }

    /** 一行：固定宽度标签列 + 控件。 */
    inline TSharedRef<SWidget> MakeRow(const FText& Label, TSharedRef<SWidget> Control, float LabelWidth = GLabelWidth)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[ MakeLabel(Label, LabelWidth) ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Control ];
    }

    inline TSharedRef<SWidget> MakeRow(const FText& Label, TSharedRef<SVerticalBox> Control, float LabelWidth = GLabelWidth)
    {
        return MakeRow(Label, StaticCastSharedRef<SWidget>(Control), LabelWidth);
    }

    inline TSharedRef<SWidget> MakeRow(const FText& Label, TSharedRef<SHorizontalBox> Control, float LabelWidth = GLabelWidth)
    {
        return MakeRow(Label, StaticCastSharedRef<SWidget>(Control), LabelWidth);
    }

    inline TSharedRef<SWidget> MakeSlider(float Min, float Max, TAttribute<float> Value, FOnFloatValueChanged OnChanged, bool bCoolHandle = false)
    {
        return SNew(SSlider)
            .MinValue(Min)
            .MaxValue(Max)
            .Value(MoveTemp(Value))
            .SliderBarColor(Style::SliderTrack())
            .SliderHandleColor(bCoolHandle ? Style::AccentCool() : Style::Accent())
            .OnValueChanged(MoveTemp(OnChanged));
    }

    /** 标签 + 滑条 + 数值。 */
    inline TSharedRef<SWidget> MakeSliderRow(const FText& Label, TSharedRef<SWidget> Slider, TAttribute<FText> ValueText, float LabelWidth = GLabelWidth)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[ MakeLabel(Label, LabelWidth) ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ Slider ]
            + SHorizontalBox::Slot().AutoWidth()[ MakeValueChip(MoveTemp(ValueText)) ];
    }

    /** 给控件套一层内凹数值框底（名称输入框这类纯文本框用）。 */
    inline TSharedRef<SWidget> MakeFieldFrame(TSharedRef<SWidget> Inner, float Height = 0.0f)
    {
        TSharedRef<SBox> Frame = SNew(SBox);
        if (Height > 0.0f)
        {
            Frame->SetHeightOverride(Height);
        }

        Frame->SetContent(
            SNew(SBorder)
            .BorderImage(Style::NumericFieldBrush())
            .Padding(FMargin(4.0f, 1.0f))
            [
                Inner
            ]);

        return Frame;
    }

    /** 颜色预览小方块：内凹边框里一块实色，用来配合 R/G/B 数值框显示当前颜色。 */
    inline TSharedRef<SWidget> MakeColorPreviewBox(TAttribute<FLinearColor> Color, float Width = 22.0f, float Height = GAxisFieldHeight)
    {
        return SNew(SBox)
            .WidthOverride(Width)
            .HeightOverride(Height)
            .VAlign(VAlign_Center)
            [
                SNew(SBorder)
                .BorderImage(Style::NumericFieldBrush())
                .Padding(2.0f)
                [
                    SNew(SBorder)
                    .BorderImage(Style::SolidFillBrush())
                    .BorderBackgroundColor_Lambda([Color]()
                    {
                        return FSlateColor(Color.Get(FLinearColor::White));
                    })
                    .Padding(0.0f)
                ]
            ];
    }

    /**
     * 三轴数值框（X / Y / Z）。
     *
     * 用的是引擎自带的 SVectorInputBox：轴色标签、按住拖动改值、滚轮微调、右键复制粘贴，
     * 和 UE 变换行是同一个控件。
     *
     * 调用方只要给"读一个向量 / 写一个向量"，分量的拆分和事件接线都在这里做。
     * 这样位置、旋转、缩放、颜色这类三分量属性都走同一个控件，不用各写一遍。
     *
     * 注意：引擎只在设了 ConstrainVector 时才会回调 OnVectorChanged；
     * 否则每个分量走各自的 OnXChanged / OnYChanged / OnZChanged，必须逐个接上，
     * 否则框里能改、数据不写回去。
     */
    inline TSharedRef<SWidget> MakeVectorBox(
        TFunction<FVector3f()> Getter,
        TFunction<void(const FVector3f&)> Setter,
        float SpinDelta = 1.0f,
        const FText& XName = FText::GetEmpty(),
        const FText& YName = FText::GetEmpty(),
        const FText& ZName = FText::GetEmpty())
    {
        auto ApplyAxis = [Getter, Setter](int32 Axis, float Value)
        {
            FVector3f Vector = Getter();
            Vector[Axis] = Value;
            Setter(Vector);
        };

        auto MakeChanged = [ApplyAxis](int32 Axis)
        {
            return SVectorInputBox::FOnNumericValueChanged::CreateLambda(
                [ApplyAxis, Axis](float Value) { ApplyAxis(Axis, Value); });
        };

        auto MakeCommitted = [ApplyAxis](int32 Axis)
        {
            return SVectorInputBox::FOnNumericValueCommitted::CreateLambda(
                [ApplyAxis, Axis](float Value, ETextCommit::Type) { ApplyAxis(Axis, Value); });
        };

        return SNew(SVectorInputBox)
            .AllowSpin(true)
            .bColorAxisLabels(true)
            .Font(Style::BodyFont())
            .SpinDelta(SpinDelta)
            .XDisplayName(XName)
            .YDisplayName(YName)
            .ZDisplayName(ZName)
            .Vector(TAttribute<TOptional<FVector3f>>::CreateLambda([Getter]() -> TOptional<FVector3f>
            {
                return TOptional<FVector3f>(Getter());
            }))
            .OnXChanged(MakeChanged(0))
            .OnYChanged(MakeChanged(1))
            .OnZChanged(MakeChanged(2))
            .OnXCommitted(MakeCommitted(0))
            .OnYCommitted(MakeCommitted(1))
            .OnZCommitted(MakeCommitted(2));
    }

    /** 一行：固定宽度标签 + 三轴数值框（位置 / 旋转 / 缩放 / 颜色都能用）。 */
    inline TSharedRef<SWidget> MakeVectorRow(
        const FText& Label,
        TFunction<FVector3f()> Getter,
        TFunction<void(const FVector3f&)> Setter,
        float SpinDelta = 1.0f,
        const FText& XName = FText::GetEmpty(),
        const FText& YName = FText::GetEmpty(),
        const FText& ZName = FText::GetEmpty(),
        float LabelWidth = GLabelWidth)
    {
        return MakeRow(
            Label,
            MakeVectorBox(MoveTemp(Getter), MoveTemp(Setter), SpinDelta, XName, YName, ZName),
            LabelWidth);
    }

    inline TSharedRef<SWidget> MakeButton(
        const FText& Label,
        const FButtonStyle& ButtonStyle,
        FOnClicked OnClicked,
        const FText& Tooltip = FText::GetEmpty(),
        const FSlateColor& TextColor = FSlateColor(ShineAIPaintStyle::TextSecondary()),
        const FSlateFontInfo& Font = ShineAIPaintStyle::BodyFont())
    {
        TSharedRef<SButton> Button = SNew(SButton)
            .ButtonStyle(&ButtonStyle)
            .OnClicked(MoveTemp(OnClicked))
            .ContentPadding(FMargin(0.0f))
            [
                SNew(STextBlock)
                .Text(Label)
                .Font(Font)
                .ColorAndOpacity(TextColor)
                .Justification(ETextJustify::Center)
            ];

        if (!Tooltip.IsEmpty())
        {
            Button->SetToolTipText(Tooltip);
        }

        return Button;
    }

    /** 工具条里的分组竖线。 */
    inline TSharedRef<SWidget> MakeToolbarSeparator()
    {
        return SNew(SBox)
            .WidthOverride(1.0f)
            .HeightOverride(18.0f)
            .VAlign(VAlign_Center)
            .Padding(FMargin(6.0f, 0.0f))
            [
                SNew(SBorder).BorderImage(Style::HairlineBrush()).Padding(0.0f)
            ];
    }

    inline TSharedRef<SWidget> MakeHintText(TAttribute<FText> Text)
    {
        return SNew(STextBlock)
            .Text(MoveTemp(Text))
            .AutoWrapText(true)
            .TextStyle(&Style::MutedTextStyle());
    }

    // ---------- 章节 / 列表 ----------

    /** 章节标题：左侧强调色细条 + 加粗小标题，右边可以挂一个控件（比如「删除」）。 */
    inline TSharedRef<SWidget> MakeSectionHeader(const FText& Label, TSharedRef<SWidget> RightContent = SNullWidget::NullWidget)
    {
        return SNew(SBorder)
            .BorderImage(Style::SectionHeaderBrush())
            .Padding(FMargin(0.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Fill)
                [
                    SNew(SBox)
                    .WidthOverride(3.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(Style::AccentBarBrush())
                        .Padding(0.0f)
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(FMargin(8.0f, 5.0f, 0.0f, 5.0f))
                [
                    SNew(STextBlock)
                    .Text(Label)
                    .TextStyle(&Style::SectionTextStyle())
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f)[ SNew(SSpacer) ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(FMargin(4.0f, 3.0f, 5.0f, 3.0f))
                [
                    RightContent
                ]
            ];
    }

    /** 色板：小的圆角色块，用来示意灯光颜色。 */
    inline TSharedRef<SWidget> MakeColorSwatch(TAttribute<FLinearColor> Color, float Width = 14.0f, float Height = 14.0f)
    {
        return SNew(SBox)
            .WidthOverride(Width)
            .HeightOverride(Height)
            .VAlign(VAlign_Center)
            [
                SNew(SBorder)
                .BorderImage(Style::RoundedFillBrush())
                .BorderBackgroundColor_Lambda([Color]()
                {
                    return FSlateColor(Color.Get(FLinearColor::White));
                })
                .Padding(0.0f)
            ];
    }

    /** 空状态：居中的一行灰字。 */
    inline TSharedRef<SWidget> MakeEmptyState(const FText& Text)
    {
        return SNew(SBox)
            .Padding(FMargin(4.0f, 10.0f))
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(Text)
                .AutoWrapText(true)
                .Justification(ETextJustify::Center)
                .TextStyle(&Style::MutedTextStyle())
            ];
    }

    /** 章节之间的一条细线。 */
    inline TSharedRef<SWidget> MakeRule()
    {
        return SNew(SBox)
            .HeightOverride(1.0f)
            [
                SNew(SBorder).BorderImage(Style::RuleBrush()).Padding(0.0f)
            ];
    }

    /**
     * 可选中列表行。
     *
     * 悬停/选中底、左侧颜色条、可选的删除按钮都在这里统一处理，
     * 调用方只需要塞内容进去。
     */
    class SShineAIPaintSelectableRow : public SBorder
    {
    public:
        SLATE_BEGIN_ARGS(SShineAIPaintSelectableRow) {}
            SLATE_DEFAULT_SLOT(FArguments, Content)
            SLATE_ATTRIBUTE(bool, IsSelected)
            SLATE_ATTRIBUTE(FLinearColor, AccentColor)
            SLATE_ARGUMENT(FSimpleDelegate, OnClicked)
            SLATE_ARGUMENT(FSimpleDelegate, OnRemove)
            SLATE_ARGUMENT(FText, RemoveTooltip)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs);

        virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
        virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
        virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

    private:
        const FSlateBrush* GetRowBrush() const;

        TAttribute<bool> IsSelected;
        TAttribute<FLinearColor> AccentColor;
        FSimpleDelegate OnClicked;
        bool bHovered = false;
    };

    /** UE 标准资产挑选框（带缩略图 + 浏览按钮 + 可从当前选中取）。 */
    inline TSharedRef<SWidget> MakeAssetPicker(UClass* AllowedClass, TAttribute<FString> ObjectPath, FOnSetObject OnObjectChanged)
    {
        return SNew(SObjectPropertyEntryBox)
            .AllowedClass(AllowedClass)
            .ObjectPath(MoveTemp(ObjectPath))
            .OnObjectChanged(MoveTemp(OnObjectChanged))
            .AllowClear(true)
            .DisplayBrowse(true)
            .DisplayUseSelected(true)
            .DisplayThumbnail(true);
    }

    /** 网格体挑选框：静态网格体或骨骼网格体都行。 */
    inline TSharedRef<SWidget> MakeMeshPicker(TAttribute<FString> ObjectPath, FOnSetObject OnObjectChanged)
    {
        return SNew(SObjectPropertyEntryBox)
            .AllowedClass(UObject::StaticClass())
            .ObjectPath(MoveTemp(ObjectPath))
            .OnObjectChanged(MoveTemp(OnObjectChanged))
            .OnShouldFilterAsset(FOnShouldFilterAsset::CreateLambda([](const FAssetData& AssetData)
            {
                // 返回 true 表示"过滤掉"。
                UClass* AssetClass = AssetData.GetClass();
                const bool bIsMesh = AssetClass
                    && (AssetClass->IsChildOf(UStaticMesh::StaticClass()) || AssetClass->IsChildOf(USkeletalMesh::StaticClass()));
                return !bIsMesh;
            }))
            .AllowClear(true)
            .DisplayBrowse(true)
            .DisplayUseSelected(true)
            .DisplayThumbnail(true);
    }

    /**
     * 画笔工具按钮。
     *
     * SButton 的 ButtonStyle 只接受常量指针（不是属性），而高亮态要跟着「当前工具」走，
     * 所以继承 SButton，在 Tick 里按需换一次样式。
     */
    class SShineAIPaintToolButton : public SButton
    {
    public:
        SLATE_BEGIN_ARGS(SShineAIPaintToolButton) {}
            SLATE_ARGUMENT(TSharedPtr<FShineAIPaintSession>, Session)
            SLATE_ARGUMENT(EShineAIPaintTool, Tool)
            SLATE_ARGUMENT(bool, bCool)
            SLATE_ARGUMENT(FText, Label)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs);

        virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

    private:
        bool IsActive() const;
        const FButtonStyle* SelectStyle() const;
        FSlateColor GetTextColor() const;
        FReply HandleClicked();

        TSharedPtr<FShineAIPaintSession> Session;
        EShineAIPaintTool Tool = EShineAIPaintTool::Paint;
        bool bCool = false;
        const FButtonStyle* CurrentStyle = nullptr;
    };

    /**
     * 画布区域的容器：把分配到的尺寸回报出去，用来算「适应窗口」的缩放。
     */
    class SShineAIPaintCanvasHost : public SBorder
    {
    public:
        SLATE_BEGIN_ARGS(SShineAIPaintCanvasHost) {}
            SLATE_DEFAULT_SLOT(FArguments, Content)
            SLATE_ARGUMENT(TFunction<void(const FVector2D&)>, OnAreaResized)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs);

        virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

    private:
        TFunction<void(const FVector2D&)> OnAreaResized;
        FVector2D LastReportedSize = FVector2D::ZeroVector;
    };
}
