#include "Actions/GameplayActionDefinition.h"

#include "Actions/GameplayActionInstance.h"
#include "GameplayActionTags.h"
#include "Misc/DataValidation.h"
#include "StructUtils/PropertyBag.h"

#define LOCTEXT_NAMESPACE "GameplayActionDefinition"

UGameplayActionDefinition::UGameplayActionDefinition()
{
	DefaultParameters.InitializeFromBagStruct(
		UPropertyBag::GetOrCreateFromDescs(TConstArrayView<FPropertyBagPropertyDesc>()));
}

#if WITH_EDITOR
EDataValidationResult UGameplayActionDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	if (!InstanceClass)
	{
		Context.AddError(LOCTEXT("MissingInstanceClass", "Instance Class is required."));
		Result = EDataValidationResult::Invalid;
	}
	else if (InstanceClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Context.AddError(LOCTEXT("AbstractInstanceClass", "Instance Class must be a concrete Gameplay Action class."));
		Result = EDataValidationResult::Invalid;
	}

	if (!ActionTag.IsValid())
	{
		Context.AddError(LOCTEXT("MissingActionTag", "Action Tag is required."));
		Result = EDataValidationResult::Invalid;
	}

	if (!DefaultParameters.IsValid())
	{
		Context.AddError(LOCTEXT("InvalidParameterBag", "Default Parameters must contain a valid Property Bag schema, including for an empty schema."));
		Result = EDataValidationResult::Invalid;
	}

	for (const FGameplayTag Lock : ExecutionLocks)
	{
		if (!Lock.MatchesTag(GameplayActionTags::Lock_Root))
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvalidLockTag", "Execution Lock '{0}' must belong to the GameplayAction.Lock hierarchy."),
				FText::FromName(Lock.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
	}

	if (OptionalTimeout.bEnabled
		&& (!FMath::IsFinite(OptionalTimeout.DurationSeconds) || OptionalTimeout.DurationSeconds < 0.0))
	{
		Context.AddError(LOCTEXT("InvalidTimeout", "Optional Timeout duration must be finite and cannot be negative."));
		Result = EDataValidationResult::Invalid;
	}

	if (!FMath::IsFinite(MaxQueueTimeSeconds) || MaxQueueTimeSeconds < 0.0)
	{
		Context.AddError(LOCTEXT("InvalidQueueTimeout", "Max Queue Time must be finite and cannot be negative."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
