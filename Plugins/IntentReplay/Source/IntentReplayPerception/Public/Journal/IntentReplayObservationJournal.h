#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "IntentReplayObservationJournal.generated.h"

/** Mutable, comparison-attempt-local diagnostics. It never mutates the source Observation Track. */
UCLASS(BlueprintType, Transient)
class INTENTREPLAYPERCEPTION_API UIntentReplayObservationJournal : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	FIntentReplayObservationJournalId GetJournalId() const { return JournalId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	FIntentReplayPlaybackSessionId GetPlaybackSessionId() const { return PlaybackSessionId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	FIntentReplayTrackId GetSourceActionTrackId() const { return SourceActionTrackId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	FIntentReplayObservationTrackId GetSourceObservationTrackId() const
	{
		return SourceObservationTrackId;
	}

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	FGuid GetObserverId() const { return ObserverId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	FName GetMatchPolicyIdentity() const { return MatchPolicyIdentity; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	int32 GetMatchPolicyVersion() const { return MatchPolicyVersion; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	EIntentReplayObservationComparisonState GetTerminalState() const
	{
		return TerminalState;
	}

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	double GetStartRelativeTime() const { return StartRelativeTime; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	double GetEndRelativeTime() const { return EndRelativeTime; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	int32 GetEntryCount() const { return Entries.Num(); }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	bool GetEntryByIndex(int32 Index, FIntentReplayObservationJournalEntry& OutEntry) const;

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Journal")
	TArray<FIntentReplayObservationJournalEntry> GetEntries() const { return Entries; }

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Journal")
	TArray<FIntentReplayObservationJournalEntry> GetEntriesForEntity(
		FPerceptionKnowledgeEntityId EntityId) const;

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Journal")
	TArray<FIntentReplayObservationJournalEntry> GetEntriesForSemanticTag(
		FGameplayTag SemanticTag) const;

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Journal")
	TArray<FIntentReplayObservationJournalEntry> GetEntriesByResult(
		EIntentReplayObservationMatchResult Result) const;

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	FIntentReplayObservationComparisonSummary GetSummary() const { return Summary; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Journal")
	bool IsTerminal() const { return bTerminal; }

private:
	void Initialize(
		FIntentReplayPlaybackSessionId InPlaybackSessionId,
		FIntentReplayTrackId InSourceActionTrackId,
		FIntentReplayObservationTrackId InSourceObservationTrackId,
		FGuid InObserverId,
		FName InMatchPolicyIdentity,
		double InStartRelativeTime,
		const FIntentReplayObservationJournalOptions& InOptions);
	void Append(FIntentReplayObservationJournalEntry&& Entry);
	void SetPendingExpected(int32 Count);
	void MarkTerminal(
		EIntentReplayObservationComparisonState InTerminalState,
		double InEndRelativeTime);

	UPROPERTY()
	FIntentReplayObservationJournalId JournalId;

	UPROPERTY()
	FIntentReplayPlaybackSessionId PlaybackSessionId;

	UPROPERTY()
	FIntentReplayTrackId SourceActionTrackId;

	UPROPERTY()
	FIntentReplayObservationTrackId SourceObservationTrackId;

	UPROPERTY()
	FGuid ObserverId;

	UPROPERTY()
	FName MatchPolicyIdentity;

	UPROPERTY()
	int32 MatchPolicyVersion = 1;

	UPROPERTY()
	EIntentReplayObservationComparisonState TerminalState =
		EIntentReplayObservationComparisonState::Created;

	UPROPERTY()
	double StartRelativeTime = 0.0;

	UPROPERTY()
	double EndRelativeTime = 0.0;

	UPROPERTY()
	FIntentReplayObservationJournalOptions Options;

	UPROPERTY()
	TArray<FIntentReplayObservationJournalEntry> Entries;

	UPROPERTY()
	FIntentReplayObservationComparisonSummary Summary;

	UPROPERTY()
	bool bTerminal = false;

	friend class UIntentReplayObservationComponent;
	friend struct FIntentReplayPerceptionTestAccessor;
};
