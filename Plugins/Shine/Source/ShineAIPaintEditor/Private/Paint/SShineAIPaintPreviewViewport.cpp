#include "Paint/SShineAIPaintPreviewViewport.h"

#include "Animation/SkeletalMeshActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "InputKeyEventArgs.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Paint/ShineAIPaintMeshPicker.h"
#include "Paint/ShineAIPaintSession.h"
#include "PreviewScene.h"
#include "UObject/Package.h"

namespace
{
    /**
     * 网格体没材质时引擎给的是 WorldGridMaterial —— 一张网格贴图，
     * 在预览里会有明显摩尔纹、还会随时间抖动。这里换成干净的亚光灰材质。
     */
    UMaterialInterface* GetPreviewFallbackMaterial()
    {
        static TWeakObjectPtr<UMaterialInterface> CachedFallback;

        if (CachedFallback.IsValid())
        {
            return CachedFallback.Get();
        }

        // 只缓存「真的加载成功」的那一个。
        // 编辑器刚起来时 Engine 内容可能还没就绪，加载失败会退回 WorldGridMaterial；
        // 要是把那个也缓存下来，预览里就会一直留着网格摩尔纹。
        UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (Loaded)
        {
            CachedFallback = Loaded;
            return Loaded;
        }

        return UMaterial::GetDefaultMaterial(MD_Surface);
    }

    /** 上一次应用材质时是否只能用引擎网格材质顶替（说明备用材质还没就绪）。 */
    bool GPreviewMaterialUsedEngineDefault = false;

    /** 我们自己的预览材质里那个贴图参数的名字；接贴图时优先按它找。 */
    const FName GPreviewTextureParameterName(TEXT("ShineAIPaintTexture"));

    /**
     * 运行时拼一张预览材质：TextureSampleParameter2D → BaseColor，再接一个粗糙度常量。
     *
     * 引擎默认材质（WorldGridMaterial 带网格纹路、DefaultMaterial 压根没有贴图参数）
     * 都没法把「正在画的贴图」显示出来 —— 预览模型就会一直是灰的。
     * 这里自己造一张结构最简单、参数名固定的材质，接贴图时不用猜参数名。
     */
    UMaterial* BuildPreviewTextureMaterial()
    {
#if WITH_EDITOR
        UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
        if (!Material)
        {
            return nullptr;
        }

        UMaterialExpressionTextureSampleParameter2D* Sampler =
            NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        UMaterialExpressionConstant* Roughness = NewObject<UMaterialExpressionConstant>(Material);
        if (!Sampler || !Roughness)
        {
            return nullptr;
        }

        Sampler->ParameterName = GPreviewTextureParameterName;
        Sampler->Material = Material;
        Sampler->MaterialExpressionEditorX = -500;

        Roughness->R = 0.55f;
        Roughness->Material = Material;
        Roughness->MaterialExpressionEditorX = -300;
        Roughness->MaterialExpressionEditorY = 180;

        UMaterialEditorOnlyData* EditorOnly = Material->GetEditorOnlyData();
        if (!EditorOnly)
        {
            return nullptr;
        }

        Material->GetExpressionCollection().AddExpression(Sampler);
        Material->GetExpressionCollection().AddExpression(Roughness);

        EditorOnly->BaseColor.Connect(0, Sampler);
        EditorOnly->Roughness.Connect(0, Roughness);

        Material->PostEditChange();
        return Material;
#else
        return nullptr;
#endif
    }

    /**
     * 预览用的贴图材质：优先用自己造的这张（一定有贴图参数），
     * 造不出来（引擎内容还没就绪）才退回亚光灰。
     */
    UMaterialInterface* GetPreviewTextureMaterial()
    {
        static TStrongObjectPtr<UMaterial> CachedGenerated;

        if (CachedGenerated.IsValid())
        {
            return CachedGenerated.Get();
        }

        if (UMaterial* Generated = BuildPreviewTextureMaterial())
        {
            CachedGenerated.Reset(Generated);
            return Generated;
        }

        return GetPreviewFallbackMaterial();
    }

