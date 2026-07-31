#pragma once

#include "Components/ActorComponent.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "IntentReplayObservationComponent.generated.h"

class APlayerController;
class UCanvas;
class UIntentReplayComponent;
class UIntentReplayObservationJournal;
class UIntentReplayObservationMatchPolicy;
class UIntentReplayObservationRecordPolicy;
class UIntentReplayObservationRecordingSession;
class UIntentReplayObservationComparisonSession;
class UIntentReplayObservationTrack;
class UIntentReplayTimelineBundle;
class UPerceptionKnowledgeListenerComponent;
class UIntentReplayTrack;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FIntentReplayObservationRecordedDelegate,
	const FIntentReplayRecordedObservation&,
	Observation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIntentReplayObservationTrackFinalizedDelegate,
	UIntentReplayObservationTrack*,
	ObservationTrack,
	UIntentReplayTimelineBundle*,
	TimelineBundle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FIntentReplayObservationComparedDelegate,
	const FIntentReplayObservationComparisonEvent&,
	Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIntentReplayObservationJournalCompletedDelegate,
	UIntentReplayObservationJournal*,
	Journal,
	const FIntentReplayObservationComparisonSummary&,
	Summary);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FIntentReplayObservationRecordedNativeDelegate,
	const FIntentReplayRecordedObservation&);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FIntentReplayObservationTrackFinalizedNativeDelegate,
	UIntentReplayObservationTrack*,
	UIntentReplayTimelineBundle*);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FIntentReplayObservationComparedNativeDelegate,
	const FIntentReplayObservationComparisonEvent&);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FIntentReplayObservationJournalCompletedNativeDelegate,
	UIntentReplayObservationJournal*,
	const FIntentReplayObservationComparisonSummary&);

/**
 * Optional adapter that records and compares PerceptionKnowledge observations on IntentReplay's
 * authoritative timeline. It never owns or mutates the Listener Knowledge Store.
 */
