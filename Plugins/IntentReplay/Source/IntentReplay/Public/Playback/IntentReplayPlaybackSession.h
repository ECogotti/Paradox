#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayTypes.h"
#include "IntentReplayPlaybackSession.generated.h"

class UIntentExecutionJournal;
class UIntentReplayTrack;

/**
 * Recipient-local mutable state for replaying one immutable track.
 *
 * Every recipient creates its own session, prepared requests, timer position, runtime handles,
 * compatibility reports, and journal. Sharing SourceTrack therefore never shares execution state.
 */
UCLASS(BlueprintType, Transient)
class INTENTREPLAY_API UIntentReplayPlaybackSession : public UObject
{
	GENERATED_BODY()

public:
	/** Current preparation/playback lifecycle state. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	EIntentReplayPlaybackState GetState() const { return State; }

	/** GUID unique to this recipient-local playback run. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	FIntentReplayPlaybackSessionId GetSessionId() const { return SessionId; }

	/** Immutable track read by this session; retaining the session also retains the track. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	UIntentReplayTrack* GetSourceTrack() const { return SourceTrack; }

	/** First track entry not yet processed for submission. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	int32 GetNextEntryIndex() const { return NextEntryIndex; }

	/** Number of active or queued GameplayActions created by this session. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	int32 GetReplayOwnedActionCount() const { return ActiveReplayHandles.Num(); }

	/** Snapshot of session-owned handles; modifying the returned array cannot alter ownership. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	TArray<FGameplayActionHandle> GetReplayOwnedActionHandles() const { return ActiveReplayHandles.Array(); }

	/** Playback-local lifecycle/divergence journal. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	UIntentExecutionJournal* GetExecutionJournal() const { return ExecutionJournal; }

	/** One compatibility report is generated for every prepared track entry. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	int32 GetCompatibilityReportCount() const { return CompatibilityReports.Num(); }

	/** Copies a compatibility report out by track index. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	bool GetCompatibilityReportByIndex(
		int32 Index,
		FIntentReplayCompatibilityReport& OutReport) const
	{
		if (!CompatibilityReports.IsValidIndex(Index))
		{
			return false;
		}
		OutReport = CompatibilityReports[Index];
		return true;
	}

private:
	/** Mutated only by the owning UIntentReplayComponent. */
	UPROPERTY()
	EIntentReplayPlaybackState State = EIntentReplayPlaybackState::Created;

	/** Unique recipient-local session identity. */
	UPROPERTY()
	FIntentReplayPlaybackSessionId SessionId;

	/** Shared immutable input; no timer or runtime handle lives on the track. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayTrack> SourceTrack;

	/** Immutable copy of options selected at PrepareReplay. */
	UPROPERTY()
	FIntentReplayPlaybackOptions Options;

	/** Recipient-local requests rebuilt from current Definitions and recorded values. */
	UPROPERTY()
	TArray<FGameplayActionRequest> PreparedRequests;

	/** Per-entry schema/configuration comparison produced during preparation. */
	UPROPERTY()
	TArray<FIntentReplayCompatibilityReport> CompatibilityReports;

	/** Runtime handle to immutable entry correlation for lifecycle routing. */
	UPROPERTY()
	TMap<FGameplayActionHandle, FRecordedIntentId> RecordByRuntimeHandle;

	/** Exact cancellation boundary: Stop/Failure never cancel handles outside this set. */
	UPROPERTY()
	TSet<FGameplayActionHandle> ActiveReplayHandles;

	/** Session-owned diagnostic journal. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentExecutionJournal> ExecutionJournal;

	// Scheduler clock/index fields remain recipient-local, which makes one track safe to replay on
	// multiple actors simultaneously.
	int32 NextEntryIndex = 0;
	int32 ProcessedEntryCount = 0;
	double StartTimeSeconds = 0.0;
	double PauseStartTimeSeconds = 0.0;
	double AccumulatedPausedSeconds = 0.0;
	bool bClockPaused = false;
	bool bAllEntriesSubmittedBroadcast = false;
	bool bPausedBoundActionsBySession = false;

	friend class UIntentReplayComponent;
};
