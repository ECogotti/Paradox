#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParadoxPairedTransferEndpoint.generated.h"

class AParadoxPairedTransferEndpoint;
class UArrowComponent;
class UGameplayActionInstance;
class UParadoxInteractionComponent;
class UPuzzleReceiverComponent;
class USceneComponent;
struct FWorldStateRestoreLifecycleContext;
struct FWorldStateRestoreResult;

/** Authoritative mutually exclusive role of one endpoint in a paired transaction. */
UENUM(BlueprintType)
enum class EParadoxTransferEndpointState : uint8
{
	Idle,
	Sending,
	Receiving
};

/** Authoritative phase shared by both endpoints while a transaction is active. */
UENUM(BlueprintType)
enum class EParadoxTransferPhase : uint8
{
	None,
	Preparing,
	TransferOut,
	Committing,
	TransferIn,
	Finalizing
};

/** Selects whether a transfer phase advances through its native timer or an explicit callback. */
UENUM(BlueprintType)
enum class EParadoxTransferPhaseCompletionMode : uint8
{
	Timed,
	Explicit
};

/** Observable result of transfer validation, startup, or cancellation. */
UENUM(BlueprintType)
enum class EParadoxTransferOperationStatus : uint8
{
	Succeeded,
	NotInitialized,
	ResetInProgress,
	InvalidRequester,
	RequesterBlocked,
	InvalidSubject,
	MissingLinkedEndpoint,
	SelfLinkedEndpoint,
	NonReciprocalPair,
	IncompatibleEndpoint,
	DifferentWorld,
	SourceInactive,
	DestinationInactive,
	SourceBusy,
	DestinationBusy,
	SubjectRejected,
	SourceRejected,
	DestinationRejected,
	PreparationFailed,
	PhaseStartFailed,
	NoActiveTransfer,
	OperationMismatch
};

/** Why an acquired transaction ended without normal completion. */
UENUM(BlueprintType)
enum class EParadoxTransferCancellationReason : uint8
{
	Requested,
	PreparationFailed,
	PhaseStartFailed,
	SubjectDestroyed,
	EndpointDestroyed,
	WorldStateRestore,
	CommitFailed,
	FinalizationFailed,
	InvalidPairState,
	EndPlay
};

