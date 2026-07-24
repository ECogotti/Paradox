#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayTypes.h"
#include "IntentExecutionJournal.generated.h"

/**
 * Transient diagnostic history of observed GameplayActions lifecycle events.
 *
 * A journal is not a replay track: it may include rejected, replayed, and synthetic failure events,
 * while only transactionally accepted recordable requests enter UIntentReplayTrack.
 */
UCLASS(BlueprintType, Transient)
class INTENTREPLAY_API UIntentExecutionJournal : public UObject
{
	GENERATED_BODY()

public:
	/** Number of events currently retained under the configured capacity policy. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Journal")
	int32 GetEntryCount() const { return Events.Num(); }

	/** Copies one event out without exposing the mutable backing array. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Journal")
	bool GetEntryByIndex(int32 Index, FIntentExecutionEvent& OutEvent) const;

	/** Returns a value copy suitable for Blueprint inspection. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Journal")
	TArray<FIntentExecutionEvent> GetEvents() const { return Events; }

	/** Drops retained diagnostics without changing recording or playback state. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Journal")
	void Clear();

private:
	/** Establishes immutable capacity options and the relative timestamp origin. */
	void Initialize(const FIntentExecutionJournalOptions& InOptions, double InStartTimeSeconds);
	/** Applies the capacity policy before appending one already isolated event snapshot. */
	void Append(FIntentExecutionEvent&& Event);
	double GetStartTimeSeconds() const { return StartTimeSeconds; }

	/** Capacity policy copied from the owning component/session. */
	UPROPERTY()
	FIntentExecutionJournalOptions Options;

	/** Oldest-to-newest retained diagnostic events. */
	UPROPERTY()
	TArray<FIntentExecutionEvent> Events;

	/** Time-source value subtracted from absolute observations. */
	double StartTimeSeconds = 0.0;

	friend class UIntentReplayComponent;
};
