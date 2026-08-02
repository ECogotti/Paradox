#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

FOOTSTEPSYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogFootstepSystem, Log, All);

#define FOOTSTEPSYSTEM_LOG_INFO(Format, ...) UE_LOG(LogFootstepSystem, Log, Format, ##__VA_ARGS__)
#define FOOTSTEPSYSTEM_LOG_WARNING(Format, ...) UE_LOG(LogFootstepSystem, Warning, Format, ##__VA_ARGS__)
#define FOOTSTEPSYSTEM_LOG_ERROR(Format, ...) UE_LOG(LogFootstepSystem, Error, Format, ##__VA_ARGS__)

/** Global half of the Global AND Local runtime debug gate. */
FOOTSTEPSYSTEM_API bool IsFootstepSystemDebugEnabled();

/** Runtime module entry point. All per-actor state is owned by UFootstepComponent. */
class FFootstepSystemModule final : public IModuleInterface
{
};
