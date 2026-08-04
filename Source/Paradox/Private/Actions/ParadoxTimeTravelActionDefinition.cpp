#include "Actions/ParadoxTimeTravelActionDefinition.h"

#include "Actions/ParadoxTimeTravelAction.h"
#include "GameplayActionTags.h"
#include "Paradox.h"

UParadoxTimeTravelActionDefinition::UParadoxTimeTravelActionDefinition()
{
	InstanceClass = UParadoxTimeTravelAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_TimeTravel;
	DefaultPriority = 1000000;
	ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_Stance);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_TimeTravel);
	bInterruptible = false;
	BlockedPolicy = EGameplayActionBlockedPolicy::Reject;
	JournalRequirement = EGameplayActionJournalRequirement::Required;
	DebugDescription =
		TEXT("Plays the avatar time-travel VFX, then rewinds a player or retires a replay clone.");
}
