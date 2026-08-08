#include "Interaction/ParadoxReceiverInteractionAction.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "GameplayActionTags.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "UObject/UnrealType.h"

void UParadoxReceiverInteractionAction::OnActionInit_Implementation()
{
	Super::OnActionInit_Implementation();
	FName ComponentName;
	FString Diagnostic;
	if (ReadReceiverParameters(ComponentName, ReceiverCommand, Diagnostic))
	{
		ResolvedReceiver = ResolveReceiver(ComponentName, Diagnostic);
		if (ResolvedReceiver)
		{
			ResolvedReceiver->OnReceiverStateChangedNative.AddUObject(this, &ThisClass::HandleReceiverStateChanged);
			ResolvedReceiver->OnReceiverActivationPrerequisitesChangedNative.AddUObject(this, &ThisClass::HandleReceiverPrerequisitesChanged);
		}
	}
}

void UParadoxReceiverInteractionAction::OnActionCleanup_Implementation()
{
	if (ResolvedReceiver)
	{
		ResolvedReceiver->OnReceiverStateChangedNative.RemoveAll(this);
		ResolvedReceiver->OnReceiverActivationPrerequisitesChangedNative.RemoveAll(this);
	}
	ResolvedReceiver = nullptr;
	Super::OnActionCleanup_Implementation();
}

bool UParadoxReceiverInteractionAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	FName ComponentName;
	EParadoxInteractionStateCommand Command;
	if (!ReadReceiverParameters(ComponentName, Command, OutDiagnostic))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		return false;
	}
	UPuzzleReceiverComponent* Receiver = ResolvedReceiver ? ResolvedReceiver.Get() : ResolveReceiver(ComponentName, OutDiagnostic);
	if (!IsValid(Receiver))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable;
		return false;
	}
	if (Receiver->GetActivationMode() != EPuzzleReceiverActivationMode::Manual)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
		OutDiagnostic = TEXT("Standard Receiver interactions require Activation Mode Manual.");
		return false;
	}
	if (Command == EParadoxInteractionStateCommand::Activate)
	{
		if (Receiver->IsReceiverActive() || Receiver->IsManualActivationRequested())
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
			OutDiagnostic = TEXT("The Receiver is already manually activated.");
			return false;
		}
		if (!Receiver->CanRequestManualActivation())
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_Prerequisites;
			OutDiagnostic = TEXT("The Receiver activation prerequisites are not satisfied.");
			return false;
		}
		return true;
	}
	if (!Receiver->IsReceiverActive() && !Receiver->IsManualActivationRequested())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
		OutDiagnostic = TEXT("The Receiver is already manually deactivated.");
		return false;
	}
	return true;
}

bool UParadoxReceiverInteractionAction::IsInteractionOutcomeSatisfied_Implementation() const
{
	FName ComponentName;
	EParadoxInteractionStateCommand Command;
	FString Diagnostic;
	UPuzzleReceiverComponent* Receiver = ReadReceiverParameters(ComponentName, Command, Diagnostic)
		? (ResolvedReceiver ? ResolvedReceiver.Get() : ResolveReceiver(ComponentName, Diagnostic))
		: nullptr;
	if (!IsValid(Receiver))
	{
		return false;
	}
	return Command == EParadoxInteractionStateCommand::Activate
		? Receiver->IsReceiverActive()
		: !Receiver->IsReceiverActive() && !Receiver->IsManualActivationRequested();
}

void UParadoxReceiverInteractionAction::ExecuteInteraction_Implementation()
{
	UPuzzleReceiverComponent* Receiver = ResolvedReceiver.Get();
	if (!IsValid(Receiver))
	{
		CompleteInteractionFailure(ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable, TEXT("The resolved Puzzle Receiver is unavailable."));
		return;
	}
	const FPuzzleReceiverActivationCommandResult Result =
		ReceiverCommand == EParadoxInteractionStateCommand::Activate
			? Receiver->RequestManualActivation()
			: Receiver->RequestManualDeactivation();
	if (Result.Status == EPuzzleReceiverActivationCommandStatus::Applied
		|| Result.Status == EPuzzleReceiverActivationCommandStatus::AlreadyInRequestedState)
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, Result.DiagnosticMessage);
		return;
	}
	const FGameplayTag Failure = Result.Status == EPuzzleReceiverActivationCommandStatus::PrerequisitesNotSatisfied
		? ParadoxGameplayTags::Result_Failure_Interaction_Prerequisites
		: ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
	CompleteInteractionFailure(Failure, Result.DiagnosticMessage);
}

