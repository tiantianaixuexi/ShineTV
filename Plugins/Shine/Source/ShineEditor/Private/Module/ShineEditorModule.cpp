#include "Module/ShineEditorModule.h"

#include "Asset/ShineCharacterAssetTypeActions.h"
#include "Asset/ShineComfyAssetTypeActions.h"
#include "Asset/ShineVideoProjectTypeActions.h"
#include "AssetToolsModule.h"
#include "Brushes/SlateImageBrush.h"
#include "Comfy/ShineComfySocket.h"
#include "Comfy/ShineVideoTaskConsole.h"
#include "EdGraphUtilities.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Pin/SShineComfyGraphStandardPin.h"
#include "Graph/ShineComfyGraphSchema.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "SGraphNode.h"
#include "SGraphPin.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FShineEditorModule"

namespace
{
    class FShineGraphNodeFactory final : public FGraphPanelNodeFactory
    {
    public:
        virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override
        {
            if (UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(Node))
            {
                return ShineNode->CreateVisualWidget();
            }

            return nullptr;
        }
    };
    class FShineGraphPinFactory final : public FGraphPanelPinFactory
    {
    public:
        virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
        {
            if (Pin && Pin->GetSchema() && Pin->GetSchema()->IsA<UShineComfyGraphSchema>())
            {
                return SNew(SShineComfyGraphStandardPin, Pin);
            }

            return nullptr;
        }
    };
}

void FShineEditorModule::StartupModule()
{
    RegisterStyle();

    GraphNodeFactory = MakeShared<FShineGraphNodeFactory>();
    GraphPinFactory = MakeShared<FShineGraphPinFactory>();
    FEdGraphUtilities::RegisterVisualNodeFactory(GraphNodeFactory);
    FEdGraphUtilities::RegisterVisualPinFactory(GraphPinFactory);
    RegisterAssetTypeActions();

    // 视频工作台的无 UI 入口（Shine.H3.*）：面板坏掉时靠它分清是链路还是界面出了问题。
    FShineVideoTaskConsole::Register();

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FShineEditorModule::RegisterMenus));
}

void FShineEditorModule::ShutdownModule()
{
    // 那条共用的 ComfyUI 常连是本模块提供的，卸载时由本模块关掉；
    // 下游（比如 ShineAIPaint 的 AI 贴图）只是订阅它，不负责生命周期。
    // 先撤命令再关连接：命令的收尾路径里还会用到 ComfyUI 那条常连。
    FShineVideoTaskConsole::Unregister();

    FShineComfySocket::Get().Shutdown();

    if (UToolMenus::TryGet())
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }

    UnregisterAssetTypeActions();

    if (GraphPinFactory.IsValid())
    {
        FEdGraphUtilities::UnregisterVisualPinFactory(GraphPinFactory);
        GraphPinFactory.Reset();
    }

    if (GraphNodeFactory.IsValid())
    {
        FEdGraphUtilities::UnregisterVisualNodeFactory(GraphNodeFactory);
        GraphNodeFactory.Reset();
    }

    UnregisterStyle();
}

void FShineEditorModule::RegisterStyle()
{
    if (StyleSet.IsValid())
    {
        return;
    }

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Shine"));
    if (!Plugin.IsValid())
    {
        return;
    }

    const FString ResourcesDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"));
    const FString IconPath = FPaths::Combine(ResourcesDir, TEXT("Icon128.png"));

    StyleSet = MakeShared<FSlateStyleSet>(TEXT("ShineEditorStyle"));
    StyleSet->SetContentRoot(ResourcesDir);

    // 三个资产类共用同一张 Icon128：内容浏览器里靠类型色（蓝/青绿/橙）区分，不必再塞图标。
    const TCHAR* const AssetClassNames[] =
    {
        TEXT("ShineComfyAsset"),
        TEXT("ShineVideoProject"),
        TEXT("ShineCharacterAsset")
    };

    for (const TCHAR* ClassName : AssetClassNames)
    {
        StyleSet->Set(FName(FString::Printf(TEXT("ClassIcon.%s"), ClassName)), new FSlateImageBrush(IconPath, FVector2D(20.0f, 20.0f)));
        StyleSet->Set(FName(FString::Printf(TEXT("ClassThumbnail.%s"), ClassName)), new FSlateImageBrush(IconPath, FVector2D(64.0f, 64.0f)));
    }

    FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FShineEditorModule::UnregisterStyle()
{
    if (!StyleSet.IsValid())
    {
        return;
    }

    FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
    ensure(StyleSet.IsUnique());
    StyleSet.Reset();
}

void FShineEditorModule::RegisterAssetTypeActions()
{
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    RegisteredAssetCategory = AssetTools.RegisterAdvancedAssetCategory(
        TEXT("Shine"),
        LOCTEXT("ShineAssetCategory", "Shine"));

    TSharedRef<IAssetTypeActions> ComfyAssetTypeActions = MakeShared<FShineComfyAssetTypeActions>(RegisteredAssetCategory);
    AssetTools.RegisterAssetTypeActions(ComfyAssetTypeActions);
    RegisteredAssetTypeActions.Add(ComfyAssetTypeActions);

    // 视频工作台的两类资产：项目（分镜表）与角色（跨镜头身份一致的参考图集合）。
    TSharedRef<IAssetTypeActions> VideoProjectTypeActions = MakeShared<FShineVideoProjectTypeActions>(RegisteredAssetCategory);
    AssetTools.RegisterAssetTypeActions(VideoProjectTypeActions);
    RegisteredAssetTypeActions.Add(VideoProjectTypeActions);

    TSharedRef<IAssetTypeActions> CharacterTypeActions = MakeShared<FShineCharacterAssetTypeActions>(RegisteredAssetCategory);
    AssetTools.RegisterAssetTypeActions(CharacterTypeActions);
    RegisteredAssetTypeActions.Add(CharacterTypeActions);
}

void FShineEditorModule::UnregisterAssetTypeActions()
{
    if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
    {
        RegisteredAssetTypeActions.Reset();
        return;
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    for (const TSharedPtr<IAssetTypeActions>& AssetTypeActions : RegisteredAssetTypeActions)
    {
        if (AssetTypeActions.IsValid())
        {
            AssetTools.UnregisterAssetTypeActions(AssetTypeActions.ToSharedRef());
        }
    }

    RegisteredAssetTypeActions.Reset();
}

void FShineEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShineEditorModule, ShineEditor)