    /** 在组件的某个材质槽上挂一张贴图；成功返回 true。 */
    bool ApplyTextureToMaterialSlot(
        UPrimitiveComponent* Component,
        int32 MaterialIndex,
        UMaterialInterface* BaseMaterial,
        UTexture2D* Texture)
    {
        if (!BaseMaterial)
        {
            return false;
        }

        UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(BaseMaterial);
        if (!DynamicMaterial)
        {
            DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, Component);
        }

        if (!DynamicMaterial)
        {
            return false;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid> ParameterIds;
        DynamicMaterial->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
        if (ParameterInfos.Num() == 0)
        {
            return false;
        }

        // 用我们自己材质的参数名最保险；别人的材质就退回第一个贴图参数。
        FName TargetParameter = ParameterInfos[0].Name;
        for (const FMaterialParameterInfo& Info : ParameterInfos)
        {
            if (Info.Name == GPreviewTextureParameterName)
            {
                TargetParameter = Info.Name;
                break;
            }
        }

        DynamicMaterial->SetTextureParameterValue(TargetParameter, Texture);
        Component->SetMaterial(MaterialIndex, DynamicMaterial);
        return true;
    }

    /**
     * 把贴图接到组件的材质上。
     * 只作用在预览场景里我们自己 new 出来的组件上，不会碰关卡里的任何 Actor。
     */
    bool ApplyTextureToPreviewComponent(UPrimitiveComponent* Component, UTexture2D* Texture)
    {
        if (!Component || !Texture)
        {
            return false;
        }

        const int32 MaterialCount = FMath::Max(Component->GetNumMaterials(), 1);
        bool bAppliedAnyMaterial = false;

        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
        {
            UMaterialInterface* CurrentMaterial = Component->GetMaterial(MaterialIndex);

            // 引擎默认的材质必须最先换掉：网格材质会留摩尔纹，默认材质根本没有贴图参数。
            const bool bIsEngineDefault = !CurrentMaterial
                || CurrentMaterial == UMaterial::GetDefaultMaterial(MD_Surface)
                || CurrentMaterial->GetName().Contains(TEXT("WorldGridMaterial"));

            if (bIsEngineDefault)
            {
                CurrentMaterial = GetPreviewTextureMaterial();
                Component->SetMaterial(MaterialIndex, CurrentMaterial);

                if (CurrentMaterial == UMaterial::GetDefaultMaterial(MD_Surface))
                {
                    GPreviewMaterialUsedEngineDefault = true;
                }
            }

            // 1) 材质里有贴图参数：把当前画的那张贴图接上去。
            if (ApplyTextureToMaterialSlot(Component, MaterialIndex, CurrentMaterial, Texture))
            {
                bAppliedAnyMaterial = true;
                continue;
            }

            // 2) 接不上（比如模型自带一张没有贴图参数的材质）：
            //    换成我们自己的预览材质，参数名固定，一定能接上。
            UMaterialInterface* PreviewMaterial = GetPreviewTextureMaterial();
            if (PreviewMaterial
                && PreviewMaterial != CurrentMaterial
                && ApplyTextureToMaterialSlot(Component, MaterialIndex, PreviewMaterial, Texture))
            {
                bAppliedAnyMaterial = true;
            }
        }

        return bAppliedAnyMaterial;
    }
}

/**
 * 预览视口客户端。
 *
 * 打开"在模型上涂画"时用左键直接在模型上涂，右键/中键/Alt 仍然走引擎的相机操作。
 */
