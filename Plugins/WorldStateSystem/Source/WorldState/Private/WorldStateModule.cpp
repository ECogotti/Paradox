#include "WorldStateModule.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogWorldState);

/** Global half of the required global-and-local visual debug gate; disabled by default. */
static TAutoConsoleVariable<int32> CVarWorldStateVisualDebug(
	TEXT("WorldState.Debug.Visual"),
	0,
	TEXT("Globally enables World State visual diagnostics. A participant's local debug flag must also be enabled."),
	ECVF_Default);

bool IsWorldStateVisualDebugEnabled()
{
	return CVarWorldStateVisualDebug.GetValueOnGameThread() != 0;
}

void FWorldStateModule::StartupModule()
{
	// Runtime behavior is owned by per-world subsystems, so module startup has no global UObject state.
	WORLDSTATE_LOG_INFO("WorldState runtime module started.");
}

void FWorldStateModule::ShutdownModule()
{
	// Console variables have static registration lifetime; world-owned state is released by subsystem teardown.
}

IMPLEMENT_MODULE(FWorldStateModule, WorldState)