bool UParadoxReceiverInteractionAction::ReadReceiverParameters(
	FName& OutComponentName,
	EParadoxInteractionStateCommand& OutCommand,
	FString& OutDiagnostic) const
{
	FParadoxReceiverInteractionActionParameters Values;
	const FProperty* NameProperty = FParadoxReceiverInteractionActionParameters::StaticStruct()->FindPropertyByName(TEXT("ReceiverComponentName"));
	const FProperty* CommandProperty = FParadoxReceiverInteractionActionParameters::StaticStruct()->FindPropertyByName(TEXT("Command"));
	const bool bRead = UGameplayActionBlueprintLibrary::GetBagValueToProperty(
		GetParameters(), ParadoxInteractionActionParameters::ReceiverComponentName, NameProperty,
		NameProperty->ContainerPtrToValuePtr<void>(&Values)) == EGameplayActionParameterAccessResult::Success
		&& UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			GetParameters(), ParadoxInteractionActionParameters::Command, CommandProperty,
			CommandProperty->ContainerPtrToValuePtr<void>(&Values)) == EGameplayActionParameterAccessResult::Success;
	if (!bRead)
	{
		OutDiagnostic = TEXT("Receiver interaction parameters require ReceiverComponentName and Command.");
		return false;
	}
	OutComponentName = Values.ReceiverComponentName;
	OutCommand = Values.Command;
	return true;
}

UPuzzleReceiverComponent* UParadoxReceiverInteractionAction::ResolveReceiver(
	const FName ComponentName,
	FString& OutDiagnostic) const
{
	AActor* Target = GetInteractionTarget();
	if (!IsValid(Target))
	{
		FParadoxInteractionActionParameters Values;
		const FProperty* Property = FParadoxInteractionActionParameters::StaticStruct()->FindPropertyByName(TEXT("Target"));
		if (UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			GetParameters(), TEXT("Target"), Property,
			Property->ContainerPtrToValuePtr<void>(&Values)) == EGameplayActionParameterAccessResult::Success)
		{
			Target = Values.Target.Get();
		}
	}
	if (!IsValid(Target))
	{
		OutDiagnostic = TEXT("The Receiver interaction Target is unresolved.");
		return nullptr;
	}
	TArray<UPuzzleReceiverComponent*> Components;
	Target->GetComponents(Components);
	Components.RemoveAll([](const UPuzzleReceiverComponent* Component) { return !IsValid(Component); });
	if (!ComponentName.IsNone())
	{
		UPuzzleReceiverComponent** Match = Components.FindByPredicate([ComponentName](const UPuzzleReceiverComponent* Component)
		{
			return Component->GetFName() == ComponentName;
		});
		if (Match)
		{
			return *Match;
		}
		OutDiagnostic = FString::Printf(TEXT("Receiver component '%s' was not found on Target '%s'."), *ComponentName.ToString(), *GetNameSafe(Target));
		return nullptr;
	}
	if (Components.Num() != 1)
	{
		OutDiagnostic = FString::Printf(TEXT("Target '%s' has %d Receiver components; ReceiverComponentName must identify exactly one."), *GetNameSafe(Target), Components.Num());
		return nullptr;
	}
	return Components[0];
}

void UParadoxReceiverInteractionAction::HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bIsActive)
{
	(void)Receiver;
	(void)bIsActive;
	ReevaluateRunningInteraction();
}

void UParadoxReceiverInteractionAction::HandleReceiverPrerequisitesChanged(UPuzzleReceiverComponent* Receiver, bool bPrerequisitesSatisfied)
{
	(void)Receiver;
	(void)bPrerequisitesSatisfied;
	ReevaluateRunningInteraction();
}
