#include "Inventory/ParadoxOxygenCanister.h"

#include "Characters/ParadoxCharacter.h"
#include "GameplayActionTags.h"
#include "Health/ParadoxHealthComponent.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxPickupableAction.h"
#include "Oxygen/ParadoxOxygenComponent.h"
#include "Paradox.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxOxygenCanister"

AParadoxOxygenCanister::AParadoxOxygenCanister()
{
	PickupableDisplayName = LOCTEXT("DisplayName", "Oxygen Canister");

	static ConstructorHelpers::FObjectFinder<UParadoxPickupableAction> UseActionFinder(
		TEXT("/Game/Data/Inventory/DA_ParadoxUsePickupableAction.DA_ParadoxUsePickupableAction"));
	if (UseActionFinder.Succeeded())
	{
		PickupableActions.AddUnique(UseActionFinder.Object);
	}
}

void AParadoxOxygenCanister::BeginPlay()
{
	Super::BeginPlay();
	if (!FMath::IsFinite(OxygenRestoreSeconds) || OxygenRestoreSeconds <= 0.0f)
	{
		PARADOX_LOG_WARNING(
			TEXT("Oxygen Canister '%s' has invalid OxygenRestoreSeconds %.3f; Use remains disabled."),
			*GetNameSafe(this),
			OxygenRestoreSeconds);
	}
}

bool AParadoxOxygenCanister::CanUseItem_Implementation(
	AParadoxCharacter* Character,
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	if (!IsValid(Character))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_OwnerNotOperational;
		OutDiagnostic = TEXT("Oxygen Canister Use requires a valid Paradox Character.");
		return false;
	}
	if (!IsValid(Character->GetInventoryComponent())
		|| Character->GetInventoryComponent()->GetEquippedItem() != this
		|| GetCurrentHolder() != Character
		|| GetPickupableState() != EParadoxPickupableState::Held)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_OwnershipConflict;
		OutDiagnostic = TEXT("Oxygen Canister Use requires authoritative ownership in the Character's equipped slot.");
		return false;
	}
	if (!IsValid(Character->GetHealthComponent())
		|| !Character->GetHealthComponent()->IsAlive())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_OwnerNotOperational;
		OutDiagnostic = TEXT("Oxygen Canister Use requires a living Paradox Character.");
		return false;
	}
	if (!FMath::IsFinite(OxygenRestoreSeconds) || OxygenRestoreSeconds <= 0.0f)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Oxygen_InvalidRestoreAmount;
		OutDiagnostic = TEXT("Oxygen Canister Use requires a finite positive OxygenRestoreSeconds value.");
		return false;
	}

	const UParadoxOxygenComponent* Oxygen = Character->GetOxygenComponent();
	if (!IsValid(Oxygen))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Oxygen_MissingComponent;
		OutDiagnostic = TEXT("Oxygen Canister Use requires the Character's Oxygen component.");
		return false;
	}
	if (Oxygen->IsOxygenDepleted())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Oxygen_RestoreFailed;
		OutDiagnostic = TEXT("A depleted Oxygen lifetime cannot be restored without ResetOxygen.");
		return false;
	}
	if (Oxygen->GetRemainingOxygenSeconds()
		>= Oxygen->GetOxygenDurationSeconds() - KINDA_SMALL_NUMBER)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Oxygen_AlreadyFull;
		OutDiagnostic = TEXT("Oxygen is already at its configured capacity.");
		return false;
	}
	return true;
}

FParadoxPickupableUseResult AParadoxOxygenCanister::ExecuteUseItem_Implementation(
	AParadoxCharacter* Character)
{
	FParadoxPickupableUseResult Result;
	PendingActualRestoredSeconds = 0.0f;

	FGameplayTag FailureReason;
	FString Diagnostic;
	if (!CanUseItem(Character, FailureReason, Diagnostic))
	{
		Result.ReasonTag = FailureReason;
		Result.DiagnosticMessage = MoveTemp(Diagnostic);
		return Result;
	}

	UParadoxOxygenComponent* Oxygen = Character->GetOxygenComponent();
	PendingActualRestoredSeconds = Oxygen->RestoreOxygenSeconds(OxygenRestoreSeconds);
	if (PendingActualRestoredSeconds <= UE_SMALL_NUMBER)
	{
		Result.ReasonTag = ParadoxGameplayTags::Result_Failure_Oxygen_RestoreFailed;
		Result.DiagnosticMessage = TEXT("The authoritative Oxygen component restored no Oxygen seconds.");
		PendingActualRestoredSeconds = 0.0f;
		return Result;
	}

	Result.bSucceeded = true;
	Result.bConsumeItemOnSuccess = true;
	Result.ReasonTag = GameplayActionTags::Result_Success;
	Result.DiagnosticMessage = FString::Printf(
		TEXT("Oxygen Canister restored %.3f of %.3f requested seconds."),
		PendingActualRestoredSeconds,
		OxygenRestoreSeconds);
	return Result;
}

void AParadoxOxygenCanister::HandleUseCommitted(
	AParadoxCharacter* Character,
	const FParadoxPickupableUseResult& Result)
{
	Super::HandleUseCommitted(Character, Result);
	if (!Result.bItemConsumed)
	{
		PARADOX_LOG_ERROR(
			TEXT("Oxygen Canister '%s' restored %.3f seconds but Inventory did not report consumption."),
			*GetNameSafe(this),
			PendingActualRestoredSeconds);
	}
	ReceiveCanisterUseSucceeded(
		Character,
		OxygenRestoreSeconds,
		PendingActualRestoredSeconds);
	PARADOX_LOG_INFO(
		TEXT("Oxygen Canister '%s' used by '%s': requested=%.3f restored=%.3f consumed=%d."),
		*GetNameSafe(this),
		*GetNameSafe(Character),
		OxygenRestoreSeconds,
		PendingActualRestoredSeconds,
		Result.bItemConsumed ? 1 : 0);
	PendingActualRestoredSeconds = 0.0f;
}

void AParadoxOxygenCanister::HandleUseFailed(
	AParadoxCharacter* Character,
	const FParadoxPickupableUseResult& Result)
{
	Super::HandleUseFailed(Character, Result);
	ReceiveCanisterUseFailed(
		Character,
		Result.ReasonTag,
		Result.DiagnosticMessage);
	PendingActualRestoredSeconds = 0.0f;
}

#if WITH_EDITOR
EDataValidationResult AParadoxOxygenCanister::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	if (!FMath::IsFinite(OxygenRestoreSeconds) || OxygenRestoreSeconds <= 0.0f)
	{
		Context.AddError(LOCTEXT(
			"InvalidRestoreSeconds",
			"OxygenRestoreSeconds must be finite and greater than zero."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
