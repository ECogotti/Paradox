#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

GAMEPLAYACTIONS_API DECLARE_LOG_CATEGORY_EXTERN(LogGameplayActions, Log, All);

#define GAMEPLAYACTIONS_LOG_INFO(Format, ...) UE_LOG(LogGameplayActions, Log, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONS_LOG_WARNING(Format, ...) UE_LOG(LogGameplayActions, Warning, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONS_LOG_ERROR(Format, ...) UE_LOG(LogGameplayActions, Error, Format, ##__VA_ARGS__)

GAMEPLAYACTIONS_API bool IsGameplayActionsDebugEnabled();

class FGameplayActionsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
