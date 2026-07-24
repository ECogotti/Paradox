#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

INTENTREPLAY_API DECLARE_LOG_CATEGORY_EXTERN(LogIntentReplay, Log, All);

/** Module-scoped logging helpers required by project diagnostics conventions. */
#define INTENTREPLAY_LOG_INFO(Format, ...) UE_LOG(LogIntentReplay, Log, Format, ##__VA_ARGS__)
#define INTENTREPLAY_LOG_WARNING(Format, ...) UE_LOG(LogIntentReplay, Warning, Format, ##__VA_ARGS__)
#define INTENTREPLAY_LOG_ERROR(Format, ...) UE_LOG(LogIntentReplay, Error, Format, ##__VA_ARGS__)

/** Global half of the Global AND Local debug gate controlled by IntentReplay.Debug. */
INTENTREPLAY_API bool IsIntentReplayDebugEnabled();

/** Runtime module entry point; policy/session objects remain owned by their components. */
class FIntentReplayModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
