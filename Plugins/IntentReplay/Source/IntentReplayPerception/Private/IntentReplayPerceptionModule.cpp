#include "IntentReplayPerceptionModule.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogIntentReplayPerception);

namespace
{
	TAutoConsoleVariable<int32> CVarIntentReplayPerceptionDebug(
		TEXT("IntentReplayPerception.Debug"),
		0,
		TEXT("Global gate for IntentReplayPerception runtime debug drawing. 0=off, 1=on."),
		ECVF_Default);
}

bool IsIntentReplayPerceptionDebugEnabled()
{
	return CVarIntentReplayPerceptionDebug.GetValueOnGameThread() != 0;
}

void FIntentReplayPerceptionModule::StartupModule()
{
}

void FIntentReplayPerceptionModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FIntentReplayPerceptionModule, IntentReplayPerception)
