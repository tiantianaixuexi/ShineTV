#include "Module/ShineTextureEditorModule.h"

#include "Asset/ShineTextureAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "EdGraphUtilities.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "SGraphNode.h"
#include "ShaderCore.h"

namespace
{
    class FShineTextureNodeFactory final : public FGraphPanelNodeFactory
    {
    public:
        virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override
        {
            if (UShineTextureGraphNodeBase* TextureNode = Cast<UShineTextureGraphNodeBase>(Node))
            {
                return TextureNode->CreateVisualWidget();
            }

            return nullptr;
        }
    };
}

void FShineTextureEditorModule::StartupModule()
{
    
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Shine")))
    {
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/Shine/Source/ShineTexture"), FPaths::Combine(Plugin->GetBaseDir()+"\\Source\\ShineTexture\\", TEXT("Shaders")));
    }

    PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FShineTextureEditorModule::RegisterEditorFeatures);
}

void FShineTextureEditorModule::RegisterEditorFeatures()
{
    if (PostEngineInitHandle.IsValid())
    {
        FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
        PostEngineInitHandle.Reset();
    }

    GraphNodeFactory = MakeShared<FShineTextureNodeFactory>();
    FEdGraphUtilities::RegisterVisualNodeFactory(GraphNodeFactory);
    RegisterAssetTypeActions();
}

void FShineTextureEditorModule::ShutdownModule()
{
    if (PostEngineInitHandle.IsValid())
    {
        FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
        PostEngineInitHandle.Reset();
    }

    UnregisterAssetTypeActions();

    if (GraphNodeFactory.IsValid())
    {
        FEdGraphUtilities::UnregisterVisualNodeFactory(GraphNodeFactory);
        GraphNodeFactory.Reset();
    }
}

void FShineTextureEditorModule::RegisterAssetTypeActions()
{
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    RegisteredAssetCategory = AssetTools.RegisterAdvancedAssetCategory(
        TEXT("ShineTexture"),
        NSLOCTEXT("FShineTextureEditorModule", "ShineTextureCategory", "Shine Texture"));

    TSharedRef<IAssetTypeActions> AssetTypeActions = MakeShared<FShineTextureAssetTypeActions>(RegisteredAssetCategory);
    AssetTools.RegisterAssetTypeActions(AssetTypeActions);
    RegisteredAssetTypeActions.Add(AssetTypeActions);
}

void FShineTextureEditorModule::UnregisterAssetTypeActions()
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


IMPLEMENT_MODULE(FShineTextureEditorModule, ShineTextureEditor)