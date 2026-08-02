#include "FootstepSystemModule.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogFootstepSystem);

namespace
{
	TAutoConsoleVariable<int32> CVarFootstepSystemDebug(
		TEXT("FootstepSystem.Debug"),
		0,
		TEXT("Enables FootstepSystem diagnostics when a component's local bEnableDebug flag is also enabled."),
		ECVF_Default);
}

bool IsFootstepSystemDebugEnabled()
{
	return CVarFootstepSystemDebug.GetValueOnGameThread() != 0;
}

IMPLEMENT_MODULE(FFootstepSystemModule, FootstepSystem)
