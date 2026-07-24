// Copyright Epic Games, Inc. All Rights Reserved.

#include "Paradox.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, Paradox, "Paradox" );

DEFINE_LOG_CATEGORY(LogParadox)

namespace
{
	TAutoConsoleVariable<int32> CVarParadoxTimeLoopDebug(
		TEXT("Paradox.TimeLoop.Debug"),
		0,
		TEXT("Draw Paradox temporal overlap authority and deduplication state.\n")
		TEXT("0: disabled (default)\n")
		TEXT("1: enabled for Temporal Vision components whose local debug flag is enabled"),
		ECVF_Default);
}

bool IsParadoxTimeLoopDebugEnabled()
{
	return CVarParadoxTimeLoopDebug.GetValueOnGameThread() != 0;
}

namespace ParadoxGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Origin_Player, "GameplayAction.Origin.Player");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Outcome_FutureObserved,
		"Paradox.Relation.Outcome.FutureObserved");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Reason_FutureTemporalOrder,
		"Paradox.Relation.Reason.FutureTemporalOrder");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Reason_SafeTemporalOrder,
		"Paradox.Relation.Reason.SafeTemporalOrder");
}
