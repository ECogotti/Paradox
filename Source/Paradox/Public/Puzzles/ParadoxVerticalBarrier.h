#pragma once

#include "Activators/PuzzleTransformMover.h"
#include "GameplayTagContainer.h"
#include "Types/WorldStateTypes.h"
#include "ParadoxVerticalBarrier.generated.h"

class ACharacter;
class AParadoxVerticalBarrier;
class UAudioComponent;
class UBoxComponent;
class UGameplayActionComponent;
class UGridNavigationModifierComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPerceptionKnowledgeSourceComponent;
class UParadoxSelectableComponent;
class UParadoxInteractionComponent;
class USmartObjectComponent;
class UPrimitiveComponent;
class USceneComponent;
class USoundBase;
class UStaticMeshComponent;
class UWorldStateParticipantComponent;

UENUM(BlueprintType)
enum class EParadoxBarrierLiftFailureReason : uint8
{
	InvalidActor,
	MissingRootComponent,
	UnsupportedMobility,
	MissingLocomotionLockOwner,
	LocomotionLockRejected,
	AttachmentFailed
};

UENUM(BlueprintType)
enum class EParadoxBarrierPassengerReleaseReason : uint8
{
	ReachedEnd,
	ReachedStart,
	Reset,
	WorldStateRestore,
	EndPlay,
	MovedComponentChanged,
	InvalidPassenger,
	TransportAborted
};

UENUM(BlueprintType)
enum class EParadoxBarrierOccupancyRefreshResult : uint8
{
	Succeeded,
	NotInitialized,
	MissingOccupancyVolume
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxBarrierCountEvent,
	AParadoxVerticalBarrier*, Barrier,
	int32, ActorCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxBarrierEvent,
	AParadoxVerticalBarrier*, Barrier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxBarrierActorEvent,
	AParadoxVerticalBarrier*, Barrier,
	AActor*, Actor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxBarrierLiftFailureEvent,
	AParadoxVerticalBarrier*, Barrier,
	AActor*, Actor,
	EParadoxBarrierLiftFailureReason, Reason);

