#include "Journal/IntentReplayObservationJournal.h"

namespace
{
	FPerceptionKnowledgeEntityId GetEntryEntity(
		const FIntentReplayObservationJournalEntry& Entry)
	{
		if (Entry.CurrentObservation.Type == EPerceptionKnowledgeObservationType::State
			&& Entry.CurrentObservation.State.Key.EntityId.IsValid())
		{
			return Entry.CurrentObservation.State.Key.EntityId;
		}
		if (Entry.CurrentObservation.Event.SourceEntityId.IsValid())
		{
			return Entry.CurrentObservation.Event.SourceEntityId;
		}
		if (Entry.bHasExpectedObservation)
		{
			return Entry.ExpectedObservation.Type == EIntentReplayRecordedObservationType::State
				? Entry.ExpectedObservation.State.EntityId
				: Entry.ExpectedObservation.Event.SourceEntityId;
		}
		return FPerceptionKnowledgeEntityId();
	}

	FGameplayTag GetEntrySemanticTag(const FIntentReplayObservationJournalEntry& Entry)
	{
		if (Entry.CurrentObservation.Type == EPerceptionKnowledgeObservationType::State
			&& Entry.CurrentObservation.State.Key.StateTag.IsValid())
		{
			return Entry.CurrentObservation.State.Key.StateTag;
		}
		if (Entry.CurrentObservation.Event.EventTag.IsValid())
		{
			return Entry.CurrentObservation.Event.EventTag;
		}
		if (Entry.bHasExpectedObservation)
		{
			return Entry.ExpectedObservation.Type == EIntentReplayRecordedObservationType::State
				? Entry.ExpectedObservation.State.StateTag
				: Entry.ExpectedObservation.Event.EventTag;
		}
		return FGameplayTag();
	}
}

bool UIntentReplayObservationJournal::GetEntryByIndex(
	const int32 Index,
	FIntentReplayObservationJournalEntry& OutEntry) const
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	OutEntry = Entries[Index];
	return true;
}

TArray<FIntentReplayObservationJournalEntry>
UIntentReplayObservationJournal::GetEntriesForEntity(
	const FPerceptionKnowledgeEntityId EntityId) const
{
	TArray<FIntentReplayObservationJournalEntry> Result;
	for (const FIntentReplayObservationJournalEntry& Entry : Entries)
	{
		if (GetEntryEntity(Entry) == EntityId)
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

TArray<FIntentReplayObservationJournalEntry>
UIntentReplayObservationJournal::GetEntriesForSemanticTag(
	const FGameplayTag SemanticTag) const
{
	TArray<FIntentReplayObservationJournalEntry> Result;
	for (const FIntentReplayObservationJournalEntry& Entry : Entries)
	{
		if (GetEntrySemanticTag(Entry) == SemanticTag)
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

TArray<FIntentReplayObservationJournalEntry>
UIntentReplayObservationJournal::GetEntriesByResult(
	const EIntentReplayObservationMatchResult ResultFilter) const
{
	TArray<FIntentReplayObservationJournalEntry> Result;
	for (const FIntentReplayObservationJournalEntry& Entry : Entries)
	{
		if (Entry.Result == ResultFilter)
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

void UIntentReplayObservationJournal::Initialize(
	const FIntentReplayPlaybackSessionId InPlaybackSessionId,
	const FIntentReplayTrackId InSourceActionTrackId,
	const FIntentReplayObservationTrackId InSourceObservationTrackId,
	const FGuid InObserverId,
	const FName InMatchPolicyIdentity,
	const double InStartRelativeTime,
	const FIntentReplayObservationJournalOptions& InOptions)
{
	JournalId = FIntentReplayObservationJournalId::NewId();
	PlaybackSessionId = InPlaybackSessionId;
	SourceActionTrackId = InSourceActionTrackId;
	SourceObservationTrackId = InSourceObservationTrackId;
	ObserverId = InObserverId;
	MatchPolicyIdentity = InMatchPolicyIdentity;
	MatchPolicyVersion = 1;
	TerminalState = EIntentReplayObservationComparisonState::Created;
	StartRelativeTime = FMath::Max(0.0, InStartRelativeTime);
	EndRelativeTime = StartRelativeTime;
	Options = InOptions;
	Entries.Reset();
	Summary = FIntentReplayObservationComparisonSummary();
	bTerminal = false;
}

void UIntentReplayObservationJournal::MarkTerminal(
	const EIntentReplayObservationComparisonState InTerminalState,
	const double InEndRelativeTime)
{
	TerminalState = InTerminalState;
	EndRelativeTime = FMath::Max(StartRelativeTime, InEndRelativeTime);
	bTerminal = true;
}

void UIntentReplayObservationJournal::Append(
	FIntentReplayObservationJournalEntry&& Entry)
{
	++Summary.Compared;
	switch (Entry.Result)
	{
	case EIntentReplayObservationMatchResult::Matched:
		++Summary.Matched;
		break;
	case EIntentReplayObservationMatchResult::UnexpectedObservation:
	case EIntentReplayObservationMatchResult::UnexpectedStateValue:
	case EIntentReplayObservationMatchResult::UnexpectedStateStatus:
		++Summary.Unexpected;
		break;
	case EIntentReplayObservationMatchResult::Ambiguous:
		++Summary.Ambiguous;
		break;
	case EIntentReplayObservationMatchResult::Duplicate:
		++Summary.Duplicate;
		break;
	case EIntentReplayObservationMatchResult::ExpectedRecordExpiredUnobserved:
		++Summary.ExpiredUnobserved;
		break;
	case EIntentReplayObservationMatchResult::IgnoredByPolicy:
	case EIntentReplayObservationMatchResult::IgnoredWhilePaused:
		++Summary.Ignored;
		break;
	default:
		break;
	}
	if (Options.bBounded && Entries.Num() >= FMath::Max(1, Options.MaxEntries))
	{
		Entries.RemoveAt(0, 1, EAllowShrinking::No);
	}
	Entries.Add(MoveTemp(Entry));
}

void UIntentReplayObservationJournal::SetPendingExpected(const int32 Count)
{
	Summary.PendingExpected = FMath::Max(0, Count);
}
