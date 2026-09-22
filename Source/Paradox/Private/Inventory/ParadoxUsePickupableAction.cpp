#include "Inventory/ParadoxUsePickupableAction.h"

#include "Characters/ParadoxCharacter.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Paradox.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UParadoxUsePickupableAction::CanExecutePickupableAction_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable,
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	if (!Character || !Character->GetInventoryComponent())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
		OutDiagnostic = TEXT("Use requires a Paradox Character with an Inventory component.");
		return false;
	}

	const FParadoxPickupableUseResult Result =
		Character->GetInventoryComponent()->EvaluateEquippedItemUse(Pickupable);
	if (!Result.IsSuccess())
	{
		OutFailureReason = Result.ReasonTag;
		OutDiagnostic = Result.DiagnosticMessage;
		return false;
	}
	return true;
}

void UParadoxUsePickupableAction::ExecutePickupableAction_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	if (!Character || !Character->GetInventoryComponent())
	{
		CompletePickupableActionFailure(
			ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest,
			TEXT("Use lost its Character or Inventory before execution."));
		return;
	}

	const FParadoxPickupableUseResult Result =
		Character->GetInventoryComponent()->TryUseEquippedItem(Pickupable);
	if (Result.IsSuccess())
	{
		CompletePickupableActionSuccess(Result.DiagnosticMessage);
	}
	else
	{
		CompletePickupableActionFailure(
			Result.ReasonTag.IsValid()
				? Result.ReasonTag
				: ParadoxGameplayTags::Result_Failure_Inventory_UseEffectFailed,
			Result.DiagnosticMessage);
	}
}

UParadoxUsePickupableActionDefinition::UParadoxUsePickupableActionDefinition()
{
	InstanceClass = UParadoxUsePickupableAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_Inventory_Use;
	DebugDescription = TEXT("Uses the currently equipped pickupable against the Character's current runtime state.");
}

#if WITH_EDITOR
EDataValidationResult UParadoxUsePickupableActionDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	if (InstanceClass != UParadoxUsePickupableAction::StaticClass()
		|| ActionTag != ParadoxGameplayTags::Action_Inventory_Use)
	{
		Context.AddError(FText::FromString(
			TEXT("Use Pickupable Definitions require the native Use action class and Inventory.Use action tag.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