class FShineAIPaintViewportClient : public FEditorViewportClient
{
public:
    FShineAIPaintViewportClient(
        FPreviewScene* InPreviewScene,
        const TSharedRef<SEditorViewport>& InViewport,
        TWeakPtr<SShineAIPaintPreviewViewport> InOwnerWidget)
        : FEditorViewportClient(nullptr, InPreviewScene, InViewport)
        , OwnerWidget(MoveTemp(InOwnerWidget))
    {
        SetRealtime(true);

        // 预览给的是"看贴图/涂画"用的干净背景：地面网格在这种小视口里全是摩尔纹。
        EngineShowFlags.SetGrid(false);
        EngineShowFlags.SetSelection(false);
        EngineShowFlags.SetSelectionOutline(false);
        EngineShowFlags.SetGrain(false);

        // 关掉自动曝光与几项时域效果：预览场景里它们会让画面亮度来回跳（看着就是在闪）。
        EngineShowFlags.SetEyeAdaptation(false);
        EngineShowFlags.SetBloom(false);
        EngineShowFlags.SetMotionBlur(false);
        EngineShowFlags.SetScreenSpaceAO(false);
    }

    virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
    {
        // Alt + 左键是引擎的"环绕"操作，永远留给相机，不被涂画抢走。
        const bool bAltDown = FSlateApplication::Get().GetModifierKeys().IsAltDown();

        const TSharedPtr<SShineAIPaintPreviewViewport> Owner = OwnerWidget.Pin();
        if (Owner.IsValid()
            && !bAltDown
            && EventArgs.Key == EKeys::LeftMouseButton
            && Owner->IsModelPaintingEnabled())
        {
            if (EventArgs.Event == IE_Pressed)
            {
                bPainting = Owner->HandlePaintButtonPressed();
                if (bPainting)
                {
                    return true;
                }
            }
            else if (EventArgs.Event == IE_Released && bPainting)
            {
                bPainting = false;
                Owner->HandlePaintReleased();
                return true;
            }
        }

        return FEditorViewportClient::InputKey(EventArgs);
    }

    virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override
    {
        FEditorViewportClient::MouseMove(InViewport, X, Y);
        ContinuePaint();
    }

    virtual void CapturedMouseMove(FViewport* InViewport, int32 InMouseX, int32 InMouseY) override
    {
        FEditorViewportClient::CapturedMouseMove(InViewport, InMouseX, InMouseY);
        ContinuePaint();
    }

    virtual EMouseCursor::Type GetCursor(FViewport* InViewport, int32 X, int32 Y) override
    {
        if (const TSharedPtr<SShineAIPaintPreviewViewport> Owner = OwnerWidget.Pin())
        {
            if (Owner->IsModelPaintingEnabled())
            {
                return EMouseCursor::Crosshairs;
            }
        }

        return FEditorViewportClient::GetCursor(InViewport, X, Y);
    }

private:
    void ContinuePaint()
    {
        if (!bPainting)
        {
            return;
        }

        if (const TSharedPtr<SShineAIPaintPreviewViewport> Owner = OwnerWidget.Pin())
        {
            Owner->HandlePaintMoved();
        }
    }

    TWeakPtr<SShineAIPaintPreviewViewport> OwnerWidget;
    bool bPainting = false;
};

SShineAIPaintPreviewViewport::~SShineAIPaintPreviewViewport()
{
    if (Session.IsValid())
    {
        Session->OnStructureChanged.RemoveAll(this);
        Session->OnPixelsChanged.RemoveAll(this);
    }

    MeshPicker.Reset();
    SpawnedComponents.Reset();
    ViewportClient.Reset();
    PreviewScene.Reset();
}

void SShineAIPaintPreviewViewport::Construct(const FArguments& InArgs)
{
    Session = InArgs._Session;
    MeshPicker = MakeShared<FShineAIPaintMeshPicker>();

    FPreviewScene::ConstructionValues ConstructionValues;
    ConstructionValues.SetLightRotation(FRotator(-30.0f, 60.0f, 0.0f));
    ConstructionValues.SetLightBrightness(5.0f);
    ConstructionValues.SetSkyBrightness(1.6f);
    ConstructionValues.SetCreatePhysicsScene(false);
    ConstructionValues.SetTransactional(false);

    PreviewScene = MakeShared<FPreviewScene>(ConstructionValues);

    // 引擎自带的那盏定向光关掉：预览里的灯统一由「放置」面板管理。
    PreviewScene->SetLightBrightness(0.0f);
    AppliedSkyIntensity = 1.6f;

    if (Session.IsValid())
    {
        Session->EnsurePreviewLightsInitialized();
    }

    SEditorViewport::Construct(SEditorViewport::FArguments());

    if (Session.IsValid())
    {
        Session->OnStructureChanged.AddSP(this, &SShineAIPaintPreviewViewport::HandleStructureChanged);
        Session->OnPixelsChanged.AddSP(this, &SShineAIPaintPreviewViewport::ApplyPaintedTextureToPreview);
    }

    RebuildPreviewMeshes();

    // 构造阶段视口可能还没真正创建出来，靠 Tick 补一次相机对焦。
    SetCanTick(true);
}

void SShineAIPaintPreviewViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    if (bPendingCameraFocus)
    {
        FocusCameraOnMeshes();
    }

    // 视口第一次搭起来时 Engine 内容可能还没就绪，备用材质会退化成引擎网格材质。
    // 这里重试一小段时间：一拿到干净材质就重建一次材质，让预览恢复正常。
    if (GPreviewMaterialUsedEngineDefault)
    {
        if (GetPreviewTextureMaterial() != UMaterial::GetDefaultMaterial(MD_Surface))
        {
            GPreviewMaterialUsedEngineDefault = false;
            RebuildPreviewMeshes();
        }
    }

    SyncPreviewLights();
}

void SShineAIPaintPreviewViewport::SyncPreviewLights()
{
    if (!Session.IsValid() || !PreviewScene.IsValid())
    {
        return;
    }

    // 天光
    if (!FMath::IsNearlyEqual(Session->PreviewSkyIntensity, AppliedSkyIntensity, 0.001f))
    {
        AppliedSkyIntensity = Session->PreviewSkyIntensity;
        PreviewScene->SetSkyBrightness(AppliedSkyIntensity);
    }

    UWorld* PreviewWorld = PreviewScene->GetWorld();
    if (!PreviewWorld)
    {
        return;
    }

    // ---- 1) 会话里已经删掉的灯：把组件销毁 ----
    for (int32 Index = AppliedLights.Num() - 1; Index >= 0; --Index)
    {
        if (Session->PreviewLights.Contains(AppliedLights[Index].Source))
        {
            continue;
        }

        if (ULightComponent* Component = AppliedLights[Index].Component.Get())
        {
            Component->DestroyComponent();
        }

        AppliedLights.RemoveAt(Index);
    }

    // ---- 2) 新增 / 更新 ----
    for (const TSharedPtr<FShineAIPaintPreviewLight>& Light : Session->PreviewLights)
    {
        if (!Light.IsValid())
        {
            continue;
        }

        FAppliedLight* Applied = AppliedLights.FindByPredicate(
            [&Light](const FAppliedLight& Candidate) { return Candidate.Source == Light; });

        ULightComponent* Component = Applied ? Applied->Component.Get() : nullptr;

        if (!Component)
        {
            switch (Light->Type)
            {
            case EShineAIPaintLightType::Directional:
                Component = NewObject<UDirectionalLightComponent>(PreviewWorld, NAME_None, RF_Transient);
                break;
            case EShineAIPaintLightType::Spot:
                Component = NewObject<USpotLightComponent>(PreviewWorld, NAME_None, RF_Transient);
                break;
            default:
                Component = NewObject<UPointLightComponent>(PreviewWorld, NAME_None, RF_Transient);
                break;
            }

            if (!Component)
            {
                continue;
            }

            Component->SetMobility(EComponentMobility::Movable);
            PreviewScene->AddComponent(Component, FTransform(Light->Rotation, Light->Location));

            if (!Applied)
            {
                AppliedLights.Add(FAppliedLight());
                Applied = &AppliedLights.Last();
                Applied->Source = Light;
            }
            Applied->Component = Component;
        }

        if (!Applied)
        {
            continue;
        }

        if (Applied->bEnabled != Light->bEnabled)
        {
            Applied->bEnabled = Light->bEnabled;
            Component->SetVisibility(Applied->bEnabled);
        }

        if (!FMath::IsNearlyEqual(Applied->Intensity, Light->Intensity, 0.01f))
        {
            Applied->Intensity = Light->Intensity;
            Component->SetIntensity(Applied->Intensity);
        }

        if (!Applied->Color.Equals(Light->Color, 0.001f))
        {
            Applied->Color = Light->Color;
            Component->SetLightColor(Applied->Color);
        }

        if (!Applied->Location.Equals(Light->Location, 0.1f) || !Applied->Rotation.Equals(Light->Rotation, 0.05f))
        {
            Applied->Location = Light->Location;
            Applied->Rotation = Light->Rotation;
            Component->SetWorldLocationAndRotation(Applied->Location, Applied->Rotation);
        }
    }
}

