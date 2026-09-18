#include "Comfy/ShineMentionResolver.h"

#include "Asset/ShineCharacterAsset.h"
#include "Asset/ShineVideoTypes.h"
#include "Comfy/ShineComfyPaths.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    /** 一段输出：要么是普通文本，要么是一个待定的 `<Picture N>` 指代。 */
    struct FPromptSegment
    {
        FString Literal;
        int32 PictureIndex = INDEX_NONE;
    };

    /** 标识（路径 / 角色名）的结束符：空白与常见标点。 */
    bool IsIdentifierStop(TCHAR Character)
    {
        return FChar::IsWhitespace(Character)
            || Character == TEXT(',') || Character == TEXT(';')
            || Character == TEXT(')') || Character == TEXT(']') || Character == TEXT('}')
            || Character == TEXT('"') || Character == TEXT('\'')
            || Character == TEXT('\u3001') || Character == TEXT('\uFF0C') || Character == TEXT('\u3002');
    }

    bool MatchesAt(const FString& Text, int32 Index, const FString& Token, bool bIgnoreCase = false)
    {
        if (Index < 0 || Index + Token.Len() > Text.Len())
        {
            return false;
        }

        return Text.Mid(Index, Token.Len()).Equals(
            Token,
            bIgnoreCase ? ESearchCase::IgnoreCase : ESearchCase::CaseSensitive);
    }

    /**
     * 从 Start 起读一段标识。
     * 用引号包起来时读到配对的引号为止（这样路径里的空格不会被当成结束符）。
     */
    FString ReadIdentifier(const FString& Text, int32 Start, int32& OutEnd)
    {
        OutEnd = FMath::Clamp(Start, 0, Text.Len());
        if (Start < 0 || Start >= Text.Len())
        {
            return FString();
        }

        const TCHAR First = Text[Start];
        if (First == TEXT('"') || First == TEXT('\''))
        {
            int32 Index = Start + 1;
            while (Index < Text.Len() && Text[Index] != First)
            {
                ++Index;
            }

            OutEnd = FMath::Min(Index + 1, Text.Len());
            return Text.Mid(Start + 1, Index - Start - 1);
        }

        int32 Index = Start;
        while (Index < Text.Len() && !IsIdentifierStop(Text[Index]))
        {
            ++Index;
        }

        OutEnd = Index;
        return Text.Mid(Start, Index - Start);
    }

    int32 SkipWhitespace(const FString& Text, int32 Index)
    {
        int32 Cursor = Index;
        while (Cursor < Text.Len() && FChar::IsWhitespace(Text[Cursor]))
        {
            ++Cursor;
        }

        return Cursor;
    }

    /** 参考图去重用的键：解析成绝对路径再统一大小写。 */
    FString MakeImageKey(const FString& LocalPath)
    {
        // FPaths::NormalizeFilename 是就地改的非 const 接口，所以得先拷一份。
        FString MutablePath = LocalPath;
        FPaths::NormalizeFilename(MutablePath);
        return FPaths::ConvertRelativePathToFull(MutablePath).ToLower();
    }
}

FString FShineMentionResolver::ResolveLocalPath(const FString& Identifier)
{
    const FString Trimmed = Identifier.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        return FString();
    }

    // 素材库相对路径是常态（面板拖进来就是相对路径），绝对路径原样用——
    // P0 的参考图就放在 ComfyUI 的 input 目录里，那种情况下没人会想把它复制进素材库。
    FString Candidate = FPaths::IsRelative(Trimmed)
        ? FPaths::Combine(ShineComfyPaths::GetConfiguredMediaLibraryDirectory(), Trimmed)
        : Trimmed;

    FPaths::NormalizeFilename(Candidate);
    return FPaths::ConvertRelativePathToFull(Candidate);
}

const UShineCharacterAsset* FShineMentionResolver::FindCharacterAsset(const FString& Identifier, const TArray<FString>& SearchPaths)
{
    const FString Token = Identifier.TrimStartAndEnd();
    if (Token.IsEmpty())
    {
        return nullptr;
    }

    if (Token.StartsWith(TEXT("/")))
    {
        if (const UShineCharacterAsset* Direct = LoadObject<UShineCharacterAsset>(nullptr, *Token))
        {
            return Direct;
        }
    }

    for (const FString& SearchPath : SearchPaths)
    {
        const FString Path = SearchPath.TrimStartAndEnd();
        if (Path.IsEmpty())
        {
            continue;
        }

        const UShineCharacterAsset* Asset = LoadObject<UShineCharacterAsset>(nullptr, *Path);
        if (!Asset)
        {
            continue;
        }

        if (!Asset->DisplayName.IsEmpty() && Asset->DisplayName.Equals(Token, ESearchCase::IgnoreCase))
        {
            return Asset;
        }

        if (Asset->GetName().Equals(Token, ESearchCase::IgnoreCase))
        {
            return Asset;
        }

        if (FPaths::GetBaseFilename(Path).Equals(Token, ESearchCase::IgnoreCase))
        {
            return Asset;
        }
    }

    return nullptr;
}

