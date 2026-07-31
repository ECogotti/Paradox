#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "IntentReplayObservationTrack.generated.h"

/** Finalized immutable semantic observations captured on one IntentReplay recording timeline. */
UCLASS(BlueprintType, Transient)
class INTENTREPLAYPERCEPTION_API UIntentReplayObservationTrack : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	FIntentReplayObservationTrackId GetObservationTrackId() const { return ObservationTrackId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	int32 GetFormatVersion() const { return FormatVersion; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	FIntentReplayTrackId GetSourceIntentReplayTrackId() const { return SourceIntentReplayTrackId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	FIntentRecordingSessionId GetSourceRecordingSessionId() const { return SourceRecordingSessionId; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	double GetRecordedDurationSeconds() const { return RecordedDurationSeconds; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	int32 GetEntryCount() const { return Entries.Num(); }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	bool GetEntryByIndex(int32 Index, FIntentReplayRecordedObservation& OutEntry) const;

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	bool FindEntryById(FRecordedObservationId Id, FIntentReplayRecordedObservation& OutEntry) const;

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Track")
	TArray<FIntentReplayRecordedObservation> GetEntriesInTimeRange(
		double StartTimeSeconds,
		double EndTimeSeconds) const;

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	bool IsFinalized() const { return bFinalized; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Track")
	FIntentReplayObservationTrackValidationResult ValidateTrack() const;

	const TArray<FIntentReplayRecordedObservation>& GetEntries() const { return Entries; }

private:
	void InitializeFinalized(
		FIntentReplayObservationTrackId InObservationTrackId,
		FIntentReplayTrackId InSourceTrackId,
		FIntentRecordingSessionId InSourceRecordingSessionId,
		TArray<FIntentReplayRecordedObservation>&& InEntries,
		double InRecordedDurationSeconds);

	UPROPERTY()
	FIntentReplayObservationTrackId ObservationTrackId;

	UPROPERTY()
	int32 FormatVersion = 1;

	UPROPERTY()
	FIntentReplayTrackId SourceIntentReplayTrackId;

	UPROPERTY()
	FIntentRecordingSessionId SourceRecordingSessionId;

	UPROPERTY()
	TArray<FIntentReplayRecordedObservation> Entries;

	UPROPERTY()
	double RecordedDurationSeconds = 0.0;

	UPROPERTY()
	bool bFinalized = false;

	friend class UIntentReplayObservationComponent;
	friend struct FIntentReplayPerceptionTestAccessor;
};
