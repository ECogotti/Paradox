#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IConsoleObject;

TACTICALPAUSE_API DECLARE_LOG_CATEGORY_EXTERN(LogTacticalPause, Log, All);

/** Module-scoped shortcuts keep committed runtime logs out of LogTemp. */
#define TACTICALPAUSE_LOG_INFO(Format, ...) UE_LOG(LogTacticalPause, Log, TEXT(Format), ##__VA_ARGS__)
#define TACTICALPAUSE_LOG_WARNING(Format, ...) UE_LOG(LogTacticalPause, Warning, TEXT(Format), ##__VA_ARGS__)
#define TACTICALPAUSE_LOG_ERROR(Format, ...) UE_LOG(LogTacticalPause, Error, TEXT(Format), ##__VA_ARGS__)

/** Runtime module entry point and owner of Tactical Pause console diagnostics. */
class FTacticalPauseModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TArray<IConsoleObject*> ConsoleCommands;
};
