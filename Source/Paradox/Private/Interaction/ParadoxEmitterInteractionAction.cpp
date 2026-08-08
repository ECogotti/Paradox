#include "Interaction/ParadoxEmitterInteractionAction.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "GameplayActionTags.h"
#include "Graph/PuzzleGraphSubsystem.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Paradox.h"
#include "UObject/UnrealType.h"

void UParadoxEmitterInteractionAction::OnActionInit_Implementation()
{
	Super::OnActionInit_Implementation();
	FName ComponentName;
	FString Diagnostic;
	if (ReadEmitterParameters(ComponentName, SignalTag, EmitterCommand, Diagnostic))
	{
		ResolvedEmitter = ResolveEmitter(ComponentName, Diagnostic);
		if (ResolvedEmitter)
		{
			ResolvedEmitter->OnSignalChangedNative.AddUObject(this, &ThisClass::HandleSignalChanged);
		}
		if (UPuzzleGraphSubsystem* Graph = GetWorld() ? GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>() : nullptr)
		{
			Graph->OnPuzzleGraphTopologyChangedNative.AddUObject(this, &ThisClass::HandleGraphTopologyChanged);
			Graph->OnPuzzleGraphLinkStateChangedNative.AddUObject(this, &ThisClass::HandleGraphLinkStateChanged);
		}
	}
}

void UParadoxEmitterInteractionAction::OnActionCleanup_Implementation()
{
	if (ResolvedEmitter)
	{
		ResolvedEmitter->OnSignalChangedNative.RemoveAll(this);
	}
	if (UPuzzleGraphSubsystem* Graph = GetWorld() ? GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>() : nullptr)
	{
		Graph->OnPuzzleGraphTopologyChangedNative.RemoveAll(this);
		Graph->OnPuzzleGraphLinkStateChangedNative.RemoveAll(this);
	}
	ResolvedEmitter = nullptr;
	Super::OnActionCleanup_Implementation();
}

bool UParadoxEmitterInteractionAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	FName ComponentName;
	FGameplayTag ExactSignalTag;
	EParadoxInteractionStateCommand Command;
	if (!ReadEmitterParameters(ComponentName, ExactSignalTag, Command, OutDiagnostic))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		return false;
	}
	UPuzzleEmitterComponent* Emitter = ResolvedEmitter ? ResolvedEmitter.Get() : ResolveEmitter(ComponentName, OutDiagnostic);
	if (!IsValid(Emitter))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable;
		return false;
	}
	if (!ExactSignalTag.IsValid())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		OutDiagnostic = TEXT("Emitter interaction SignalTag must be valid.");
		return false;
	}
	const bool bActive = IsSignalActive(*Emitter, ExactSignalTag);
	if (Command == EParadoxInteractionStateCommand::Deactivate)
	{
		if (!bActive)
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
			OutDiagnostic = TEXT("The exact Emitter signal is already inactive.");
			return false;
		}
		return true;
	}
	if (bActive)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
		OutDiagnostic = TEXT("The exact Emitter signal is already active.");
		return false;
	}
	if (!DoGatesAllowActivation(*Emitter, ExactSignalTag, OutDiagnostic))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_GateClosed;
		return false;
	}
	return true;
}

bool UParadoxEmitterInteractionAction::IsInteractionOutcomeSatisfied_Implementation() const
{
	FName ComponentName;
	FGameplayTag ExactSignalTag;
	EParadoxInteractionStateCommand Command;
	FString Diagnostic;
	UPuzzleEmitterComponent* Emitter = ReadEmitterParameters(ComponentName, ExactSignalTag, Command, Diagnostic)
		? (ResolvedEmitter ? ResolvedEmitter.Get() : ResolveEmitter(ComponentName, Diagnostic))
		: nullptr;
	if (!IsValid(Emitter) || !ExactSignalTag.IsValid())
	{
		return false;
	}
	const bool bActive = IsSignalActive(*Emitter, ExactSignalTag);
	return Command == EParadoxInteractionStateCommand::Activate ? bActive : !bActive;
}

void UParadoxEmitterInteractionAction::ExecuteInteraction_Implementation()
{
	UPuzzleEmitterComponent* Emitter = ResolvedEmitter.Get();
	if (!IsValid(Emitter) || !SignalTag.IsValid())
	{
		CompleteInteractionFailure(ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable, TEXT("The resolved Puzzle Emitter or exact SignalTag is unavailable."));
		return;
	}
	const bool bDesiredActive = EmitterCommand == EParadoxInteractionStateCommand::Activate;
	if (IsSignalActive(*Emitter, SignalTag) == bDesiredActive)
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, TEXT("The requested Emitter state was satisfied during final validation."));
		return;
	}
	if (!Emitter->SetSignalState(SignalTag, bDesiredActive, nullptr))
	{
		CompleteInteractionFailure(ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable, TEXT("The Puzzle Emitter rejected the exact signal state."));
		return;
	}
	CompleteInteractionSuccess(GameplayActionTags::Result_Success, TEXT("The Puzzle Emitter signal state was applied."));
}

