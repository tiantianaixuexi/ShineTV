#include "Comfy/ShineComfyPaths.h"

#include "Comfy/ShineComfyPathSettings.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

namespace ShineComfyPaths
{
    FString NormalizeDirectory(const FString& Directory)
    {
        FString Result = Directory;
        Result.ReplaceInline(TEXT("\\"), TEXT("/"));
        while (Result.EndsWith(TEXT("/")))
        {
            Result.LeftChopInline(1);
        }

        return Result;
    }

    FString GetConfiguredOutputDirectory()
    {
        const UShineComfyPathSettings* Settings = GetDefault<UShineComfyPathSettings>();
        if (!Settings || Settings->ComfyUIOutputDirectory.IsEmpty())
        {
            return FString();
        }

        return NormalizeDirectory(Settings->ComfyUIOutputDirectory);
    }

    FString GetConfiguredVideoDirectory()
    {
        const UShineComfyPathSettings* Settings = GetDefault<UShineComfyPathSettings>();
        if (Settings && !Settings->ComfyUIVideoOutputDirectory.IsEmpty())
        {
            return NormalizeDirectory(Settings->ComfyUIVideoOutputDirectory);
        }

        // 没单独配就退回 output 目录：视频相对 output 的相对路径本来就含 "video/"。
        return GetConfiguredOutputDirectory();
    }

    FString GetConfiguredMediaLibraryDirectory()
    {
        const UShineComfyPathSettings* Settings = GetDefault<UShineComfyPathSettings>();
        if (Settings && !Settings->MediaLibraryDirectory.IsEmpty())
        {
            return NormalizeDirectory(Settings->MediaLibraryDirectory);
        }

        // 项目目录常常不在大容量盘上，但至少是个确定可写的位置。
        return NormalizeDirectory(FPaths::ProjectSavedDir() / TEXT("ShineMedia"));
    }

    namespace
    {
        /** 把 (Subfolder, Filename) 拼到给定根目录下的绝对路径。 */
        FString MakePathUnder(const FString& Directory, const FString& Subfolder, const FString& Filename)
        {
            if (Directory.IsEmpty() || Filename.IsEmpty())
            {
                return FString();
            }

            FString Relative = Subfolder;
            Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
            while (Relative.StartsWith(TEXT("/")))
            {
                Relative.RightChopInline(1);
            }

            const FString RelativePath = Relative.IsEmpty() ? Filename : FString::Printf(TEXT("%s/%s"), *Relative, *Filename);
            return FPaths::ConvertRelativePathToFull(FString::Printf(TEXT("%s/%s"), *Directory, *RelativePath));
        }
    }

    FString MakeLocalImagePath(const FString& Subfolder, const FString& Filename)
    {
        return MakePathUnder(GetConfiguredOutputDirectory(), Subfolder, Filename);
    }

    bool LocalImageExists(const FString& Subfolder, const FString& Filename)
    {
        const FString Path = MakeLocalImagePath(Subfolder, Filename);
        return !Path.IsEmpty() && FPaths::FileExists(Path);
    }

    FString MakeLocalMediaPath(const FShineComfyHistoryMedia& Media)
    {
        // 只有视频走视频目录；图片和音频仍按 output 目录解析。
        // 注意：音频虽然也是 SaveVideo 一起封装进 mp4 的，但独立音频节点（PreviewAudio）
        // 落在 temp/ 下，它的 type 字段是 "temp"，这里不再细分目录，交给调用方决定要不要下载。
        const FString Directory = Media.IsVideo() ? GetConfiguredVideoDirectory() : GetConfiguredOutputDirectory();
        return MakePathUnder(Directory, Media.Subfolder, Media.FileName);
    }

    bool LocalMediaExists(const FShineComfyHistoryMedia& Media)
    {
        const FString Path = MakeLocalMediaPath(Media);
        return !Path.IsEmpty() && FPaths::FileExists(Path);
    }

    FString FindSiblingImage(const FString& ColorImagePath, const TCHAR* ChannelSuffix)
    {
        if (ColorImagePath.TrimStartAndEnd().IsEmpty() || !ChannelSuffix || !*ChannelSuffix)
        {
            return FString();
        }

        const FString FullPath = FPaths::ConvertRelativePathToFull(ColorImagePath);
        const FString Directory = FPaths::GetPath(FullPath);
        const FString Extension = FPaths::GetExtension(FullPath, true);
        const FString Stem = FPaths::GetBaseFilename(FullPath);

        // 命名约定是 Scene_<时间>_Color.png / _Depth.png / _Normal.png，
        // 兄弟文件就是把末尾的 _Color 换掉；也兼容"名字里没有 _Color"的素材。
        constexpr int32 ColorSuffixLength = 6; // "_Color" 的长度
        const bool bHasColorSuffix = Stem.EndsWith(TEXT("_Color"), ESearchCase::IgnoreCase);

        TArray<FString> Candidates;
        if (bHasColorSuffix)
        {
            Candidates.Add(FString::Printf(TEXT("%s%s%s"), *Stem.LeftChop(ColorSuffixLength), ChannelSuffix, *Extension));
        }
        Candidates.Add(FString::Printf(TEXT("%s%s%s"), *Stem, ChannelSuffix, *Extension));

        for (const FString& Candidate : Candidates)
        {
            const FString Path = FPaths::Combine(Directory, Candidate);
            if (FPaths::FileExists(Path))
            {
                return Path;
            }
        }

        return FString();
    }

    FString MakeUploadFileName(const FString& LocalPath)
    {
        const FString FullPath = FPaths::ConvertRelativePathToFull(LocalPath);
        const uint32 PathHash = FCrc::StrCrc32(*FullPath.ToLower());

        const FString BaseName = FPaths::GetBaseFilename(FullPath);
        FString SafeName;
        SafeName.Reserve(BaseName.Len());
        for (const TCHAR Character : BaseName)
        {
            SafeName.AppendChar(
                (FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-'))
                    ? Character
                    : TEXT('_'));
        }

        if (SafeName.IsEmpty())
        {
            SafeName = TEXT("image");
        }

        return FString::Printf(TEXT("Shine_%08x_%s%s"), PathHash, *SafeName, *FPaths::GetExtension(FullPath, true).ToLower());
    }
}
