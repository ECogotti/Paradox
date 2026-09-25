#include "Actions/ParadoxChronoSpawnActionDefinition.h"

#include "Actions/ParadoxChronoSpawnAction.h"
#include "GameplayActionTags.h"
#include "Paradox.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UParadoxChronoSpawnActionDefinition::UParadoxChronoSpawnActionDefinition()
{
	InstanceClass = UParadoxChronoSpawnAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_ChronoSpawn;
	ExecutionMode =
		EParadoxInteractionExecutionMode::ExecuteWithoutSmartObject;
	DefaultPriority = 1000001;
	ExecutionLocks.RemoveTag(GameplayActionTags::Lock_Movement);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_ChronoSpawn);
	bInterruptible = false;
	BlockedPolicy = EGameplayActionBlockedPolicy::Reject;
	JournalRequirement = EGameplayActionJournalRequirement::Required;
	DebugDescription =
		TEXT("Materializes a temporal avatar at a puzzle-controlled Chrono Spawn without spatial interaction movement.");
}

void UParadoxChronoSpawnActionDefinition::PostLoad()
{
	Super::PostLoad();
	// This native Definition has a fixed execution contract. Reapply it after loading the legacy
	// asset so the old serialized lock/schema values cannot preserve spatial behavior.
	ExecutionMode =
		EParadoxInteractionExecutionMode::ExecuteWithoutSmartObject;
	ExecutionLocks.RemoveTag(GameplayActionTags::Lock_Movement);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_Interaction);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_ChronoSpawn);
	JournalRequirement = EGameplayActionJournalRequirement::Required;
}

#if WITH_EDITOR
EDataValidationResult UParadoxChronoSpawnActionDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	if (ExecutionMode
		!= EParadoxInteractionExecutionMode::ExecuteWithoutSmartObject)
	{
		Context.AddError(FText::FromString(
			TEXT("Chrono Spawn Definition must execute without a Smart Object slot.")));
		Result = EDataValidationResult::Invalid;
	}
	if (JournalRequirement != EGameplayActionJournalRequirement::Required)
	{
		Context.AddError(FText::FromString(
			TEXT("Chrono Spawn Definition requires journaling so materialization can replay.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
