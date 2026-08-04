#pragma once

#include "Components/ActorComponent.h"
#include "TimeLoop/ParadoxTimeLoopTypes.h"
#include "ParadoxTimeLoopComponent.generated.h"

class AParadoxCloneCharacter;
class AParadoxCloneController;
class AParadoxChronoSpawn;
class AParadoxCharacter;
class AParadoxPlayerCharacter;
class AParadoxWorldStateAnchor;
class UEntityRelationPolicySet;
class UIntentReplayComponent;
class UIntentReplayTimelineBundle;
class UParadoxTemporalVisionComponent;
class UWorldStateSubsystem;

/** Private, recipient-local orchestration state. Immutable source tracks remain in timelines. */
struct FParadoxClonePlaybackRuntime
{
	TWeakObjectPtr<AParadoxCloneCharacter> Clone;
	TWeakObjectPtr<UIntentReplayComponent> ReplayComponent;
	int32 TemporalIndex = INDEX_NONE;
	EParadoxClonePlaybackState State = EParadoxClonePlaybackState::Unprepared;
	FIntentReplayPlaybackSessionId SessionId;
	TWeakObjectPtr<UIntentReplayTimelineBundle> TimelineBundle;
	FParadoxClonePlaybackFailure LastFailure;
};

/** Unique per-GameMode authority for Paradox recording, reset and clone reconstruction. */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxTimeLoopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxTimeLoopComponent();

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopPhaseChangedEvent OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxChronoSpawnEvent OnChronoSpawnSelected;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxChronoSpawnEvent OnChronoSpawnRejected;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnRunStarted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnRunEnded;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnTimelineConsolidated;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnWorldResetCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxCloneReconstructedEvent OnCloneReconstructed;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnSynchronizedStartAwaiting;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxClonePlaybackEvent OnClonePlaybackReady;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxClonePlaybackEvent OnClonePlaybackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxClonePlaybackEvent OnClonePlaybackCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxClonePlaybackEvent OnClonePlaybackStopped;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxClonePlaybackFailureEvent OnClonePlaybackFailed;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTemporalOverlapEvent OnTemporalOverlapDetected;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTemporalCandidateEvent OnTemporalCandidateIgnored;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxAcceptedEvent OnParadoxAccepted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnParadoxRecoveryCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxGameOverEvent OnGameOver;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxLevelCompleteEvent OnLevelCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnRestartRequested;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnOperationFailed;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Time Loop|Events")
	FParadoxTimeLoopOperationEvent OnError;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult InitializeTimeLoop();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult SelectChronoSpawn(AParadoxChronoSpawn* ChronoSpawn);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult RequestTimeRewind();

	/** Retires a replay clone in place after its recorded Time Travel VFX completes. */
	bool CompleteCloneTimeTravelDeparture(
		AParadoxCloneCharacter& Clone,
		FString& OutDiagnostic);

	/** Presentation acknowledgement that authorizes reset after the fade reached black. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult ContinueParadoxRecovery(FGuid ParadoxEventId);

	/** External puzzle authority command; the loop does not invent a victory condition. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult RequestLevelComplete();

	/** Reopens the current map so World, GameMode and temporal state are constructed from zero. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult RequestRestartLevel();

	/** Updates presentation-only hover state. It never selects a spawn. */
	void UpdateHoveredChronoSpawn(AParadoxChronoSpawn* ChronoSpawn);

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	bool IsTimeLoopEnabled() const { return bTimeLoopEnabled; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	EParadoxTimeLoopPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	bool IsMovementAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	int32 GetMaximumTimelineCount() const { return MaximumTimelineCount; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	int32 GetConsolidatedTimelineCount() const { return ConsolidatedTimelines.Num(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	TArray<FParadoxConsolidatedTimeline> GetConsolidatedTimelines() const { return ConsolidatedTimelines; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	AParadoxChronoSpawn* GetSelectedChronoSpawn() const { return SelectedChronoSpawn; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult GetLastOperationResult() const { return LastOperationResult; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Playback")
	int32 GetClonePlaybackParticipantCount() const { return ClonePlaybackRuntimes.Num(); }

	/** Copies one runtime snapshot by Temporal Index without exposing session internals. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Playback")
	bool GetClonePlaybackSnapshot(
		int32 TemporalIndex,
		FParadoxClonePlaybackSnapshot& OutSnapshot) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Playback")
	FParadoxClonePlaybackFailure GetLastClonePlaybackFailure() const
	{
		return LastClonePlaybackFailure;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Temporal Vision")
	bool IsTemporalDetectionAuthoritative() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Temporal Vision")
	int32 GetTemporalDetectionParticipantCount() const
	{
		return TemporalVisionParticipants.Num();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Temporal Vision")
	int32 GetDeduplicatedTemporalOverlapPairCount() const;

	/** Copies one participant's debug state by Temporal Index. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Temporal Vision")
	bool GetTemporalVisionDebugSnapshot(
		int32 TemporalIndex,
		FParadoxTemporalVisionDebugSnapshot& OutSnapshot) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Temporal Vision")
	FParadoxTemporalCandidateSnapshot GetLastTemporalCandidate() const
	{
		return LastTemporalCandidate;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop|Temporal Vision")
	FParadoxContext GetLastParadoxContext() const { return LastParadoxContext; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	FParadoxGameOverContext GetLastGameOverContext() const
	{
		return LastGameOverContext;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	FParadoxLevelCompleteContext GetLastLevelCompleteContext() const
	{
		return LastLevelCompleteContext;
	}

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FParadoxTimeLoopOperationResult MakeResult(
		EParadoxTimeLoopOperationStatus Status,
		const FString& DiagnosticMessage);
	FParadoxTimeLoopOperationResult FailOperation(
		EParadoxTimeLoopOperationStatus Status,
		const FString& DiagnosticMessage,
		bool bEnterErrorPhase);
	void SetPhase(EParadoxTimeLoopPhase NewPhase);
	void DiscoverChronoSpawns();
	bool PrepareWorldState(FString& OutFailure);
	bool EnsureWorldStateAnchor(FString& OutFailure);
	AParadoxPlayerCharacter* ResolvePlayerCharacter() const;
	bool ActivatePlayerAtSelectedSpawn(FString& OutFailure);
	void DeactivatePlayer();
	void SetTemporalAvatarGridPresence(
		AParadoxCharacter& Character,
		bool bEnabled) const;
	bool PreparePlayerRecorder(FString& OutFailure);
	bool BeginPlayerRecording(FString& OutFailure);
	bool ConfigureEntityRelations(FString& OutFailure);
	bool PrepareTemporalDetection(FString& OutFailure);
	void EnableTemporalDetection();
	void DisableTemporalDetection(bool bClearParticipants);
	void AcceptParadox(const FParadoxTemporalCandidateSnapshot& Candidate);
	bool RestoreWorldAndReconstructAfterParadox(FString& OutFailure);
	void PresentParadoxOrRecoverImmediately();
	void PresentGameOver();
	void PresentLevelComplete();
	void StopActiveRunWithoutConsolidation();
	void IgnoreTemporalCandidate(
		const FParadoxTemporalOverlapSnapshot& PhysicalOverlap,
		EParadoxTemporalCandidateDisposition Disposition,
		const FString& DiagnosticMessage,
		const FEntityRelationResult* RelationResult = nullptr);
	bool PrepareClonePlaybacks(FString& OutFailure);
	void TryReleaseSynchronizedStart();
	void RecoverFromSynchronizedStartFailure(const FString& DiagnosticMessage);
	void StopAndUnbindClonePlaybacks(bool bBroadcastStopped);
	void BindClonePlaybackDelegates(UIntentReplayComponent& ReplayComponent);
	void UnbindClonePlaybackDelegates(UIntentReplayComponent& ReplayComponent);
	FParadoxClonePlaybackRuntime* FindClonePlaybackRuntime(
		FIntentReplayPlaybackSessionId SessionId);
	const FParadoxClonePlaybackRuntime* FindClonePlaybackRuntime(
		int32 TemporalIndex) const;
	bool IsSynchronizedStartBarrierResolved() const;
	FParadoxClonePlaybackSnapshot MakeClonePlaybackSnapshot(
		const FParadoxClonePlaybackRuntime& Runtime) const;
	void BroadcastClonePlaybackState(FParadoxClonePlaybackRuntime& Runtime);
	void MarkClonePlaybackFailed(
		FParadoxClonePlaybackRuntime& Runtime,
		const FIntentReplayFailure& Failure,
		EIntentReplayPlaybackState ExecutorState);
	void SetClonePlaybackMovementEnabled(
		AParadoxCloneCharacter& Clone,
		bool bEnabled) const;
	FParadoxClonePlaybackFailure BuildClonePlaybackFailure(
		const FParadoxClonePlaybackRuntime& Runtime,
		const FIntentReplayFailure& Failure,
		EIntentReplayPlaybackState ExecutorState) const;
	bool ReconstructConsolidatedClones(FString& OutFailure);
	void DestroyRuntimeClones();
	void ReapplyChronoSpawnStates();
	bool IsConfiguredCloneClassUsable() const;

	UFUNCTION()
	void HandleCloneReplayPrepared(
		FIntentReplayPlaybackSessionId SessionId,
		UIntentReplayTrack* Track);

	UFUNCTION()
	void HandleCloneReplayStarted(FIntentReplayPlaybackSessionId SessionId);

	UFUNCTION()
	void HandleCloneReplayCompleted(const FIntentReplayResult& Result);

	UFUNCTION()
	void HandleCloneReplayFailed(const FIntentReplayResult& Result);

	UFUNCTION()
	void HandleCloneReplayStopped(const FIntentReplayResult& Result);

	UFUNCTION()
	void HandleTemporalOverlapDetected(
		const FParadoxTemporalOverlapSnapshot& Snapshot);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Time Loop", meta = (AllowPrivateAccess = "true"))
	bool bTimeLoopEnabled = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Time Loop", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AParadoxCloneCharacter> CloneCharacterClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Time Loop", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AParadoxCloneController> CloneControllerClass;

	/** Complete per-world policy set used by time-loop relation queries in this map. */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadOnly,
		Category = "Paradox|Time Loop|Temporal Vision",
		meta = (AllowPrivateAccess = "true", AllowedClasses = "/Script/EntityRelations.EntityRelationPolicySet"))
	TSoftObjectPtr<UEntityRelationPolicySet> TemporalRelationPolicySet;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Paradox|Time Loop", meta = (AllowPrivateAccess = "true"))
	EParadoxTimeLoopPhase CurrentPhase = EParadoxTimeLoopPhase::Disabled;

	UPROPERTY(Transient)
	FParadoxTimeLoopOperationResult LastOperationResult;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AParadoxChronoSpawn>> ChronoSpawns;

	UPROPERTY(Transient)
	TArray<FParadoxConsolidatedTimeline> ConsolidatedTimelines;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AParadoxCloneCharacter>> RuntimeClones;

	TArray<FParadoxClonePlaybackRuntime> ClonePlaybackRuntimes;

	TArray<TWeakObjectPtr<UParadoxTemporalVisionComponent>>
		TemporalVisionParticipants;

	UPROPERTY(Transient)
	FParadoxClonePlaybackFailure LastClonePlaybackFailure;

	UPROPERTY(Transient)
	FParadoxTemporalCandidateSnapshot LastTemporalCandidate;

	UPROPERTY(Transient)
	FParadoxContext LastParadoxContext;

	UPROPERTY(Transient)
	FParadoxGameOverContext LastGameOverContext;

	UPROPERTY(Transient)
	FParadoxLevelCompleteContext LastLevelCompleteContext;

	UPROPERTY(Transient)
	TObjectPtr<AParadoxPlayerCharacter> PlayerCharacter = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AParadoxChronoSpawn> SelectedChronoSpawn = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AParadoxChronoSpawn> HoveredChronoSpawn = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AParadoxWorldStateAnchor> WorldStateAnchor = nullptr;

	UPROPERTY(Transient)
	int32 MaximumTimelineCount = 0;

	bool bPlayerCollisionWasEnabled = true;
	int32 TemporalDetectionSessionId = 0;
	bool bParadoxAcceptedForRun = false;
	bool bEntityRelationsOverrideApplied = false;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxTimeLoopTestAccessor;
#endif
};