TSharedRef<FEditorViewportClient> SShineAIPaintPreviewViewport::MakeEditorViewportClient()
{
    ViewportClient = MakeShared<FShineAIPaintViewportClient>(PreviewScene.Get(), SharedThis(this), SharedThis(this));
    return ViewportClient.ToSharedRef();
}

void SShineAIPaintPreviewViewport::BindCommands()
{
    SEditorViewport::BindCommands();
}

bool SShineAIPaintPreviewViewport::IsModelPaintingEnabled() const
{
    return Session.IsValid() && Session->bPaintOnModel;
}

void SShineAIPaintPreviewViewport::HandleStructureChanged()
{
    RebuildPreviewMeshes();
}

void SShineAIPaintPreviewViewport::RebuildPreviewMeshes()
{
    if (!Session.IsValid() || !PreviewScene.IsValid())
    {
        return;
    }

    // OnStructureChanged 也会因为"忙碌 / 状态文本"触发，目标没换就不重建，避免预览跳动。
    TArray<FWeakObjectPtr> NewSignature;
    for (const FShineAIPaintTarget& Target : Session->GetTargets())
    {
        NewSignature.Add(FWeakObjectPtr(Target.MeshAsset.Get()));
    }

    if (bHasBuiltPreview && NewSignature == BuiltTargetSignature)
    {
        return;
    }

    BuiltTargetSignature = MoveTemp(NewSignature);
    bHasBuiltPreview = true;

    PreviewBounds = FBox(ForceInit);
    LastAppliedPreviewTexture = nullptr;

    UWorld* PreviewWorld = PreviewScene->GetWorld();
    if (!PreviewWorld)
    {
        return;
    }

    const TArray<FShineAIPaintTarget>& Targets = Session->GetTargets();

    // 能原地换网格就原地换：槽位数量一样、而且每个槽位现有的组件类型和新资产对得上
    // （静态网格体对静态网格体组件、骨骼网格体对骨骼网格体组件）。
    // 这样换个预览网格不会新建 Actor，也就不会有旧模型残留在场景里。
    bool bCanReuseActors = SpawnedComponents.Num() > 0 && SpawnedComponents.Num() == Targets.Num();
    if (bCanReuseActors)
    {
        for (int32 Index = 0; Index < Targets.Num(); ++Index)
        {
            UPrimitiveComponent* Existing = SpawnedComponents[Index].Get();
            UObject* MeshAsset = Targets[Index].MeshAsset.Get();

            const bool bSameKind =
                (Existing && Cast<UStaticMesh>(MeshAsset) && Existing->IsA<UStaticMeshComponent>())
                || (Existing && Cast<USkeletalMesh>(MeshAsset) && Existing->IsA<USkeletalMeshComponent>());

            if (!bSameKind)
            {
                bCanReuseActors = false;
                break;
            }
        }
    }

    if (bCanReuseActors)
    {
        for (int32 Index = 0; Index < Targets.Num(); ++Index)
        {
            UPrimitiveComponent* Component = SpawnedComponents[Index].Get();
            UObject* MeshAsset = Targets[Index].MeshAsset.Get();

            if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset))
            {
                CastChecked<UStaticMeshComponent>(Component)->SetStaticMesh(StaticMesh);
            }
            else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset))
            {
                CastChecked<USkeletalMeshComponent>(Component)->SetSkeletalMeshAsset(SkeletalMesh);
            }

            Component->MarkRenderStateDirty();
            PreviewBounds += Component->CalcBounds(Component->GetComponentTransform()).GetBox();
        }
    }
    else
    {
        // 类型变了（或多 / 少了一个槽位）：把旧的 Actor 真的销毁掉再重建，
        // 否则旧模型会留在预览场景里和新的叠在一起。
        DestroyPreviewActors();

        for (const FShineAIPaintTarget& Target : Targets)
        {
            UObject* MeshAsset = Target.MeshAsset.Get();
            if (!MeshAsset)
            {
                continue;
            }

            FActorSpawnParameters SpawnInfo;
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            SpawnInfo.bNoFail = true;
            SpawnInfo.ObjectFlags = RF_Transient;

            if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshAsset))
            {
                AStaticMeshActor* PreviewActor = PreviewWorld->SpawnActor<AStaticMeshActor>(SpawnInfo);
                if (!PreviewActor)
                {
                    continue;
                }

                PreviewActor->SetActorEnableCollision(false);
                UStaticMeshComponent* Component = PreviewActor->GetStaticMeshComponent();
                if (!Component)
                {
                    continue;
                }

                Component->SetCanEverAffectNavigation(false);
                Component->SetMobility(EComponentMobility::Movable);
                Component->SetStaticMesh(StaticMesh);
                Component->MarkRenderStateDirty();

                SpawnedComponents.Add(TStrongObjectPtr<UPrimitiveComponent>(Component));
                PreviewBounds += Component->CalcBounds(Component->GetComponentTransform()).GetBox();
            }
            else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshAsset))
            {
                ASkeletalMeshActor* PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>(SpawnInfo);
                if (!PreviewActor)
                {
                    continue;
                }

                PreviewActor->SetActorEnableCollision(false);
                USkeletalMeshComponent* Component = PreviewActor->GetSkeletalMeshComponent();
                if (!Component)
                {
                    continue;
                }

                Component->SetCanEverAffectNavigation(false);
                Component->SetMobility(EComponentMobility::Movable);
                Component->SetSkeletalMeshAsset(SkeletalMesh);
                Component->MarkRenderStateDirty();

                SpawnedComponents.Add(TStrongObjectPtr<UPrimitiveComponent>(Component));
                PreviewBounds += Component->CalcBounds(Component->GetComponentTransform()).GetBox();
            }
        }
    }

    RebuildMeshPicker();
    ApplyPaintedTextureToPreview();
    FocusCameraOnMeshes();
}

