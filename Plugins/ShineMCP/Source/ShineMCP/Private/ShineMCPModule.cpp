#include "ShineMCPModule.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "ShineMCPServer.h"
#include "ShineMCPSettings.h"
#include "ShineMCPToolRegistry.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Tools/ShineMCPBuiltinTools.h"

#define LOCTEXT_NAMESPACE "FShineMCPModule"

DEFINE_LOG_CATEGORY_STATIC(LogShineMCP, Log, All);

namespace
{
    void ShowToast(const FText& Text, bool bSuccess)
    {
        FNotificationInfo Info(Text);
        Info.ExpireDuration = 8.0f;
        Info.bUseSuccessFailIcons = true;
        TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
        if (Item.IsValid())
        {
            Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
        }
    }
}

void FShineMCPModule::StartupModule()
{
    RegisterBuiltinTools();

    const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
    if (!Settings || Settings->bAutoStartServer)
    {
        const FString Address = Settings ? Settings->ListenAddress : TEXT("127.0.0.1");
        const int32 Port = Settings ? Settings->Port : 8931;

        FString ErrorMessage;
        if (FShineMCPServer::Get().Start(Address, Port, ErrorMessage))
        {
            FShineMCPServer& Server = FShineMCPServer::Get();
            UE_LOG(LogShineMCP, Display, TEXT("MCP 服务已启动：%s"), *Server.GetUrl());
            UE_LOG(LogShineMCP, Display, TEXT("健康检查：%s/health  |  工具数：%d"),
                *Server.GetUrl().LeftChop(4), FShineMCPToolRegistry::Get().GetTools().Num());
            UE_LOG(LogShineMCP, Display, TEXT("可执行文件位置：Plugins/ShineMCP，MCP 客户端把 URL 填 %s 即可接入。"), *Server.GetUrl());
        }
        else
        {
            UE_LOG(LogShineMCP, Error, TEXT("MCP 服务启动失败：%s"), *ErrorMessage);
        }
    }
    else
    {
        UE_LOG(LogShineMCP, Display, TEXT("按设置未自动启动 MCP 服务（bAutoStartServer=false）。"));
    }

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FShineMCPModule::RegisterMenus));
}

void FShineMCPModule::ShutdownModule()
{
    if (UToolMenus::TryGet())
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }

    FShineMCPServer::Get().Stop();
    FShineMCPBuiltinTools::UnregisterAll();
}

void FShineMCPModule::RegisterBuiltinTools()
{
    if (bToolsRegistered)
    {
        return;
    }

    FShineMCPBuiltinTools::RegisterAll();
    bToolsRegistered = true;
}

void FShineMCPModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    if (!ToolsMenu)
    {
        return;
    }

    FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("ShineMCP"), LOCTEXT("ShineMCPSection", "Shine MCP"));

    Section.AddMenuEntry(
        TEXT("ShineMCP_ShowStatus"),
        LOCTEXT("ShineMCPShowStatus", "Shine MCP 状态"),
        LOCTEXT("ShineMCPShowStatusTooltip", "显示当前 MCP 服务地址与工具数量"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FShineMCPServer& Server = FShineMCPServer::Get();
            if (Server.IsRunning())
            {
                ShowToast(FText::FromString(FString::Printf(
                    TEXT("Shine MCP 运行中\n%s\n工具数：%d"),
                    *Server.GetUrl(), FShineMCPToolRegistry::Get().GetTools().Num())), true);
            }
            else
            {
                ShowToast(LOCTEXT("ShineMCPStopped", "Shine MCP 未运行"), false);
            }
        })));

    Section.AddMenuEntry(
        TEXT("ShineMCP_Restart"),
        LOCTEXT("ShineMCPRestart", "重启 Shine MCP 服务"),
        LOCTEXT("ShineMCPRestartTooltip", "停止后按设置里的地址与端口重新启动"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            const UShineMCPSettings* Settings = GetDefault<UShineMCPSettings>();
            const FString Address = Settings ? Settings->ListenAddress : TEXT("127.0.0.1");
            const int32 Port = Settings ? Settings->Port : 8931;

            FShineMCPServer::Get().Stop();

            FString ErrorMessage;
            const bool bStarted = FShineMCPServer::Get().Start(Address, Port, ErrorMessage);
            ShowToast(bStarted
                ? FText::FromString(FString::Printf(TEXT("Shine MCP 已重启：%s"), *FShineMCPServer::Get().GetUrl()))
                : FText::FromString(FString::Printf(TEXT("Shine MCP 启动失败：%s"), *ErrorMessage)),
                bStarted);
        })));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShineMCPModule, ShineMCP)
