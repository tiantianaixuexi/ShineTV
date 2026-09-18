#include "Module/ShineToolsRuntimeModule.h"

#include "Tools/DebugDraw/ShineDebugDrawComponent.h"
#include "ShowFlags.h"

void FShineToolsRuntimeModule::StartupModule()
{
	FEngineShowFlags::RegisterCustomShowFlag(
		ShineDebugDraw::ShowFlagName,
		/*DefaultEnabled*/ true,
		SFG_Normal,
		NSLOCTEXT("ShineToolsRuntime", "ShineDebugDrawShowFlag", "Shine Debug Draw"));
}

void FShineToolsRuntimeModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FShineToolsRuntimeModule, ShineToolsRuntime)