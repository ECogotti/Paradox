#include "Journal/IntentExecutionJournal.h"

bool UIntentExecutionJournal::GetEntryByIndex(const int32 Index, FIntentExecutionEvent& OutEvent) const
{
	if (!Events.IsValidIndex(Index))
	{
		return false;
	}
	OutEvent = Events[Index];
	return true;
}

void UIntentExecutionJournal::Clear()
{
	Events.Reset();
}

void UIntentExecutionJournal::Initialize(
	const FIntentExecutionJournalOptions& InOptions,
	const double InStartTimeSeconds)
{
	// Clamp even when the selected policy ignores capacity so a later diagnostic inspection never
	// observes an internally invalid options snapshot.
	Options = InOptions;
	Options.MaxEntries = FMath::Max(1, Options.MaxEntries);
	StartTimeSeconds = InStartTimeSeconds;
	Events.Reset();
}

void UIntentExecutionJournal::Append(FIntentExecutionEvent&& Event)
{
	if (Options.CapacityPolicy == EIntentExecutionJournalCapacityPolicy::Disabled)
	{
		return;
	}
	if (Options.CapacityPolicy == EIntentExecutionJournalCapacityPolicy::BoundedRingBuffer
		&& Events.Num() >= Options.MaxEntries)
	{
		// Preserve chronological order for Blueprint/debug consumers by evicting from the front.
		const int32 RemoveCount = Events.Num() - Options.MaxEntries + 1;
		Events.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
	Events.Add(MoveTemp(Event));
}
