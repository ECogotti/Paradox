#pragma once

#include "Puzzles/ParadoxVerticalBarrier.h"
#include "ParadoxElevator.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
struct FWorldStateParticipantId;
struct FWorldStateRestoreResult;

/** A pressure-button platform that travels to the opposite endpoint once per occupancy cycle. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxElevator : public AParadoxVerticalBarrier
{
	GENERATED_BODY()

public:
	AParadoxElevator();

	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ResetMover() override;
	virtual bool IsPassageOpen() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** Visual button attached to the moving platform; its collision can be configured in Blueprint. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Elevator|Components")
	TObjectPtr<UStaticMeshComponent> ButtonMesh = nullptr;

	/** Button-only trigger; the inherited PassageOccupancyVolume detects passengers across the platform. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Paradox Elevator|Components")
	TObjectPtr<UBoxComponent> ButtonOccupancyVolume = nullptr;

	/** Every listed AActor tag is required for non-Character objects. Characters bypass this filter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Elevator|Button")
	TArray<FName> RequiredButtonActorTags;

	/** Local downward displacement of the button from its authored raised position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Elevator|Button", meta = (ClampMin = "0.0", Units = "cm"))
	float PressDepth = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Elevator|Button", meta = (ClampMin = "0.0", Units = "s"))
	float PressDuration = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox Elevator|Button", meta = (ClampMin = "0.0", Units = "s"))
	float ReleaseDuration = 0.15f;

	/** Logs button/Receiver transitions for this instance. Enabled by default in non-Shipping builds; no per-frame logs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox Elevator|Debug")
	bool bLogButtonDiagnostics = !UE_BUILD_SHIPPING;

	UFUNCTION(BlueprintPure, Category = "Paradox Elevator|Button")
	bool IsButtonOccupied() const { return !ButtonOccupants.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Paradox Elevator|Button")
	bool IsButtonPressed() const { return bButtonPressed; }

	UFUNCTION(BlueprintPure, Category = "Paradox Elevator|Button")
	bool IsButtonArmed() const { return bButtonArmed; }

	/** Controller prerequisites enable the button; the completed press manually activates the Receiver. */
	UFUNCTION(BlueprintPure, Category = "Paradox Elevator|Button")
	bool IsButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Paradox Elevator|Button")
	float GetButtonMovementAlpha() const { return ButtonMovementAlpha; }

	/** Rechecks this button's current overlaps, including after a pickupable is dropped onto it. */
	UFUNCTION(BlueprintCallable, Category = "Paradox Elevator|Button")
	bool RefreshButtonOccupancy();

protected:
	/** Additional project-specific acceptance policy after Actor validity and tag filtering. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox Elevator|Button")
	bool CanActorActivateButton(AActor* Candidate, UPrimitiveComponent* CandidateComponent) const;
	virtual bool CanActorActivateButton_Implementation(AActor* Candidate, UPrimitiveComponent* CandidateComponent) const;

	virtual bool ShouldBlockPassageAtEndpoint(EPuzzleTransformMoverTarget Endpoint) const override;
	virtual bool ShouldMoverTick() const override;
	virtual bool ShouldElevatorTick() const;
	virtual bool ShouldProcessReceiverStateNative(bool bReceiverActive) override;
	virtual EPuzzleTransformMoverRequestDecision EvaluateMovementRequestNative(EPuzzleTransformMoverTarget RequestedTarget) override;
	virtual void OnMovementUpdatedNative(float CurrentMovementAlpha, float CurrentEasedAlpha) override;
	virtual void OnReachedStartNative() override;
	virtual void OnReachedEndNative() override;

	UFUNCTION()
	void HandleButtonBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleButtonEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);
	void HandleReceiverPrerequisitesChanged(UPuzzleReceiverComponent* ChangedReceiver, bool bPrerequisitesSatisfied);

private:
	bool IsButtonCandidateAccepted(AActor* Actor, UPrimitiveComponent* Component) const;
	bool ReconcileButtonOccupancy(
		UPrimitiveComponent* ExcludedComponent,
		UPrimitiveComponent* IncludedComponent,
		bool bUpdateOverlaps);
	void BeginButtonPress();
	void CancelPendingButtonPress();
	void TryStartJourney();
	void QueueButtonActivationRetry();
	void StartButtonAnimation(bool bPressed);
	void AdvanceButtonAnimation(float DeltaSeconds);
	void StopButtonAnimation();
	void ApplyButtonAlpha(float Alpha);
	void FinishButtonAnimation();
	void RebuildButtonAfterAuthorityChange();
	void RefreshButtonDebug() const;
	void LogButtonState(const TCHAR* Stage, const AActor* OtherActor = nullptr,
		const UPrimitiveComponent* OtherComponent = nullptr) const;

	UFUNCTION()
	void HandleButtonOccupantDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void HandleElevatorWorldStatePreRestore(FWorldStateParticipantId ParticipantId);
	void HandleWorldStateRestoreFinished(const FWorldStateRestoreResult& Result);

	TSet<TWeakObjectPtr<AActor>> ButtonOccupants;
	FTransform RaisedButtonRelativeTransform = FTransform::Identity;
	FDelegateHandle WorldStateRestoreCompletedHandle;
	FDelegateHandle WorldStateRestoreFailedHandle;
	float ButtonMovementAlpha = 0.0f;
	float ButtonAnimationStartAlpha = 0.0f;
	float ButtonAnimationTargetAlpha = 0.0f;
	float ButtonAnimationElapsed = 0.0f;
	float ButtonAnimationDuration = 0.0f;
	bool bButtonPressed = false;
	bool bButtonArmed = false;
	bool bButtonPressPending = false;
	bool bButtonAnimating = false;
	bool bButtonInitialized = false;
	bool bButtonReconciliationInProgress = false;
	bool bSuppressButtonActivation = false;
	bool bWorldStateRestoring = false;
	bool bRequestFromButton = false;
	bool bButtonActivationRetryQueued = false;
	bool bLastReconciledHasValidOccupant = false;
};
