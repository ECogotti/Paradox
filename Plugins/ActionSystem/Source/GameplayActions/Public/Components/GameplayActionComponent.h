#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/GameplayActionTypes.h"
#include "GameplayActionComponent.generated.h"

class IGameplayActionJournalSink;
class UGameplayActionInstance;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameplayActionEventDelegate, const FGameplayActionEvent&, Event);
DECLARE_MULTICAST_DELEGATE_OneParam(FGameplayActionNativeEndedDelegate, const FGameplayActionEvent&);

/**
 * Game-thread authority for action ownership, scheduling, transitions, and ordered event delivery.
 *
 * Accepted instances are reflected children of this component. Callers interact through handles and
 * immutable event/result snapshots; they must never retain an instance past Ended. All lock changes,
 * terminal transitions, journal writes, and queue decisions are serialized through this component.
 */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class GAMEPLAYACTIONS_API UGameplayActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGameplayActionComponent();
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Validates and submits an isolated request on the Game Thread.
	 * @return A structured accepted/queued/rejected decision and, when accepted, a new monotonic handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions")
	FGameplayActionSubmissionResult SubmitAction(const FGameplayActionRequest& Request);

	/** Performs validation and reports the current scheduler decision without allocating a handle or changing state. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions")
	FGameplayActionSubmissionResult PreflightAction(const FGameplayActionRequest& Request);

	/** Cancels an accepted queued, starting, running, or paused action through the common terminal path. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions")
	EGameplayActionOperationResult CancelAction(FGameplayActionHandle Handle, FGameplayTag ReasonTag);

	/** Force-aborts every accepted non-terminal action in deterministic scheduler order. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions")
	int32 AbortAllActions(FGameplayTag ReasonTag);

	/** Pauses running actions while retaining their locks and freezes queue timeout accounting. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions")
	EGameplayActionOperationResult PauseActions();

	/** Resumes paused actions and reevaluates queued candidates after all resume hooks have run. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions")
	EGameplayActionOperationResult ResumeActions();

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	bool IsActionsPaused() const { return bActionsPaused; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	bool IsAcceptingSubmissions() const { return bAcceptingSubmissions && !bShuttingDown; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	bool GetActionState(FGameplayActionHandle Handle, EGameplayActionState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	bool GetActionResult(FGameplayActionHandle Handle, FGameplayActionResult& OutResult) const;

	/**
	 * Returns the transient instance only while it is still owned by this component.
	 * Never retain the pointer after Ended; use the handle for durable queries.
	 */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	UGameplayActionInstance* GetActionInstance(FGameplayActionHandle Handle) const;

	/** Snapshot of actions that currently own their complete lock sets. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	TArray<FGameplayActionHandle> GetActiveActionHandles() const { return ActiveHandles; }

	/** Snapshot of accepted initialized actions that own no execution locks yet. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	TArray<FGameplayActionHandle> GetQueuedActionHandles() const { return QueuedHandles; }

	/**
	 * Registers one object implementing GameplayActionJournalSink.
	 *
	 * The component retains the UObject through a reflected interface property. Registration is rejected
	 * during validation, Action Init, and the initial Accepted journal transaction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Journal")
	bool RegisterJournalSink(UObject* Sink);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Journal")
	void UnregisterJournalSink(UObject* Sink = nullptr);

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions|Debug")
	FGameplayActionDebugSnapshot GetDebugSnapshot() const;

	UPROPERTY(BlueprintAssignable, Category = "Gameplay Actions|Events")
	FGameplayActionEventDelegate OnActionEvent;

	UPROPERTY(BlueprintAssignable, Category = "Gameplay Actions|Events")
	FGameplayActionEventDelegate OnActionAccepted;

	UPROPERTY(BlueprintAssignable, Category = "Gameplay Actions|Events")
	FGameplayActionEventDelegate OnActionRejected;

	UPROPERTY(BlueprintAssignable, Category = "Gameplay Actions|Events")
	FGameplayActionEventDelegate OnActionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Gameplay Actions|Events")
	FGameplayActionEventDelegate OnActionPaused;

	UPROPERTY(BlueprintAssignable, Category = "Gameplay Actions|Events")
	FGameplayActionEventDelegate OnActionResumed;

	UPROPERTY(BlueprintAssignable, Category = "Gameplay Actions|Events")
	FGameplayActionEventDelegate OnActionEnded;

	/**
	 * Native-only Ended observer used by systems that must retain an exact FDelegateHandle.
	 *
	 * Dispatch occurs after OnActionEvent and OnActionEnded Blueprint delegates, while the transient
	 * instance is still owned by this component. The instance is released immediately after this
	 * delegate returns, so observers must retain Event.Handle and Event.Result rather than the UObject.
	 * Reentrant component calls are supported and their events are appended to the FIFO dispatch queue.
	 */
	FGameplayActionNativeEndedDelegate& OnActionEndedNative() { return NativeActionEnded; }

	/** Detailed diagnostics require this flag and the global GameplayActions.Debug CVar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions|Debug")
	bool bEnableDebug = false;

	void FinishActionFromInstance(
		UGameplayActionInstance* Instance,
		EGameplayActionState TerminalState,
		FGameplayTag ReasonTag,
		const FString& DiagnosticMessage);

public:
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void BeginDestroy() override;

private:
	struct FValidatedRequest
	{
		TObjectPtr<class UGameplayActionDefinition> Definition;
		int32 Priority = 0;
		EGameplayActionBlockedPolicy BlockedPolicy = EGameplayActionBlockedPolicy::Queue;
	};

	bool ValidateRequest(const FGameplayActionRequest& Request, FValidatedRequest& OutValidated, FString& OutDiagnostic) const;
	bool ValidateInstanceCanStart(UGameplayActionInstance& Instance, FGameplayTag& OutReason, FString& OutDiagnostic);
	FGameplayActionSubmissionResult EvaluateSchedule(const UGameplayActionInstance& Incoming, TArray<FGameplayActionHandle>& OutConflicts) const;
	TArray<FGameplayActionHandle> FindConflicts(const UGameplayActionInstance& Incoming) const;
	bool CanPreemptAll(const UGameplayActionInstance& Incoming, const TArray<FGameplayActionHandle>& Conflicts) const;
	void SortHandlesBySchedulerOrder(TArray<FGameplayActionHandle>& Handles) const;

	void InitializeAcceptedAction(UGameplayActionInstance& Instance);
	void StartAction(UGameplayActionInstance& Instance);
	bool FinishActionInternal(
		UGameplayActionInstance& Instance,
		EGameplayActionState TerminalState,
		FGameplayTag ReasonTag,
		const FString& DiagnosticMessage,
		bool bEvaluateQueueAfterRelease,
		FGameplayActionHandle CausingActionHandle = FGameplayActionHandle());
	void UpdateQueuedTimeouts(float DeltaTime);
	void EvaluateQueuedActions();
	void RefreshComponentTickEnabled();
	void NotifyActionTickStateChanged(UGameplayActionInstance* Instance);

	FGameplayActionEvent BuildEvent(const UGameplayActionInstance& Instance, EGameplayActionEventType EventType) const;
	FGameplayActionEvent BuildRequestEvent(
		const FGameplayActionRequest& Request,
		const FValidatedRequest* Validated,
		EGameplayActionEventType EventType,
		const FGameplayActionSubmissionResult& SubmissionResult) const;
	void QueueEvent(FGameplayActionEvent&& Event, bool bSkipJournal = false);
	void FlushEventQueue();
	FGameplayActionJournalResult WriteJournalEvent(const FGameplayActionEvent& Event);
	void BroadcastEvent(const FGameplayActionEvent& Event);

	FGameplayActionSubmissionResult MakeRejectedResult(
		EGameplayActionSubmissionStatus Status,
		FGameplayTag ReasonTag,
		FString DiagnosticMessage) const;
	void RecordSchedulerDecision(const FString& Decision);
	bool IsDetailedDebugEnabled() const;
	void ShutdownActions(FGameplayTag ReasonTag);
	void ReleaseInstanceAfterEndedEvent(FGameplayActionHandle Handle);

	UPROPERTY(Transient)
	TMap<FGameplayActionHandle, TObjectPtr<UGameplayActionInstance>> ActionsByHandle;

	UPROPERTY(Transient)
	TArray<FGameplayActionHandle> ActiveHandles;

	UPROPERTY(Transient)
	TArray<FGameplayActionHandle> QueuedHandles;

	UPROPERTY(Transient)
	TMap<FGameplayActionHandle, EGameplayActionState> TerminalStates;

	UPROPERTY(Transient)
	TMap<FGameplayActionHandle, FGameplayActionResult> TerminalResults;

	UPROPERTY(Transient)
	TArray<FGameplayActionEvent> PendingEvents;

	UPROPERTY(Transient)
	TArray<uint8> PendingEventSkipJournal;

	UPROPERTY(Transient)
	TScriptInterface<IGameplayActionJournalSink> JournalSink;

	FGameplayActionNativeEndedDelegate NativeActionEnded;

	int64 NextHandleValue = 1;
	int64 NextSubmissionSequence = 1;
	bool bActionsPaused = false;
	bool bAcceptingSubmissions = true;
	bool bShuttingDown = false;
	bool bInInitialJournalTransaction = false;
	bool bInValidationCallback = false;
	bool bInInitCallback = false;
	bool bDispatchingEvents = false;
	bool bEvaluatingQueue = false;
	bool bHasLastResult = false;
	FGameplayActionResult LastResult;
	FString LastSchedulerDecision;

	friend class UGameplayActionInstance;
};
