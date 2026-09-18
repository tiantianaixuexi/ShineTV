#include "ShineMCPHttpClient.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
    constexpr int32 ReadChunkSize = 64 * 1024;
    constexpr int64 MaxResponseBytes = 512LL * 1024 * 1024;

    bool SendAll(FSocket* Socket, const uint8* Data, int64 Length, double DeadlineSeconds)
    {
        int64 Sent = 0;
        while (Sent < Length)
        {
            if (FPlatformTime::Seconds() > DeadlineSeconds)
            {
                return false;
            }

            const int32 ToSend = static_cast<int32>(FMath::Min<int64>(Length - Sent, 256 * 1024));
            int32 BytesSent = 0;
            if (Socket->Send(Data + Sent, ToSend, BytesSent) && BytesSent > 0)
            {
                Sent += BytesSent;
            }
            else
            {
                FPlatformProcess::Sleep(0.0005f);
            }
        }

        return true;
    }

    int32 FindHeaderEnd(const TArray<uint8>& Buffer)
    {
        for (int32 Index = 0; Index + 3 < Buffer.Num(); ++Index)
        {
            if (Buffer[Index] == '\r' && Buffer[Index + 1] == '\n' && Buffer[Index + 2] == '\r' && Buffer[Index + 3] == '\n')
            {
                return Index + 4;
            }
        }
        return INDEX_NONE;
    }

    struct FResponseHead
    {
        int32 StatusCode = 0;
        int32 HeaderEnd = INDEX_NONE;
        int64 ContentLength = -1;
        bool bChunked = false;
        FString ContentType;
    };

    FResponseHead ParseResponseHead(const TArray<uint8>& Buffer)
    {
        FResponseHead Head;
        Head.HeaderEnd = FindHeaderEnd(Buffer);
        if (Head.HeaderEnd == INDEX_NONE)
        {
            return Head;
        }

        const FString HeaderBlock = FString(Head.HeaderEnd, reinterpret_cast<const ANSICHAR*>(Buffer.GetData()));
        TArray<FString> Lines;
        HeaderBlock.ParseIntoArrayLines(Lines, false);
        if (Lines.Num() == 0)
        {
            return Head;
        }

        int32 FirstSpace = INDEX_NONE;
        if (Lines[0].FindChar(TEXT(' '), FirstSpace))
        {
            Head.StatusCode = FCString::Atoi(*Lines[0].RightChop(FirstSpace + 1).Left(3));
        }

        for (int32 Index = 1; Index < Lines.Num(); ++Index)
        {
            FString Key;
            FString Value;
            if (!Lines[Index].Split(TEXT(":"), &Key, &Value))
            {
                continue;
            }

            const FString TrimmedKey = Key.TrimStartAndEnd().ToLower();
            const FString TrimmedValue = Value.TrimStartAndEnd();

            if (TrimmedKey == TEXT("content-type"))
            {
                Head.ContentType = TrimmedValue;
            }
            else if (TrimmedKey == TEXT("content-length"))
            {
                Head.ContentLength = FCString::Atoi64(*TrimmedValue);
            }
            else if (TrimmedKey == TEXT("transfer-encoding") && TrimmedValue.Contains(TEXT("chunked"), ESearchCase::IgnoreCase))
            {
                Head.bChunked = true;
            }
        }

        return Head;
    }

    /** chunked 传输体是否已经收到结束块（长度为 0 的 chunk）。 */
    bool IsChunkedBodyComplete(const TArray<uint8>& Buffer, int32 Offset)
    {
        int32 Cursor = Offset;
        while (true)
        {
            int32 LineEnd = Cursor;
            while (LineEnd + 1 < Buffer.Num() && !(Buffer[LineEnd] == '\r' && Buffer[LineEnd + 1] == '\n'))
            {
                ++LineEnd;
            }

            if (LineEnd + 1 >= Buffer.Num())
            {
                return false;
            }

            FString LengthLine = FString(LineEnd - Cursor, reinterpret_cast<const ANSICHAR*>(Buffer.GetData() + Cursor));
            LengthLine.TrimStartAndEndInline();

            int32 SemicolonIndex = INDEX_NONE;
            if (LengthLine.FindChar(TEXT(';'), SemicolonIndex))
            {
                LengthLine = LengthLine.Left(SemicolonIndex).TrimStartAndEnd();
            }

            const int64 ChunkSize = FParse::HexNumber64(*LengthLine);
            Cursor = LineEnd + 2;

            if (ChunkSize <= 0)
            {
                return true;
            }

            if (Buffer.Num() < Cursor + ChunkSize + 2)
            {
                return false;
            }

            Cursor += static_cast<int32>(ChunkSize) + 2;
        }
    }

    void DecodeChunked(const TArray<uint8>& In, TArray<uint8>& Out)
    {
        int32 Offset = 0;
        while (Offset < In.Num())
        {
            int32 LineEnd = Offset;
            while (LineEnd + 1 < In.Num() && !(In[LineEnd] == '\r' && In[LineEnd + 1] == '\n'))
            {
                ++LineEnd;
            }

            if (LineEnd + 1 >= In.Num())
            {
                break;
            }

            FString LengthLine = FString(LineEnd - Offset, reinterpret_cast<const ANSICHAR*>(In.GetData() + Offset));
            LengthLine.TrimStartAndEndInline();

            int32 SemicolonIndex = INDEX_NONE;
            if (LengthLine.FindChar(TEXT(';'), SemicolonIndex))
            {
                LengthLine = LengthLine.Left(SemicolonIndex).TrimStartAndEnd();
            }

            const int64 ChunkSize = FParse::HexNumber64(*LengthLine);
            Offset = LineEnd + 2;
            if (ChunkSize <= 0)
            {
                break;
            }

            const int64 Available = In.Num() - Offset;
            const int64 ToCopy = FMath::Min(ChunkSize, Available);
            Out.Append(In.GetData() + Offset, static_cast<int32>(ToCopy));
            Offset += static_cast<int32>(ToCopy) + 2;
        }
    }
}

