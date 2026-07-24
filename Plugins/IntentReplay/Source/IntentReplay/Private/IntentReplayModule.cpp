#include "IntentReplayModule.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogIntentReplay);

namespace
{
	// Detailed instance logging is intentionally gated globally and locally to keep disabled cost low.
	TAutoConsoleVariable<int32> CVarIntentReplayDebug(
		TEXT("IntentReplay.Debug"),
		0,
		TEXT("Enables detailed IntentReplay diagnostics when the component-local bEnableDebug flag is also enabled."),
		ECVF_Default);
}

bool IsIntentReplayDebugEnabled()
{
	return CVarIntentReplayDebug.GetValueOnGameThread() != 0;
}

void FIntentReplayModule::StartupModule()
{
}

void FIntentReplayModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FIntentReplayModule, IntentReplay)
