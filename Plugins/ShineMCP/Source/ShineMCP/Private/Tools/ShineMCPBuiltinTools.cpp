#include "Tools/ShineMCPBuiltinTools.h"

#include "Asset/ShineComfyAsset.h"
#include "Capture/ShineCapturePreview.h"
#include "Capture/ShineSceneCapture.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Graph/ShineComfyGraph.h"
#include "Graph/Slate/ShineImageZoom.h"
#include "HAL/FileManager.h"
#include "IPythonScriptPlugin.h"
#include "ImageUtils.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SEditorViewport.h"
#include "ShineComfyBridgeLibrary.h"
#include "ShineMCPPrivate.h"
#include "ShineMCPSettings.h"
#include "ShineMCPToolRegistry.h"
#include "ShineSceneCaptureLibrary.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"

namespace
{
    // ------------------------------------------------------------------ 小工具

    FShineMCPResult FromJsonText(const FString& Text)
    {
        TSharedPtr<FJsonObject> Object;
        if (ShineMCPJson::Parse(Text, Object) && Object.IsValid())
        {
            const bool bSuccess = ShineMCPJson::GetBool(Object, TEXT("success"), true);
            return bSuccess ? FShineMCPResult::Ok(Text) : FShineMCPResult::Fail(Text);
        }

        return FShineMCPResult::Ok(Text);
    }

    UEditorActorSubsystem* GetActorSubsystem()
    {
        return GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    }

    ULevelEditorSubsystem* GetLevelSubsystem()
    {
        return GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
    }

    TArray<AActor*> GetAllEditorActors()
    {
        if (UEditorActorSubsystem* Subsystem = GetActorSubsystem())
        {
            return Subsystem->GetAllLevelActors();
        }
        return TArray<AActor*>();
    }

    AActor* FindEditorActor(const FString& LabelOrName)
    {
        if (LabelOrName.IsEmpty())
        {
            return nullptr;
        }

        for (AActor* Actor : GetAllEditorActors())
        {
            if (!Actor)
            {
                continue;
            }

            if (Actor->GetActorLabel().Equals(LabelOrName, ESearchCase::IgnoreCase) ||
                Actor->GetName().Equals(LabelOrName, ESearchCase::IgnoreCase) ||
                Actor->GetPathName().Equals(LabelOrName, ESearchCase::IgnoreCase))
            {
                return Actor;
            }
        }
        return nullptr;
    }

    /** 支持短名（PointLight）与完整路径（/Script/Engine.PointLight）。 */
    UClass* ResolveActorClass(const FString& InName)
    {
        FString Name = InName;
        Name.TrimStartAndEndInline();
        if (Name.IsEmpty())
        {
            return nullptr;
        }

        if (Name.StartsWith(TEXT("/")))
        {
            return LoadObject<UClass>(nullptr, *Name);
        }

        const TArray<FString> Candidates = {
            FString::Printf(TEXT("/Script/Engine.%s"), *Name),
            FString::Printf(TEXT("/Script/Engine.A%s"), *Name),
            FString::Printf(TEXT("/Script/Engine.%sActor"), *Name)
        };

        for (const FString& Candidate : Candidates)
        {
            if (UClass* Found = LoadObject<UClass>(nullptr, *Candidate))
            {
                return Found;
            }
        }

        if (!Name.StartsWith(TEXT("A")))
        {
            return ResolveActorClass(TEXT("A") + Name);
        }

        return nullptr;
    }

