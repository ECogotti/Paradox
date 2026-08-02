#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

PERCEPTIONKNOWLEDGE_API DECLARE_LOG_CATEGORY_EXTERN(LogPerceptionKnowledge, Log, All);

#define PERCEPTIONKNOWLEDGE_LOG_INFO(Format, ...) UE_LOG(LogPerceptionKnowledge, Log, Format, ##__VA_ARGS__)
#define PERCEPTIONKNOWLEDGE_LOG_WARNING(Format, ...) UE_LOG(LogPerceptionKnowledge, Warning, Format, ##__VA_ARGS__)
#define PERCEPTIONKNOWLEDGE_LOG_ERROR(Format, ...) UE_LOG(LogPerceptionKnowledge, Error, Format, ##__VA_ARGS__)

/** Global half of the Global AND Local runtime debug gate. */
PERCEPTIONKNOWLEDGE_API bool IsPerceptionKnowledgeDebugEnabled();

DECLARE_MULTICAST_DELEGATE(FPerceptionKnowledgeDebugConfigurationChanged);
PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeDebugConfigurationChanged&
GetPerceptionKnowledgeDebugConfigurationChanged();

/** Runtime module entry point. World state remains owned by components and world subsystems. */
class FPerceptionKnowledgeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FConsoleVariableSinkHandle ConsoleVariableSinkHandle;
	bool bConsoleVariableSinkRegistered = false;
};
