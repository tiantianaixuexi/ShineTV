#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * ShineAIPaint 模块。
 *
 * 这里不持有 ComfyUI 的连接：那条常连由 ShineEditor 统一提供
 * （Comfy/ShineComfySocket.h），本模块只是订阅它。
 */
IMPLEMENT_MODULE(FDefaultModuleImpl, ShineAIPaint)