bool FShineMCPHttpClient::ParseUrl(const FString& Url, FString& OutHost, int32& OutPort, FString& OutPath)
{
    FString Remaining = Url;

    bool bHttps = false;
    if (Remaining.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
    {
        Remaining = Remaining.RightChop(8);
        bHttps = true;
    }
    else if (Remaining.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
    {
        Remaining = Remaining.RightChop(7);
    }

    FString Authority;
    FString PathPart;
    if (!Remaining.Split(TEXT("/"), &Authority, &PathPart))
    {
        Authority = Remaining;
        PathPart.Reset();
    }
    OutPath = FString(TEXT("/")) + PathPart;

    OutPort = bHttps ? 443 : 80;

    FString HostPart;
    FString PortPart;
    if (Authority.Split(TEXT(":"), &HostPart, &PortPart))
    {
        OutHost = HostPart;
        OutPort = FCString::Atoi(*PortPart);
    }
    else
    {
        OutHost = Authority;
    }

    OutHost.TrimStartAndEndInline();
    return !OutHost.IsEmpty();
}

bool FShineMCPHttpClient::LoadFileBytes(const FString& FilePath, TArray<uint8>& OutBytes, FString& OutError)
{
    if (!FPaths::FileExists(FilePath))
    {
        OutError = FString::Printf(TEXT("文件不存在: %s"), *FilePath);
        return false;
    }

    if (!FFileHelper::LoadFileToArray(OutBytes, *FilePath))
    {
        OutError = FString::Printf(TEXT("读取文件失败: %s"), *FilePath);
        return false;
    }

    return true;
}

bool FShineMCPHttpClient::SaveFileBytes(const FString& FilePath, const TArray<uint8>& Bytes, FString& OutError)
{
    const FString Directory = FPaths::GetPath(FilePath);
    if (!Directory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    if (!FFileHelper::SaveArrayToFile(Bytes, *FilePath))
    {
        OutError = FString::Printf(TEXT("写入文件失败: %s"), *FilePath);
        return false;
    }

    return true;
}

FString FShineMCPHttpClient::ExtractFilenameFromUrl(const FString& Url)
{
    FString Path = Url;
    int32 QuestionIndex = INDEX_NONE;
    if (Path.FindChar(TEXT('?'), QuestionIndex))
    {
        Path = Path.Left(QuestionIndex);
    }

    int32 SlashIndex = INDEX_NONE;
    if (Path.FindLastChar(TEXT('/'), SlashIndex))
    {
        return Path.RightChop(SlashIndex + 1);
    }
    return Path;
}

FShineMCPHttpResult FShineMCPHttpClient::Request(
    const FString& Verb,
    const FString& Url,
    const TArray<FString>& ExtraHeaders,
    const TArray<uint8>& Body,
    const FString& ContentType,
    int32 TimeoutSeconds)
{
    FShineMCPHttpResult Result;

    FString Host;
    int32 Port = 80;
    FString Path;
    if (!ParseUrl(Url, Host, Port, Path))
    {
        Result.Error = FString::Printf(TEXT("无法解析 URL: %s"), *Url);
        return Result;
    }

    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        Result.Error = TEXT("Socket 子系统不可用。");
        return Result;
    }

    const double Deadline = FPlatformTime::Seconds() + FMath::Max(1, TimeoutSeconds);

    TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
    bool bValidAddress = false;
    Address->SetIp(*Host, bValidAddress);
    Address->SetPort(Port);
    if (!bValidAddress)
    {
        Result.Error = FString::Printf(TEXT("无法解析主机地址: %s"), *Host);
        return Result;
    }

    FSocket* Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("ShineMCPHttp"), false);
    if (!Socket)
    {
        Result.Error = TEXT("创建 Socket 失败。");
        return Result;
    }

    Socket->SetNoDelay(true);

    // 目标是本机/局域网服务：先用阻塞 connect（对端拒绝会立刻返回），成功后转非阻塞做收发。
    if (!Socket->Connect(*Address))
    {
        Result.Error = FString::Printf(TEXT("连接失败: %s:%d"), *Host, Port);
        SocketSubsystem->DestroySocket(Socket);
        return Result;
    }

    Socket->SetNonBlocking(true);

    // 组装请求
    FString HeaderText = FString::Printf(TEXT("%s %s HTTP/1.1\r\n"), *Verb, *Path);
    HeaderText += FString::Printf(TEXT("Host: %s:%d\r\n"), *Host, Port);
    HeaderText += TEXT("User-Agent: ShineMCP/1.0 (Unreal Engine)\r\n");
    HeaderText += TEXT("Accept: */*\r\n");
    HeaderText += TEXT("Accept-Encoding: identity\r\n");
    HeaderText += TEXT("Connection: close\r\n");
    if (!ContentType.IsEmpty())
    {
        HeaderText += FString::Printf(TEXT("Content-Type: %s\r\n"), *ContentType);
    }
    HeaderText += FString::Printf(TEXT("Content-Length: %d\r\n"), Body.Num());
    for (const FString& Header : ExtraHeaders)
    {
        HeaderText += Header + TEXT("\r\n");
    }
    HeaderText += TEXT("\r\n");

    FTCHARToUTF8 HeaderUtf8(*HeaderText);
    TArray<uint8> RequestBytes;
    RequestBytes.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
    RequestBytes.Append(Body);

    if (!SendAll(Socket, RequestBytes.GetData(), RequestBytes.Num(), Deadline))
    {
        Result.Error = TEXT("发送请求失败或超时。");
        SocketSubsystem->DestroySocket(Socket);
        return Result;
    }

    // ---------------------------------------------------------------- 读响应
    // 关键：一旦头部解析出 Content-Length / chunked 就按声明长度收完即止。
    // 若这里改成"读到连接关闭"，遇到 keep-alive 的服务端就会一直等到超时，
    // 把调用线程（游戏线程）整段卡住 —— 编辑器会表现为假死。
    TArray<uint8> RawResponse;
    TArray<uint8> Chunk;
    Chunk.SetNumUninitialized(ReadChunkSize);

    FResponseHead Head;
    bool bConnectionClosed = false;

    while (FPlatformTime::Seconds() <= Deadline && RawResponse.Num() < MaxResponseBytes)
    {
        if (Head.HeaderEnd == INDEX_NONE)
        {
            Head = ParseResponseHead(RawResponse);

            if (Head.HeaderEnd == INDEX_NONE)
            {
                // 头部还没收全；如果对端已经关了就直接放弃。
                if (bConnectionClosed && RawResponse.Num() > 0)
                {
                    break;
                }
            }
        }

        if (Head.HeaderEnd != INDEX_NONE)
        {
            if (Head.ContentLength >= 0 && static_cast<int64>(RawResponse.Num()) >= Head.HeaderEnd + Head.ContentLength)
            {
                break;
            }

            if (Head.ContentLength < 0 && !Head.bChunked && bConnectionClosed)
            {
                break; // 没有长度信息，只能读到关闭
            }

            if (Head.bChunked && IsChunkedBodyComplete(RawResponse, Head.HeaderEnd))
            {
                break;
            }
        }

        int32 BytesRead = 0;
        if (Socket->Recv(Chunk.GetData(), Chunk.Num(), BytesRead, ESocketReceiveFlags::None) && BytesRead > 0)
        {
            RawResponse.Append(Chunk.GetData(), BytesRead);
            continue;
        }

        if (Socket->GetConnectionState() == ESocketConnectionState::SCS_NotConnected)
        {
            bConnectionClosed = true;
            if (Head.HeaderEnd != INDEX_NONE)
            {
                break;
            }
        }

        FPlatformProcess::Sleep(0.0005f);
    }

    Socket->Close();
    SocketSubsystem->DestroySocket(Socket);

    if (Head.HeaderEnd == INDEX_NONE)
    {
        Result.Error = RawResponse.Num() > 0
            ? TEXT("响应头解析失败。")
            : TEXT("服务端没有返回任何数据（是否已启动？）。");
        return Result;
    }

    Result.StatusCode = Head.StatusCode;
    Result.ContentType = Head.ContentType;

    TArray<uint8> Payload;
    Payload.Append(RawResponse.GetData() + Head.HeaderEnd, RawResponse.Num() - Head.HeaderEnd);

    if (Head.bChunked)
    {
        TArray<uint8> Decoded;
        DecodeChunked(Payload, Decoded);
        Payload = MoveTemp(Decoded);
    }
    else if (Head.ContentLength >= 0 && Payload.Num() > Head.ContentLength)
    {
        Payload.SetNum(static_cast<int32>(Head.ContentLength));
    }

    Result.RawBody = MoveTemp(Payload);
    Result.Body = FString(Result.RawBody.Num(), reinterpret_cast<const ANSICHAR*>(Result.RawBody.GetData()));
    Result.bSuccess = Result.StatusCode >= 200 && Result.StatusCode < 400;
    if (!Result.bSuccess && Result.Error.IsEmpty())
    {
        Result.Error = FString::Printf(TEXT("HTTP %d"), Result.StatusCode);
    }

    return Result;
}

FShineMCPHttpResult FShineMCPHttpClient::UploadMultipart(
    const FString& Url,
    const TArray<TPair<FString, FString>>& TextFields,
    const TArray<FMultipartFile>& Files,
    int32 TimeoutSeconds)
{
    const FString Boundary = FString::Printf(TEXT("----ShineMCPBoundary%08x%08x"), FMath::Rand(), FMath::Rand());

    TArray<uint8> Body;
    auto AppendString = [&Body](const FString& Text)
    {
        FTCHARToUTF8 Utf8(*Text);
        Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    };

    for (const TPair<FString, FString>& Field : TextFields)
    {
        AppendString(FString::Printf(TEXT("--%s\r\n"), *Boundary));
        AppendString(FString::Printf(TEXT("Content-Disposition: form-data; name=\"%s\"\r\n\r\n"), *Field.Key));
        AppendString(Field.Value);
        AppendString(TEXT("\r\n"));
    }

    for (const FMultipartFile& File : Files)
    {
        AppendString(FString::Printf(TEXT("--%s\r\n"), *Boundary));
        AppendString(FString::Printf(TEXT("Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"),
            *File.FieldName, *File.FileName));
        AppendString(FString::Printf(TEXT("Content-Type: %s\r\n\r\n"), *File.ContentType));
        Body.Append(File.Data);
        AppendString(TEXT("\r\n"));
    }

    AppendString(FString::Printf(TEXT("--%s--\r\n"), *Boundary));

    return Request(TEXT("POST"), Url, TArray<FString>(),
        Body, FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary), TimeoutSeconds);
}
