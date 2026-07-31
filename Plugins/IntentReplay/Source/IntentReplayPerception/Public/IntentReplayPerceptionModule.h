#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

INTENTREPLAYPERCEPTION_API DECLARE_LOG_CATEGORY_EXTERN(LogIntentReplayPerception, Log, All);

#define INTENTREPLAYPERCEPTION_LOG_INFO(Format, ...) \
	UE_LOG(LogIntentReplayPerception, Log, Format, ##__VA_ARGS__)
#define INTENTREPLAYPERCEPTION_LOG_WARNING(Format, ...) \
	UE_LOG(LogIntentReplayPerception, Warning, Format, ##__VA_ARGS__)
#define INTENTREPLAYPERCEPTION_LOG_ERROR(Format, ...) \
	UE_LOG(LogIntentReplayPerception, Error, Format, ##__VA_ARGS__)

INTENTREPLAYPERCEPTION_API bool IsIntentReplayPerceptionDebugEnabled();

class FIntentReplayPerceptionModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
