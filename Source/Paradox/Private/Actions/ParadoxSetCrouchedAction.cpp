#include "Actions/ParadoxSetCrouchedAction.h"

#include "Actions/ParadoxSetCrouchedActionDefinition.h"
#include "Components/GameplayActionComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayActionTags.h"

bool UParadoxSetCrouchedAction::CanStartAction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	const UGameplayActionComponent* ActionComponent = GetOwningComponent();
	const ACharacter* Character =
		ActionComponent ? Cast<ACharacter>(ActionComponent->GetOwner()) : nullptr;
	if (!Character || !Character->GetCharacterMovement())
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic =
			TEXT("Set Crouched requires an ACharacter with Character Movement.");
		return false;
	}

	const TValueOrError<bool, EPropertyBagResult> DesiredCrouched =
		GetParameters().GetValueBool(
			ParadoxSetCrouchedActionParameters::DesiredCrouched);
	if (!DesiredCrouched.HasValue())
	{
		OutFailureReason = GameplayActionTags::Result_Failure_InvalidRequest;
		OutDiagnostic =
			TEXT("Set Crouched requires a compatible DesiredCrouched bool parameter.");
		return false;
	}
	return true;
}

void UParadoxSetCrouchedAction::OnActionStarted_Implementation()
{
	UGameplayActionComponent* ActionComponent = GetOwningComponent();
	ACharacter* Character =
		ActionComponent ? Cast<ACharacter>(ActionComponent->GetOwner()) : nullptr;
	const TValueOrError<bool, EPropertyBagResult> DesiredCrouched =
		GetParameters().GetValueBool(
			ParadoxSetCrouchedActionParameters::DesiredCrouched);
	if (!Character || !DesiredCrouched.HasValue())
	{
		FailAction(
			GameplayActionTags::Result_Failure_InvalidRequest,
			TEXT("Set Crouched lost its validated Character or parameter before execution."));
		return;
	}

	if (DesiredCrouched.GetValue())
	{
		Character->Crouch();
	}
	else
	{
		Character->UnCrouch();
	}

	const UCharacterMovementComponent* CharacterMovement =
		Character->GetCharacterMovement();
	if (!CharacterMovement
		|| CharacterMovement->bWantsToCrouch != DesiredCrouched.GetValue())
	{
		FailAction(
			GameplayActionTags::Result_Failure_CannotStart,
			TEXT("Character Movement did not accept the requested persistent crouch state."));
		return;
	}

	SucceedAction(
		GameplayActionTags::Result_Success,
		DesiredCrouched.GetValue()
			? TEXT("Crouch request accepted by Character Movement.")
			: TEXT("Uncrouch request accepted by Character Movement."));
}
