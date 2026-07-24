// Copyright (c) 2021 Evgeniy Oshmarin

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

LINEOFSIGHT_API DECLARE_LOG_CATEGORY_EXTERN(LogLineOfSight, Log, All);

#define LINEOFSIGHT_LOG_INFO(Format, ...) UE_LOG(LogLineOfSight, Log, Format, ##__VA_ARGS__)
#define LINEOFSIGHT_LOG_WARNING(Format, ...) UE_LOG(LogLineOfSight, Warning, Format, ##__VA_ARGS__)
#define LINEOFSIGHT_LOG_ERROR(Format, ...) UE_LOG(LogLineOfSight, Error, Format, ##__VA_ARGS__)

/** Global kill switch for all LineOfSight-owned visual debug output. */
LINEOFSIGHT_API bool IsLineOfSightGlobalDebugEnabled();

class FLineOfSightModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
