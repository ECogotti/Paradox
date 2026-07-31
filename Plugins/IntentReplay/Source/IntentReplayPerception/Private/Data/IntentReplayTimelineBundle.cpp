#include "Data/IntentReplayTimelineBundle.h"

#include "Data/IntentReplayObservationTrack.h"
#include "Recording/IntentReplayTrack.h"

FIntentReplayTimelineBundleValidationResult UIntentReplayTimelineBundle::ValidateBundle() const
{
	FIntentReplayTimelineBundleValidationResult Result;
	if (!bFinalized || FormatVersion != 1)
	{
		Result.DiagnosticMessage = TEXT("Timeline Bundle is not finalized or uses an unsupported format.");
		return Result;
	}
	if (!IsValid(ActionTrack) || !IsValid(ObservationTrack))
	{
		Result.DiagnosticMessage = TEXT("Timeline Bundle is missing one of its tracks.");
		return Result;
	}
	const FIntentReplayTrackValidationResult ActionValidation = ActionTrack->ValidateTrack();
	const FIntentReplayObservationTrackValidationResult ObservationValidation =
		ObservationTrack->ValidateTrack();
	if (!ActionValidation.bValid)
	{
		Result.DiagnosticMessage = ActionValidation.DiagnosticMessage;
		return Result;
	}
	if (!ObservationValidation.bValid)
	{
		Result.DiagnosticMessage = ObservationValidation.DiagnosticMessage;
		return Result;
	}
	if (ActionTrack->GetFormatVersion() < 2)
	{
		Result.DiagnosticMessage = TEXT("Perception timeline bundles require Action Replay Track format 2.");
		return Result;
	}
	if (ObservationTrack->GetSourceIntentReplayTrackId() != ActionTrack->GetTrackId()
		|| ObservationTrack->GetSourceRecordingSessionId()
			!= ActionTrack->GetSourceRecordingSessionId())
	{
		Result.DiagnosticMessage = TEXT("Action and Observation Tracks belong to different recording sessions.");
		return Result;
	}
	if (!FMath::IsNearlyEqual(
		ObservationTrack->GetRecordedDurationSeconds(),
		ActionTrack->GetRecordedDurationSeconds(),
		0.001))
	{
		Result.DiagnosticMessage = TEXT("Action and Observation Track durations are inconsistent.");
		return Result;
	}
	Result.bValid = true;
	return Result;
}

void UIntentReplayTimelineBundle::InitializeFinalized(
	UIntentReplayTrack* InActionTrack,
	UIntentReplayObservationTrack* InObservationTrack)
{
	ActionTrack = InActionTrack;
	ObservationTrack = InObservationTrack;
	FormatVersion = 1;
	bFinalized = true;
}
