#include "GameplayActionsModule.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogGameplayActions);

namespace
{
	TAutoConsoleVariable<int32> CVarGameplayActionsDebug(
		TEXT("GameplayActions.Debug"),
		0,
		TEXT("Enables detailed GameplayActions diagnostics when the component-local bEnableDebug flag is also enabled."),
		ECVF_Default);
}

bool IsGameplayActionsDebugEnabled()
{
	return CVarGameplayActionsDebug.GetValueOnGameThread() != 0;
}

void FGameplayActionsModule::StartupModule()
{
}

void FGameplayActionsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FGameplayActionsModule, GameplayActions)
