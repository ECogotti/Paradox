#include "PerceptionKnowledgeModule.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogPerceptionKnowledge);

namespace
{
	TAutoConsoleVariable<int32> CVarPerceptionKnowledgeDebug(
		TEXT("PerceptionKnowledge.Debug"),
		0,
		TEXT("Enables PerceptionKnowledge visual diagnostics when the relevant component-local flag is also enabled."),
		ECVF_Default);
	FPerceptionKnowledgeDebugConfigurationChanged
		DebugConfigurationChanged;
	int32 LastDebugValue = 0;

	void HandleConsoleVariablesChanged()
	{
		const int32 CurrentValue =
			CVarPerceptionKnowledgeDebug.GetValueOnGameThread();
		if (CurrentValue != LastDebugValue)
		{
			LastDebugValue = CurrentValue;
			DebugConfigurationChanged.Broadcast();
		}
	}
}

bool IsPerceptionKnowledgeDebugEnabled()
{
	return CVarPerceptionKnowledgeDebug.GetValueOnGameThread() != 0;
}

FPerceptionKnowledgeDebugConfigurationChanged&
GetPerceptionKnowledgeDebugConfigurationChanged()
{
	return DebugConfigurationChanged;
}

void FPerceptionKnowledgeModule::StartupModule()
{
	LastDebugValue = CVarPerceptionKnowledgeDebug.GetValueOnGameThread();
	ConsoleVariableSinkHandle =
		IConsoleManager::Get().RegisterConsoleVariableSink_Handle(
			FConsoleCommandDelegate::CreateStatic(
				&HandleConsoleVariablesChanged));
	bConsoleVariableSinkRegistered = true;
}

void FPerceptionKnowledgeModule::ShutdownModule()
{
	if (bConsoleVariableSinkRegistered)
	{
		IConsoleManager::Get().UnregisterConsoleVariableSink_Handle(
			ConsoleVariableSinkHandle);
		bConsoleVariableSinkRegistered = false;
	}
	DebugConfigurationChanged.Clear();
}

IMPLEMENT_MODULE(FPerceptionKnowledgeModule, PerceptionKnowledge)
