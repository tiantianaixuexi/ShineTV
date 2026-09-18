#include "WebSocket/ShineWebSocketObj.h"

#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"


UShineWebSocketObj::UShineWebSocketObj()
{
    
}

void UShineWebSocketObj::Tick(float DeltaTime)
{
    
}

void UShineWebSocketObj::Initialize(const FString& InUrl)
{
    if (Url == InUrl)
    {
        return;
    }

    Url = InUrl;
    ResetWebSocket();
}

bool UShineWebSocketObj::Connect()
{
    if (Url.IsEmpty())
    {
        return false;
    }

    if (!WebSocket.IsValid())
    {
        SetupWebSocket();
    }

    if (!WebSocket.IsValid() || WebSocket->IsConnected())
    {
        return WebSocket.IsValid() && WebSocket->IsConnected();
    }

    WebSocket->Connect();
    return true;
}

void UShineWebSocketObj::Close()
{
    ResetWebSocket();
}

bool UShineWebSocketObj::SendText(const FString& Message)
{
    if (!WebSocket.IsValid() || !WebSocket->IsConnected())
    {
        return false;
    }

    WebSocket->Send(Message);
    return true;
}

bool UShineWebSocketObj::IsConnected() const
{
    return WebSocket.IsValid() && WebSocket->IsConnected();
}

const FString& UShineWebSocketObj::GetUrl() const
{
    return Url;
}

void UShineWebSocketObj::BeginDestroy()
{
    ResetWebSocket();
    Super::BeginDestroy();
}

void UShineWebSocketObj::SetupWebSocket()
{
    ResetWebSocket();
    if (Url.IsEmpty())
    {
        return;
    }

    FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    WebSocket = FWebSocketsModule::Get().CreateWebSocket(Url);

    WebSocket->OnMessage().AddUObject(this, &UShineWebSocketObj::OnWebSocketMessageReceived);
    WebSocket->OnConnected().AddUObject(this, &UShineWebSocketObj::OnWebSocketConnected);
    WebSocket->OnConnectionError().AddUObject(this, &UShineWebSocketObj::OnWebSocketConnectionError);
    WebSocket->OnClosed().AddUObject(this, &UShineWebSocketObj::OnWebSocketClosed);
}

void UShineWebSocketObj::ResetWebSocket()
{
    if (!WebSocket.IsValid())
    {
        return;
    }

    WebSocket->OnMessage().Clear();
    WebSocket->OnConnected().Clear();
    WebSocket->OnConnectionError().Clear();
    WebSocket->OnClosed().Clear();

    if (WebSocket->IsConnected())
    {
        WebSocket->Close();
    }

    WebSocket.Reset();
}

void UShineWebSocketObj::OnWebSocketMessageReceived(const FString& Message)
{
    UE_LOG(LogTemp, Log, TEXT("Received WebSocket message: %s"), *Message);
    if (MessageEvent.IsBound())
    {
        MessageEvent.Broadcast(Message);
    }
}

void UShineWebSocketObj::OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    if (ClosedEvent.IsBound())
    {
        ClosedEvent.Broadcast(StatusCode, Reason, bWasClean);
    }
}

void UShineWebSocketObj::OnWebSocketConnectionError(const FString& Message)
{
    if (ConnectionErrorEvent.IsBound())
    {
        ConnectionErrorEvent.Broadcast(Message);
    }
}


void UShineWebSocketObj::OnWebSocketConnected()
{
    if (ConnectedEvent.IsBound())
    {
        ConnectedEvent.Broadcast();
    }
}
