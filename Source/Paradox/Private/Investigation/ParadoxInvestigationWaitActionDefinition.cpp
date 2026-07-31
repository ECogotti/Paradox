#include "Investigation/ParadoxInvestigationWaitActionDefinition.h"

#include "Actions/GameplayWaitAction.h"
#include "GameplayActionTags.h"
#include "Paradox.h"
#include "StructUtils/PropertyBag.h"

UParadoxInvestigationWaitActionDefinition::UParadoxInvestigationWaitActionDefinition()
{
	InstanceClass = UGameplayWaitAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_InvestigationInspect;
	DefaultPriority = 100;
	ExecutionLocks.AddTag(GameplayActionTags::Lock_Primary);
	bInterruptible = true;
	BlockedPolicy = EGameplayActionBlockedPolicy::Queue;
	JournalRequirement = EGameplayActionJournalRequirement::Optional;
	DebugDescription = TEXT("GameplayActions-owned Paradox investigation inspection wait.");

	const TArray<FPropertyBagPropertyDesc> Descriptors = {
		{ TEXT("Duration"), EPropertyBagPropertyType::Double }
	};
	DefaultParameters.InitializeFromBagStruct(
		UPropertyBag::GetOrCreateFromDescs(Descriptors));
	DefaultParameters.SetValueDouble(TEXT("Duration"), 2.0);
}

