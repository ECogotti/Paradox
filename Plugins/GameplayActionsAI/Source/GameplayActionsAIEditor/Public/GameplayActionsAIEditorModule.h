#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

GAMEPLAYACTIONSAIEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogGameplayActionsAIEditor, Log, All);

#define GAMEPLAYACTIONSAIEDITOR_LOG_INFO(Format, ...) UE_LOG(LogGameplayActionsAIEditor, Log, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONSAIEDITOR_LOG_WARNING(Format, ...) UE_LOG(LogGameplayActionsAIEditor, Warning, Format, ##__VA_ARGS__)
#define GAMEPLAYACTIONSAIEDITOR_LOG_ERROR(Format, ...) UE_LOG(LogGameplayActionsAIEditor, Error, Format, ##__VA_ARGS__)

class FGameplayActionsAIEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
