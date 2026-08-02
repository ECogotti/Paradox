#include "Actions/ParadoxSetCrouchedActionDefinition.h"

#include "Actions/ParadoxSetCrouchedAction.h"
#include "Paradox.h"
#include "StructUtils/PropertyBag.h"

namespace ParadoxSetCrouchedActionParameters
{
	const FName DesiredCrouched(TEXT("DesiredCrouched"));
}

UParadoxSetCrouchedActionDefinition::UParadoxSetCrouchedActionDefinition()
{
	InstanceClass = UParadoxSetCrouchedAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_SetCrouched;
	DefaultPriority = 0;
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_Stance);
	bInterruptible = true;
	BlockedPolicy = EGameplayActionBlockedPolicy::Queue;
	JournalRequirement = EGameplayActionJournalRequirement::Optional;
	DebugDescription =
		TEXT("Instantly requests a deterministic crouched or standing stance without owning movement.");

	const TArray<FPropertyBagPropertyDesc> Descriptors = {
		{
			ParadoxSetCrouchedActionParameters::DesiredCrouched,
			EPropertyBagPropertyType::Bool
		}
	};
	DefaultParameters.InitializeFromBagStruct(
		UPropertyBag::GetOrCreateFromDescs(Descriptors));
	DefaultParameters.SetValueBool(
		ParadoxSetCrouchedActionParameters::DesiredCrouched,
		false);
}
