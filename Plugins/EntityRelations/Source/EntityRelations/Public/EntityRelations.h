#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IConsoleObject;

ENTITYRELATIONS_API DECLARE_LOG_CATEGORY_EXTERN(LogEntityRelations, Log, All);

#define ENTITYRELATIONS_LOG_INFO(Format, ...) UE_LOG(LogEntityRelations, Log, TEXT(Format), ##__VA_ARGS__)
#define ENTITYRELATIONS_LOG_WARNING(Format, ...) UE_LOG(LogEntityRelations, Warning, TEXT(Format), ##__VA_ARGS__)
#define ENTITYRELATIONS_LOG_ERROR(Format, ...) UE_LOG(LogEntityRelations, Error, TEXT(Format), ##__VA_ARGS__)

/** Returns the effective module-wide debug gate. The console override takes precedence over project settings. */
ENTITYRELATIONS_API bool IsEntityRelationsGlobalDebugEnabled();

/** Runtime module entry point and owner of process-wide console diagnostics. */
class FEntityRelationsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TArray<IConsoleObject*> ConsoleCommands;
};