void SShineAIPaintPreviewViewport::DestroyPreviewActors()
{
    UWorld* PreviewWorld = PreviewScene.IsValid() ? PreviewScene->GetWorld() : nullptr;
    if (PreviewWorld)
    {
        for (const TStrongObjectPtr<UPrimitiveComponent>& Component : SpawnedComponents)
        {
            if (UPrimitiveComponent* Primitive = Component.Get())
            {
                if (AActor* Owner = Primitive->GetOwner())
                {
                    PreviewWorld->DestroyActor(Owner);
                }
            }
        }
    }

    SpawnedComponents.Reset();
}

void SShineAIPaintPreviewViewport::RebuildMeshPicker()
{
    if (MeshPicker.IsValid() && Session.IsValid())
    {
        MeshPicker->Rebuild(*Session);
    }
}

void SShineAIPaintPreviewViewport::ApplyPaintedTextureToPreview()
{
    if (!Session.IsValid())
    {
        return;
    }

    UTexture2D* Texture = Session->GetBaseTexture();
    if (!Texture || Texture == LastAppliedPreviewTexture)
    {
        // 贴图内容走 UpdateTextureRegions 原地更新，只有换了贴图对象才需要重接材质。
        return;
    }

    for (const TStrongObjectPtr<UPrimitiveComponent>& Component : SpawnedComponents)
    {
        ApplyTextureToPreviewComponent(Component.Get(), Texture);
    }

    if (PreviewScene.IsValid())
    {
        PreviewScene->UpdateCaptureContents();
    }

    LastAppliedPreviewTexture = Texture;
}