/**
 * Concrete Receiver-driven vertical barrier. Start is raised/closed and End is lowered/open;
 * every moving or paused state blocks GridWorld navigation. Safe mode defers closing upward,
 * while lift mode transports current occupants.
 */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxVerticalBarrier : public APuzzleTransformMover
{
	GENERATED_BODY()

public:
	AParadoxVerticalBarrier();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ResetMover() override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UStaticMeshComponent> BarrierMesh = nullptr;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UAudioComponent> MovementAudio = nullptr;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UNiagaraComponent> MovementVFX = nullptr;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UBoxComponent> PassageOccupancyVolume = nullptr;

	/** Authoritative passage transform and box extent; the overlap volume mirrors this component. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UGridNavigationModifierComponent> GridNavigationModifier = nullptr;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipant = nullptr;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UPerceptionKnowledgeSourceComponent> PerceptionSource = nullptr;

	/** Project-level hover, selection, outline, and optional world-widget capability. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Barrier|Components")
	TObjectPtr<UParadoxSelectableComponent> SelectableComponent = nullptr;

	/** Smart Object slot authority; a null Definition is valid until a Blueprint assigns content. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Vertical Barrier|Components")
	TObjectPtr<USmartObjectComponent> SmartObjectComponent = nullptr;

	/** Paradox-owned multi-interaction catalog projected from the Smart Object slots. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Vertical Barrier|Components")
	TObjectPtr<UParadoxInteractionComponent> InteractionComponent = nullptr;

	/** Safe mode waits for an empty passage; disabled mode transports eligible occupants upward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Occupants")
	bool bWaitForClearPassage = true;

	/**
	 * Lets BarrierMesh contribute a walkable surface to dynamic navigation at Start and End.
	 * Navigation relevance is disabled for the complete movement, including an intermediate pause,
	 * so the moving mesh cannot trigger a topology rebuild every frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Navigation")
	bool bGenerateNavigationAtStableEndpoints = false;

	/** Empty accepts any otherwise valid Actor; non-empty requires every ordinary AActor FName tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Occupants")
	TArray<FName> RequiredOccupantActorTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Feedback")
	TObjectPtr<USoundBase> RaiseSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Feedback")
	TObjectPtr<USoundBase> LowerSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Feedback")
	TObjectPtr<UNiagaraSystem> RaiseNiagaraSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Feedback")
	TObjectPtr<UNiagaraSystem> LowerNiagaraSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Perception")
	bool bEmitNoiseOnRaiseStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Perception")
	bool bEmitNoiseOnLowerStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Perception")
	bool bEmitNoiseOnReachedEndpoint = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Perception", meta = (ClampMin = "0.0"))
	float MovementNoiseLoudness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Perception", meta = (ClampMin = "0.0", Units = "cm"))
	float MovementNoiseMaxRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Barrier|Perception", meta = (ClampMin = "0.0"))
	float MovementNoiseStrength = 1.0f;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierCountEvent OnPassageOccupancyChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierCountEvent OnRaiseDeferred;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierEvent OnPendingRaiseCancelled;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierEvent OnPassageClearanceRestored;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierEvent OnPassageBecameBlocked;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierEvent OnPassageBecameNavigable;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierActorEvent OnOccupantLiftStarted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierLiftFailureEvent OnOccupantLiftFailed;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierActorEvent OnOccupantLiftCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierCountEvent OnAllOccupantsPrepared;

	UPROPERTY(BlueprintAssignable, Category = "Paradox Barrier|Events")
	FParadoxBarrierCountEvent OnAllOccupantsReleased;

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	bool IsPassageOpen() const;

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	bool IsPassageBlockingNavigation() const;

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	bool IsRaiseRequestPending() const { return bRaiseRequestPending; }

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	bool IsPassageOccupied() const { return !OverlappingActors.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	int32 GetPassageOccupantCount() const { return OverlappingActors.Num(); }

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	TArray<AActor*> GetPassageOccupants() const;

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	bool IsActorOccupyingPassage(AActor* Actor) const;

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	int32 GetLiftedActorCount() const { return LiftedActors.Num(); }

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	bool IsTransportingOccupants() const { return !LiftedActors.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|State")
	bool IsActorBeingLifted(AActor* Actor) const;

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|Components")
	UGridNavigationModifierComponent* GetGridNavigationModifier() const { return GridNavigationModifier; }

	UFUNCTION(BlueprintPure, Category = "Paradox Barrier|Components")
	UBoxComponent* GetPassageOccupancyVolume() const { return PassageOccupancyVolume; }

	/** Rebuilds distinct-Actor occupancy from the overlap component without searching the world. */
	UFUNCTION(BlueprintCallable, Category = "Paradox Barrier|Occupants")
	EParadoxBarrierOccupancyRefreshResult RefreshPassageOccupants();

	UFUNCTION(BlueprintCallable, Category = "Paradox Barrier|Occupants")
	bool CancelPendingRaiseRequest();

protected:
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Occupants")
	bool CanActorOccupyPassage(AActor* OccupantActor, UPrimitiveComponent* OccupantComponent) const;
	virtual bool CanActorOccupyPassage_Implementation(AActor* OccupantActor, UPrimitiveComponent* OccupantComponent) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandlePassageOccupancyChanged(int32 OccupantCount);
	virtual void HandlePassageOccupancyChanged_Implementation(int32 OccupantCount);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandleRaiseDeferred(int32 OccupantCount);
	virtual void HandleRaiseDeferred_Implementation(int32 OccupantCount);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandlePendingRaiseCancelled();
	virtual void HandlePendingRaiseCancelled_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandlePassageClearanceRestored();
	virtual void HandlePassageClearanceRestored_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandlePassageNavigationChanged(bool bNowNavigable);
	virtual void HandlePassageNavigationChanged_Implementation(bool bNowNavigable);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandleOccupantLiftStarted(AActor* OccupantActor);
	virtual void HandleOccupantLiftStarted_Implementation(AActor* OccupantActor);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandleOccupantLiftFailed(AActor* OccupantActor, EParadoxBarrierLiftFailureReason Reason);
	virtual void HandleOccupantLiftFailed_Implementation(AActor* OccupantActor, EParadoxBarrierLiftFailureReason Reason);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandleOccupantLiftCompleted(AActor* OccupantActor);
	virtual void HandleOccupantLiftCompleted_Implementation(AActor* OccupantActor);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandleAllOccupantsPrepared(int32 PreparedCount);
	virtual void HandleAllOccupantsPrepared_Implementation(int32 PreparedCount);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Barrier|Events")
	void HandleAllOccupantsReleased(int32 ReleasedCount);
	virtual void HandleAllOccupantsReleased_Implementation(int32 ReleasedCount);

	virtual void OnMovementTargetRequestedNative(EPuzzleTransformMoverTarget RequestedTarget) override;
	virtual EPuzzleTransformMoverRequestDecision EvaluateMovementRequestNative(EPuzzleTransformMoverTarget RequestedTarget) override;
	virtual bool ShouldProcessReceiverStateNative(bool bReceiverActive) override;
	virtual void OnMovementStartedNative() override;
	virtual void OnMovementResumedNative() override;
	virtual void OnMovementReversedNative() override;
	virtual void OnMovementPausedNative() override;
	virtual void OnMovementUpdatedNative(float CurrentMovementAlpha, float CurrentEasedAlpha) override;
	virtual void OnReachedStartNative() override;
	virtual void OnReachedEndNative() override;
	virtual void OnMovedComponentChangingNative(USceneComponent* PreviousComponent, USceneComponent* NewComponent) override;
	virtual void OnMovedComponentChangedNative(USceneComponent* PreviousComponent, USceneComponent* NewComponent) override;
	virtual void OnMoverResetNative() override;

	void ReleaseAllLiftedActors(EParadoxBarrierPassengerReleaseReason Reason);

	UFUNCTION()
	void HandlePassageBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandlePassageEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

private:
	struct FLiftedActorRecord
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<USceneComponent> RootComponent;
		TWeakObjectPtr<USceneComponent> PreviousAttachParent;
		TWeakObjectPtr<UGameplayActionComponent> ActionComponent;
		FName PreviousSocket = NAME_None;
		bool bWasAttached = false;
		bool bWasSimulatingPhysics = false;
		bool bWasGravityEnabled = false;
		bool bPhysicsStateChanged = false;
		bool bOwnsMovementLock = false;
		bool bCharacter = false;
	};

	bool IsOccupantAccepted(AActor* Actor, UPrimitiveComponent* Component) const;
	bool HasRequiredOccupantTags(const AActor* Actor) const;
	void AddOverlappingComponent(AActor* Actor, UPrimitiveComponent* Component);
	void RemoveOverlappingComponent(AActor* Actor, UPrimitiveComponent* Component);
	void NotifyOccupancyChanged(int32 PreviousCount);
	void ClearOverlappingActors();
	void BindActorDestroyed(AActor* Actor);
	void UnbindActorDestroyedIfUnused(AActor* Actor);

	UFUNCTION()
	void HandleTrackedActorDestroyed(AActor* DestroyedActor);

	void SetRaiseRequestPending(bool bPending);
	bool IsPendingRaiseStillValid() const;
	void TryRetryPendingRaise();
	void SchedulePendingRaiseRetry();
	void ClearPendingRaiseRetry();

	int32 PrepareCurrentOccupantsForLift();
	bool PrepareActorForLift(AActor* Actor);
	bool PrepareCharacterForLift(ACharacter* Character, FLiftedActorRecord& OutRecord);
	bool PrepareAttachedActorForLift(AActor* Actor, FLiftedActorRecord& OutRecord);
	void ReportLiftFailure(AActor* Actor, EParadoxBarrierLiftFailureReason Reason);
	void ReleaseLiftedActor(const TWeakObjectPtr<AActor>& ActorKey, EParadoxBarrierPassengerReleaseReason Reason);
	void ApplyCharacterTransportDelta();

	void SynchronizePassageBounds();
	void EnforceComponentInvariants();
	void SetBarrierMeshNavigationRelevant(bool bRelevant);
	void RefreshBarrierMeshNavigationRelevance();
	void SetPassageNavigationBlocking(bool bBlocking);
	void RebuildDerivedState();
	void PublishPerceptionState();
	void StartMovementFeedback(bool bRaising, bool bEmitNoise);
	void StopMovementFeedback();
	void EmitMovementNoise(bool bRaising, bool bImpact);
	void DrawBarrierDebug() const;
	bool ShouldDrawBarrierDebug() const;

	UFUNCTION()
	void HandleWorldStatePreCapture(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStatePreRestore(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStatePropertiesRestored(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStateParticipantRestored(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStateParticipantFailed(const FWorldStateParticipantResult& Result);
	void HandleWorldStateRestoreCompleted(const FWorldStateRestoreResult& Result);
	void HandleWorldStateRestoreFailed(const FWorldStateRestoreResult& Result);
	void FinishWorldStateRestore();

	TMap<TWeakObjectPtr<AActor>, TSet<TWeakObjectPtr<UPrimitiveComponent>>> OverlappingActors;
	TMap<TWeakObjectPtr<AActor>, FLiftedActorRecord> LiftedActors;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox Barrier|Runtime", meta = (AllowPrivateAccess = "true"))
	bool bRaiseRequestPending = false;

	/** Reflected whole-struct authority selected by WorldState; transient maps are intentionally excluded. */
	UPROPERTY(VisibleInstanceOnly, Category = "Paradox Barrier|World State")
	FPuzzleTransformMoverRuntimeState WorldStateMoverRuntimeState;

	TObjectPtr<USoundBase> DefaultMovementSound = nullptr;
	TObjectPtr<UNiagaraSystem> DefaultMovementNiagaraSystem = nullptr;
	FVector PreviousBarrierLocation = FVector::ZeroVector;
	FTimerHandle PendingRaiseRetryHandle;
	FDelegateHandle WorldStateRestoreCompletedHandle;
	FDelegateHandle WorldStateRestoreFailedHandle;
	bool bBarrierInitialized = false;
	bool bOverlapDelegatesBound = false;
	bool bSuppressPresentation = true;
	bool bApplyingWorldState = false;
	bool bSafetyReturnInProgress = false;
	bool bPassageBlockingNavigation = true;
	FString LastBarrierDiagnostic;
};
