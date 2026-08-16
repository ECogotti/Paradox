#include "Inventory/ParadoxInventoryInteractionActions.h"

#include "Characters/ParadoxCharacter.h"
#include "GameplayActionTags.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Paradox.h"

namespace UE::Paradox::InventoryInteraction::Private
{
	FGameplayTag FailureTagForStatus(const EParadoxInventoryOperationStatus Status)
	{
		switch (Status)
		{
		case EParadoxInventoryOperationStatus::SlotOccupied:
			return ParadoxGameplayTags::Result_Failure_Inventory_SlotOccupied;
		case EParadoxInventoryOperationStatus::SlotEmpty:
			return ParadoxGameplayTags::Result_Failure_Inventory_SlotEmpty;
		case EParadoxInventoryOperationStatus::ItemUnavailable:
			return ParadoxGameplayTags::Result_Failure_Inventory_ItemUnavailable;
		case EParadoxInventoryOperationStatus::OwnershipConflict:
			return ParadoxGameplayTags::Result_Failure_Inventory_OwnershipConflict;
		default:
			return ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
		}
	}

	bool Resolve(
		const UParadoxInteractionActionBase& Action,
		AParadoxCharacter*& OutCharacter,
		UParadoxInventoryComponent*& OutInventory,
		AParadoxPickupableActor*& OutTarget,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic)
	{
		AActor* Requester = Action.GetInteractionRequester();
		AActor* TargetActor = Action.GetInteractionTarget();
		OutCharacter = Cast<AParadoxCharacter>(Requester);
		OutInventory = OutCharacter ? OutCharacter->GetInventoryComponent() : nullptr;
		OutTarget = Cast<AParadoxPickupableActor>(TargetActor);
		if (!OutCharacter)
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
			OutDiagnostic = FString::Printf(
				TEXT("Inventory interaction requester '%s' (class '%s') must be the Paradox Character that owns the executing Gameplay Action Component."),
				*GetNameSafe(Requester),
				*GetNameSafe(Requester ? Requester->GetClass() : nullptr));
			return false;
		}
		if (!OutInventory)
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
			OutDiagnostic = FString::Printf(
				TEXT("Paradox Character '%s' has no valid Inventory Component."),
				*GetNameSafe(OutCharacter));
			return false;
		}
		if (!OutTarget)
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
			OutDiagnostic = FString::Printf(
				TEXT("Inventory interaction Target '%s' (class '%s') is not a Paradox Pickupable Actor."),
				*GetNameSafe(TargetActor),
				*GetNameSafe(TargetActor ? TargetActor->GetClass() : nullptr));
			return false;
		}
		return true;
	}
}

bool UParadoxPickupInteractionAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	return ValidatePickupSource(OutFailureReason, OutDiagnostic);
}

bool UParadoxPickupInteractionAction::ValidatePickupSource(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = nullptr;
	UParadoxInventoryComponent* Inventory = nullptr;
	AParadoxPickupableActor* Target = nullptr;
	if (!UE::Paradox::InventoryInteraction::Private::Resolve(
		*this, Character, Inventory, Target, OutFailureReason, OutDiagnostic))
	{
		return false;
	}
	if (Inventory->HasItem())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_SlotOccupied;
		OutDiagnostic = TEXT("Pickup requires an empty single inventory slot.");
		return false;
	}
	if (!Target->IsAvailableInWorld())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_ItemUnavailable;
		OutDiagnostic = TEXT("The pickupable target is no longer available in the world.");
		return false;
	}
	return true;
}

bool UParadoxPickupInteractionAction::IsInteractionOutcomeSatisfied_Implementation() const
{
	return IsPickupSourceAcquired();
}

bool UParadoxPickupInteractionAction::IsPickupSourceAcquired() const
{
	const AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	const AParadoxPickupableActor* Target = Cast<AParadoxPickupableActor>(GetInteractionTarget());
	return Character && Character->GetInventoryComponent()
		&& Character->GetInventoryComponent()->GetEquippedItem() == Target
		&& Target && Target->GetCurrentHolder() == Character;
}

void UParadoxPickupInteractionAction::ExecuteInteraction_Implementation()
{
	FGameplayTag FailureReason;
	FString Diagnostic;
	if (CommitPickupSource(FailureReason, Diagnostic))
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, Diagnostic);
		return;
	}
	CompleteInteractionFailure(FailureReason, Diagnostic);
}

bool UParadoxPickupInteractionAction::CommitPickupSource(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic)
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	AParadoxPickupableActor* Target = Cast<AParadoxPickupableActor>(GetInteractionTarget());
	UParadoxInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	const FParadoxInventoryOperationResult Result = Inventory
		? Inventory->TryEquip(Target)
		: FParadoxInventoryOperationResult();
	if (Result.IsSuccess())
	{
		OutDiagnostic = Result.DiagnosticMessage;
		return true;
	}
	OutFailureReason =
		UE::Paradox::InventoryInteraction::Private::FailureTagForStatus(Result.Status);
	OutDiagnostic = Result.DiagnosticMessage;
	return false;
}

bool UParadoxSwapInteractionAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = nullptr;
	UParadoxInventoryComponent* Inventory = nullptr;
	AParadoxPickupableActor* Target = nullptr;
	if (!UE::Paradox::InventoryInteraction::Private::Resolve(
		*this, Character, Inventory, Target, OutFailureReason, OutDiagnostic))
	{
		return false;
	}
	if (!Inventory->HasItem())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_SlotEmpty;
		OutDiagnostic = TEXT("Swap requires an occupied single inventory slot.");
		return false;
	}
	if (!Target->IsAvailableInWorld())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_ItemUnavailable;
		OutDiagnostic = TEXT("The swap target is no longer available in the world.");
		return false;
	}
	return true;
}

bool UParadoxSwapInteractionAction::IsInteractionOutcomeSatisfied_Implementation() const
{
	const AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	const AParadoxPickupableActor* Target = Cast<AParadoxPickupableActor>(GetInteractionTarget());
	return Character && Character->GetInventoryComponent()
		&& Character->GetInventoryComponent()->GetEquippedItem() == Target
		&& Target && Target->GetCurrentHolder() == Character;
}

void UParadoxSwapInteractionAction::ExecuteInteraction_Implementation()
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	AParadoxPickupableActor* Target = Cast<AParadoxPickupableActor>(GetInteractionTarget());
	UParadoxInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	const FParadoxInventoryOperationResult Result = Inventory
		? Inventory->TrySwap(Target)
		: FParadoxInventoryOperationResult();
	if (Result.IsSuccess())
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, Result.DiagnosticMessage);
		return;
	}
	CompleteInteractionFailure(
		UE::Paradox::InventoryInteraction::Private::FailureTagForStatus(Result.Status),
		Result.DiagnosticMessage);
}
