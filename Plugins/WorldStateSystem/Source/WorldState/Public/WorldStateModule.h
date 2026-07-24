#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Single runtime/editor log category owned by the plugin. */
WORLDSTATE_API DECLARE_LOG_CATEGORY_EXTERN(LogWorldState, Log, All);

/** Scoped logging shortcuts required by project policy; callers should include relevant operation identities. */
#define WORLDSTATE_LOG_INFO(Format, ...) UE_LOG(LogWorldState, Log, TEXT(Format), ##__VA_ARGS__)
#define WORLDSTATE_LOG_WARNING(Format, ...) UE_LOG(LogWorldState, Warning, TEXT(Format), ##__VA_ARGS__)
#define WORLDSTATE_LOG_ERROR(Format, ...) UE_LOG(LogWorldState, Error, TEXT(Format), ##__VA_ARGS__)

/** Returns whether module-wide World State visual diagnostics are enabled. */
WORLDSTATE_API bool IsWorldStateVisualDebugEnabled();

/** Runtime module entry point for the World State System plugin. */
class FWorldStateModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
