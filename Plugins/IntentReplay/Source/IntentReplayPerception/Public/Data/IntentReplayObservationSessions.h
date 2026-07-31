#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "IntentReplayObservationSessions.generated.h"

class UIntentReplayObservationJournal;
class UIntentReplayTimelineBundle;

UCLASS(BlueprintType, Transient)
class INTENTREPLAYPERCEPTION_API UIntentReplayObservationRecordingSession : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Recording")
	EIntentReplayObservationRecordingState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Recording")
	FIntentReplayObservationRecordingSessionId GetSessionId() const { return SessionId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Recording")
	FIntentReplayObservationTrackId GetObservationTrackId() const { return ObservationTrackId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Recording")
	int32 GetEntryCount() const { return MutableEntries.Num(); }

private:
	UPROPERTY()
	EIntentReplayObservationRecordingState State =
		EIntentReplayObservationRecordingState::Created;

	UPROPERTY()
	FIntentReplayObservationRecordingSessionId SessionId;

	UPROPERTY()
	FIntentReplayObservationTrackId ObservationTrackId;

	UPROPERTY()
	FIntentRecordingSessionId SourceRecordingSessionId;

	UPROPERTY()
	FIntentReplayTrackId SourceTrackId;

	UPROPERTY()
	FIntentReplayObservationRecordOptions Options;

	UPROPERTY()
	TArray<FIntentReplayRecordedObservation> MutableEntries;

	double FinalRecordedDurationSeconds = 0.0;

	friend class UIntentReplayObservationComponent;
	friend struct FIntentReplayPerceptionTestAccessor;
};

UCLASS(BlueprintType, Transient)
class INTENTREPLAYPERCEPTION_API UIntentReplayObservationComparisonSession : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Comparison")
	EIntentReplayObservationComparisonState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Comparison")
	FIntentReplayObservationComparisonSessionId GetSessionId() const { return SessionId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Comparison")
	UIntentReplayTimelineBundle* GetTimelineBundle() const { return TimelineBundle; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Comparison")
	UIntentReplayObservationJournal* GetJournal() const { return Journal; }

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Comparison")
	TArray<FRecordedObservationId> GetConsumedObservationIds() const
	{
		return ConsumedObservationIds.Array();
	}

private:
	struct FStateIndexKey
	{
		FPerceptionKnowledgeEntityId EntityId;
		FGameplayTag StateTag;
		FGameplayTag SenseTag;
		friend bool operator==(const FStateIndexKey& Left, const FStateIndexKey& Right)
		{
			return Left.EntityId == Right.EntityId
				&& Left.StateTag == Right.StateTag
				&& Left.SenseTag == Right.SenseTag;
		}
		friend uint32 GetTypeHash(const FStateIndexKey& Key)
		{
			return HashCombine(
				HashCombine(GetTypeHash(Key.EntityId), GetTypeHash(Key.StateTag)),
				GetTypeHash(Key.SenseTag));
		}
	};

	struct FEventIndexKey
	{
		FGameplayTag EventTag;
		FPerceptionKnowledgeEntityId SourceEntityId;
		FGameplayTag SenseTag;
		friend bool operator==(const FEventIndexKey& Left, const FEventIndexKey& Right)
		{
			return Left.EventTag == Right.EventTag
				&& Left.SourceEntityId == Right.SourceEntityId
				&& Left.SenseTag == Right.SenseTag;
		}
		friend uint32 GetTypeHash(const FEventIndexKey& Key)
		{
			return HashCombine(
				HashCombine(GetTypeHash(Key.EventTag), GetTypeHash(Key.SourceEntityId)),
				GetTypeHash(Key.SenseTag));
		}
	};

	UPROPERTY()
	EIntentReplayObservationComparisonState State =
		EIntentReplayObservationComparisonState::Created;

	UPROPERTY()
	FIntentReplayObservationComparisonSessionId SessionId;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayTimelineBundle> TimelineBundle;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationJournal> Journal;

	UPROPERTY()
	FIntentReplayObservationMatchOptions Options;

	TMap<FStateIndexKey, TArray<int32>> StateIndex;
	TMap<FEventIndexKey, TArray<int32>> EventIndex;
	TMultiMap<FGameplayTag, int32> StateTagIndex;
	TMultiMap<FGameplayTag, int32> EventTagIndex;
	TSet<FRecordedObservationId> ConsumedObservationIds;
	TSet<FGuid> SeenCurrentEventIds;
	TSet<FRecordedObservationId> ExpiredObservationIds;
	int64 NextJournalSequence = 0;
	bool bComparisonEnabled = true;
	bool bLocallyPaused = false;

	friend class UIntentReplayObservationComponent;
	friend struct FIntentReplayPerceptionTestAccessor;
};