/** Immutable event snapshot for one paired transfer operation. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxTransferOperationContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	TObjectPtr<AParadoxPairedTransferEndpoint> Source = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	TObjectPtr<AParadoxPairedTransferEndpoint> Destination = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	TObjectPtr<AActor> Subject = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	TObjectPtr<AActor> Requester = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	FGuid OperationId;
};

/** Structured result that never exposes mutable endpoint state. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxTransferOperationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	EParadoxTransferOperationStatus Status = EParadoxTransferOperationStatus::InvalidSubject;

	/** Valid only when RequestTransfer acquired a transaction or when CancelTransfer names one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	FGuid OperationId;

	/** Human-readable diagnostics only; Status remains authoritative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer")
	FString DiagnosticMessage;

	bool IsSuccess() const { return Status == EParadoxTransferOperationStatus::Succeeded; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxTransferOperationEvent,
	const FParadoxTransferOperationContext&, Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxTransferCancelledEvent,
	const FParadoxTransferOperationContext&, Context,
	EParadoxTransferCancellationReason, Reason,
	bool, bWasCommitted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxTransferEndpointStateChangedEvent,
	AParadoxPairedTransferEndpoint*, Endpoint,
	EParadoxTransferEndpointState, PreviousState,
	EParadoxTransferEndpointState, NewState);

/**
 * Abstract Receiver-gated endpoint that owns one complete paired asynchronous transfer transaction.
 *
 * The base owns validation, atomic pair locking, phase progression, stale-callback protection,
 * generic Actor placement, cancellation, reset safety, and presentation notifications. Concrete
 * endpoints add only their subject-domain rules and semantic interaction actions.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class PARADOX_API AParadoxPairedTransferEndpoint : public AActor
{
	GENERATED_BODY()

public:
	AParadoxPairedTransferEndpoint();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Components")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	/** World transform used by the generic commit and by concrete destination policies. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Components")
	TObjectPtr<UArrowComponent> TransferAnchor = nullptr;

	/** Controller-driven permission gate; this Actor never inspects Emitters directly. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Components")
	TObjectPtr<UPuzzleReceiverComponent> PuzzleReceiver = nullptr;

	/** Explicit reciprocal pair. Runtime world searches and automatic pair repair are unsupported. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Pairing")
	TObjectPtr<AParadoxPairedTransferEndpoint> LinkedEndpoint = nullptr;

	/** Native fallback delay before the transfer-out phase completes; zero completes next tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float TransferOutDuration = 0.0f;

	/** Native fallback delay before this endpoint's transfer-in phase completes; zero completes next tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float TransferInDuration = 0.0f;

	/** Timed preserves the native duration fallback; Explicit waits for Complete Transfer Out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Timing")
	EParadoxTransferPhaseCompletionMode TransferOutCompletionMode =
		EParadoxTransferPhaseCompletionMode::Timed;

	/** Timed preserves the native duration fallback; Explicit waits for Complete Transfer In. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Timing")
	EParadoxTransferPhaseCompletionMode TransferInCompletionMode =
		EParadoxTransferPhaseCompletionMode::Timed;

	/** Local half of the Paradox.PairedTransfer.Debug visual/logging gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Debug")
	bool bEnableDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Debug", meta = (ClampMin = "0.0", Units = "cm"))
	float DebugVerticalOffset = 100.0f;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Paired Transfer|Events")
	FParadoxTransferOperationEvent OnTransferRequested;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Paired Transfer|Events")
	FParadoxTransferOperationEvent OnTransferOutStarted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Paired Transfer|Events")
	FParadoxTransferOperationEvent OnTransferCommitted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Paired Transfer|Events")
	FParadoxTransferOperationEvent OnTransferInStarted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Paired Transfer|Events")
	FParadoxTransferOperationEvent OnTransferCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Paired Transfer|Events")
	FParadoxTransferCancelledEvent OnTransferCancelled;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Paired Transfer|Events")
	FParadoxTransferEndpointStateChangedEvent OnTransferStateChanged;

	/** Performs a side-effect-free validation. An optional action identifies the lock-owning request. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Paired Transfer")
	FParadoxTransferOperationResult EvaluateTransfer(
		AActor* TransferSubject,
		AActor* Requester,
		UGameplayActionInstance* RequestingAction = nullptr) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer")
	bool CanRequestTransfer(
		AActor* TransferSubject,
		AActor* Requester,
		UGameplayActionInstance* RequestingAction = nullptr) const;

	/** Revalidates and atomically acquires both endpoints before starting asynchronous work. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Paired Transfer")
	FParadoxTransferOperationResult RequestTransfer(
		AActor* TransferSubject,
		AActor* Requester,
		UGameplayActionInstance* RequestingAction = nullptr);

	/** Cancels only the currently matching operation; stale operation IDs have no effect. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Paired Transfer")
	FParadoxTransferOperationResult CancelTransfer(FGuid OperationId);

	/** Local reset that invalidates callbacks and releases the active pair without changing Receiver state. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Paired Transfer")
	bool ResetTransferEndpoint();

	/** Explicit asynchronous completion boundary; stale IDs and incorrect phases are ignored. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Paired Transfer|Lifecycle")
	bool CompleteTransferOut(FGuid OperationId);

	/** Explicit asynchronous completion boundary; may be called on either endpoint of the active pair. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Paired Transfer|Lifecycle")
	bool CompleteTransferIn(FGuid OperationId);

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer|State")
	bool IsTransferInProgress() const { return TransferState != EParadoxTransferEndpointState::Idle; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer|State")
	EParadoxTransferEndpointState GetTransferState() const { return TransferState; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer|State")
	EParadoxTransferPhase GetTransferPhase() const { return TransferPhase; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer|State")
	AParadoxPairedTransferEndpoint* GetLinkedEndpoint() const { return LinkedEndpoint.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer|State")
	AActor* GetCurrentTransferSubject() const { return CurrentTransferSubject.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer|State")
	FGuid GetCurrentTransferOperationId() const { return CurrentOperationId; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Paired Transfer|State")
	FTransform GetTransferAnchorTransform() const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Paired Transfer|Debug")
	void SetTransferDebugEnabled(bool bInEnableDebug);

protected:
	/** Optional semantic-request policy. The generic endpoint accepts a null or valid action. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Validation", meta = (BlueprintProtected = "true"))
	bool CanRequestTransferWithAction(
		AActor* TransferSubject,
		AActor* Requester,
		UGameplayActionInstance* RequestingAction,
		FString& OutDiagnostic) const;
	virtual bool CanRequestTransferWithAction_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		UGameplayActionInstance* RequestingAction,
		FString& OutDiagnostic) const;

	/** Generic default accepts an unattached Actor with a movable root component. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Validation", meta = (BlueprintProtected = "true"))
	bool CanTransferSubject(AActor* TransferSubject, AActor* Requester, FString& OutDiagnostic) const;
	virtual bool CanTransferSubject_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		FString& OutDiagnostic) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Validation", meta = (BlueprintProtected = "true"))
	bool CanSourceStartTransfer(
		AActor* TransferSubject,
		AActor* Requester,
		AParadoxPairedTransferEndpoint* Destination,
		FString& OutDiagnostic) const;
	virtual bool CanSourceStartTransfer_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		AParadoxPairedTransferEndpoint* Destination,
		FString& OutDiagnostic) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Validation", meta = (BlueprintProtected = "true"))
	bool CanDestinationReceiveTransfer(
		AActor* TransferSubject,
		AActor* Requester,
		AParadoxPairedTransferEndpoint* Source,
		FString& OutDiagnostic) const;
	virtual bool CanDestinationReceiveTransfer_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		AParadoxPairedTransferEndpoint* Source,
		FString& OutDiagnostic) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Lifecycle", meta = (BlueprintProtected = "true"))
	bool PrepareSubjectForTransfer(const FParadoxTransferOperationContext& Context, FString& OutDiagnostic);
	virtual bool PrepareSubjectForTransfer_Implementation(
		const FParadoxTransferOperationContext& Context,
		FString& OutDiagnostic);

	/** Generic default places the subject at Destination.TransferAnchor using TeleportPhysics. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Lifecycle", meta = (BlueprintProtected = "true"))
	bool PerformTransferCommit(const FParadoxTransferOperationContext& Context, FString& OutDiagnostic);
	virtual bool PerformTransferCommit_Implementation(
		const FParadoxTransferOperationContext& Context,
		FString& OutDiagnostic);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Lifecycle", meta = (BlueprintProtected = "true"))
	bool FinalizeTransferredSubject(const FParadoxTransferOperationContext& Context, FString& OutDiagnostic);
	virtual bool FinalizeTransferredSubject_Implementation(
		const FParadoxTransferOperationContext& Context,
		FString& OutDiagnostic);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Paired Transfer|Lifecycle", meta = (BlueprintProtected = "true"))
	void HandleTransferCancelled(
		const FParadoxTransferOperationContext& Context,
		EParadoxTransferCancellationReason Reason,
		bool bWasCommitted);
	virtual void HandleTransferCancelled_Implementation(
		const FParadoxTransferOperationContext& Context,
		EParadoxTransferCancellationReason Reason,
		bool bWasCommitted);

	/** C++ pair-compatibility hook used by runtime validation and editor Data Validation. */
	virtual bool IsLinkedEndpointCompatible(
		const AParadoxPairedTransferEndpoint* Candidate,
		FString& OutDiagnostic) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Paired Transfer|Presentation", meta = (BlueprintProtected = "true"))
	void ReceiveTransferRequested(const FParadoxTransferOperationContext& Context);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Paired Transfer|Presentation", meta = (BlueprintProtected = "true"))
	void ReceiveTransferOutStarted(const FParadoxTransferOperationContext& Context);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Paired Transfer|Presentation", meta = (BlueprintProtected = "true"))
	void ReceiveTransferCommitted(const FParadoxTransferOperationContext& Context);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Paired Transfer|Presentation", meta = (BlueprintProtected = "true"))
	void ReceiveTransferInStarted(const FParadoxTransferOperationContext& Context);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Paired Transfer|Presentation", meta = (BlueprintProtected = "true"))
	void ReceiveTransferCompleted(const FParadoxTransferOperationContext& Context);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Paired Transfer|Presentation", meta = (BlueprintProtected = "true"))
	void ReceiveTransferCancelled(
		const FParadoxTransferOperationContext& Context,
		EParadoxTransferCancellationReason Reason,
		bool bWasCommitted);