FShineMentionResolveResult FShineMentionResolver::Resolve(const FShineMentionResolveRequest& Request)
{
    FShineMentionResolveResult Result;
    Result.bSuccess = true;

    TArray<FString> OrderedPaths;
    TSet<FString> ImageKeys;
    TSet<const UShineCharacterAsset*> ReferencedCharacters;
    TArray<FPromptSegment> Segments;

    auto AppendLiteral = [&Segments](const FString& Text)
    {
        if (Text.IsEmpty())
        {
            return;
        }

        if (Segments.Num() > 0 && Segments.Last().PictureIndex == INDEX_NONE)
        {
            Segments.Last().Literal.Append(Text);
            return;
        }

        FPromptSegment Segment;
        Segment.Literal = Text;
        Segments.Add(MoveTemp(Segment));
    };

    auto AddImage = [&OrderedPaths, &ImageKeys](const FString& Identifier)
    {
        const FString LocalPath = FShineMentionResolver::ResolveLocalPath(Identifier);
        if (LocalPath.IsEmpty())
        {
            return;
        }

        const FString Key = MakeImageKey(LocalPath);
        if (ImageKeys.Contains(Key))
        {
            return;
        }

        ImageKeys.Add(Key);
        OrderedPaths.Add(LocalPath);
    };

    auto AddCharacterImages = [&AddImage](const UShineCharacterAsset& Character)
    {
        for (const FString& ReferenceImage : Character.ReferenceImages)
        {
            AddImage(ReferenceImage);
        }
    };

    // ------------------------------------------------------------ 第一遍：扫提示词

    const FString Prompt = Request.Prompt;
    TArray<FString> SearchPaths = Request.CharacterAssetPaths;
    SearchPaths.Append(Request.ProjectCharacterAssetPaths);

    int32 Cursor = 0;
    while (Cursor < Prompt.Len())
    {
        if (Prompt[Cursor] == TEXT('@') && MatchesAt(Prompt, Cursor, ShineVideoMention::ImagePrefix))
        {
            int32 End = Cursor;
            const FString Identifier = ReadIdentifier(
                Prompt, Cursor + FCString::Strlen(ShineVideoMention::ImagePrefix), End).TrimStartAndEnd();

            if (Identifier.IsEmpty())
            {
                Result.Warnings.Add(TEXT("提示词里有个 `@image:` 后面没写路径，已原样保留。"));
                AppendLiteral(TEXT("@"));
                ++Cursor;
                continue;
            }

            AddImage(Identifier);
            AppendLiteral(TEXT(" "));
            Cursor = End;
            continue;
        }

        if (Prompt[Cursor] == TEXT('@') && MatchesAt(Prompt, Cursor, ShineVideoMention::CharacterPrefix))
        {
            int32 End = Cursor;
            const FString Identifier = ReadIdentifier(
                Prompt, Cursor + FCString::Strlen(ShineVideoMention::CharacterPrefix), End).TrimStartAndEnd();

            if (Identifier.IsEmpty())
            {
                Result.Warnings.Add(TEXT("提示词里有个 `@char:` 后面没写角色名，已原样保留。"));
                AppendLiteral(TEXT("@"));
                ++Cursor;
                continue;
            }

            const UShineCharacterAsset* Character = FindCharacterAsset(Identifier, SearchPaths);
            if (!Character)
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("找不到角色资产“%s”，这个 `@char:` 引用被忽略了。"), *Identifier));
                AppendLiteral(TEXT(" "));
                Cursor = End;
                continue;
            }

            AddCharacterImages(*Character);
            ReferencedCharacters.Add(Character);

            if (!Character->DisplayName.IsEmpty() && !Result.ResolvedCharacters.Contains(Character->DisplayName))
            {
                Result.ResolvedCharacters.Add(Character->DisplayName);
            }

            if (Character->bLockSeed)
            {
                if (!Result.ForcedSeed.IsSet())
                {
                    Result.ForcedSeed = Character->LockedSeed;
                }
                else if (Result.ForcedSeed.GetValue() != Character->LockedSeed)
                {
                    Result.Warnings.Add(FString::Printf(
                        TEXT("角色“%s”要求锁定种子 %d，与前面角色的 %d 冲突，已用先出现的那个。"),
                        *Identifier, Character->LockedSeed, Result.ForcedSeed.GetValue()));
                }
            }

            const FString IdentityPrompt = Character->IdentityPrompt.TrimStartAndEnd();
            AppendLiteral(IdentityPrompt.IsEmpty() ? TEXT(" ") : IdentityPrompt + TEXT(" "));
            Cursor = End;
            continue;
        }

        if (Prompt[Cursor] == TEXT('{') && MatchesAt(Prompt, Cursor, TEXT("{{")))
        {
            int32 Probe = SkipWhitespace(Prompt, Cursor + 2);
            if (MatchesAt(Prompt, Probe, TEXT("Mixed"), true))
            {
                Probe = SkipWhitespace(Prompt, Probe + 5);
                const int32 DigitsStart = Probe;
                while (Probe < Prompt.Len() && FChar::IsDigit(Prompt[Probe]))
                {
                    ++Probe;
                }

                if (Probe > DigitsStart)
                {
                    const int32 Number = FCString::Atoi(*Prompt.Mid(DigitsStart, Probe - DigitsStart));
                    const int32 Closing = SkipWhitespace(Prompt, Probe);
                    if (MatchesAt(Prompt, Closing, TEXT("}}")))
                    {
                        // 指代先记下来，等参考图列表定稿后再翻译成 <Picture N>：
                        // 用户完全可能把 {{Mixed 1}} 写在定义它的那张图前面。
                        FPromptSegment Segment;
                        Segment.PictureIndex = Number;
                        Segments.Add(MoveTemp(Segment));
                        AppendLiteral(TEXT(" "));
                        Cursor = Closing + 2;
                        continue;
                    }
                }
            }
        }

        AppendLiteral(Prompt.Mid(Cursor, 1));
        ++Cursor;
    }

    // ------------------------------------------------ 第二遍：字段里没被提及的引用

    for (const FString& CharacterPath : Request.CharacterAssetPaths)
    {
        const FString Path = CharacterPath.TrimStartAndEnd();
        if (Path.IsEmpty())
        {
            continue;
        }

        const UShineCharacterAsset* Character = LoadObject<UShineCharacterAsset>(nullptr, *Path);
        if (!Character)
        {
            Result.Warnings.Add(FString::Printf(TEXT("分镜的角色表里有条加载不到的资产：“%s”。"), *Path));
            continue;
        }

        if (ReferencedCharacters.Contains(Character))
        {
            // 提示词里已经用 @char: 引过了，图也在 Picture 列表里了。
            continue;
        }

        AddCharacterImages(*Character);
        ReferencedCharacters.Add(Character);

        if (!Character->DisplayName.IsEmpty() && !Result.ResolvedCharacters.Contains(Character->DisplayName))
        {
            Result.ResolvedCharacters.Add(Character->DisplayName);
        }

        const FString IdentityPrompt = Character->IdentityPrompt.TrimStartAndEnd();
        if (!IdentityPrompt.IsEmpty())
        {
            AppendLiteral(TEXT(" ") + IdentityPrompt);
        }
    }

    for (const FString& ReferenceImage : Request.ReferenceImages)
    {
        const FString Trimmed = ReferenceImage.TrimStartAndEnd();
        if (!Trimmed.IsEmpty())
        {
            AddImage(Trimmed);
        }
    }

    // ------------------------------------------------------------ 定稿：拼提示词

    FString ResolvedPrompt;
    for (const FPromptSegment& Segment : Segments)
    {
        if (Segment.PictureIndex == INDEX_NONE)
        {
            ResolvedPrompt.Append(Segment.Literal);
            continue;
        }

        if (Segment.PictureIndex >= 1 && Segment.PictureIndex <= OrderedPaths.Num())
        {
            ResolvedPrompt.Append(FString::Printf(TEXT("<Picture %d>"), Segment.PictureIndex));
            continue;
        }

        Result.Warnings.Add(FString::Printf(
            TEXT("`{{Mixed %d}}` 指向的参考图不存在（当前共 %d 张），这个记号被丢掉了。"),
            Segment.PictureIndex, OrderedPaths.Num()));
    }

    // 记号被摘掉后会留下连续空格，顺手压一下——H3 的 tokenizer 不在乎，但人看着别扭。
    while (ResolvedPrompt.Contains(TEXT("  ")))
    {
        ResolvedPrompt.ReplaceInline(TEXT("  "), TEXT(" "));
    }

    Result.ResolvedPrompt = ResolvedPrompt.TrimStartAndEnd();
    Result.OrderedImagePaths = MoveTemp(OrderedPaths);

    if (Result.OrderedImagePaths.Num() > MaxReferenceImages)
    {
        // 这里只提示，不裁：真正决定"能接几张"的是 builder（链式末帧也要占一个位）。
        Result.Warnings.Add(FString::Printf(
            TEXT("该分镜解析出 %d 张参考图，超过 H3 的 %d 张上限，多余的会被 builder 裁掉。"),
            Result.OrderedImagePaths.Num(), MaxReferenceImages));
    }

    return Result;
}
