// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/** Main log category used across the project */
DECLARE_LOG_CATEGORY_EXTERN(LogParadox, Log, All);

#define PARADOX_LOG_INFO(Format, ...) UE_LOG(LogParadox, Log, Format, ##__VA_ARGS__)
#define PARADOX_LOG_WARNING(Format, ...) UE_LOG(LogParadox, Warning, Format, ##__VA_ARGS__)
#define PARADOX_LOG_ERROR(Format, ...) UE_LOG(LogParadox, Error, Format, ##__VA_ARGS__)

/** Returns whether module-wide time-loop visual debugging is enabled. */
PARADOX_API bool IsParadoxTimeLoopDebugEnabled();

namespace ParadoxGameplayTags
{
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Origin_Player);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Outcome_FutureObserved);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Reason_FutureTemporalOrder);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Reason_SafeTemporalOrder);
}
