#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

GAMEPLAYACTIONSAI_API DECLARE_LOG_CATEGORY_EXTERN(LogGameplayActionsAI, Log, All);

#define GAMEPLAYACTIONSAI_LOG_INFO(Format, ...) UE_LOG(LogGameplayActionsAI, Log, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONSAI_LOG_WARNING(Format, ...) UE_LOG(LogGameplayActionsAI, Warning, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONSAI_LOG_ERROR(Format, ...) UE_LOG(LogGameplayActionsAI, Error, Format, ##__VA_ARGS__)

class FGameplayActionsAIModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