void SShineAIPaintPreviewViewport::FocusCameraOnMeshes()
{
    if (!PreviewBounds.IsValid)
    {
        bPendingCameraFocus = false;
        return;
    }

    if (!ViewportClient.IsValid() || !ViewportClient->Viewport)
    {
        // 视口还没真正创建出来（拿不到 Viewport 就算不出相机），挂起，等 Tick 里补。
        bPendingCameraFocus = true;
        return;
    }

    // 布局还没跑完时视口尺寸是 0，这时算出来的相机距离会发散（画面全黑），等下一帧再来。
    const FIntPoint ViewportSize = ViewportClient->Viewport->GetSizeXY();
    if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
    {
        bPendingCameraFocus = true;
        return;
    }

    bPendingCameraFocus = false;

    // 自己摆相机：FocusViewportOnBox 会把相机放在 -X 方向，正好看到背光面（一片黑）。
    // 这里固定成右前上方的 3/4 视角，同时和默认光向配合，看到的是受光面。
    const FVector Center = PreviewBounds.GetCenter();
    const float Radius = FMath::Max(static_cast<float>(PreviewBounds.GetExtent().Size()), 10.0f);
    const float HalfFovRadians = FMath::DegreesToRadians(FMath::Clamp(ViewportClient->ViewFOV, 10.0f, 90.0f) * 0.5f);
    const FVector ViewDirection = FVector(-0.85f, -1.0f, 0.55f).GetSafeNormal();
    const float Distance = (Radius / FMath::Max(FMath::Sin(HalfFovRadians), 0.05f)) * 1.15f;

    ViewportClient->SetViewLocation(Center + ViewDirection * Distance);
    ViewportClient->SetViewRotation((-ViewDirection).Rotation());
    ViewportClient->Invalidate();
}

bool SShineAIPaintPreviewViewport::PickUVUnderCursor(FVector2D& OutUV)
{
    if (!ViewportClient.IsValid() || !ViewportClient->Viewport || !MeshPicker.IsValid() || MeshPicker->IsEmpty())
    {
        return false;
    }

    const FViewportCursorLocation CursorLocation = ViewportClient->GetCursorWorldLocationFromMousePos();

    FShineAIPaintHit Hit;
    if (!MeshPicker->Raycast(CursorLocation.GetOrigin(), CursorLocation.GetDirection(), Hit))
    {
        return false;
    }

    OutUV = Hit.UV;
    return true;
}

bool SShineAIPaintPreviewViewport::HandlePaintButtonPressed()
{
    if (!IsModelPaintingEnabled())
    {
        return false;
    }

    FVector2D UV = FVector2D::ZeroVector;
    if (!PickUVUnderCursor(UV))
    {
        // 没打到网格：不消费输入，让引擎照旧处理。
        return false;
    }

    bStrokeActive = true;
    LastPaintUV = UV;

    Session->BeginStroke();

    // 模型上的笔迹要顺着 UV 平铺补一份，跨 UV 缝才不会断。
    Session->PaintStrokeWrapped(UV, UV, Session->BrushRadiusUV, Session->BrushColor, Session->CurrentTool);
    return true;
}

void SShineAIPaintPreviewViewport::HandlePaintMoved()
{
    if (!bStrokeActive || !Session.IsValid())
    {
        return;
    }

    FVector2D UV = FVector2D::ZeroVector;
    if (!PickUVUnderCursor(UV))
    {
        // 拖出模型就当作抬笔；再回到模型上会重新起笔，避免拉出一条穿过模型的直线。
        bStrokeActive = false;
        Session->EndStroke();
        return;
    }

    Session->PaintStrokeWrapped(LastPaintUV, UV, Session->BrushRadiusUV, Session->BrushColor, Session->CurrentTool);
    LastPaintUV = UV;
}

void SShineAIPaintPreviewViewport::HandlePaintReleased()
{
    if (bStrokeActive && Session.IsValid())
    {
        // 抬笔才把整笔写进贴图。
        Session->EndStroke();
    }

    bStrokeActive = false;
}
