#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayTypes.h"
#include "IntentRecordingSession.generated.h"

class UIntentExecutionJournal;

/**
 * Component-owned mutable state for one recording attempt.
 *
 * The session is not transferred to clones. Finalization moves its entry storage into a separate
 * immutable UIntentReplayTrack and leaves this object available only for state/journal inspection.
 */
UCLASS(BlueprintType, Transient)
class INTENTREPLAY_API UIntentRecordingSession : public UObject
{
	GENERATED_BODY()

public:
	/** Current lifecycle state. Draining means AsyncStop is waiting for terminal callbacks. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	EIntentRecordingState GetState() const { return State; }

	/** Identity reserved for the track that this session will publish. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	FIntentReplayTrackId GetTrackId() const { return TrackId; }

	/** Identity of this mutable attempt; distinct from the track it may publish. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	FIntentRecordingSessionId GetSessionId() const { return SessionId; }

	/** Number of Accepted action snapshots currently committed to mutable storage. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	int32 GetEntryCount() const { return MutableEntries.Num(); }

	/** Number of accepted actions whose Ended result AsyncStop still awaits. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	int32 GetPendingDrainCount() const { return PendingTrackedHandles.Num(); }

	/** Recording-local journal, retained with the terminal session for diagnostics. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	UIntentExecutionJournal* GetExecutionJournal() const { return ExecutionJournal; }

private:
	/** Mutated only through UIntentReplayComponent state-transition helpers. */
	UPROPERTY()
	EIntentRecordingState State = EIntentRecordingState::Created;

	/** New GUID for every StartRecording; never recycled from a previous track. */
	UPROPERTY()
	FIntentReplayTrackId TrackId;

	UPROPERTY()
	FIntentRecordingSessionId SessionId;

	/** Immutable copy of caller options for this session. */
	UPROPERTY()
	FIntentRecordingOptions Options;

	/** Transactionally committed Accepted snapshots, moved out on finalization. */
	UPROPERTY()
	TArray<FRecordedIntent> MutableEntries;

	/** Source handle to entry lookup used only while source actions still exist. */
	UPROPERTY()
	TMap<FGameplayActionHandle, int32> EntryIndexByHandle;

	/** Source handles still eligible to contribute OriginalResult before publication. */
	UPROPERTY()
	TSet<FGameplayActionHandle> PendingTrackedHandles;

	/** Session-owned diagnostic journal; does not decide which entries enter the track. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentExecutionJournal> ExecutionJournal;

	// Raw clock fields are intentionally private and non-reflected: the component owns all state
	// transitions, and the transient session never needs persistence or editor serialization.
	double StartTimeSeconds = 0.0;
	double PauseStartTimeSeconds = 0.0;
	double AccumulatedPausedSeconds = 0.0;
	double FinalRecordedDurationSeconds = 0.0;
	int64 NextTimelineSequence = 0;
	bool bClockPaused = false;

	friend class UIntentReplayComponent;
};
