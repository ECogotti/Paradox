#include "Data/IntentReplayObservationTrack.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

bool UIntentReplayObservationTrack::GetEntryByIndex(
	const int32 Index,
	FIntentReplayRecordedObservation& OutEntry) const
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	OutEntry = Entries[Index];
	return true;
}

bool UIntentReplayObservationTrack::FindEntryById(
	const FRecordedObservationId Id,
	FIntentReplayRecordedObservation& OutEntry) const
{
	const FIntentReplayRecordedObservation* Found = Entries.FindByPredicate(
		[Id](const FIntentReplayRecordedObservation& Entry)
		{
			return Entry.GetRecordedObservationId() == Id;
		});
	if (!Found)
	{
		return false;
	}
	OutEntry = *Found;
	return true;
}

TArray<FIntentReplayRecordedObservation> UIntentReplayObservationTrack::GetEntriesInTimeRange(
	const double StartTimeSeconds,
	const double EndTimeSeconds) const
{
	TArray<FIntentReplayRecordedObservation> Result;
	if (!FMath::IsFinite(StartTimeSeconds)
		|| !FMath::IsFinite(EndTimeSeconds)
		|| EndTimeSeconds < StartTimeSeconds)
	{
		return Result;
	}
	for (const FIntentReplayRecordedObservation& Entry : Entries)
	{
		const double Time = Entry.GetRelativeTimestamp();
		if (Time >= StartTimeSeconds && Time <= EndTimeSeconds)
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

FIntentReplayObservationTrackValidationResult UIntentReplayObservationTrack::ValidateTrack() const
{
	FIntentReplayObservationTrackValidationResult Result;
	if (!bFinalized)
	{
		Result.DiagnosticMessage = TEXT("Observation Track is not finalized.");
		return Result;
	}
	if (FormatVersion != 1)
	{
		Result.DiagnosticMessage = FString::Printf(
			TEXT("Unsupported Observation Track format %d."),
			FormatVersion);
		return Result;
	}
	if (!ObservationTrackId.IsValid()
		|| !SourceIntentReplayTrackId.IsValid()
		|| !SourceRecordingSessionId.IsValid())
	{
		Result.DiagnosticMessage = TEXT("Observation Track has invalid identity metadata.");
		return Result;
	}
	if (!FMath::IsFinite(RecordedDurationSeconds) || RecordedDurationSeconds < 0.0)
	{
		Result.DiagnosticMessage = TEXT("Observation Track has invalid duration.");
		return Result;
	}

	TSet<FRecordedObservationId> Ids;
	TSet<int64> Sequences;
	double PreviousTime = -1.0;
	int64 PreviousSequence = INDEX_NONE;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		Result.InvalidEntryIndex = Index;
		const FIntentReplayRecordedObservation& Entry = Entries[Index];
		const FRecordedObservationId Id = Entry.GetRecordedObservationId();
		const double Time = Entry.GetRelativeTimestamp();
		const int64 Sequence = Entry.GetTimelineSequence();
		if (!Id.IsValid() || Ids.Contains(Id))
		{
			Result.DiagnosticMessage = TEXT("Recorded Observation ID is invalid or duplicated.");
			return Result;
		}
		if (!FMath::IsFinite(Time) || Time < 0.0 || Sequence < 0 || Sequences.Contains(Sequence))
		{
			Result.DiagnosticMessage = TEXT("Recorded observation time/sequence is invalid or duplicated.");
			return Result;
		}
		if (Time < PreviousTime
			|| (FMath::IsNearlyEqual(Time, PreviousTime) && Sequence <= PreviousSequence))
		{
			Result.DiagnosticMessage = TEXT("Recorded observations are not deterministically ordered.");
			return Result;
		}
		if (Entry.Type == EIntentReplayRecordedObservationType::State)
		{
			if (!Entry.State.EntityId.IsValid()
				|| !Entry.State.StateTag.IsValid()
				|| !Entry.State.SenseTag.IsValid()
				|| (Entry.State.Status == EPerceptionKnowledgeFactStatus::Known
					&& !Entry.State.Value.IsValid()))
			{
				Result.DiagnosticMessage = TEXT("Recorded State observation has invalid semantic data.");
				return Result;
			}
		}
		else if (!Entry.Event.SourceObservationId.IsValid()
			|| !Entry.Event.EventTag.IsValid()
			|| !Entry.Event.SenseTag.IsValid())
		{
			Result.DiagnosticMessage = TEXT("Recorded Event observation has invalid semantic data.");
			return Result;
		}
		Ids.Add(Id);
		Sequences.Add(Sequence);
		PreviousTime = Time;
		PreviousSequence = Sequence;
	}
	Result.bValid = true;
	Result.InvalidEntryIndex = INDEX_NONE;
	return Result;
}

void UIntentReplayObservationTrack::InitializeFinalized(
	const FIntentReplayObservationTrackId InObservationTrackId,
	const FIntentReplayTrackId InSourceTrackId,
	const FIntentRecordingSessionId InSourceRecordingSessionId,
	TArray<FIntentReplayRecordedObservation>&& InEntries,
	const double InRecordedDurationSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplayPerception_FinalizeTrack);
	ObservationTrackId = InObservationTrackId;
	SourceIntentReplayTrackId = InSourceTrackId;
	SourceRecordingSessionId = InSourceRecordingSessionId;
	Entries = MoveTemp(InEntries);
	RecordedDurationSeconds = FMath::Max(0.0, InRecordedDurationSeconds);
	FormatVersion = 1;
	bFinalized = true;
}
