#include "Recording/IntentReplayTrack.h"

#include "Actions/GameplayActionDefinition.h"

bool UIntentReplayTrack::GetEntryByIndex(const int32 Index, FRecordedIntent& OutEntry) const
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	OutEntry = Entries[Index];
	return true;
}

bool UIntentReplayTrack::FindEntryById(const FRecordedIntentId RecordedIntentId, FRecordedIntent& OutEntry) const
{
	const FRecordedIntent* Found = Entries.FindByPredicate(
		[RecordedIntentId](const FRecordedIntent& Entry)
		{
			return Entry.RecordedIntentId == RecordedIntentId;
		});
	if (!Found)
	{
		return false;
	}
	OutEntry = *Found;
	return true;
}

FIntentReplayTrackValidationResult UIntentReplayTrack::ValidateTrack() const
{
	FIntentReplayTrackValidationResult Result;
	// Validation is deliberately repeated at PrepareReplay. A Blueprint cannot mutate track storage,
	// but this protects C++ callers and future format migrations from partially initialized objects.
	if (!bFinalized)
	{
		Result.DiagnosticMessage = TEXT("The replay track is not finalized.");
		return Result;
	}
	if (FormatVersion != 1 && FormatVersion != 2)
	{
		Result.DiagnosticMessage = FString::Printf(TEXT("Unsupported replay track format version %d."), FormatVersion);
		return Result;
	}
	if (!TrackId.IsValid())
	{
		Result.DiagnosticMessage = TEXT("The replay track has an invalid Track ID.");
		return Result;
	}
	if (FormatVersion >= 2 && !SourceRecordingSessionId.IsValid())
	{
		Result.DiagnosticMessage = TEXT("The replay track has an invalid source Recording Session ID.");
		return Result;
	}
	if (!FMath::IsFinite(RecordedDurationSeconds) || RecordedDurationSeconds < 0.0)
	{
		Result.DiagnosticMessage = TEXT("The replay track duration is invalid.");
		return Result;
	}

	TSet<FRecordedIntentId> UniqueIds;
	TSet<int64> UniqueTimelineSequences;
	double PreviousTime = -1.0;
	int64 PreviousTimelineSequence = INDEX_NONE;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FRecordedIntent& Entry = Entries[Index];
		Result.InvalidEntryIndex = Index;
		if (!Entry.RecordedIntentId.IsValid() || UniqueIds.Contains(Entry.RecordedIntentId))
		{
			Result.DiagnosticMessage = TEXT("A Recorded Intent ID is invalid or duplicated.");
			return Result;
		}
		UniqueIds.Add(Entry.RecordedIntentId);
		if ((!Entry.Definition.IsValid() && Entry.Definition.IsNull() && !Entry.DefinitionId.IsValid())
			|| !Entry.ActionTag.IsValid()
			|| !Entry.GetParameters().IsValid())
		{
			Result.DiagnosticMessage = TEXT("A recorded intent has invalid Definition identity, Action Tag, or parameters.");
			return Result;
		}
		if (!FMath::IsFinite(Entry.RelativeAcceptedTimeSeconds) || Entry.RelativeAcceptedTimeSeconds < 0.0)
		{
			Result.DiagnosticMessage = TEXT("A recorded intent has an invalid timestamp.");
			return Result;
		}
		// Contiguous sequence plus nondecreasing time gives the scheduler a deterministic tie-breaker.
		if (Entry.TrackSequence != Index)
		{
			Result.DiagnosticMessage = TEXT("Track sequence indices are not contiguous.");
			return Result;
		}
		const int64 EffectiveTimelineSequence =
			FormatVersion >= 2 ? Entry.TimelineSequence : Entry.TrackSequence;
		if (EffectiveTimelineSequence < 0
			|| (FormatVersion >= 2 && UniqueTimelineSequences.Contains(EffectiveTimelineSequence)))
		{
			Result.DiagnosticMessage = TEXT("Timeline sequence indices are invalid or duplicated.");
			return Result;
		}
		UniqueTimelineSequences.Add(EffectiveTimelineSequence);
		if (Entry.RelativeAcceptedTimeSeconds < PreviousTime
			|| (FMath::IsNearlyEqual(Entry.RelativeAcceptedTimeSeconds, PreviousTime)
				&& EffectiveTimelineSequence <= PreviousTimelineSequence))
		{
			Result.DiagnosticMessage = TEXT("Recorded intents are not deterministically ordered.");
			return Result;
		}
		PreviousTime = Entry.RelativeAcceptedTimeSeconds;
		PreviousTimelineSequence = EffectiveTimelineSequence;
	}

	Result.bValid = true;
	Result.InvalidEntryIndex = INDEX_NONE;
	return Result;
}

void UIntentReplayTrack::InitializeFinalized(
	const FIntentReplayTrackId InTrackId,
	const FIntentRecordingSessionId InSourceRecordingSessionId,
	TArray<FRecordedIntent>&& InEntries,
	const double InRecordedDurationSeconds,
	FString InSourceLabel,
	FGameplayTagContainer InMetadataTags)
{
	// This is the sole mutation point. The component calls it once after moving entries out of the
	// recording session, and no public mutator exists afterward.
	TrackId = InTrackId;
	SourceRecordingSessionId = InSourceRecordingSessionId;
	Entries = MoveTemp(InEntries);
	RecordedDurationSeconds = FMath::Max(0.0, InRecordedDurationSeconds);
	SourceLabel = MoveTemp(InSourceLabel);
	MetadataTags = MoveTemp(InMetadataTags);
	FormatVersion = 2;
	bFinalized = true;
}
