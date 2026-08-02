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
/** Global half of the clone-behavior debug gate. */
PARADOX_API bool IsParadoxCloneBehaviorDebugEnabled();
/** Global half of the project footstep-perception debug gate. */
PARADOX_API bool IsParadoxFootstepDebugEnabled();

namespace ParadoxGameplayTags
{
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Origin_Player);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_SetCrouched);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_Stance);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Outcome_FutureObserved);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Reason_FutureTemporalOrder);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Reason_SafeTemporalOrder);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Interrupted_ByInvestigation);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Interrupted_InvestigationSuperseded);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Origin_Investigation);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_InvestigationInspect);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Computer_Powered);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Test_State_Active);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Test_Event_Noise);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Noise_Character_Footstep);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cause_CharacterMovement_Footstep);
}
