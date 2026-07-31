#include "Playback/ParadoxReplayRecoveryPolicy.h"

#include "Actions/GridMoveToCellActionDefinition.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Subsystems/PerceptionKnowledgeWorldSubsystem.h"

namespace ParadoxReplayRecoveryParameters
{
	const FName Metadata(TEXT("ParadoxReplayRecovery"));
}

bool UParadoxReplayRecoveryPolicy::ReadMetadata(
	const FRecordedIntent& RecordedIntent,
	FParadoxReplayRecoveryMetadata& OutMetadata)
{
	const TValueOrError<FStructView, EPropertyBagResult> Value =
		RecordedIntent.GetParameters().GetValueStruct(
			ParadoxReplayRecoveryParameters::Metadata,
			FParadoxReplayRecoveryMetadata::StaticStruct());
	const FParadoxReplayRecoveryMetadata* Metadata = Value.HasValue()
		? Value.GetValue().GetPtr<FParadoxReplayRecoveryMetadata>()
		: nullptr;
	if (!Metadata)
	{
		return false;
	}
	OutMetadata = *Metadata;
	return true;
}

FParadoxReplayRecoveryDecision UParadoxReplayRecoveryPolicy::Evaluate(
	const FIntentReplaySuspendedIntent& SuspendedIntent,
	AActor* Clone,
	UPerceptionKnowledgeListenerComponent* Listener) const
{
	FParadoxReplayRecoveryDecision Decision;
	if (!IsValid(Clone))
	{
		Decision.DiagnosticMessage = TEXT("Replay recovery has no valid clone.");
		return Decision;
	}

	FParadoxReplayRecoveryMetadata Metadata;
	Decision.bHasMetadata = ReadMetadata(SuspendedIntent.RecordedIntent, Metadata);
	if (const UGameplayActionDefinition* Definition =
		SuspendedIntent.RecordedIntent.Definition.Get();
		Definition && Definition->IsA<UGridMoveToCellActionDefinition>())
	{
		Decision.Outcome = EParadoxReplayRecoveryOutcome::ReissueNow;
		Decision.DiagnosticMessage =
			TEXT("Movement intent will be reissued; clone path adaptation recalculates from its current cell.");
		return Decision;
	}

	if (!Decision.bHasMetadata)
	{
		Decision.Outcome = EParadoxReplayRecoveryOutcome::ReissueNow;
		Decision.DiagnosticMessage =
			TEXT("No recovery metadata is present; the non-movement semantic intent is reissued unchanged.");
		return Decision;
	}

	AActor* TargetActor = nullptr;
	if (Metadata.TargetEntityId.IsValid())
	{
		if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
			Clone->GetWorld()
				? Clone->GetWorld()->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()
				: nullptr)
		{
			if (UPerceptionKnowledgeSourceComponent* Source =
				Subsystem->FindSource(Metadata.TargetEntityId))
			{
				TargetActor = Source->GetOwner();
			}
		}
	}
	if (Metadata.bRequireTargetEntity && !IsValid(TargetActor))
	{
		Decision.DiagnosticMessage =
			TEXT("The semantic target required by replay recovery no longer exists.");
		return Decision;
	}

	if (Metadata.bHasExpectedSatisfiedState
		&& Metadata.TargetEntityId.IsValid()
		&& Metadata.ExpectedStateTag.IsValid()
		&& Listener)
	{
		FPerceptionKnowledgeKnownState KnownState;
		if (Listener->GetKnownState(
				Metadata.TargetEntityId,
				Metadata.ExpectedStateTag,
				KnownState)
			&& KnownState.Status == EPerceptionKnowledgeFactStatus::Known
			&& KnownState.Value == Metadata.ExpectedStateValue)
		{
			Decision.Outcome = EParadoxReplayRecoveryOutcome::AlreadySatisfied;
			Decision.DiagnosticMessage =
				TEXT("The intended semantic state is already satisfied.");
			return Decision;
		}
	}

	if (Metadata.bRequiresExecutionLocation)
	{
		const FVector RequiredLocation = IsValid(TargetActor)
			? TargetActor->GetActorLocation()
			: Metadata.RequiredExecutionLocation;
		const float Tolerance = FMath::Max(0.0f, Metadata.ExecutionLocationTolerance);
		if (!Clone->GetActorLocation().Equals(RequiredLocation, Tolerance))
		{
			Decision.Outcome = EParadoxReplayRecoveryOutcome::MoveThenReissue;
			Decision.RepositionLocation = RequiredLocation;
			Decision.DiagnosticMessage =
				TEXT("Clone must reacquire the semantic execution location before reissue.");
			return Decision;
		}
	}

	Decision.Outcome = EParadoxReplayRecoveryOutcome::ReissueNow;
	Decision.DiagnosticMessage = TEXT("The semantic execution preconditions are currently satisfied.");
	return Decision;
}