    bool SetBoolProperty(UObject* Object, const FName& PropertyName, bool bValue)
    {
        if (!Object)
        {
            return false;
        }

        if (FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName))
        {
            Property->SetPropertyValue_InContainer(Object, bValue);
            return true;
        }
        return false;
    }

    FVector ReadVector(const TSharedPtr<FJsonObject>& Args, const FString& Field, const FVector& Default)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Args.IsValid() && Args->TryGetArrayField(Field, Array) && Array && Array->Num() >= 3)
        {
            double X = Default.X;
            double Y = Default.Y;
            double Z = Default.Z;
            (*Array)[0]->TryGetNumber(X);
            (*Array)[1]->TryGetNumber(Y);
            (*Array)[2]->TryGetNumber(Z);
            return FVector(X, Y, Z);
        }
        return Default;
    }

    FRotator ReadRotator(const TSharedPtr<FJsonObject>& Args, const FString& Field, const FRotator& Default)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Args.IsValid() && Args->TryGetArrayField(Field, Array) && Array && Array->Num() >= 3)
        {
            double P = Default.Pitch;
            double Y = Default.Yaw;
            double R = Default.Roll;
            (*Array)[0]->TryGetNumber(P);
            (*Array)[1]->TryGetNumber(Y);
            (*Array)[2]->TryGetNumber(R);
            return FRotator(P, Y, R);
        }
        return Default;
    }

    FLinearColor ReadColor(const TSharedPtr<FJsonObject>& Args, const FString& Field, const FLinearColor& Default)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Args.IsValid() && Args->TryGetArrayField(Field, Array) && Array && Array->Num() >= 3)
        {
            double R = Default.R;
            double G = Default.G;
            double B = Default.B;
            (*Array)[0]->TryGetNumber(R);
            (*Array)[1]->TryGetNumber(G);
            (*Array)[2]->TryGetNumber(B);
            return FLinearColor(R, G, B, 1.0f);
        }
        return Default;
    }

    void AddTool(
        const FString& Name,
        const FString& Title,
        const FString& Description,
        const TSharedPtr<FJsonObject>& Schema,
        TFunction<FShineMCPResult(const TSharedPtr<FJsonObject>&)> Handler)
    {
        FShineMCPTool Tool;
        Tool.Name = Name;
        Tool.Title = Title;
        Tool.Description = Description;
        Tool.InputSchema = Schema.IsValid() ? Schema : ShineMCPSchema::Object();
        Tool.Handler = MoveTemp(Handler);
        FShineMCPToolRegistry::Get().RegisterTool(Tool);
    }

    FString GetBaseUrlArg(const TSharedPtr<FJsonObject>& Args)
    {
        FString BaseUrl = ShineMCPJson::GetString(Args, TEXT("baseUrl"));
        if (BaseUrl.IsEmpty())
        {
            const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
            BaseUrl = Settings ? Settings->ComfyBaseUrl : TEXT("http://127.0.0.1:8188");
        }
        return BaseUrl;
    }

    /** 找到一个可以摆进场景的网格 + 材质。 */
    UStaticMesh* LoadBasicShape(const FString& ShapeName)
    {
        const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), *ShapeName, *ShapeName);
        return LoadObject<UStaticMesh>(nullptr, *Path);
    }

    UMaterialInterface* LoadBasicShapeMaterial()
    {
        return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    }

    AActor* SpawnMeshActor(const FString& Label, const FString& Shape, const FVector& Location, const FVector& Scale, const FRotator& Rotation)
    {
        UEditorActorSubsystem* Subsystem = GetActorSubsystem();
        if (!Subsystem)
        {
            return nullptr;
        }

        AActor* Actor = Subsystem->SpawnActorFromClass(AStaticMeshActor::StaticClass(), Location, Rotation);
        if (!Actor)
        {
            return nullptr;
        }

        Actor->SetActorLabel(Label);
        Actor->SetActorScale3D(Scale);

        if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
        {
            UStaticMeshComponent* MeshComponent = MeshActor->GetStaticMeshComponent();
            if (MeshComponent)
            {
                MeshComponent->SetMobility(EComponentMobility::Movable);
                if (UStaticMesh* Mesh = LoadBasicShape(Shape))
                {
                    MeshComponent->SetStaticMesh(Mesh);
                }
                if (UMaterialInterface* Material = LoadBasicShapeMaterial())
                {
                    MeshComponent->SetMaterial(0, Material);
                }
            }
        }

        return Actor;
    }

    AActor* SpawnLightActor(const FString& ClassName, const FString& Label, const FVector& Location, const FRotator& Rotation, const FLinearColor& Color, float Intensity)
    {
        UEditorActorSubsystem* Subsystem = GetActorSubsystem();
        UClass* Class = ResolveActorClass(ClassName);
        if (!Subsystem || !Class)
        {
            return nullptr;
        }

        AActor* Actor = Subsystem->SpawnActorFromClass(Class, Location, Rotation);
        if (!Actor)
        {
            return nullptr;
        }

        Actor->SetActorLabel(Label);

        if (ULightComponent* LightComponent = Actor->FindComponentByClass<ULightComponent>())
        {
            LightComponent->SetMobility(EComponentMobility::Movable);
            LightComponent->SetLightColor(Color);
            LightComponent->SetIntensity(Intensity);
        }

        return Actor;
    }

    /** ShineAI 演示场景：地面 + 若干几何体 + 移动端光照 + 彩色点光。 */
    FString BuildDemoSceneInternal(bool bClearExisting)
    {
        if (!GetActorSubsystem())
        {
            return ShineMCPJson::ToText(ShineMCPJson::Error(TEXT("编辑器 Actor 子系统不可用。")));
        }

        int32 RemovedCount = 0;
        if (bClearExisting)
        {
            TArray<AActor*> ToRemove;
            for (AActor* Actor : GetAllEditorActors())
            {
                if (Actor && Actor->GetActorLabel().StartsWith(TEXT("ShineDemo_")))
                {
                    ToRemove.Add(Actor);
                }
            }
            if (ToRemove.Num() > 0)
            {
                GetActorSubsystem()->DestroyActors(ToRemove);
                RemovedCount = ToRemove.Num();
            }
        }

        TArray<TSharedPtr<FJsonValue>> Spawned;

        auto Record = [&Spawned](AActor* Actor)
        {
            if (Actor)
            {
                Spawned.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
            }
        };

        // ---- 地面（够大即可：太小会看到棋盘边缘，太大又白白拖慢 Lumen/阴影） ----
        Record(SpawnMeshActor(TEXT("ShineDemo_Ground"), TEXT("Plane"), FVector(0.0f, 0.0f, 0.0f),
            FVector(50.0f, 50.0f, 1.0f), FRotator::ZeroRotator));

        // ---- 几何体集群（做出高低错落的“玩具城”轮廓，便于深度/法线看出结构） ----
        Record(SpawnMeshActor(TEXT("ShineDemo_Cube_A"), TEXT("Cube"), FVector(-320.0f, 60.0f, 150.0f),
            FVector(3.0f, 3.0f, 3.0f), FRotator(0.0f, 18.0f, 0.0f)));
        Record(SpawnMeshActor(TEXT("ShineDemo_Cube_B"), TEXT("Cube"), FVector(-120.0f, -260.0f, 220.0f),
            FVector(4.4f, 4.4f, 4.4f), FRotator(0.0f, -25.0f, 0.0f)));
        Record(SpawnMeshActor(TEXT("ShineDemo_Cylinder_A"), TEXT("Cylinder"), FVector(180.0f, 180.0f, 260.0f),
            FVector(2.6f, 2.6f, 5.2f), FRotator::ZeroRotator));
        Record(SpawnMeshActor(TEXT("ShineDemo_Sphere_A"), TEXT("Sphere"), FVector(30.0f, -90.0f, 150.0f),
            FVector(3.2f, 3.2f, 3.2f), FRotator::ZeroRotator));
        Record(SpawnMeshActor(TEXT("ShineDemo_Sphere_B"), TEXT("Sphere"), FVector(420.0f, -180.0f, 110.0f),
            FVector(2.2f, 2.2f, 2.2f), FRotator::ZeroRotator));
        Record(SpawnMeshActor(TEXT("ShineDemo_Cone_A"), TEXT("Cone"), FVector(-460.0f, -220.0f, 130.0f),
            FVector(2.6f, 2.6f, 2.6f), FRotator(0.0f, 40.0f, 0.0f)));

        // ---- 光照（全部 Movable，避免需要烘焙） ----
        Record(SpawnLightActor(TEXT("DirectionalLight"), TEXT("ShineDemo_Sun"),
            FVector(0.0f, 0.0f, 1200.0f), FRotator(-42.0f, 35.0f, 0.0f),
            FLinearColor(1.0f, 0.94f, 0.85f), 6.0f));

        if (AActor* SunActor = FindEditorActor(TEXT("ShineDemo_Sun")))
        {
            SetBoolProperty(SunActor, TEXT("bAtmosphereSunLight"), true);
            if (ULightComponent* LightComponent = SunActor->FindComponentByClass<ULightComponent>())
            {
                SetBoolProperty(LightComponent, TEXT("bAtmosphereSunLight"), true);
            }
        }

        // 彩色的点光，让"颜色"通道更有辨识度（红/绿/蓝三色补光）
        Record(SpawnLightActor(TEXT("PointLight"), TEXT("ShineDemo_Point_Red"),
            FVector(-380.0f, -320.0f, 420.0f), FRotator::ZeroRotator, FLinearColor(1.0f, 0.18f, 0.12f), 4000.0f));
        Record(SpawnLightActor(TEXT("PointLight"), TEXT("ShineDemo_Point_Green"),
            FVector(120.0f, 380.0f, 360.0f), FRotator::ZeroRotator, FLinearColor(0.15f, 1.0f, 0.35f), 3400.0f));
        Record(SpawnLightActor(TEXT("PointLight"), TEXT("ShineDemo_Point_Blue"),
            FVector(460.0f, -120.0f, 320.0f), FRotator::ZeroRotator, FLinearColor(0.2f, 0.45f, 1.0f), 3800.0f));

        // 天光：实时捕获，不需要烘焙
        if (AActor* SkyLightActor = SpawnLightActor(TEXT("SkyLight"), TEXT("ShineDemo_SkyLight"),
            FVector(0.0f, 0.0f, 900.0f), FRotator::ZeroRotator, FLinearColor::White, 1.0f))
        {
            Record(SkyLightActor);

            if (USkyLightComponent* SkyLightComponent = SkyLightActor->FindComponentByClass<USkyLightComponent>())
            {
                SkyLightComponent->SourceType = ESkyLightSourceType::SLS_CapturedScene;
                // 关掉实时捕获：开启后编辑器每帧都要额外渲染一次天空，交互会明显变卡。
                // 这里只捕获一次，对截图/捕获场景完全够用。
                SkyLightComponent->bRealTimeCapture = false;
                SkyLightComponent->SetMobility(EComponentMobility::Movable);
                SkyLightComponent->RecaptureSky();
            }
        }

        // 大气 + 雾，给颜色通道一点氛围
        if (UClass* AtmosphereClass = ResolveActorClass(TEXT("SkyAtmosphere")))
        {
            if (AActor* Atmosphere = GetActorSubsystem()->SpawnActorFromClass(AtmosphereClass, FVector::ZeroVector, FRotator::ZeroRotator))
            {
                Atmosphere->SetActorLabel(TEXT("ShineDemo_SkyAtmosphere"));
                Record(Atmosphere);
            }
        }

        if (UClass* FogClass = ResolveActorClass(TEXT("ExponentialHeightFog")))
        {
            if (AActor* Fog = GetActorSubsystem()->SpawnActorFromClass(FogClass, FVector(0.0f, 0.0f, 200.0f), FRotator::ZeroRotator))
            {
                Fog->SetActorLabel(TEXT("ShineDemo_Fog"));
                Record(Fog);
            }
        }

        // 后处理体积（无边界），关掉自动曝光，避免截图忽明忽暗
        if (UClass* VolumeClass = ResolveActorClass(TEXT("PostProcessVolume")))
        {
            if (AActor* Volume = GetActorSubsystem()->SpawnActorFromClass(VolumeClass, FVector(0.0f, 0.0f, 300.0f), FRotator::ZeroRotator))
            {
                Volume->SetActorLabel(TEXT("ShineDemo_PostProcess"));
                SetBoolProperty(Volume, TEXT("bUnbound"), true);
                SetBoolProperty(Volume, TEXT("bEnabled"), true);
                Record(Volume);
            }
        }

        TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
        Out->SetNumberField(TEXT("spawned"), Spawned.Num());
        Out->SetNumberField(TEXT("removed"), RemovedCount);
        Out->SetArrayField(TEXT("actors"), Spawned);
        return ShineMCPJson::ToText(Out);
    }
}

