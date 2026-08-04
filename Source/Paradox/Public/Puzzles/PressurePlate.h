#pragma once

#include "CoreMinimal.h"
#include "Emitters/PuzzleSwitch.h"
#include "GameplayTagContainer.h"
#include "Types/WorldStateTypes.h"
#include "PressurePlate.generated.h"

class UAudioComponent;
class UBillboardComponent;
class UBoxComponent;
class UCurveFloat;
class UNiagaraComponent;
class UNiagaraSystem;
class UPerceptionKnowledgeSourceComponent;
class UPrimitiveComponent;
class USoundBase;
class UStaticMeshComponent;
class UWorldStateParticipantComponent;

/** Authoritative physical occupancy of one pressure plate. */
UENUM(BlueprintType)
enum class EPressurePlateOccupancyState : uint8
{
	/** No accepted Actor currently owns the plate. */
	Free,

	/** Exactly one accepted Actor currently owns the plate. */
	Occupied
};

/**
 * Concrete overlap-driven Paradox pressure plate built on the reusable Puzzle Switch state machine.
 *
 * The first accepted Actor becomes the single logical occupant. Physical Free/Occupied edges are
 * translated only into inherited Press/Release requests; signal policy remains owned by APuzzleSwitch.
 */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API APressurePlate : public APuzzleSwitch
{
	GENERATED_BODY()

public:
	APressurePlate(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Reasserts navigation and Tick invariants after every component registration pass. */
	virtual void PostRegisterAllComponents() override;

	/** Initializes overlap tracking, feedback, World State bindings, and the authored plate baseline. */
	virtual void BeginPlay() override;

	/** Removes every delegate and transient reference without manufacturing a final Release edge. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Advances plate interpolation only while moving and optionally draws local diagnostics. */
	virtual void Tick(float DeltaSeconds) override;

	/** Clears physical occupancy and restores inherited switch state without reset feedback. */
	virtual void ResetSwitch() override;

#if WITH_EDITOR
	/** Validates component hierarchy, overlap, movement, navigation, tags, and integration settings. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

	/** Prevents editing the derived start-output flag independently from InitialInputState. */
	virtual bool CanEditChange(const FProperty* InProperty) const override;

	/** Prevents editor changes from making the moving plate or detector navigation relevant. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Editor-facing root and orientation marker; hidden during gameplay. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UBillboardComponent> BillboardRoot = nullptr;

	/** Static frame or walkable support; never moved by pressure-plate logic. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UStaticMeshComponent> FloorMesh = nullptr;

	/** Movable visual plate whose relative transform is derived from switch output. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UStaticMeshComponent> PlateMesh = nullptr;

	/** Stable query-only detector attached to FloorMesh rather than the moving plate. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UBoxComponent> OccupancyVolume = nullptr;

	/** Reusable spatial audio source started once for each meaningful movement. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UAudioComponent> MovementAudio = nullptr;

	/** Reusable Niagara source started once for each meaningful movement. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UNiagaraComponent> MovementVFX = nullptr;

	/** Generic World State participant configured to capture the inherited active output value. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipant = nullptr;

	/** PerceptionKnowledge source used for movement noise identity and native Hearing publication. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Pressure Plate|Components")
	TObjectPtr<UPerceptionKnowledgeSourceComponent> PerceptionSource = nullptr;

	/** Empty accepts every otherwise valid Actor; non-empty requires all configured AActor FName tags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Occupancy")
	TArray<FName> RequiredOccupantActorTags;

	/** Non-cumulative local negative-Z travel from the authored raised PlateMesh transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float PressDepth = 20.0f;

	/** Full raised-to-pressed travel duration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Movement", meta = (ClampMin = "0.0", Units = "s"))
	float PressDuration = 0.15f;

	/** Full pressed-to-raised travel duration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Movement", meta = (ClampMin = "0.0", Units = "s"))
	float ReleaseDuration = 0.15f;

	/** Optional normalized easing curve; smooth-step interpolation is used when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Movement")
	TObjectPtr<UCurveFloat> MovementCurve = nullptr;

	/** Optional direction-specific sound; the component's authored sound is the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Feedback")
	TObjectPtr<USoundBase> PressSound = nullptr;

	/** Optional direction-specific sound; the component's authored sound is the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Feedback")
	TObjectPtr<USoundBase> ReleaseSound = nullptr;

	/** Optional direction-specific Niagara System; the component's authored system is the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Feedback")
	TObjectPtr<UNiagaraSystem> PressNiagaraSystem = nullptr;

	/** Optional direction-specific Niagara System; the component's authored system is the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Feedback")
	TObjectPtr<UNiagaraSystem> ReleaseNiagaraSystem = nullptr;

	/** Publishes one semantic Hearing event when a meaningful downward movement begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise")
	bool bEmitNoiseOnPressMovement = true;

	/** Publishes one semantic Hearing event when a meaningful upward movement begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise")
	bool bEmitNoiseOnReleaseMovement = true;

	/** Semantic event used for downward mechanism movement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise", meta = (EditCondition = "bEmitNoiseOnPressMovement"))
	FGameplayTag PressNoiseEventTag;

	/** Semantic event used for upward mechanism movement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise", meta = (EditCondition = "bEmitNoiseOnReleaseMovement"))
	FGameplayTag ReleaseNoiseEventTag;

	/** Semantic cause shared by both pressure-plate movement directions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise", meta = (EditCondition = "bEmitNoiseOnPressMovement || bEmitNoiseOnReleaseMovement"))
	FGameplayTag MovementNoiseCauseTag;

	/** Native Hearing loudness for movement events. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise", meta = (ClampMin = "0.0", EditCondition = "bEmitNoiseOnPressMovement || bEmitNoiseOnReleaseMovement"))
	float MovementNoiseLoudness = 1.0f;

	/** Zero lets native Hearing use listener configuration; positive values cap movement-event range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "bEmitNoiseOnPressMovement || bEmitNoiseOnReleaseMovement"))
	float MovementNoiseMaxRange = 0.0f;

	/** Semantic event strength retained in PerceptionKnowledge observations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pressure Plate|Noise", meta = (ClampMin = "0.0", EditCondition = "bEmitNoiseOnPressMovement || bEmitNoiseOnReleaseMovement"))
	float MovementNoiseStrength = 1.0f;

	/** Returns the single authoritative physical occupancy state. */
	UFUNCTION(BlueprintPure, Category = "Pressure Plate|State")
	EPressurePlateOccupancyState GetOccupancyState() const { return OccupancyState; }

	/** Returns true exactly while one accepted Actor owns the plate. */
	UFUNCTION(BlueprintPure, Category = "Pressure Plate|State")
	bool IsOccupied() const { return OccupancyState == EPressurePlateOccupancyState::Occupied; }

	/** Returns the weakly tracked current occupant, or nullptr. */
	UFUNCTION(BlueprintPure, Category = "Pressure Plate|State")
	AActor* GetCurrentOccupant() const { return CurrentOccupant.Get(); }

	/** Reconciles only the detector's current overlaps and applies at most one physical edge. */
	UFUNCTION(BlueprintCallable, Category = "Pressure Plate|Occupancy")
	bool RefreshOccupantFromVolume();

	/** Returns normalized visual travel: zero is raised and one is fully pressed. */
	UFUNCTION(BlueprintPure, Category = "Pressure Plate|State")
	float GetPlateMovementAlpha() const { return PlateMovementAlpha; }

	/** Returns true only while movement owns active Tick work. */
	UFUNCTION(BlueprintPure, Category = "Pressure Plate|State")
	bool IsPlateMoving() const { return bIsPlateMoving; }

protected:
	/** Additional subclass acceptance policy, evaluated only after native validity and required-tag checks. */
	UFUNCTION(BlueprintNativeEvent, Category = "Pressure Plate|Occupancy")
	bool CanOccupantActivatePlate(AActor* OccupantActor, UPrimitiveComponent* OccupantComponent) const;
	virtual bool CanOccupantActivatePlate_Implementation(AActor* OccupantActor, UPrimitiveComponent* OccupantComponent) const;

	/** Presentation hook called after one Actor becomes authoritative and before inherited Press(). */
	UFUNCTION(BlueprintNativeEvent, Category = "Pressure Plate|Events")
	void HandleOccupantAccepted(AActor* OccupantActor);
	virtual void HandleOccupantAccepted_Implementation(AActor* OccupantActor);

	/** Presentation hook called after one Actor is cleared and before inherited Release(). */
	UFUNCTION(BlueprintNativeEvent, Category = "Pressure Plate|Events")
	void HandleOccupantReleased(AActor* OccupantActor);
	virtual void HandleOccupantReleased_Implementation(AActor* OccupantActor);

	/** Presentation hook for Occupied-to-Occupied transfer without Release/Press churn. */
	UFUNCTION(BlueprintNativeEvent, Category = "Pressure Plate|Events")
	void HandleOccupantReplaced(AActor* PreviousOccupant, AActor* NewOccupant);
	virtual void HandleOccupantReplaced_Implementation(AActor* PreviousOccupant, AActor* NewOccupant);

	/** Presentation hook called after the authoritative physical state changes. */
	UFUNCTION(BlueprintNativeEvent, Category = "Pressure Plate|Events")
	void HandleOccupancyStateChanged(EPressurePlateOccupancyState PreviousState, EPressurePlateOccupancyState NewState);
	virtual void HandleOccupancyStateChanged_Implementation(EPressurePlateOccupancyState PreviousState, EPressurePlateOccupancyState NewState);

	/** Presentation hook called once when meaningful movement starts or reverses. */
	UFUNCTION(BlueprintNativeEvent, Category = "Pressure Plate|Events")
	void HandlePlateMovementStarted(bool bMovingDown);
	virtual void HandlePlateMovementStarted_Implementation(bool bMovingDown);

	/** Presentation hook called after the plate reaches an exact endpoint. */
	UFUNCTION(BlueprintNativeEvent, Category = "Pressure Plate|Events")
	void HandlePlateMovementCompleted(bool bIsPressed);
	virtual void HandlePlateMovementCompleted_Implementation(bool bIsPressed);

	/** Drives the plate down after inherited activation state and signal publication are complete. */
	virtual void HandleSwitchActivated_Implementation() override;

	/** Drives the plate up after inherited deactivation state and signal publication are complete. */
	virtual void HandleSwitchDeactivated_Implementation() override;

	/** Preserves the inherited reset extension chain; physical reset is owned by ResetSwitch(). */
	virtual void HandleSwitchReset_Implementation() override;

	/** Keeps Tick enabled only for inherited debug or active plate interpolation. */
	virtual bool ShouldEnableSwitchTick() const override;

	/** Native begin-overlap boundary exposed to the automation fixture without making state mutable. */
	UFUNCTION()
	void HandleOccupancyBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	/** Native end-overlap boundary exposed to the automation fixture without making state mutable. */
	UFUNCTION()
	void HandleOccupancyEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

private:
	/** Rejects invalid candidates, enforces all RequiredOccupantActorTags, then calls the subclass hook. */
	bool IsOccupantCandidateAccepted(AActor* OccupantActor, UPrimitiveComponent* OccupantComponent) const;

	/** Evaluates ordinary AActor::Tags with ALL semantics. */
	bool HasRequiredOccupantActorTags(const AActor* OccupantActor) const;

	/** Acquires the first physical occupant and emits one inherited Press request. */
	void AcquireOccupant(AActor* OccupantActor, const TArray<TWeakObjectPtr<UPrimitiveComponent>>& OccupantComponents);

	/** Transfers Occupied ownership without a Free edge or inherited input churn. */
	void ReplaceOccupant(AActor* NewOccupant, const TArray<TWeakObjectPtr<UPrimitiveComponent>>& OccupantComponents);

	/** Clears the physical occupant and optionally emits one inherited Release request. */
	void ReleaseOccupant(bool bRequestInheritedRelease, bool bNotifyHooks, AActor* PreviousOccupantOverride = nullptr);

	/** Applies one physical-state transition and its BlueprintNativeEvent. */
	void SetOccupancyState(EPressurePlateOccupancyState NewState, bool bNotifyHook = true);

	/** Finds at most one valid current overlap, preferring the still-valid current occupant. */
	bool FindOccupancyCandidate(
		UPrimitiveComponent* ExcludedComponent,
		AActor* ExcludedActor,
		AActor*& OutActor,
		TArray<TWeakObjectPtr<UPrimitiveComponent>>& OutComponents) const;

	/** Reconciles the current detector set without any world search. */
	bool ReconcileOccupancy(UPrimitiveComponent* ExcludedComponent = nullptr, AActor* ExcludedActor = nullptr);

	/** Applies the physical pressure-plate invariant between occupancy and inherited raw input. */
	bool SynchronizeSwitchInputWithOccupancy();

	/** Binds destruction cleanup only to the current logical occupant. */
	void BindCurrentOccupantDestroyed();

	/** Removes the current occupant destruction binding symmetrically. */
	void UnbindCurrentOccupantDestroyed();

	/** Handles current-occupant destruction when no matching end-overlap is available. */
	UFUNCTION()
	void HandleCurrentOccupantDestroyed(AActor* DestroyedActor);

	/** Starts or reverses movement from the current alpha without transform discontinuity. */
	void StartPlateMovement(bool bMoveToPressed);

	/** Advances normalized interpolation and completes exactly at the target. */
	void UpdatePlateMovement(float DeltaSeconds);

	/** Applies the exact endpoint, ends movement Tick, and emits the completion hook. */
	void CompletePlateMovement();

	/** Writes the one transform derived from the cached authored raised baseline. */
	void ApplyPlateAlpha(float Alpha);

	/** Stops interpolation and snaps presentation to inherited authoritative output. */
	void SnapPlateToAuthoritativeState();

	/** Stops movement without changing current visual alpha. */
	void StopPlateMovement();

	/** Starts direction-specific optional audio, Niagara, and semantic movement noise. */
	void StartMovementFeedback(bool bMovingDown);

	/** Stops reusable feedback components during resets and teardown. */
	void StopMovementFeedback();

	/** Emits one PerceptionKnowledge semantic Hearing event through this Actor's source. */
	void EmitMovementNoise(bool bMovingDown);

	/** Reasserts the no-navigation contract for every moving or query-only component. */
	void EnforceNavigationSafety();

	/** Keeps the inherited initial output coherent with the pressure plate's initial physical input. */
	void SynchronizeInitialPressurePlateState();

	/** Publishes a directly restored inherited active value back through the owned emitter cache. */
	void SynchronizeEmitterAfterWorldStateRestore();

	/** Enters feedback-suppressed reset preparation before World State mutates properties. */
	UFUNCTION()
	void HandleWorldStatePreRestore(FWorldStateParticipantId ParticipantId);

	/** Rebuilds derived transform/signal state and one local overlap after a successful restore. */
	UFUNCTION()
	void HandleWorldStateRestored(FWorldStateParticipantId ParticipantId);

	/** Leaves the plate in a safe reconciled state if its participant restore fails. */
	UFUNCTION()
	void HandleWorldStateRestoreFailed(const FWorldStateParticipantResult& Result);

	/** Completes the shared success/failure cleanup without feedback or world searches. */
	void FinishWorldStateRestore();

	/** Draws event-driven occupancy and movement diagnostics while both debug gates allow it. */
	void DrawPressurePlateDebug() const;

	/** Returns the local AND module-global pressure-plate debug gate. */
	bool ShouldDrawPressurePlateDebug() const;

	/** Single authoritative physical state. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Pressure Plate|Runtime", meta = (AllowPrivateAccess = "true"))
	EPressurePlateOccupancyState OccupancyState = EPressurePlateOccupancyState::Free;

	/** GC-aware weak reference to the only logical occupant. */
	TWeakObjectPtr<AActor> CurrentOccupant;

	/** Accepted overlapping components belonging only to CurrentOccupant. */
	TSet<TWeakObjectPtr<UPrimitiveComponent>> CurrentOccupantComponents;

	/** Authored PlateMesh relative transform cached exactly once at runtime initialization. */
	FTransform RaisedPlateRelativeTransform = FTransform::Identity;

	/** Current normalized raised-to-pressed presentation value. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Pressure Plate|Runtime", meta = (AllowPrivateAccess = "true"))
	float PlateMovementAlpha = 0.0f;

	/** Current normalized movement target. */
	float TargetPlateMovementAlpha = 0.0f;

	/** Alpha captured at the latest movement start or reversal. */
	float MovementStartAlpha = 0.0f;

	/** Elapsed seconds since the latest movement start or reversal. */
	float MovementElapsedSeconds = 0.0f;

	/** Distance-scaled movement duration for the active segment. */
	float ActiveMovementDuration = 0.0f;

	/** True only while Tick must update plate movement. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Pressure Plate|Runtime", meta = (AllowPrivateAccess = "true"))
	bool bIsPlateMoving = false;

	/** Prevents construction, initialization, and restore paths from producing physical feedback. */
	bool bSuppressMovementFeedback = true;

	/** Prevents overlap and switch hooks from treating World State restoration as gameplay. */
	bool bIsApplyingWorldState = false;

	/** Guards runtime-only overlap and movement work. */
	bool bPressurePlateInitialized = false;

	/** Tracks whether dynamic overlap bindings require cleanup. */
	bool bOverlapDelegatesBound = false;

	/** Prevents recaching a transform after the plate has animated. */
	bool bRaisedTransformCached = false;

	/** Strong transient fallback retained when direction-specific sounds replace the component sound. */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> DefaultMovementSound = nullptr;

	/** Strong transient fallback retained when direction-specific systems replace the component asset. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> DefaultMovementNiagaraSystem = nullptr;
};
