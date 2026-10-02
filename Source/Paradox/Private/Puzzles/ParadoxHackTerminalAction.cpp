#include "Puzzles/ParadoxHackTerminalAction.h"

#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "IntentReplayTags.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Paradox.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Puzzles/ParadoxHackingTerminal.h"
#include "Recording/IntentReplayTrack.h"
#include "StructUtils/PropertyBag.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UParadoxHackTerminalActionDefinition::UParadoxHackTerminalActionDefinition()
{
	InstanceClass = UParadoxHackTerminalAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_HackTerminal;
	bAllowBackgroundExecution = true;
	JournalRequirement = EGameplayActionJournalRequirement::Required;
}

#if WITH_EDITOR
EDataValidationResult UParadoxHackTerminalActionDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const auto Result = Super::IsDataValid(Context);
	if (!bAllowBackgroundExecution || JournalRequirement != EGameplayActionJournalRequirement::Required || !RequiresSmartObjectSlot())
	{
		Context.AddError(FText::FromString(TEXT("HackTerminal requires background execution, Required journaling and spatial SmartObject slots.")));
		return EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

bool UParadoxHackTerminalAction::ReadRecordedOutcome(double& OutDuration, bool& OutSuccess, FString& OutDiagnostic) const
{
	const AActor* Owner = GetInteractionRequester();
	const UIntentReplayComponent* Replay = Owner ? Owner->FindComponentByClass<UIntentReplayComponent>() : nullptr;
	const UIntentReplayPlaybackSession* Session = Replay ? Replay->GetActivePlaybackSession() : nullptr;
	FRecordedIntent Intent;
	if (!Session || !Session->GetSourceTrack() || !GetCorrelation().Type.MatchesTagExact(IntentReplayTags::Correlation_RecordedIntent)
		|| !Session->GetSourceTrack()->FindEntryById(FRecordedIntentId(GetCorrelation().Id), Intent) || !Intent.bHasOriginalResult)
	{
		OutDiagnostic = TEXT("Replay hacking requires the source intent's semantic terminal outcome."); return false;
	}
	const auto Duration = Intent.OriginalResult.OutcomeParameters.GetValueDouble(TEXT("DurationSeconds"));
	const auto Success = Intent.OriginalResult.OutcomeParameters.GetValueBool(TEXT("bSucceeded"));
	if (!Duration.HasValue() || !Success.HasValue() || !FMath::IsFinite(Duration.GetValue()) || Duration.GetValue() < 0)
	{
		OutDiagnostic = TEXT("Recorded hacking outcome is missing or invalid (DurationSeconds, bSucceeded)."); return false;
	}
	OutDuration = Duration.GetValue(); OutSuccess = Success.GetValue(); return true;
}

bool UParadoxHackTerminalAction::CanSatisfyInteractionPreconditions_Implementation(FGameplayTag& OutReason, FString& OutDiagnostic) const
{
	const auto* Target = Cast<AParadoxHackingTerminal>(GetInteractionTarget());
	OutReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
	if (!Target || !GetDefinition() || !IsBackgroundExecutionAllowed()
		|| GetJournalRequirement() != EGameplayActionJournalRequirement::Required)
	{
		OutDiagnostic = TEXT("HackTerminal requires a hacking terminal and a Required, background-enabled definition."); return false;
	}
	if (!Target->ValidateConfiguration(OutDiagnostic)) { return false; }
	if (Target->Emitter->IsInvalidated(Target->OutputSignalTag))
	{
		OutReason = ParadoxGameplayTags::Result_Hacking_Failure;
		OutDiagnostic = TEXT("Terminal emitter is blocked by its Controller gates."); return false;
	}
	FParadoxHackingAttemptSnapshot Existing;
	if (!AttemptId.IsValid() && Target->GetAttemptForInstigator(GetInteractionRequester(), Existing)
		&& Existing.State == EParadoxHackingAttemptState::Active)
	{
		OutDiagnostic = TEXT("This instigator already has an active attempt on the terminal."); return false;
	}
	if (GetOriginTag().MatchesTagExact(IntentReplayTags::Origin_Replay))
	{
		double Duration; bool bSuccess;
		if (!ReadRecordedOutcome(Duration, bSuccess, OutDiagnostic)) { return false; }
	}
	return true;
}

bool UParadoxHackTerminalAction::IsInteractionOutcomeSatisfied_Implementation() const
{
	const auto* Target = Cast<AParadoxHackingTerminal>(GetInteractionTarget());
	return !GetOriginTag().MatchesTagExact(IntentReplayTags::Origin_Replay)
		&& Target && Target->GetTerminalState() == EParadoxHackingTerminalState::Hacked;
}

bool UParadoxHackTerminalAction::IsInteractionExecutionPending_Implementation() const { return AttemptId.IsValid(); }

void UParadoxHackTerminalAction::ExecuteInteraction_Implementation()
{
	Terminal = Cast<AParadoxHackingTerminal>(GetInteractionTarget());
	AttemptInstigator = GetInteractionRequester();
	if (!Terminal.IsValid()) { CompleteInteractionFailure(ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest, TEXT("Missing hacking terminal.")); return; }
	EParadoxHackingMode Mode = GetOriginTag().MatchesTagExact(ParadoxGameplayTags::Origin_Player)
		? EParadoxHackingMode::Interactive : EParadoxHackingMode::Simulated;
	double Duration = 0; bool bSuccess = false; FString Diagnostic;
	if (GetOriginTag().MatchesTagExact(IntentReplayTags::Origin_Replay))
	{
		Mode = EParadoxHackingMode::Replay;
		if (!ReadRecordedOutcome(Duration, bSuccess, Diagnostic)) { CompleteInteractionFailure(ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest, Diagnostic); return; }
	}
	AttemptChangedHandle = Terminal->OnAttemptChanged().AddUObject(this, &ThisClass::HandleAttemptChanged);
	if (!Terminal->BeginAttempt(AttemptInstigator.Get(), Mode, Duration, bSuccess, AttemptId, Diagnostic))
	{
		CompleteInteractionFailure(ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest, Diagnostic); return;
	}
	Terminal->GetAttemptSnapshot(AttemptId, LastSnapshot);
	AttemptGeneration = LastSnapshot.Generation;
	if (EnterBackgroundInteraction() != EGameplayActionOperationResult::Succeeded)
	{
		CancelOwnedAttempt();
		CompleteInteractionFailure(ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest, TEXT("Could not enter background hacking.")); return;
	}
	if (GetState() == EGameplayActionState::Running && Terminal.IsValid()) { Terminal->OnAttemptChanged().Broadcast(LastSnapshot); }
}

void UParadoxHackTerminalAction::HandleAttemptChanged(const FParadoxHackingAttemptSnapshot& Snapshot)
{
	if (Snapshot.AttemptId != AttemptId || Snapshot.Generation != AttemptGeneration) { return; }
	LastSnapshot = Snapshot;
	if (bCancellingAttempt || Snapshot.State == EParadoxHackingAttemptState::Active) { return; }
	if (Snapshot.State == EParadoxHackingAttemptState::Success)
	{
		CompleteInteractionSuccess(FGameplayTag(), TEXT("Hacking completed."));
	}
	else if (Snapshot.State == EParadoxHackingAttemptState::Superseded)
	{
		GetOwningComponent()->CancelAction(GetHandle(), ParadoxGameplayTags::Result_Hacking_Superseded);
	}
	else if (Snapshot.State == EParadoxHackingAttemptState::Cancelled)
	{
		GetOwningComponent()->CancelAction(GetHandle(), FGameplayTag());
	}
	else { CompleteInteractionFailure(ParadoxGameplayTags::Result_Hacking_Failure, TEXT("Hacking failed.")); }
}

void UParadoxHackTerminalAction::CancelOwnedAttempt()
{
	TGuardValue<bool> Guard(bCancellingAttempt, true);
	if (Terminal.IsValid() && AttemptId.IsValid())
	{
		Terminal->CancelAttempt(AttemptInstigator.Get(), AttemptId, AttemptGeneration);
		Terminal->GetAttemptSnapshot(AttemptId, LastSnapshot);
	}
}

void UParadoxHackTerminalAction::OnActionCancelled_Implementation(FGameplayTag Reason) { CancelOwnedAttempt(); Super::OnActionCancelled_Implementation(Reason); }
void UParadoxHackTerminalAction::OnActionInterrupted_Implementation(FGameplayTag Reason) { CancelOwnedAttempt(); Super::OnActionInterrupted_Implementation(Reason); }
void UParadoxHackTerminalAction::OnActionAborted_Implementation(FGameplayTag Reason) { CancelOwnedAttempt(); Super::OnActionAborted_Implementation(Reason); }

FInstancedPropertyBag UParadoxHackTerminalAction::BuildTerminalOutcomeParameters(EGameplayActionState TerminalState) const
{
	FInstancedPropertyBag Bag;
	Bag.AddProperties({ FPropertyBagPropertyDesc(TEXT("DurationSeconds"), EPropertyBagPropertyType::Double), FPropertyBagPropertyDesc(TEXT("bSucceeded"), EPropertyBagPropertyType::Bool) });
	Bag.SetValueDouble(TEXT("DurationSeconds"), LastSnapshot.DurationSeconds);
	Bag.SetValueBool(TEXT("bSucceeded"), TerminalState == EGameplayActionState::Succeeded);
	return Bag;
}

void UParadoxHackTerminalAction::OnActionCleanup_Implementation()
{
	CancelOwnedAttempt();
	if (Terminal.IsValid()) { Terminal->OnAttemptChanged().Remove(AttemptChangedHandle); }
	AttemptChangedHandle.Reset();
	Terminal.Reset(); AttemptInstigator.Reset(); AttemptId.Invalidate();
	Super::OnActionCleanup_Implementation();
}
