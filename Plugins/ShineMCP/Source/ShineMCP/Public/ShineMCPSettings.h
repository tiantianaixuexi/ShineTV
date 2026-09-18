#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ShineMCPSettings.generated.h"

/**
 * Shine MCP 插件设置。
 *
 * 可在「项目设置 → 插件 → Shine MCP」中修改，也可直接编辑
 * Config/DefaultEditorPerProjectUserSettings.ini 的 [/Script/ShineMCP.ShineMCPSettings] 段。
 */
UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "Shine MCP"))
class SHINEMCP_API UShineMCPSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UShineMCPSettings();

    /** 编辑器启动后是否自动开启 MCP 服务端。 */
    UPROPERTY(config, EditAnywhere, Category = "MCP")
    bool bAutoStartServer = true;

    /** 监听地址。默认仅本机可访问；填 0.0.0.0 可对局域网开放。 */
    UPROPERTY(config, EditAnywhere, Category = "MCP")
    FString ListenAddress = TEXT("127.0.0.1");

    /** 监听端口。 */
    UPROPERTY(config, EditAnywhere, Category = "MCP")
    int32 Port = 8931;

    /** 服务端名称，会在 MCP initialize 握手中返回。 */
    UPROPERTY(config, EditAnywhere, Category = "MCP")
    FString ServerName = TEXT("ShineMCP");

    /** 服务端版本。 */
    UPROPERTY(config, EditAnywhere, Category = "MCP")
    FString ServerVersion = TEXT("1.0.0");

    /** 单次请求读取超时（秒）。 */
    UPROPERTY(config, EditAnywhere, Category = "MCP")
    int32 RequestTimeoutSeconds = 15;

    /** ComfyUI 服务地址（供 MCP 工具默认使用）。 */
    UPROPERTY(config, EditAnywhere, Category = "MCP")
    FString ComfyBaseUrl = TEXT("http://127.0.0.1:8188");

    virtual FName GetCategoryName() const override;
};