private:
	FParadoxTransferOperationResult EvaluateTransferInternal(
		AActor* TransferSubject,
		AActor* Requester,
		UGameplayActionInstance* RequestingAction,
		bool bRequireRunningAction) const;
	FParadoxTransferOperationResult MakeResult(
		EParadoxTransferOperationStatus Status,
		FString Diagnostic,
		FGuid OperationId = FGuid()) const;
	bool ValidateRequesterAuthority(
		AActor* Requester,
		UGameplayActionInstance* RequestingAction,
		bool bRequireRunningAction,
		FString& OutDiagnostic) const;
	bool AcquirePair(AActor* TransferSubject, AActor* Requester, const FGuid& OperationId);
	bool StartTransferOutPhase();
	bool StartTransferInPhase();
	bool SchedulePhaseCompletion(EParadoxTransferPhase Phase, float Duration, const FGuid& OperationId);
	void HandleTransferOutTimerElapsed(FGuid ExpectedOperationId);
	void HandleTransferInTimerElapsed(FGuid ExpectedOperationId);
	bool IsActiveSourceOperation(const FGuid& OperationId, EParadoxTransferPhase ExpectedPhase) const;
	bool IsPairTransactionConsistent() const;
	bool InternalCancelTransfer(EParadoxTransferCancellationReason Reason, bool bNotifyObservers);
	void ReleasePairState();
	void ApplyPairPhase(EParadoxTransferPhase NewPhase);
	FParadoxTransferOperationContext BuildCurrentContext() const;
	void BroadcastPairAcquired(const FParadoxTransferOperationContext& Context);
	void BroadcastPairCommitted(const FParadoxTransferOperationContext& Context);
	void BroadcastPairCompleted(const FParadoxTransferOperationContext& Context);
	void BroadcastPairCancelled(
		const FParadoxTransferOperationContext& Context,
		EParadoxTransferCancellationReason Reason,
		bool bWasCommitted);
	void BroadcastStateTransition(
		EParadoxTransferEndpointState PreviousState,
		EParadoxTransferEndpointState NewState);
	void NotifyInteractionAffordanceChanged();
	void BindLinkedEndpointObservers();
	void UnbindLinkedEndpointObservers();
	void BindWorldStateObservers();
	void UnbindWorldStateObservers();
	void RecordOperationResult(const FParadoxTransferOperationResult& Result);
	void LogDebugState(const TCHAR* EventName, const FString& Diagnostic = FString()) const;
	void DrawTransferDebug() const;
	bool ShouldDrawTransferDebug() const;

	UFUNCTION()
	void HandleCurrentSubjectDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleLinkedEndpointDestroyed(AActor* DestroyedActor);

	void HandleLinkedReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bIsActive);
	void HandleWorldStateRestoreStarted(const FWorldStateRestoreLifecycleContext& Context);
	void HandleWorldStateRestoreCompleted(const FWorldStateRestoreResult& Result);
	void HandleWorldStateRestoreFailed(const FWorldStateRestoreResult& Result);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Runtime", meta = (AllowPrivateAccess = "true"))
	EParadoxTransferEndpointState TransferState = EParadoxTransferEndpointState::Idle;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Runtime", meta = (AllowPrivateAccess = "true"))
	EParadoxTransferPhase TransferPhase = EParadoxTransferPhase::None;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AActor> CurrentTransferSubject = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CurrentTransferRequester = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Runtime", meta = (AllowPrivateAccess = "true"))
	FGuid CurrentOperationId;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Runtime", meta = (AllowPrivateAccess = "true"))
	EParadoxTransferOperationStatus LastOperationStatus = EParadoxTransferOperationStatus::NotInitialized;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Runtime", meta = (AllowPrivateAccess = "true"))
	EParadoxTransferCancellationReason LastCancellationReason = EParadoxTransferCancellationReason::Requested;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Paired Transfer|Runtime", meta = (AllowPrivateAccess = "true"))
	FString LastTransferDiagnostic;

	TWeakObjectPtr<AParadoxPairedTransferEndpoint> CurrentTransferPartner;
	TWeakObjectPtr<AParadoxPairedTransferEndpoint> ActiveTransferSource;
	TWeakObjectPtr<AParadoxPairedTransferEndpoint> BoundLinkedEndpoint;
	TWeakObjectPtr<UPuzzleReceiverComponent> BoundLinkedReceiver;
	TWeakObjectPtr<UParadoxInteractionComponent> CachedInteractionComponent;
	FTimerHandle TransferOutTimerHandle;
	FTimerHandle TransferInTimerHandle;
	FDelegateHandle OwnReceiverStateChangedHandle;
	FDelegateHandle LinkedReceiverStateChangedHandle;
	FDelegateHandle WorldStateRestoreStartedHandle;
	FDelegateHandle WorldStateRestoreCompletedHandle;
	FDelegateHandle WorldStateRestoreFailedHandle;
	bool bTransferCommitted = false;
	bool bRuntimeInitialized = false;
	bool bWorldStateRestoreInProgress = false;
};
