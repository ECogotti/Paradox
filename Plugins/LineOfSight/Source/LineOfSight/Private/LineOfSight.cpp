// Copyright (c) 2021 Evgeniy Oshmarin

#include "LineOfSight.h"

#define LOCTEXT_NAMESPACE "FLineOfSightModule"

DEFINE_LOG_CATEGORY(LogLineOfSight);

namespace
{
	TAutoConsoleVariable<int32> CVarLineOfSightDebug(
		TEXT("LineOfSight.Debug"),
		0,
		TEXT("Global LineOfSight visual debug switch. 0 disables all plugin debug drawing, 1 enables local debug flags."),
		ECVF_Default);
}

bool IsLineOfSightGlobalDebugEnabled()
{
	return CVarLineOfSightDebug.GetValueOnGameThread() > 0;
}

void FLineOfSightModule::StartupModule()
{
}

void FLineOfSightModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLineOfSightModule, LineOfSight)
