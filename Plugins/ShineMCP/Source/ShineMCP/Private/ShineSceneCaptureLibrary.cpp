#include "ShineSceneCaptureLibrary.h"

#include "Capture/ShineSceneCapture.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Level.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/World.h"
#include "ShineMCPPrivate.h"

namespace
{
    UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    /** 把 Shine 侧的捕获结果转成 MCP 工具约定的 JSON。 */
    FString CaptureResultToJson(const FShineCaptureResult& Capture)
    {
        TSharedPtr<FJsonObject> Result = Capture.bSuccess ? ShineMCPJson::Success() : ShineMCPJson::Error(Capture.ErrorMessage);

        Result->SetStringField(TEXT("directory"), Capture.Directory);
        Result->SetStringField(TEXT("uniquePrefix"), Capture.UniquePrefix);
        Result->SetNumberField(TEXT("width"), Capture.Width);
        Result->SetNumberField(TEXT("height"), Capture.Height);
        Result->SetStringField(TEXT("location"), Capture.Location.ToString());
        Result->SetStringField(TEXT("rotation"), Capture.Rotation.ToString());
        Result->SetStringField(TEXT("cameraLabel"), Capture.CameraLabel);
        Result->SetStringField(TEXT("cameraSource"), Capture.bCameraFromViewport ? TEXT("viewport") : TEXT("fallback"));
        Result->SetBoolField(TEXT("captured"), Capture.bAnyExported);

        TSharedPtr<FJsonObject> Files = ShineMCPJson::NewObject();
        TSharedPtr<FJsonObject> Stats = ShineMCPJson::NewObject();

        auto AddChannel = [&Files, &Stats](const TCHAR* Key, const FShineCaptureFile& File)
        {
            if (File.Path.IsEmpty())
            {
                return;
            }

            TSharedPtr<FJsonObject> FileObject = ShineMCPJson::NewObject();
            FileObject->SetStringField(TEXT("path"), File.Path);
            FileObject->SetNumberField(TEXT("bytes"), static_cast<double>(File.Bytes));
            Files->SetObjectField(Key, FileObject);

            if (File.bHasStats)
            {
                TSharedPtr<FJsonObject> ChannelStats = ShineMCPJson::NewObject();
                ChannelStats->SetNumberField(TEXT("minR"), File.MinValue);
                ChannelStats->SetNumberField(TEXT("maxR"), File.MaxValue);
                ChannelStats->SetNumberField(TEXT("meanR"), File.MeanValue);
                if (FCString::Stricmp(Key, TEXT("normal")) == 0)
                {
                    ChannelStats->SetBoolField(TEXT("rawNormal"), File.bRawNormal);
                }
                Stats->SetObjectField(Key, ChannelStats);
            }
        };

        AddChannel(TEXT("color"), Capture.Color);
        AddChannel(TEXT("depth"), Capture.Depth);
        AddChannel(TEXT("normal"), Capture.Normal);

        Result->SetObjectField(TEXT("files"), Files);
        Result->SetObjectField(TEXT("stats"), Stats);
        return ShineMCPJson::ToText(Result);
    }

    FShineCaptureRequest MakeRequest(
        const FVector& Location,
        const FRotator& Rotation,
        float FOVAngle,
        int32 Width,
        int32 Height,
        const FString& OutputDirectory,
        const FString& FilePrefix,
        bool bCaptureColor,
        bool bCaptureDepth,
        bool bCaptureNormal)
    {
        FShineCaptureRequest Request;
        Request.Location = Location;
        Request.Rotation = Rotation;
        Request.FOVAngle = FOVAngle;
        Request.Width = Width;
        Request.Height = Height;
        Request.OutputDirectory = OutputDirectory;
        Request.FilePrefix = FilePrefix;
        Request.bCaptureColor = bCaptureColor;
        Request.bCaptureDepth = bCaptureDepth;
        Request.bCaptureNormal = bCaptureNormal;
        Request.bShowPreview = true;
        Request.CameraLabel = FString::Printf(
            TEXT("机位：调用方指定  P=%.1f  Y=%.1f  FOV=%.0f  (X=%.0f Y=%.0f Z=%.0f)"),
            Rotation.Pitch, Rotation.Yaw, FOVAngle, Location.X, Location.Y, Location.Z);
        return Request;
    }
}

FString UShineSceneCaptureLibrary::CaptureSceneViews(
    FVector Location,
    FRotator Rotation,
    float FOVAngle,
    int32 Width,
    int32 Height,
    const FString& OutputDirectory,
    const FString& FilePrefix,
    bool bCaptureColor,
    bool bCaptureDepth,
    bool bCaptureNormal)
{
    return CaptureResultToJson(ShineSceneCapture::Capture(MakeRequest(
        Location, Rotation, FOVAngle, Width, Height, OutputDirectory, FilePrefix,
        bCaptureColor, bCaptureDepth, bCaptureNormal)));
}

FString UShineSceneCaptureLibrary::CaptureFromEditorViewport(
    int32 Width,
    int32 Height,
    const FString& OutputDirectory,
    const FString& FilePrefix,
    bool bCaptureColor,
    bool bCaptureDepth,
    bool bCaptureNormal)
{
    return CaptureResultToJson(ShineSceneCapture::CaptureFromEditorViewport(
        Width, Height, OutputDirectory, FilePrefix,
        bCaptureColor, bCaptureDepth, bCaptureNormal, true));
}

FString UShineSceneCaptureLibrary::GetEditorViewportCamera()
{
    TSharedPtr<FJsonObject> Result = ShineMCPJson::Success();
    Result->SetStringField(TEXT("location"), ShineSceneCapture::GetEditorViewportLocation().ToString());
    Result->SetStringField(TEXT("rotation"), ShineSceneCapture::GetEditorViewportRotation().ToString());
    Result->SetNumberField(TEXT("fov"), ShineSceneCapture::GetEditorViewportFOV());
    Result->SetStringField(TEXT("source"), ShineSceneCapture::HasEditorViewport() ? TEXT("viewport") : TEXT("fallback"));
    return ShineMCPJson::ToText(Result);
}

FRotator UShineSceneCaptureLibrary::LookAtRotation(FVector From, FVector To)
{
    return (To - From).Rotation();
}

FString UShineSceneCaptureLibrary::GetLevelBounds()
{
    UWorld* World = GetEditorWorld();
    if (!World)
    {
        return ShineMCPJson::ToText(ShineMCPJson::Error(TEXT("拿不到编辑器世界。")));
    }

    FBox Bounds(ForceInit);
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level)
        {
            continue;
        }

        for (AActor* Actor : Level->Actors)
        {
            if (!Actor || Actor->IsA<ASceneCapture2D>())
            {
                continue;
            }

            const FBox ActorBox = Actor->GetComponentsBoundingBox(true, false);
            if (ActorBox.IsValid)
            {
                Bounds += ActorBox;
            }
        }
    }

    TSharedPtr<FJsonObject> Result = ShineMCPJson::Success();
    if (Bounds.IsValid)
    {
        Result->SetStringField(TEXT("min"), Bounds.Min.ToString());
        Result->SetStringField(TEXT("max"), Bounds.Max.ToString());
        Result->SetStringField(TEXT("center"), Bounds.GetCenter().ToString());
        Result->SetNumberField(TEXT("size"), Bounds.GetSize().Size());
    }
    else
    {
        Result->SetBoolField(TEXT("empty"), true);
    }

    return ShineMCPJson::ToText(Result);
}