// ============================================================================
//  注册
// ============================================================================

void FShineMCPBuiltinTools::RegisterAll()
{
    FShineMCPToolRegistry& Registry = FShineMCPToolRegistry::Get();

    // ---------------------------------------------------------------- 基础

    AddTool(TEXT("ue_ping"), TEXT("UE 探活"), TEXT("确认 Unreal 编辑器侧的 MCP 服务可用。"),
        ShineMCPSchema::Object(),
        [](const TSharedPtr<FJsonObject>&)
        {
            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
            Out->SetStringField(TEXT("engine"), TEXT("Unreal Engine 5"));
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_get_editor_state"), TEXT("编辑器状态"),
        TEXT("返回当前关卡、关卡内 Actor 数量、编辑器视口相机位姿。"),
        ShineMCPSchema::Object(),
        [](const TSharedPtr<FJsonObject>&)
        {
            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();

            UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (World)
            {
                Out->SetStringField(TEXT("level"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
                Out->SetNumberField(TEXT("actorCount"), GetAllEditorActors().Num());
            }

            TSharedPtr<FJsonObject> Camera;
            ShineMCPJson::Parse(UShineSceneCaptureLibrary::GetEditorViewportCamera(), Camera);
            Out->SetObjectField(TEXT("viewportCamera"), Camera);

            const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
            Out->SetStringField(TEXT("comfyBaseUrl"), Settings ? Settings->ComfyBaseUrl : TEXT(""));
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_run_python"), TEXT("执行 Python"),
        TEXT("在编辑器里执行一段 Python 代码（unreal 模块可用），返回执行结果。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("code") });
            ShineMCPSchema::AddString(Schema, TEXT("code"), TEXT("要执行的 Python 代码"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FString Code = ShineMCPJson::GetString(Args, TEXT("code"));
            if (Code.IsEmpty())
            {
                return FShineMCPResult::Fail(TEXT("{\"success\":false,\"error\":\"code 不能为空\"}"));
            }

            IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
            if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
            {
                TSharedPtr<FJsonObject> Out = ShineMCPJson::Error(TEXT("Python 不可用，请确认 PythonScriptPlugin 已启用。"));
                return FShineMCPResult::FailJson(Out);
            }

            FPythonCommandEx Command;
            Command.Command = Code.EndsWith(TEXT("\n")) ? Code : Code + TEXT("\n");
            // ExecuteFile 走多语句编译，脚本里可以放心写循环/函数定义。
            Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;

            const bool bExecuted = PythonPlugin->ExecPythonCommandEx(Command);

            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
            Out->SetBoolField(TEXT("executed"), bExecuted);
            Out->SetStringField(TEXT("commandResult"), Command.CommandResult);
            if (!Command.LogOutput.IsEmpty())
            {
                TArray<TSharedPtr<FJsonValue>> Logs;
                for (const FPythonLogOutputEntry& Entry : Command.LogOutput)
                {
                    Logs.Add(MakeShared<FJsonValueString>(Entry.Output));
                }
                Out->SetArrayField(TEXT("log"), Logs);
            }
            return bExecuted ? FShineMCPResult::OkJson(Out) : FShineMCPResult::FailJson(Out);
        });

    // ------------------------------------------------------------ 关卡与场景

    AddTool(TEXT("ue_create_level"), TEXT("新建关卡"),
        TEXT("新建一个空白关卡并另存到指定资产路径（例如 /Game/ShineAI/Maps/L_ShineDemo）。不会弹保存对话框。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("关卡资产路径"), true, TEXT("/Game/ShineAI/Maps/L_ShineDemo"));
            ShineMCPSchema::AddBoolean(Schema, TEXT("buildDemoScene"), TEXT("是否顺便摆好演示场景"), false, true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FString AssetPath = ShineMCPJson::GetString(Args, TEXT("assetPath"), TEXT("/Game/ShineAI/Maps/L_ShineDemo"));
            const bool bBuildDemoScene = ShineMCPJson::GetBool(Args, TEXT("buildDemoScene"), true);

            ULevelEditorSubsystem* LevelSubsystem = GetLevelSubsystem();
            if (!LevelSubsystem)
            {
                return FShineMCPResult::FailJson(ShineMCPJson::Error(TEXT("关卡子系统不可用。")));
            }

            // NewLevel 内部会置 GIsRunningUnattendedScript，因此不会弹保存对话框。
            const bool bCreated = LevelSubsystem->NewLevel(AssetPath, false);
            if (!bCreated)
            {
                TSharedPtr<FJsonObject> Out = ShineMCPJson::Error(FString::Printf(
                    TEXT("新建关卡失败：%s（路径是否已存在？）"), *AssetPath));
                return FShineMCPResult::FailJson(Out);
            }

            UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
            Out->SetStringField(TEXT("assetPath"), AssetPath);
            Out->SetBoolField(TEXT("saved"), true);
            Out->SetStringField(TEXT("worldName"), World ? World->GetName() : TEXT(""));

            if (bBuildDemoScene)
            {
                TSharedPtr<FJsonObject> SceneResult;
                if (ShineMCPJson::Parse(BuildDemoSceneInternal(false), SceneResult) && SceneResult.IsValid())
                {
                    Out->SetObjectField(TEXT("demoScene"), SceneResult);
                }
            }

            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_build_demo_scene"), TEXT("搭建演示场景"),
        TEXT("在当前关卡里摆放一个简单的演示场景：地面、若干几何体、方向光、天光、大气、雾和红绿蓝三盏点光。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddBoolean(Schema, TEXT("clearExisting"), TEXT("是否先删掉上一次生成的 ShineDemo_* Actor"), false, true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(BuildDemoSceneInternal(ShineMCPJson::GetBool(Args, TEXT("clearExisting"), true)));
        });

    AddTool(TEXT("ue_load_level"), TEXT("打开关卡"),
        TEXT("把指定关卡资产加载进编辑器。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("关卡资产路径"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            ULevelEditorSubsystem* LevelSubsystem = GetLevelSubsystem();
            if (!LevelSubsystem)
            {
                return FShineMCPResult::FailJson(ShineMCPJson::Error(TEXT("关卡子系统不可用。")));
            }

            const FString AssetPath = ShineMCPJson::GetString(Args, TEXT("assetPath"));
            const bool bLoaded = LevelSubsystem->LoadLevel(AssetPath);

            TSharedPtr<FJsonObject> Out = bLoaded ? ShineMCPJson::Success() : ShineMCPJson::Error(
                FString::Printf(TEXT("打开关卡失败：%s"), *AssetPath));
            Out->SetBoolField(TEXT("loaded"), bLoaded);
            Out->SetStringField(TEXT("assetPath"), AssetPath);
            return bLoaded ? FShineMCPResult::OkJson(Out) : FShineMCPResult::FailJson(Out);
        });

    AddTool(TEXT("ue_save_current_level"), TEXT("保存当前关卡"),
        TEXT("保存当前关卡到磁盘。"),
        ShineMCPSchema::Object(),
        [](const TSharedPtr<FJsonObject>&)
        {
            ULevelEditorSubsystem* LevelSubsystem = GetLevelSubsystem();
            const bool bSaved = LevelSubsystem ? LevelSubsystem->SaveCurrentLevel() : false;

            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
            Out->SetBoolField(TEXT("saved"), bSaved);
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_list_actors"), TEXT("列出 Actor"),
        TEXT("列出当前关卡里的 Actor（标签、类名、位置）。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("classFilter"), TEXT("按类名子串过滤，例如 Light"), false, TEXT(""));
            ShineMCPSchema::AddInteger(Schema, TEXT("limit"), TEXT("最多返回多少个"), false, 200);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FString Filter = ShineMCPJson::GetString(Args, TEXT("classFilter"));
            const int32 Limit = FMath::Max(1, ShineMCPJson::GetInt(Args, TEXT("limit"), 200));

            TArray<TSharedPtr<FJsonValue>> Actors;
            for (AActor* Actor : GetAllEditorActors())
            {
                if (!Actor)
                {
                    continue;
                }

                const FString ClassName = Actor->GetClass()->GetName();
                if (!Filter.IsEmpty() && !ClassName.Contains(Filter, ESearchCase::IgnoreCase))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> Entry = ShineMCPJson::NewObject();
                Entry->SetStringField(TEXT("label"), Actor->GetActorLabel());
                Entry->SetStringField(TEXT("name"), Actor->GetName());
                Entry->SetStringField(TEXT("class"), ClassName);
                Entry->SetStringField(TEXT("location"), Actor->GetActorLocation().ToString());
                Actors.Add(MakeShared<FJsonValueObject>(Entry));

                if (Actors.Num() >= Limit)
                {
                    break;
                }
            }

            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
            Out->SetArrayField(TEXT("actors"), Actors);
            Out->SetNumberField(TEXT("count"), Actors.Num());
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_spawn_actor"), TEXT("放置 Actor"),
        TEXT("在当前关卡放置一个 Actor。className 可用短名（StaticMeshActor / PointLight / DirectionalLight / SkyLight / "
             "SkyAtmosphere / ExponentialHeightFog / PostProcessVolume / CameraActor）或完整类路径。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("className") });
            ShineMCPSchema::AddString(Schema, TEXT("className"), TEXT("Actor 类名或类路径"), true);
            ShineMCPSchema::AddString(Schema, TEXT("label"), TEXT("Actor 标签"), false, TEXT(""));
            ShineMCPSchema::AddNumber(Schema, TEXT("x"), TEXT("位置 X"), false, 0.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("y"), TEXT("位置 Y"), false, 0.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("z"), TEXT("位置 Z"), false, 0.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("pitch"), TEXT("旋转 Pitch"), false, 0.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("yaw"), TEXT("旋转 Yaw"), false, 0.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("roll"), TEXT("旋转 Roll"), false, 0.0);
            ShineMCPSchema::AddString(Schema, TEXT("staticMesh"), TEXT("可选：StaticMeshActor 要用的网格资产路径"), false, TEXT(""));
            ShineMCPSchema::AddInteger(Schema, TEXT("intensity"), TEXT("可选：光源强度"), false, 0);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FString ClassName = ShineMCPJson::GetString(Args, TEXT("className"));
            UClass* Class = ResolveActorClass(ClassName);
            if (!Class)
            {
                TSharedPtr<FJsonObject> Out = ShineMCPJson::Error(FString::Printf(TEXT("找不到 Actor 类: %s"), *ClassName));
                return FShineMCPResult::FailJson(Out);
            }

            UEditorActorSubsystem* Subsystem = GetActorSubsystem();
            if (!Subsystem)
            {
                return FShineMCPResult::FailJson(ShineMCPJson::Error(TEXT("Actor 子系统不可用。")));
            }

            const FVector Location(
                ShineMCPJson::GetNumber(Args, TEXT("x")),
                ShineMCPJson::GetNumber(Args, TEXT("y")),
                ShineMCPJson::GetNumber(Args, TEXT("z")));
            const FRotator Rotation(
                ShineMCPJson::GetNumber(Args, TEXT("pitch")),
                ShineMCPJson::GetNumber(Args, TEXT("yaw")),
                ShineMCPJson::GetNumber(Args, TEXT("roll")));

            AActor* Actor = Subsystem->SpawnActorFromClass(Class, Location, Rotation);
            if (!Actor)
            {
                return FShineMCPResult::FailJson(ShineMCPJson::Error(TEXT("放置 Actor 失败。")));
            }

            const FString Label = ShineMCPJson::GetString(Args, TEXT("label"));
            if (!Label.IsEmpty())
            {
                Actor->SetActorLabel(Label);
            }

            const FString StaticMeshPath = ShineMCPJson::GetString(Args, TEXT("staticMesh"));
            if (!StaticMeshPath.IsEmpty())
            {
                if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath))
                {
                    if (UStaticMeshComponent* MeshComponent = Actor->FindComponentByClass<UStaticMeshComponent>())
                    {
                        MeshComponent->SetMobility(EComponentMobility::Movable);
                        MeshComponent->SetStaticMesh(Mesh);
                    }
                }
            }

            const int32 Intensity = ShineMCPJson::GetInt(Args, TEXT("intensity"), 0);
            if (Intensity > 0)
            {
                if (ULightComponent* LightComponent = Actor->FindComponentByClass<ULightComponent>())
                {
                    LightComponent->SetMobility(EComponentMobility::Movable);
                    LightComponent->SetIntensity(static_cast<float>(Intensity));
                }
            }

            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
            Out->SetStringField(TEXT("label"), Actor->GetActorLabel());
            Out->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_delete_actor"), TEXT("删除 Actor"),
        TEXT("按标签或名称删除一个 Actor。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("label") });
            ShineMCPSchema::AddString(Schema, TEXT("label"), TEXT("Actor 标签或名称"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            AActor* Actor = FindEditorActor(ShineMCPJson::GetString(Args, TEXT("label")));
            if (!Actor)
            {
                return FShineMCPResult::FailJson(ShineMCPJson::Error(TEXT("找不到该 Actor。")));
            }

            const bool bDestroyed = GetActorSubsystem() ? GetActorSubsystem()->DestroyActor(Actor) : false;

            TSharedPtr<FJsonObject> Out = ShineMCPJson::Success();
            Out->SetBoolField(TEXT("destroyed"), bDestroyed);
            return FShineMCPResult::OkJson(Out);
        });

    // ---------------------------------------------------------------- 捕获

    AddTool(TEXT("ue_get_viewport_camera"), TEXT("读取视口相机"),
        TEXT("读取当前编辑器视口相机的位置、朝向与 FOV。"),
        ShineMCPSchema::Object(),
        [](const TSharedPtr<FJsonObject>&)
        {
            return FromJsonText(UShineSceneCaptureLibrary::GetEditorViewportCamera());
        });

    AddTool(TEXT("ue_get_level_bounds"), TEXT("关卡包围盒"),
        TEXT("计算当前关卡所有 Actor 的包围盒，便于自动找机位。"),
        ShineMCPSchema::Object(),
        [](const TSharedPtr<FJsonObject>&)
        {
            return FromJsonText(UShineSceneCaptureLibrary::GetLevelBounds());
        });

    AddTool(TEXT("ue_capture_scene"), TEXT("捕获场景三视图"),
        TEXT("从指定机位把场景渲染成颜色 / 深度 / 法线三张 PNG（AI 生成用的三种引导信号）。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddNumber(Schema, TEXT("x"), TEXT("机位 X"), false, -620.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("y"), TEXT("机位 Y"), false, -760.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("z"), TEXT("机位 Z"), false, 420.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("pitch"), TEXT("朝向 Pitch"), false, -14.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("yaw"), TEXT("朝向 Yaw"), false, 38.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("roll"), TEXT("朝向 Roll"), false, 0.0);
            ShineMCPSchema::AddNumber(Schema, TEXT("fov"), TEXT("FOV（度）"), false, 60.0);
            ShineMCPSchema::AddInteger(Schema, TEXT("width"), TEXT("宽度"), false, 1024);
            ShineMCPSchema::AddInteger(Schema, TEXT("height"), TEXT("高度"), false, 1024);
            ShineMCPSchema::AddString(Schema, TEXT("outputDirectory"), TEXT("输出目录，留空则用 Saved/ShineCapture/<时间戳>"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("prefix"), TEXT("文件名前缀"), false, TEXT("Scene"));
            ShineMCPSchema::AddBoolean(Schema, TEXT("captureColor"), TEXT("是否捕获颜色"), false, true);
            ShineMCPSchema::AddBoolean(Schema, TEXT("captureDepth"), TEXT("是否捕获深度"), false, true);
            ShineMCPSchema::AddBoolean(Schema, TEXT("captureNormal"), TEXT("是否捕获法线"), false, true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FVector Location(
                ShineMCPJson::GetNumber(Args, TEXT("x"), -620.0),
                ShineMCPJson::GetNumber(Args, TEXT("y"), -760.0),
                ShineMCPJson::GetNumber(Args, TEXT("z"), 420.0));
            const FRotator Rotation(
                ShineMCPJson::GetNumber(Args, TEXT("pitch"), -14.0),
                ShineMCPJson::GetNumber(Args, TEXT("yaw"), 38.0),
                ShineMCPJson::GetNumber(Args, TEXT("roll"), 0.0));

            return FromJsonText(UShineSceneCaptureLibrary::CaptureSceneViews(
                Location,
                Rotation,
                static_cast<float>(ShineMCPJson::GetNumber(Args, TEXT("fov"), 60.0)),
                ShineMCPJson::GetInt(Args, TEXT("width"), 1024),
                ShineMCPJson::GetInt(Args, TEXT("height"), 1024),
                ShineMCPJson::GetString(Args, TEXT("outputDirectory")),
                ShineMCPJson::GetString(Args, TEXT("prefix"), TEXT("Scene")),
                ShineMCPJson::GetBool(Args, TEXT("captureColor"), true),
                ShineMCPJson::GetBool(Args, TEXT("captureDepth"), true),
                ShineMCPJson::GetBool(Args, TEXT("captureNormal"), true)));
        });

    AddTool(TEXT("ue_capture_from_viewport"), TEXT("按视口相机捕获"),
        TEXT("直接用当前编辑器视口的机位与 FOV 捕获颜色 / 深度 / 法线。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddInteger(Schema, TEXT("width"), TEXT("宽度"), false, 1024);
            ShineMCPSchema::AddInteger(Schema, TEXT("height"), TEXT("高度"), false, 1024);
            ShineMCPSchema::AddString(Schema, TEXT("outputDirectory"), TEXT("输出目录"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("prefix"), TEXT("文件名前缀"), false, TEXT("Scene"));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineSceneCaptureLibrary::CaptureFromEditorViewport(
                ShineMCPJson::GetInt(Args, TEXT("width"), 1024),
                ShineMCPJson::GetInt(Args, TEXT("height"), 1024),
                ShineMCPJson::GetString(Args, TEXT("outputDirectory")),
                ShineMCPJson::GetString(Args, TEXT("prefix"), TEXT("Scene")),
                true, true, true));
        });

    AddTool(TEXT("ue_capture_to_graph"), TEXT("捕获并接入 Shine 图表"),
        TEXT("一步完成：用当前视口机位捕获 颜色/深度/法线 → 上传到 ComfyUI → "
             "把 Shine 图表里的 ColorImage / DepthImage / NormalImage（以及视频路线的 LTXVideoImage）"
             "换成这次的新文件。之后在图表里点“开始任务”就会用你刚摆好的画面。"
             "捕获结果会贴在关卡视口右侧预览（不遮挡视口操作）。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("Shine Comfy 图表资产路径；留空则只捕获+上传，不改图"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            ShineMCPSchema::AddInteger(Schema, TEXT("width"), TEXT("捕获宽度"), false, 768);
            ShineMCPSchema::AddInteger(Schema, TEXT("height"), TEXT("捕获高度"), false, 768);
            ShineMCPSchema::AddString(Schema, TEXT("outputDirectory"), TEXT("输出目录，留空则用 Saved/ShineCapture/<时间戳>"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("prefix"), TEXT("文件名前缀"), false, TEXT("Scene"));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FString BaseUrl = GetBaseUrlArg(Args);
            const FString AssetPath = ShineMCPJson::GetString(Args, TEXT("assetPath"));

            // 1) 捕获（同时把三张图贴到视口右侧预览）
            const FString CaptureJson = UShineSceneCaptureLibrary::CaptureFromEditorViewport(
                ShineMCPJson::GetInt(Args, TEXT("width"), 768),
                ShineMCPJson::GetInt(Args, TEXT("height"), 768),
                ShineMCPJson::GetString(Args, TEXT("outputDirectory")),
                ShineMCPJson::GetString(Args, TEXT("prefix"), TEXT("Scene")),
                true, true, true);

            TSharedPtr<FJsonObject> Capture;
            if (!ShineMCPJson::Parse(CaptureJson, Capture) || !Capture.IsValid())
            {
                return FShineMCPResult::Fail(CaptureJson);
            }
            if (!ShineMCPJson::GetBool(Capture, TEXT("success"), false))
            {
                return FShineMCPResult::Fail(CaptureJson);
            }

            const TSharedPtr<FJsonObject>* Files = nullptr;
            Capture->TryGetObjectField(TEXT("files"), Files);

            auto GetCapturedFilePath = [Files](const TCHAR* ChannelKey) -> FString
            {
                if (!Files || !Files->IsValid())
                {
                    return FString();
                }

                const TSharedPtr<FJsonObject>* Entry = nullptr;
                if (!(*Files)->TryGetObjectField(ChannelKey, Entry) || !Entry || !Entry->IsValid())
                {
                    return FString();
                }

                return ShineMCPJson::GetString(*Entry, TEXT("path"));
            };

            struct FCaptureBinding
            {
                const TCHAR* ChannelKey;
                const TCHAR* ParameterName;
            };

            const FCaptureBinding Bindings[] =
            {
                { TEXT("color"),  TEXT("ColorImage") },
                { TEXT("depth"),  TEXT("DepthImage") },
                { TEXT("normal"), TEXT("NormalImage") },
            };

            // 2) 上传到 ComfyUI
            TSharedPtr<FJsonObject> Uploaded = ShineMCPJson::NewObject();
            TArray<TPair<FString, FString>> ParameterValues;

            for (const FCaptureBinding& Binding : Bindings)
            {
                const FString FilePath = GetCapturedFilePath(Binding.ChannelKey);
                if (FilePath.IsEmpty())
                {
                    continue;
                }

                const FString UploadJson = UShineComfyBridgeLibrary::UploadImage(BaseUrl, FilePath, FString(), true);

                TSharedPtr<FJsonObject> Upload;
                if (!ShineMCPJson::Parse(UploadJson, Upload) || !Upload.IsValid()
                    || !ShineMCPJson::GetBool(Upload, TEXT("success"), false))
                {
                    return FShineMCPResult::Fail(FString::Printf(
                        TEXT("把 %s 图上传到 ComfyUI 失败：%s"), Binding.ChannelKey, *UploadJson));
                }

                const FString UploadedName = ShineMCPJson::GetString(Upload, TEXT("name"));
                Uploaded->SetStringField(Binding.ChannelKey, UploadedName);
                ParameterValues.Emplace(Binding.ParameterName, UploadedName);
            }

            // 视频路线用颜色图当首帧，一起换掉。
            // 注意：先取出来再 Emplace，别在遍历中改数组（会让迭代器失效）。
            FString ColorUploadedName;
            for (const TPair<FString, FString>& Pair : ParameterValues)
            {
                if (Pair.Key == TEXT("ColorImage"))
                {
                    ColorUploadedName = Pair.Value;
                    break;
                }
            }
            if (!ColorUploadedName.IsEmpty())
            {
                ParameterValues.Emplace(TEXT("LTXVideoImage"), ColorUploadedName);
            }

            // 3) 把新文件写回图里的参数
            TArray<TSharedPtr<FJsonValue>> UpdatedParameters;
            bool bAssetSaved = false;
            if (!AssetPath.IsEmpty())
            {
                const FString ObjectPath = AssetPath.Contains(TEXT("."))
                    ? AssetPath
                    : FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetBaseFilename(AssetPath));

                UShineComfyAsset* Asset = LoadObject<UShineComfyAsset>(nullptr, *ObjectPath);
                if (!Asset)
                {
                    return FShineMCPResult::Fail(FString::Printf(TEXT("找不到图表资产：%s"), *ObjectPath));
                }

                UShineComfyGraph* Graph = Asset->GetOrCreateGraph();
                if (!Graph)
                {
                    return FShineMCPResult::Fail(TEXT("图表资产里没有图。"));
                }

                for (const TPair<FString, FString>& Pair : ParameterValues)
                {
                    const int32 ChangedCount = Graph->SetTextParameterOnNodesByName(FName(*Pair.Key), Pair.Value);
                    if (ChangedCount > 0)
                    {
                        UpdatedParameters.Add(MakeShared<FJsonValueString>(
                            FString::Printf(TEXT("%s = %s（%d 个节点）"), *Pair.Key, *Pair.Value, ChangedCount)));
                    }
                }

                if (UpdatedParameters.Num() > 0)
                {
                    Asset->Modify();
                    Asset->MarkPackageDirty();

                    // 立刻落盘：否则编辑器一重启，图里的图片参数又回到旧值
                    // （"我明明捕获过了，怎么还是上一张图"）。
                    if (UPackage* Package = Asset->GetOutermost())
                    {
                        Package->MarkPackageDirty();

                        FSavePackageArgs SaveArgs;
                        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
                        SaveArgs.SaveFlags = SAVE_NoError;

                        const FString FileName = FPackageName::LongPackageNameToFilename(
                            Package->GetName(), FPackageName::GetAssetPackageExtension());

                        bAssetSaved = UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
                    }
                }
            }

            TSharedPtr<FJsonObject> Out = ShineMCPJson::NewObject();
            Out->SetBoolField(TEXT("success"), true);
            Out->SetStringField(TEXT("assetPath"), AssetPath);
            Out->SetStringField(TEXT("directory"), ShineMCPJson::GetString(Capture, TEXT("directory")));
            Out->SetStringField(TEXT("uniquePrefix"), ShineMCPJson::GetString(Capture, TEXT("uniquePrefix")));
            Out->SetStringField(TEXT("location"), ShineMCPJson::GetString(Capture, TEXT("location")));
            Out->SetStringField(TEXT("rotation"), ShineMCPJson::GetString(Capture, TEXT("rotation")));
            Out->SetStringField(TEXT("cameraLabel"), ShineMCPJson::GetString(Capture, TEXT("cameraLabel")));
            Out->SetStringField(TEXT("cameraSource"), ShineMCPJson::GetString(Capture, TEXT("cameraSource")));
            if (Files && Files->IsValid())
            {
                Out->SetObjectField(TEXT("files"), *Files);
            }
            Out->SetObjectField(TEXT("uploaded"), Uploaded);
            Out->SetArrayField(TEXT("updatedParameters"), UpdatedParameters);
            Out->SetBoolField(TEXT("assetSaved"), bAssetSaved);
            Out->SetBoolField(TEXT("previewShown"), true);
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_screenshot_viewport"), TEXT("截关卡视口"),
        TEXT("把当前关卡视口截成 PNG（包含 Shine 贴在右侧的颜色/深度/法线预览）。"
             "用来核对「送进 AI 的图」和「视口里看到的画面」是否一致。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("outputDirectory"), TEXT("输出目录，留空则用 Saved/ShineViewport"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("fileName"), TEXT("文件名，留空按时间生成"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("target"), TEXT("viewport=只拍关卡视口（默认）；window=拍当前整个编辑器窗口（能拍到 Shine 面板/图表）"), false, TEXT("viewport"));
            ShineMCPSchema::AddString(Schema, TEXT("windowTitle"), TEXT("按标题找顶层窗口来拍（例如放大窗口的文件名）；填了就优先用它"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            TSharedPtr<SWidget> CaptureWidget;

            // 先按标题找窗口：放大窗口是独立顶层窗口，用标题就能精确定位到它。
            const FString WindowTitle = ShineMCPJson::GetString(Args, TEXT("windowTitle"));
            if (!WindowTitle.IsEmpty())
            {
                for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
                {
                    if (Window->GetTitle().ToString().Contains(WindowTitle, ESearchCase::IgnoreCase))
                    {
                        CaptureWidget = Window;
                        break;
                    }
                }
            }

            const FString Target = ShineMCPJson::GetString(Args, TEXT("target"), TEXT("viewport"));
            if (!CaptureWidget.IsValid() && Target.Equals(TEXT("window"), ESearchCase::IgnoreCase))
            {
                if (TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
                {
                    CaptureWidget = ActiveWindow;
                }
            }

            if (!CaptureWidget.IsValid())
            {
                FLevelEditorViewportClient* ViewportClient = ShineSceneCapture::GetActiveViewportClient();
                if (!ViewportClient)
                {
                    return FShineMCPResult::Fail(TEXT("拿不到当前关卡视口。"));
                }

                CaptureWidget = ViewportClient->GetEditorViewportWidget();
            }

            if (!CaptureWidget.IsValid())
            {
                return FShineMCPResult::Fail(TEXT("找不到可截图的控件。"));
            }

            TArray<FColor> Pixels;
            FIntVector Size(0, 0, 0);
            if (!FSlateApplication::Get().TakeScreenshot(CaptureWidget.ToSharedRef(), Pixels, Size)
                || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y)
            {
                return FShineMCPResult::Fail(TEXT("截图失败：Slate 没能读到窗口后备缓冲。"));
            }

            const TArray<FColor>& OutputPixels = Pixels;

            FString Directory = ShineMCPJson::GetString(Args, TEXT("outputDirectory"));
            if (Directory.IsEmpty())
            {
                Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShineViewport"));
            }
            IFileManager::Get().MakeDirectory(*Directory, true);

            FString FileName = ShineMCPJson::GetString(Args, TEXT("fileName"));
            if (FileName.IsEmpty())
            {
                FileName = FString::Printf(TEXT("Viewport_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
            }
            if (!FileName.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
            {
                FileName += TEXT(".png");
            }

            const FString FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, FileName));

            TArray64<uint8> PngData;
            FImageUtils::PNGCompressImageArray(
                Size.X,
                Size.Y,
                TArrayView64<const FColor>(OutputPixels.GetData(), OutputPixels.Num()),
                PngData);

            if (!FFileHelper::SaveArrayToFile(PngData, *FilePath))
            {
                return FShineMCPResult::Fail(FString::Printf(TEXT("写文件失败：%s"), *FilePath));
            }

            TSharedPtr<FJsonObject> Out = ShineMCPJson::NewObject();
            Out->SetBoolField(TEXT("success"), true);
            Out->SetStringField(TEXT("path"), FilePath);
            Out->SetNumberField(TEXT("width"), Size.X);
            Out->SetNumberField(TEXT("height"), Size.Y);
            Out->SetNumberField(TEXT("bytes"), static_cast<double>(IFileManager::Get().FileSize(*FilePath)));
            Out->SetBoolField(TEXT("previewVisible"), ShineCapturePreview::IsVisible());
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_show_image_preview"), TEXT("在视口看结果图"),
        TEXT("把磁盘上的图片贴到关卡视口右侧的预览面板（和捕获预览同一块区域），"
             "用来在编辑器里直接看 ComfyUI 的生成结果，不用切窗口找文件。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddStringArray(Schema, TEXT("filePaths"), TEXT("要显示的图片路径（可多张）"), true);
            ShineMCPSchema::AddString(Schema, TEXT("title"), TEXT("面板顶部说明文字"), false, TEXT("生成结果"));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const TArray<TSharedPtr<FJsonValue>>* FilePaths = nullptr;
            if (!Args->TryGetArrayField(TEXT("filePaths"), FilePaths) || !FilePaths || FilePaths->Num() == 0)
            {
                return FShineMCPResult::Fail(TEXT("filePaths 不能为空（至少一张图片路径）。"));
            }

            TArray<FShineCapturePreviewItem> Items;
            for (const TSharedPtr<FJsonValue>& Value : *FilePaths)
            {
                FString FilePath;
                if (!Value.IsValid() || !Value->TryGetString(FilePath) || FilePath.IsEmpty())
                {
                    continue;
                }
                if (!FPaths::FileExists(FilePath))
                {
                    continue;
                }

                FShineCapturePreviewItem Item;
                Item.FilePath = FPaths::ConvertRelativePathToFull(FilePath);
                Item.Label = FPaths::GetCleanFilename(FilePath);
                Item.Detail = FString::Printf(TEXT("%.0f KB"), IFileManager::Get().FileSize(*Item.FilePath) / 1024.0);
                Items.Add(MoveTemp(Item));
            }

            if (Items.Num() == 0)
            {
                return FShineMCPResult::Fail(TEXT("给的路径都不存在。"));
            }

            const FString Title = ShineMCPJson::GetString(Args, TEXT("title"), TEXT("生成结果"));
            ShineCapturePreview::Show(Items, FString::Printf(TEXT("%s（%d 张）"), *Title, Items.Num()));

            // 已经打开的"放大"窗口跟着换到这批结果（并重读文件）。
            TArray<FString> PreviewPaths;
            PreviewPaths.Reserve(Items.Num());
            for (const FShineCapturePreviewItem& Item : Items)
            {
                PreviewPaths.Add(Item.FilePath);
            }
            ShineImageZoom::RefreshOpenWindows(PreviewPaths);

            TSharedPtr<FJsonObject> Out = ShineMCPJson::NewObject();
            Out->SetBoolField(TEXT("success"), true);
            Out->SetNumberField(TEXT("shown"), Items.Num());
            Out->SetBoolField(TEXT("previewVisible"), ShineCapturePreview::IsVisible());
            return FShineMCPResult::OkJson(Out);
        });

    // -------------------------------------------------------------- ComfyUI

    AddTool(TEXT("ue_comfy_ping"), TEXT("ComfyUI 探活"),
        TEXT("连接 ComfyUI 的 /system_stats，返回版本、显卡与显存。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::PingComfy(GetBaseUrlArg(Args)));
        });

    AddTool(TEXT("ue_comfy_list_checkpoints"), TEXT("列出 checkpoint"),
        TEXT("列出 ComfyUI 当前可用的 checkpoint 名称。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::ListCheckpoints(GetBaseUrlArg(Args)));
        });

    AddTool(TEXT("ue_comfy_object_info"), TEXT("查询节点定义"),
        TEXT("查询 ComfyUI 某个节点类的 /object_info，用于确认节点是否存在及入参。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("classType") });
            ShineMCPSchema::AddString(Schema, TEXT("classType"), TEXT("节点类名，例如 ControlNetApplyAdvanced"), true);
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::GetComfyObjectInfo(
                GetBaseUrlArg(Args), ShineMCPJson::GetString(Args, TEXT("classType"))));
        });

    AddTool(TEXT("ue_comfy_upload_image"), TEXT("上传图片到 ComfyUI"),
        TEXT("把本地 PNG 上传到 ComfyUI 的 input 目录，返回文件名（供 LoadImage 使用）。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("filePath") });
            ShineMCPSchema::AddString(Schema, TEXT("filePath"), TEXT("本地图片路径"), true);
            ShineMCPSchema::AddString(Schema, TEXT("subfolder"), TEXT("子目录"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::UploadImage(
                GetBaseUrlArg(Args),
                ShineMCPJson::GetString(Args, TEXT("filePath")),
                ShineMCPJson::GetString(Args, TEXT("subfolder")),
                true));
        });

    AddTool(TEXT("ue_create_comfy_graph"), TEXT("新建 Shine Comfy 图"),
        TEXT("新建一个 Shine Comfy 图资产（UShineComfyAsset），并写入 ComfyUI 服务地址。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("packagePath"), TEXT("包路径，例如 /Game/ShineAI/Comfy"), false, TEXT("/Game/ShineAI/Comfy"));
            ShineMCPSchema::AddString(Schema, TEXT("assetName"), TEXT("资产名"), false, TEXT("SA_SceneToImage"));
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::CreateComfyGraphAsset(
                ShineMCPJson::GetString(Args, TEXT("packagePath"), TEXT("/Game/ShineAI/Comfy")),
                ShineMCPJson::GetString(Args, TEXT("assetName"), TEXT("SA_SceneToImage")),
                GetBaseUrlArg(Args)));
        });

    AddTool(TEXT("ue_build_comfy_graph"), TEXT("用 JSON 建图"),
        TEXT("用 JSON 重建 Shine Comfy 图（支持 Shine Graph JSON / ComfyUI workflow / API prompt）。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath"), TEXT("graphJson") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("Shine Comfy 图资产路径"), true);
            ShineMCPSchema::AddString(Schema, TEXT("graphJson"), TEXT("图 JSON 文本"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::BuildComfyGraphFromJson(
                ShineMCPJson::GetString(Args, TEXT("assetPath")),
                ShineMCPJson::GetString(Args, TEXT("graphJson"))));
        });

    AddTool(TEXT("ue_zoom_image"), TEXT("放大看图"),
        TEXT("按原图尺寸弹一个可缩放的独立窗口显示图片（和图表 Preview 节点上的“放大”按钮同一个窗口）。"
             "同一张图重复调用只重读+置顶；之后有新的结果会跟着自动刷新。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("filePath") });
            ShineMCPSchema::AddString(Schema, TEXT("filePath"), TEXT("要放大的图片路径"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FString FilePath = ShineMCPJson::GetString(Args, TEXT("filePath"));
            if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
            {
                return FShineMCPResult::Fail(FString::Printf(TEXT("文件不存在：%s"), *FilePath));
            }

            ShineImageZoom::Show(FilePath);

            TSharedPtr<FJsonObject> Out = ShineMCPJson::NewObject();
            Out->SetBoolField(TEXT("success"), true);
            Out->SetStringField(TEXT("path"), FPaths::ConvertRelativePathToFull(FilePath));
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_set_graph_result_images"), TEXT("把结果图放进图里"),
        TEXT("把生成结果的本地图片路径写进 Shine 图里的显示节点（Preview / MultiImageGallery），"
             "这样打开图表就能直接看到结果，不用去翻文件夹。会顺便保存资产。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("Shine Comfy 图资产路径"), true);
            ShineMCPSchema::AddStringArray(Schema, TEXT("filePaths"), TEXT("结果图路径（可多张）"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            const FString AssetPath = ShineMCPJson::GetString(Args, TEXT("assetPath"));
            const TArray<TSharedPtr<FJsonValue>>* FilePaths = nullptr;
            if (AssetPath.IsEmpty() || !Args->TryGetArrayField(TEXT("filePaths"), FilePaths) || !FilePaths)
            {
                return FShineMCPResult::Fail(TEXT("需要 assetPath 与 filePaths。"));
            }

            TArray<FString> ImagePaths;
            for (const TSharedPtr<FJsonValue>& Value : *FilePaths)
            {
                FString FilePath;
                if (Value.IsValid() && Value->TryGetString(FilePath) && FPaths::FileExists(FilePath))
                {
                    ImagePaths.Add(FPaths::ConvertRelativePathToFull(FilePath));
                }
            }

            if (ImagePaths.Num() == 0)
            {
                return FShineMCPResult::Fail(TEXT("filePaths 里没有存在的文件。"));
            }

            const FString ObjectPath = AssetPath.Contains(TEXT("."))
                ? AssetPath
                : FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetBaseFilename(AssetPath));

            UShineComfyAsset* Asset = LoadObject<UShineComfyAsset>(nullptr, *ObjectPath);
            if (!Asset)
            {
                return FShineMCPResult::Fail(FString::Printf(TEXT("找不到图表资产：%s"), *ObjectPath));
            }

            UShineComfyGraph* Graph = Asset->GetOrCreateGraph();
            if (!Graph)
            {
                return FShineMCPResult::Fail(TEXT("图表资产里没有图。"));
            }

            // 图里所有"能显示图片"的节点都塞上这批结果图。
            TArray<FString> NodeTitles;
            const int32 UpdatedNodeCount = Graph->SetResultImagesOnDisplayNodes(ImagePaths, NodeTitles);

            bool bSaved = false;
            if (UpdatedNodeCount > 0)
            {
                Asset->Modify();
                Asset->MarkPackageDirty();

                if (UPackage* Package = Asset->GetOutermost())
                {
                    Package->MarkPackageDirty();
                    FSavePackageArgs SaveArgs;
                    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
                    SaveArgs.SaveFlags = SAVE_NoError;
                    const FString FileName = FPackageName::LongPackageNameToFilename(
                        Package->GetName(), FPackageName::GetAssetPackageExtension());
                    bSaved = UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
                }
            }

            TSharedPtr<FJsonObject> Out = ShineMCPJson::NewObject();
            Out->SetBoolField(TEXT("success"), UpdatedNodeCount > 0);
            Out->SetNumberField(TEXT("updatedNodes"), UpdatedNodeCount);
            Out->SetNumberField(TEXT("imageCount"), ImagePaths.Num());
            Out->SetBoolField(TEXT("assetSaved"), bSaved);
            Out->SetArrayField(TEXT("nodeTitles"),
                [&NodeTitles]()
                {
                    TArray<TSharedPtr<FJsonValue>> Values;
                    for (const FString& Title : NodeTitles)
                    {
                        Values.Add(MakeShared<FJsonValueString>(Title));
                    }
                    return Values;
                }());
            if (UpdatedNodeCount == 0)
            {
                Out->SetStringField(TEXT("error"),
                    TEXT("图里没有 Preview / MultiImageGallery 显示节点；先往图里放一个这样的节点再试。"));
            }
            return FShineMCPResult::OkJson(Out);
        });

    AddTool(TEXT("ue_export_comfy_definition"), TEXT("导出图定义"),
        TEXT("导出 Shine 图的可读定义（节点 + 每个节点的参数值），排查「参数没生效 / 图没连上」时用。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("Shine Comfy 图资产路径"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::ExportComfyGraphDefinition(
                ShineMCPJson::GetString(Args, TEXT("assetPath"))));
        });

    AddTool(TEXT("ue_export_comfy_prompt"), TEXT("导出 ComfyUI prompt"),
        TEXT("把 Shine Comfy 图导出成可直接提交给 ComfyUI 的 prompt JSON。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("Shine Comfy 图资产路径"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::ExportComfyPromptJson(
                ShineMCPJson::GetString(Args, TEXT("assetPath"))));
        });

    AddTool(TEXT("ue_comfy_graph_definition"), TEXT("导出图定义"),
        TEXT("导出 Shine Comfy 图的节点与连线定义，便于检查图结构。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("Shine Comfy 图资产路径"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::ExportComfyGraphDefinition(
                ShineMCPJson::GetString(Args, TEXT("assetPath"))));
        });

    AddTool(TEXT("ue_comfy_submit"), TEXT("提交生成任务"),
        TEXT("把 prompt JSON 提交给 ComfyUI，立即返回 promptId（不等待生成完成）。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("promptJson") });
            ShineMCPSchema::AddString(Schema, TEXT("promptJson"), TEXT("ComfyUI API prompt JSON 文本"), true);
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::SubmitPrompt(
                GetBaseUrlArg(Args),
                ShineMCPJson::GetString(Args, TEXT("promptJson")),
                TEXT("shine-mcp")));
        });

    AddTool(TEXT("ue_comfy_queue"), TEXT("查询队列"),
        TEXT("返回 ComfyUI 队列中正在执行与等待的数量。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::GetQueueStatus(GetBaseUrlArg(Args)));
        });

    AddTool(TEXT("ue_comfy_prompt_result"), TEXT("查询任务结果"),
        TEXT("查询某个 promptId 的状态：pending / running / done / error / not_found，完成后返回输出文件列表。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("promptId") });
            ShineMCPSchema::AddString(Schema, TEXT("promptId"), TEXT("提交任务时返回的 id"), true);
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::GetPromptResult(
                GetBaseUrlArg(Args), ShineMCPJson::GetString(Args, TEXT("promptId"))));
        });

    AddTool(TEXT("ue_comfy_download_image"), TEXT("下载生成结果"),
        TEXT("把 ComfyUI 输出的图片/视频下载到本地路径。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("filename") });
            ShineMCPSchema::AddString(Schema, TEXT("filename"), TEXT("输出文件名"), true);
            ShineMCPSchema::AddString(Schema, TEXT("subfolder"), TEXT("子目录"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("type"), TEXT("类型，默认 output"), false, TEXT("output"));
            ShineMCPSchema::AddString(Schema, TEXT("savePath"), TEXT("本地保存路径，留空则存到 Saved/ShineComfy/"), false, TEXT(""));
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::DownloadImage(
                GetBaseUrlArg(Args),
                ShineMCPJson::GetString(Args, TEXT("filename")),
                ShineMCPJson::GetString(Args, TEXT("subfolder")),
                ShineMCPJson::GetString(Args, TEXT("type"), TEXT("output")),
                ShineMCPJson::GetString(Args, TEXT("savePath"))));
        });

    AddTool(TEXT("ue_comfy_interrupt"), TEXT("中断任务"),
        TEXT("中断 ComfyUI 当前正在执行的任务。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object();
            ShineMCPSchema::AddString(Schema, TEXT("baseUrl"), TEXT("ComfyUI 地址"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::Interrupt(GetBaseUrlArg(Args)));
        });

    AddTool(TEXT("ue_import_texture"), TEXT("导入纹理资产"),
        TEXT("把磁盘上的图片导入为 UE 纹理资产，便于在编辑器里查看 AI 生成结果。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("sourceFile") });
            ShineMCPSchema::AddString(Schema, TEXT("sourceFile"), TEXT("本地图片路径"), true);
            ShineMCPSchema::AddString(Schema, TEXT("packagePath"), TEXT("导入到的包路径"), false, TEXT("/Game/ShineAI/Comfy/Output"));
            ShineMCPSchema::AddString(Schema, TEXT("assetName"), TEXT("资产名"), false, TEXT(""));
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::ImportTextureFromFile(
                ShineMCPJson::GetString(Args, TEXT("sourceFile")),
                ShineMCPJson::GetString(Args, TEXT("packagePath"), TEXT("/Game/ShineAI/Comfy/Output")),
                ShineMCPJson::GetString(Args, TEXT("assetName"))));
        });

    AddTool(TEXT("ue_open_asset"), TEXT("打开资产"),
        TEXT("在编辑器里打开一个资产（例如查看生成结果）。"),
        []()
        {
            TSharedPtr<FJsonObject> Schema = ShineMCPSchema::Object({ TEXT("assetPath") });
            ShineMCPSchema::AddString(Schema, TEXT("assetPath"), TEXT("资产路径"), true);
            return Schema;
        }(),
        [](const TSharedPtr<FJsonObject>& Args)
        {
            return FromJsonText(UShineComfyBridgeLibrary::OpenAsset(ShineMCPJson::GetString(Args, TEXT("assetPath"))));
        });

    (void)Registry;
}

void FShineMCPBuiltinTools::UnregisterAll()
{
    FShineMCPToolRegistry::Get().Clear();
}
