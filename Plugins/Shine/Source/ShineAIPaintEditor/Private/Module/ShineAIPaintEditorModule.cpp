#include "Module/ShineAIPaintEditorModule.h"

#include "Asset/ShineAIPaintAsset.h"
#include "Asset/ShineAIPaintAssetFactory.h"
#include "Asset/ShineAIPaintAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FShineAIPaintEditorModule"

void FShineAIPaintEditorModule::StartupModule()
{
    RegisterStyle();
    RegisterAssetTypeActions();

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FShineAIPaintEditorModule::RegisterMenus));
}

void FShineAIPaintEditorModule::ShutdownModule()
{
    if (UToolMenus::TryGet())
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }

    UnregisterAssetTypeActions();
    UnregisterStyle();
}

void FShineAIPaintEditorModule::RegisterStyle()
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

    StyleSet = MakeShared<FSlateStyleSet>(TEXT("ShineAIPaintEditorStyle"));
    StyleSet->SetContentRoot(ResourcesDir);
    StyleSet->Set("ClassIcon.ShineAIPaintAsset", new FSlateImageBrush(IconPath, FVector2D(20.0f, 20.0f)));
    StyleSet->Set("ClassThumbnail.ShineAIPaintAsset", new FSlateImageBrush(IconPath, FVector2D(64.0f, 64.0f)));
    FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FShineAIPaintEditorModule::UnregisterStyle()
{
    if (!StyleSet.IsValid())
    {
        return;
    }

    FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
    ensure(StyleSet.IsUnique());
    StyleSet.Reset();
}

void FShineAIPaintEditorModule::RegisterAssetTypeActions()
{
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    RegisteredAssetCategory = AssetTools.RegisterAdvancedAssetCategory(
        TEXT("Shine"),
        LOCTEXT("ShineAssetCategory", "Shine"));

    TSharedRef<IAssetTypeActions> AIPaintAssetTypeActions = MakeShared<FShineAIPaintAssetTypeActions>(RegisteredAssetCategory);
    AssetTools.RegisterAssetTypeActions(AIPaintAssetTypeActions);
    RegisteredAssetTypeActions.Add(AIPaintAssetTypeActions);
}

void FShineAIPaintEditorModule::UnregisterAssetTypeActions()
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

void FShineAIPaintEditorModule::CreateAIPaintAsset()
{
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    // bEditAfterNew = true，创建完会自动打开 AI 贴图编辑器。
    UFactory* Factory = NewObject<UShineAIPaintAssetFactory>();
    AssetTools.CreateAssetWithDialog(UShineAIPaintAsset::StaticClass(), Factory);
}

void FShineAIPaintEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    auto CreateAIPaintAssetAction = FExecuteAction::CreateRaw(this, &FShineAIPaintEditorModule::CreateAIPaintAsset);

    UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    FToolMenuSection& WindowSection = WindowMenu->FindOrAddSection("WindowLayout");
    WindowSection.AddMenuEntry(
        "ShineAIPaintAsset",
        LOCTEXT("ShineAIPaintAssetLabel", "AI 贴图（新建资产）"),
        LOCTEXT("ShineAIPaintAssetTooltip", "新建一个「Shine AI 贴图」资产并打开：指认网格体、手动画贴图、用遮罩保护区域、让 AI 更新贴图"),
        FSlateIcon(),
        FUIAction(CreateAIPaintAssetAction));

    UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection("Shine");
    ToolsSection.AddMenuEntry(
        "ShineAIPaintAssetTools",
        LOCTEXT("ShineAIPaintAssetToolsLabel", "新建 AI 贴图资产"),
        LOCTEXT("ShineAIPaintAssetToolsTooltip", "新建一个「Shine AI 贴图」资产并打开"),
        FSlateIcon(),
        FUIAction(CreateAIPaintAssetAction));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShineAIPaintEditorModule, ShineAIPaintEditor)
