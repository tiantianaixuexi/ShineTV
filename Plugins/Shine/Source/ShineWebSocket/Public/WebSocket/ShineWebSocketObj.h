#pragma once

#include "CoreMinimal.h"
#include "IWebSocket.h"
#include "ShineWebSocketObj.generated.h"

DECLARE_EVENT(UShineWebSocketObj, FWebSocketConnected);
DECLARE_EVENT_OneParam(UShineWebSocketObj, FWebSocketConnectionError, const FString& /* Error */);
DECLARE_EVENT_ThreeParams(UShineWebSocketObj, FWebSocketClosed, int32 /* StatusCode */, const FString& /* Reason */, bool /* bWasClean */);
DECLARE_EVENT_OneParam(UShineWebSocketObj, FWebSocketMessage, const FString& /* MessageString */);

UCLASS()
class SHINEWEBSOCKET_API UShineWebSocketObj : public UObject,public FTickableEditorObject
{
    GENERATED_BODY()


public:

    UShineWebSocketObj();

    void Initialize(const FString& InUrl);
    bool Connect();
    void Close();
    bool SendText(const FString& Message);
    bool IsConnected() const;
    const FString& GetUrl() const;
    virtual void BeginDestroy() override;
    
    FWebSocketConnected ConnectedEvent;
    FWebSocketClosed ClosedEvent;
    FWebSocketConnectionError ConnectionErrorEvent;
    FWebSocketMessage MessageEvent;
    
    
    virtual void Tick(float DeltaTime) override;
    
    virtual ETickableTickType GetTickableTickType() const override
    {
        return ETickableTickType::Always;
    }

    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FMyTickableThing, STATGROUP_Tickables);
    }	
    
    virtual bool IsTickableWhenPaused() const
    {
        return true;
    }
    virtual bool IsTickableInEditor() const
    {
        return true;
    }
    

private:
    TSharedPtr<IWebSocket> WebSocket;
    FString Url;

    void SetupWebSocket();
    void ResetWebSocket();
    void OnWebSocketMessageReceived(const FString& Message);
    void OnWebSocketClosed( int32  StatusCode , const FString&  Reason , bool  bWasClean );
    void OnWebSocketConnectionError(const FString& Message);
    void OnWebSocketConnected();
};