bool UParadoxEmitterInteractionAction::ReadEmitterParameters(
	FName& OutComponentName,
	FGameplayTag& OutSignalTag,
	EParadoxInteractionStateCommand& OutCommand,
	FString& OutDiagnostic) const
{
	FParadoxEmitterInteractionActionParameters Values;
	const UScriptStruct* Struct = FParadoxEmitterInteractionActionParameters::StaticStruct();
	const FProperty* NameProperty = Struct->FindPropertyByName(TEXT("EmitterComponentName"));
	const FProperty* SignalProperty = Struct->FindPropertyByName(TEXT("SignalTag"));
	const FProperty* CommandProperty = Struct->FindPropertyByName(TEXT("Command"));
	const bool bRead = UGameplayActionBlueprintLibrary::GetBagValueToProperty(
		GetParameters(), ParadoxInteractionActionParameters::EmitterComponentName, NameProperty,
		NameProperty->ContainerPtrToValuePtr<void>(&Values)) == EGameplayActionParameterAccessResult::Success
		&& UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			GetParameters(), ParadoxInteractionActionParameters::SignalTag, SignalProperty,
			SignalProperty->ContainerPtrToValuePtr<void>(&Values)) == EGameplayActionParameterAccessResult::Success
		&& UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			GetParameters(), ParadoxInteractionActionParameters::Command, CommandProperty,
			CommandProperty->ContainerPtrToValuePtr<void>(&Values)) == EGameplayActionParameterAccessResult::Success;
	if (!bRead)
	{
		OutDiagnostic = TEXT("Emitter interaction parameters require EmitterComponentName, SignalTag and Command.");
		return false;
	}
	OutComponentName = Values.EmitterComponentName;
	OutSignalTag = Values.SignalTag;
	OutCommand = Values.Command;
	return true;
}

UPuzzleEmitterComponent* UParadoxEmitterInteractionAction::ResolveEmitter(
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
		OutDiagnostic = TEXT("The Emitter interaction Target is unresolved.");
		return nullptr;
	}
	TArray<UPuzzleEmitterComponent*> Components;
	Target->GetComponents(Components);
	Components.RemoveAll([](const UPuzzleEmitterComponent* Component) { return !IsValid(Component); });
	if (!ComponentName.IsNone())
	{
		UPuzzleEmitterComponent** Match = Components.FindByPredicate([ComponentName](const UPuzzleEmitterComponent* Component)
		{
			return Component->GetFName() == ComponentName;
		});
		if (Match)
		{
			return *Match;
		}
		OutDiagnostic = FString::Printf(TEXT("Emitter component '%s' was not found on Target '%s'."), *ComponentName.ToString(), *GetNameSafe(Target));
		return nullptr;
	}
	if (Components.Num() != 1)
	{
		OutDiagnostic = FString::Printf(TEXT("Target '%s' has %d Emitter components; EmitterComponentName must identify exactly one."), *GetNameSafe(Target), Components.Num());
		return nullptr;
	}
	return Components[0];
}

bool UParadoxEmitterInteractionAction::IsSignalActive(
	const UPuzzleEmitterComponent& Emitter,
	const FGameplayTag ExactSignalTag) const
{
	FPuzzleSignalState SignalState;
	return Emitter.TryGetSignalState(ExactSignalTag, SignalState) && SignalState.bIsValid && SignalState.bIsActive;
}

bool UParadoxEmitterInteractionAction::DoGatesAllowActivation(
	const UPuzzleEmitterComponent& Emitter,
	const FGameplayTag ExactSignalTag,
	FString& OutDiagnostic) const
{
	const UPuzzleGraphSubsystem* Graph = GetWorld() ? GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>() : nullptr;
	if (!Graph)
	{
		OutDiagnostic = TEXT("The Puzzle Graph subsystem is unavailable.");
		return false;
	}
	bool bHasExactConsumer = false;
	for (const FPuzzleGraphLink& Link : Graph->QueryLinksForEmitterComponent(const_cast<UPuzzleEmitterComponent*>(&Emitter)))
	{
		if (Link.LinkKind != EPuzzleGraphLinkKind::PrimarySignal
			|| Link.PrimaryEmitterComponent.Get() != &Emitter
			|| Link.PrimarySignalTag != ExactSignalTag)
		{
			continue;
		}
		bHasExactConsumer = true;
		FPuzzleGraphLinkState LinkState;
		if (Graph->TryGetLinkState(Link.LinkHandle, LinkState)
			&& (LinkState.GateMode == EPuzzleGraphGateMode::Bypassed
				|| LinkState.GateMode == EPuzzleGraphGateMode::Open))
		{
			return true;
		}
	}
	if (!bHasExactConsumer)
	{
		return true;
	}
	OutDiagnostic = TEXT("Every exact-signal Puzzle consumer has a Closed or Invalid gate.");
	return false;
}

void UParadoxEmitterInteractionAction::HandleSignalChanged(
	UPuzzleEmitterComponent* Emitter,
	const FGameplayTag ChangedSignalTag,
	const FPuzzleSignalState SignalState)
{
	(void)SignalState;
	if (Emitter == ResolvedEmitter.Get() && ChangedSignalTag == SignalTag)
	{
		ReevaluateRunningInteraction();
	}
}

void UParadoxEmitterInteractionAction::HandleGraphTopologyChanged(
	const int64 Revision,
	APuzzleController* Controller,
	const EPuzzleGraphTopologyChangeKind ChangeKind)
{
	(void)Revision;
	(void)Controller;
	(void)ChangeKind;
	ReevaluateRunningInteraction();
}

void UParadoxEmitterInteractionAction::HandleGraphLinkStateChanged(
	const FPuzzleGraphLinkHandle& LinkHandle,
	const FPuzzleGraphLinkState& PreviousState,
	const FPuzzleGraphLinkState& NewState)
{
	(void)LinkHandle;
	(void)PreviousState;
	(void)NewState;
	ReevaluateRunningInteraction();
}
