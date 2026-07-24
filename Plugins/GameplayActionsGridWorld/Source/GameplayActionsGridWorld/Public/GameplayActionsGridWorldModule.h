#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

GAMEPLAYACTIONSGRIDWORLD_API DECLARE_LOG_CATEGORY_EXTERN(LogGameplayActionsGridWorld, Log, All);

#define GAMEPLAYACTIONSGRIDWORLD_LOG_INFO(Format, ...) UE_LOG(LogGameplayActionsGridWorld, Log, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONSGRIDWORLD_LOG_WARNING(Format, ...) UE_LOG(LogGameplayActionsGridWorld, Warning, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONSGRIDWORLD_LOG_ERROR(Format, ...) UE_LOG(LogGameplayActionsGridWorld, Error, Format, ##__VA_ARGS__)

class FGameplayActionsGridWorldModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