UCLASS(ClassGroup = (IntentReplay), BlueprintType, meta = (BlueprintSpawnableComponent))
class INTENTREPLAYPERCEPTION_API UIntentReplayObservationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIntentReplayObservationComponent();

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Binding")
	FIntentReplayObservationOperationResult InitializeObservationReplay();

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Binding")
	FIntentReplayObservationOperationResult SetIntentReplaySource(
		UIntentReplayComponent* InIntentReplaySource);

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Binding")
	FIntentReplayObservationOperationResult SetPerceptionKnowledgeListener(
		UPerceptionKnowledgeListenerComponent* InListener);

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Binding")
	UIntentReplayComponent* GetBoundIntentReplaySource() const { return BoundIntentReplaySource; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Binding")
	UPerceptionKnowledgeListenerComponent* GetBoundPerceptionKnowledgeListener() const
	{
		return BoundPerceptionListener;
	}

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Binding")
	bool IsObservationReplayInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Recording")
	FIntentReplayObservationOperationResult StartSynchronizedObservationRecording(
		const FIntentReplayObservationRecordOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Recording")
	FIntentReplayObservationOperationResult StopObservationRecording();

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Recording")
	FIntentReplayObservationOperationResult CancelObservationRecording();

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Recording")
	UIntentReplayObservationRecordingSession* GetActiveObservationRecordingSession() const
	{
		return ActiveRecordingSession;
	}

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Recording")
	UIntentReplayObservationTrack* GetLastFinalizedObservationTrack() const
	{
		return LastFinalizedObservationTrack;
	}

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Bundle")
	UIntentReplayTimelineBundle* GetLastTimelineBundle() const { return LastTimelineBundle; }

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Bundle")
	UIntentReplayTimelineBundle* CreateTimelineBundle(
		UIntentReplayTrack* ActionTrack,
		UIntentReplayObservationTrack* ObservationTrack,
		FIntentReplayObservationOperationResult& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Comparison")
	FIntentReplayObservationOperationResult StartObservationComparison(
		UIntentReplayTimelineBundle* TimelineBundle,
		const FIntentReplayObservationMatchOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Comparison")
	FIntentReplayObservationOperationResult PauseObservationComparison();

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Comparison")
	FIntentReplayObservationOperationResult ResumeObservationComparison();

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Comparison")
	FIntentReplayObservationOperationResult SetObservationComparisonEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Comparison")
	FIntentReplayObservationOperationResult StopObservationComparison();

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Comparison")
	UIntentReplayObservationComparisonSession* GetActiveObservationComparisonSession() const
	{
		return ActiveComparisonSession;
	}

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Comparison")
	UIntentReplayObservationJournal* GetActiveObservationJournal() const;

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Comparison")
	FIntentReplayObservationComparisonSummary GetComparisonSummary() const;

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Comparison")
	TArray<FIntentReplayRecordedObservation> GetPendingExpectedObservations() const;

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Debug")
	void SetDebugEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Debug")
	FIntentReplayObservationDebugFrame BuildDebugFrame() const;

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Debug")
	FIntentReplayPerceptionRuntimeStats GetRuntimeStats() const { return RuntimeStats; }

	UFUNCTION(BlueprintCallable, Category = "Intent Replay Perception|Debug")
	void DumpObservationTimelineToLog() const;

	FIntentReplayObservationRecordedNativeDelegate& OnObservationRecordedNative()
	{
		return ObservationRecordedNative;
	}
	FIntentReplayObservationTrackFinalizedNativeDelegate& OnObservationTrackFinalizedNative()
	{
		return ObservationTrackFinalizedNative;
	}
	FIntentReplayObservationComparedNativeDelegate& OnObservationComparedNative()
	{
		return ObservationComparedNative;
	}
	FIntentReplayObservationComparedNativeDelegate& OnObservationUnexpectedNative()
	{
		return ObservationUnexpectedNative;
	}
	FIntentReplayObservationComparedNativeDelegate& OnObservationMatchedNative()
	{
		return ObservationMatchedNative;
	}
	FIntentReplayObservationComparedNativeDelegate& OnObservationAmbiguousNative()
	{
		return ObservationAmbiguousNative;
	}
	FIntentReplayObservationJournalCompletedNativeDelegate& OnObservationJournalCompletedNative()
	{
		return ObservationJournalCompletedNative;
	}

	UPROPERTY(BlueprintAssignable, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationRecordedDelegate OnObservationRecorded;

	UPROPERTY(BlueprintAssignable, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationTrackFinalizedDelegate OnObservationTrackFinalized;

	UPROPERTY(BlueprintAssignable, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationComparedDelegate OnObservationCompared;

	UPROPERTY(BlueprintAssignable, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationComparedDelegate OnObservationMatched;

	UPROPERTY(BlueprintAssignable, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationComparedDelegate OnObservationUnexpected;

	UPROPERTY(BlueprintAssignable, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationComparedDelegate OnObservationAmbiguous;

	UPROPERTY(BlueprintAssignable, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationJournalCompletedDelegate OnObservationJournalCompleted;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Intent Replay Perception|Binding")
	TObjectPtr<UIntentReplayComponent> IntentReplaySourceOverride;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Intent Replay Perception|Binding")
	TObjectPtr<UPerceptionKnowledgeListenerComponent> PerceptionListenerOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	bool bAutoStartObservationRecording = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	FIntentReplayObservationRecordOptions DefaultRecordingOptions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Policies")
	TSubclassOf<UIntentReplayObservationRecordPolicy> RecordPolicyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Policies")
	TSubclassOf<UIntentReplayObservationMatchPolicy> MatchPolicyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bEnableDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	FIntentReplayObservationDebugFilter DebugFilter;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	UFUNCTION(BlueprintNativeEvent, Category = "Intent Replay Perception|Correlation")
	void ResolveObservationCorrelation(
		const FPerceptionKnowledgeObservation& Observation,
		FIntentReplayObservationCorrelation& OutCorrelation) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
	struct FObservedStateKey
	{
		FPerceptionKnowledgeEntityId EntityId;
		FGameplayTag StateTag;
		FGameplayTag SenseTag;
		friend bool operator==(const FObservedStateKey& Left, const FObservedStateKey& Right)
		{
			return Left.EntityId == Right.EntityId
				&& Left.StateTag == Right.StateTag
				&& Left.SenseTag == Right.SenseTag;
		}
		friend uint32 GetTypeHash(const FObservedStateKey& Key)
		{
			return HashCombine(
				HashCombine(GetTypeHash(Key.EntityId), GetTypeHash(Key.StateTag)),
				GetTypeHash(Key.SenseTag));
		}
	};

	struct FStateSignature
	{
		FPerceptionKnowledgeValue Value;
		EPerceptionKnowledgeFactStatus Status = EPerceptionKnowledgeFactStatus::Unknown;
		int64 PerceptionEpoch = 0;
		double WorldTimestamp = 0.0;
	};

	FIntentReplayObservationOperationResult MakeResult(
		EIntentReplayObservationOperationStatus Status,
		FString Diagnostic = FString()) const;
	bool HasActiveSessions() const;
	void BindSources();
	void UnbindSources();
	void ShutdownObservationReplay();
	void HandleTimelineLifecycleChanged(const FIntentReplayTimelineLifecycleEvent& Event);
	void HandleObservationProduced(const FPerceptionKnowledgeObservation& Observation);
	void HandleEntityPerceptionChanged(
		FPerceptionKnowledgeEntityId EntityId,
		FGameplayTag SenseTag,
		bool bCurrentlyPerceived);
	void FreezeObservationRecording(double FinalDuration);
	void FinalizeObservationRecording(UIntentReplayTrack& ActionTrack);
	FIntentReplayObservationOperationResult RecordObservation(
		const FPerceptionKnowledgeObservation& Observation);
	FIntentReplayRecordedObservation MakeRecordedObservation(
		const FPerceptionKnowledgeObservation& Observation,
		const FIntentReplayTimelinePointResult& TimelinePoint,
		const FIntentReplayObservationCorrelation& Correlation) const;
	bool IsRedundantStateObservation(
		const FPerceptionKnowledgeStateObservation& State,
		bool bForRecording);
	void BuildComparisonIndexes(UIntentReplayObservationComparisonSession& Session);
	void CompareObservation(const FPerceptionKnowledgeObservation& Observation);
	FIntentReplayObservationJournalEntry MatchStateObservation(
		const FPerceptionKnowledgeStateObservation& State,
		const FIntentReplayObservationCorrelation& Correlation,
		const FIntentReplayTimelinePointResult& TimelinePoint);
	FIntentReplayObservationJournalEntry MatchEventObservation(
		const FPerceptionKnowledgeEventObservation& Event,
		const FIntentReplayObservationCorrelation& Correlation,
		const FIntentReplayTimelinePointResult& TimelinePoint);
	void AppendComparisonEntry(FIntentReplayObservationJournalEntry&& Entry);
	void SetComparisonState(EIntentReplayObservationComparisonState NewState);
	void CompleteComparison(EIntentReplayObservationComparisonState TerminalState);
	void ScheduleExpectedExpiration();
	void ProcessExpectedExpirations();
	void ExpireAllPendingExpected();
	FPerceptionKnowledgeEntityId ResolveObserverEntityId() const;
	FColor GetDebugColor(EIntentReplayObservationDebugStatus Status) const;
	void UpdateDebugResources();
	void DrawDebugTimer();
	void DrawTimelineHud(UCanvas* Canvas, APlayerController* PlayerController);

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayComponent> BoundIntentReplaySource;

	UPROPERTY(Transient)
	TObjectPtr<UPerceptionKnowledgeListenerComponent> BoundPerceptionListener;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationRecordPolicy> RecordPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationMatchPolicy> MatchPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationRecordingSession> ActiveRecordingSession;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationComparisonSession> ActiveComparisonSession;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationTrack> LastFinalizedObservationTrack;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayTimelineBundle> LastTimelineBundle;

	UPROPERTY(Transient)
	FIntentReplayPerceptionRuntimeStats RuntimeStats;

	UPROPERTY(Transient)
	FIntentReplayObservationDebugFrame CachedDebugFrame;

	FDelegateHandle TimelineLifecycleHandle;
	FDelegateHandle ObservationProducedHandle;
	FDelegateHandle EntityPerceptionChangedHandle;
	FDelegateHandle DebugCanvasHandle;
	FTimerHandle ExpectedExpirationTimerHandle;
	FTimerHandle DebugTimerHandle;
	TSet<FGuid> RecordedRuntimeEventIds;
	TMap<FObservedStateKey, FStateSignature> LastRecordedStates;
	TMap<FObservedStateKey, FStateSignature> LastComparedStates;
	TMap<FObservedStateKey, int64> PerceptionEpochs;
	FGuid ObserverId;
	bool bInitialized = false;
	bool bShuttingDown = false;

	FIntentReplayObservationRecordedNativeDelegate ObservationRecordedNative;
	FIntentReplayObservationTrackFinalizedNativeDelegate ObservationTrackFinalizedNative;
	FIntentReplayObservationComparedNativeDelegate ObservationComparedNative;
	FIntentReplayObservationComparedNativeDelegate ObservationMatchedNative;
	FIntentReplayObservationComparedNativeDelegate ObservationUnexpectedNative;
	FIntentReplayObservationComparedNativeDelegate ObservationAmbiguousNative;
	FIntentReplayObservationJournalCompletedNativeDelegate ObservationJournalCompletedNative;

	friend struct FIntentReplayPerceptionTestAccessor;
};
