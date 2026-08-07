// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/ParadoxCameraTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "TimeLoop/ParadoxTimeLoopTypes.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxPlayerController.generated.h"

class UGameplayActionDefinition;
class UNiagaraSystem;
class UInputMappingContext;
class UInputAction;
class UGridWorldPathFollowingComponent;
class UGridCellPointerComponent;
class UGridPathPreviewComponent;
class UTacticalPauseWorldSubsystem;
class UParadoxTimeTravelAction;
class UParadoxTimeLoopComponent;
class UParadoxOutcomePresentationComponent;
class UPerceptionKnowledgeHearingRangeRendererComponent;
class UPerceptionKnowledgeListenerComponent;
class UPerceptionKnowledgeProfile;
class AParadoxCameraBoundsVolume;
class AParadoxCameraRig;
struct FInputActionValue;
struct FTacticalPauseStateChange;

/**
 *  Player controller for a top-down perspective game.
 *  Converts point-and-click input into semantic Move To Grid Cell actions.
 */
UCLASS(abstract)
class AParadoxPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:

	/** GridWorld path follower that enforces the selected cell-center movement style. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UGridWorldPathFollowingComponent> PathFollowingComponent;

	/** Converts cursor/touch hits into stable GridWorld cell identities and hover state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UGridCellPointerComponent> GridCellPointerComponent;

	/** Owns the deduplicated, revision-aware path prediction shown before a click. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UGridPathPreviewComponent> GridPathPreviewComponent;

	/** Continuously predicts under the local mouse cursor. Touch prediction updates while pressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Prediction")
	bool bEnablePointerPathPrediction = true;

	/** Time Threshold to know if it was a short press */
	UPROPERTY(EditAnywhere, Category="Input")
	float ShortPressThreshold;

	/** FX Class that we will spawn when clicking */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UNiagaraSystem> FXCursor;

	/** MappingContext */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	
	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SetDestinationClickAction;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SetDestinationTouchAction;

	/** Configurable command that consolidates the current run and starts a rewind. */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> RewindAction;

	/** Toggle command that uses the possessed Character's authoritative crouch state. */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** Frame-rate independent free-camera pan input. Expected value type: Axis2D. */
	UPROPERTY(EditAnywhere, Category = "Input|Camera")
	TObjectPtr<UInputAction> CameraMoveAction;

	/** Incremental orthographic zoom input. Expected value type: Axis1D. */
	UPROPERTY(EditAnywhere, Category = "Input|Camera")
	TObjectPtr<UInputAction> CameraZoomAction;

	/** One-shot recenter command. */
	UPROPERTY(EditAnywhere, Category = "Input|Camera")
	TObjectPtr<UInputAction> CameraRecenterAction;

	/** One-shot command that rotates the free camera 90 degrees to the left. */
	UPROPERTY(EditAnywhere, Category = "Input|Camera")
	TObjectPtr<UInputAction> CameraRotateLeftAction;

	/** One-shot command that rotates the free camera 90 degrees to the right. */
	UPROPERTY(EditAnywhere, Category = "Input|Camera")
	TObjectPtr<UInputAction> CameraRotateRightAction;

	/** Replaceable presentation class; the native rig is a complete default. */
	UPROPERTY(EditDefaultsOnly, Category = "Paradox|Camera")
	TSubclassOf<AParadoxCameraRig> CameraRigClass;

	/** Gameplay Action Definition submitted for every player movement request. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gameplay Actions")
	TObjectPtr<UGameplayActionDefinition> MoveToGridCellActionDefinition;

	/** Instant stance command. It owns Stance rather than Movement and therefore runs in parallel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gameplay Actions")
	TObjectPtr<UGameplayActionDefinition> SetCrouchedActionDefinition;

	/** Terminal semantic command recorded before its Niagara-driven rewind is executed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gameplay Actions")
	TObjectPtr<UGameplayActionDefinition> TimeTravelActionDefinition;

	/** Project movement rejects an exact destination owned or claimed by another character. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gameplay Actions")
	EGridGoalContentionPolicy MoveGoalContentionPolicy = EGridGoalContentionPolicy::StopBeforeOccupied;

	/** True if the controlled character should navigate to the mouse cursor. */
	uint32 bMoveToMouseCursor : 1;

	/** Set to true if we're using touch input */
	uint32 bIsTouch : 1;

	/** True when the current pointer gesture produced a valid world destination. */
	uint32 bHasCachedDestination : 1;

	/** Saved location of the character movement destination */
	UPROPERTY(Transient)
	FVector CachedDestination;

	/** Time that the click input has been pressed */
	float FollowTime = 0.0f;

	/** Strictly increasing priority makes a newer move preempt an older recorded move. */
	int32 MoveRequestPriority = 0;

	/** Reflected storage used to copy exact payload fields into an isolated Gameplay Action request. */
	UPROPERTY(Transient)
	EGridMovePathSource PendingMovePathSource = EGridMovePathSource::Destination;

	UPROPERTY(Transient)
	FGridInjectedPath PendingInjectedPath;

	/** Reflected source used for exact Property Bag assignment to the crouch request. */
	UPROPERTY(Transient)
	bool bPendingDesiredCrouched = false;

	/** Prevents the cleared prediction from immediately reappearing under an unchanged cursor after commit. */
	FGridCellId SuppressedPreviewGoalCell;

	/** Explicitly selected destination for the replaceable move planned during Tactical Pause. */
	UPROPERTY(Transient)
	FGridCellId PlannedMoveGoalCell;

	/** Per-world temporal authority observed for planning presentation and queued submission. */
	UPROPERTY(Transient)
	TObjectPtr<UTacticalPauseWorldSubsystem> TacticalPauseSubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AParadoxCameraRig> FreeCameraRig = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AParadoxCameraBoundsVolume> CameraBoundsVolume = nullptr;

	UPROPERTY(Transient)
	FParadoxCameraConfiguration ActiveCameraConfiguration;

	UPROPERTY(Transient)
	FParadoxCameraOperationResult CameraInitializationResult;

	FVector CameraFocusLocation = FVector::ZeroVector;
	FVector CameraRecenterStart = FVector::ZeroVector;
	FVector CameraRecenterTarget = FVector::ZeroVector;
	FVector CameraRecenterRequestedTarget = FVector::ZeroVector;
	FVector2D CameraMoveInput = FVector2D::ZeroVector;
	float CurrentOrthoWidth = 0.0f;
	float CameraRecenterElapsed = 0.0f;
	float CameraRotationElapsed = 0.0f;
	int32 CurrentCameraQuarterTurnIndex = 0;
	int32 CameraRotationStartQuarterTurnIndex = 0;
	int32 CameraRotationDirection = 0;
	bool bCameraRecenterActive = false;
	bool bCameraRotationActive = false;
	bool bWarnedRuntimeAspectConstraint = false;
	bool bRecordedTimeTravelPending = false;
	bool bRecordedTimeTravelExecutionScheduled = false;

	/** Native complete, Blueprint-replaceable presentation for paradox and terminal outcomes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Presentation")
	TObjectPtr<UParadoxOutcomePresentationComponent> OutcomePresentationComponent;

	/** Controller-owned semantic listener; its Body Actor follows the possessed player Pawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPerceptionKnowledgeListenerComponent> PerceptionKnowledgeListener;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPerceptionKnowledgeHearingRangeRendererComponent> HearingRangeRenderer;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Perception")
	TObjectPtr<UPerceptionKnowledgeProfile> PerceptionProfile;

public:

	/** Constructor */
	AParadoxPlayerController();

	/**
	 * Uses the possessed Pawn's physical facing for gameplay perception while the independent
	 * top-down camera and Control Rotation remain free to point elsewhere.
	 */
	virtual void GetActorEyesViewPoint(
		FVector& OutLocation,
		FRotator& OutRotation) const override;

	UFUNCTION(BlueprintPure, Category = "Paradox|Perception")
	UPerceptionKnowledgeListenerComponent* GetPerceptionKnowledgeListener() const
	{
		return PerceptionKnowledgeListener.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Perception")
	UPerceptionKnowledgeHearingRangeRendererComponent*
	GetHearingRangeRenderer() const
	{
		return HearingRangeRenderer.Get();
	}

	/** Submits one semantic movement request through the possessed Paradox Character. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Movement")
	FGameplayActionSubmissionResult RequestMoveToGridCell(FVector Destination);

	/** Submits an exact path exported by UGridPathPreviewComponent. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Movement")
	FGameplayActionSubmissionResult RequestMoveAlongGridPath(const FGridInjectedPath& InjectedPath);

	/** Requests an absolute stance without interrupting an active movement action. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Movement")
	FGameplayActionSubmissionResult RequestSetCrouched(bool bDesiredCrouched);

	/** Submits the recorded Time Travel action; rewind occurs after its Niagara system finishes. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Time Loop")
	FParadoxTimeLoopOperationResult RequestTimeRewind();

	UFUNCTION(BlueprintPure, Category = "Paradox|Presentation")
	UParadoxOutcomePresentationComponent* GetOutcomePresentationComponent() const
	{
		return OutcomePresentationComponent.Get();
	}

	/** Discovers one enabled map volume and creates the independent orthographic view target. */
	FParadoxCameraOperationResult EnsureFreeCameraInitialized(bool bRequiredForTimeLoop);

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	bool IsFreeCameraReady() const { return CameraInitializationResult.IsSuccess() && FreeCameraRig != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	AParadoxCameraRig* GetFreeCameraRig() const { return FreeCameraRig; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	AParadoxCameraBoundsVolume* GetCameraBoundsVolume() const { return CameraBoundsVolume; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	FParadoxCameraOperationResult GetCameraInitializationResult() const { return CameraInitializationResult; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	FVector GetCameraFocusLocation() const { return CameraFocusLocation; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	float GetCurrentCameraOrthoWidth() const { return CurrentOrthoWidth; }

	UFUNCTION(BlueprintCallable, Category = "Paradox|Camera")
	void RequestCameraRecenter();

protected:

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;
	
	/** Input handlers */
	void OnInputStarted();
	void OnSetDestinationTriggered();
	void OnSetDestinationReleased();
	void OnTouchTriggered();
	void OnTouchReleased();
	void OnRewindTriggered();
	void OnCrouchTriggered();
	void OnCameraMoveTriggered(const FInputActionValue& Value);
	void OnCameraMoveCompleted();
	void OnCameraZoomTriggered(const FInputActionValue& Value);
	void OnCameraRecenterTriggered();
	void OnCameraRotateLeftTriggered();
	void OnCameraRotateRightTriggered();

	/** Helper function to get the move destination */
	void UpdateCachedDestination();
	void UpdatePointerPrediction(bool bUseTouchInput);
	void UpdateChronoSpawnHover(bool bUseTouchInput);
	void TrySelectChronoSpawn(bool bUseTouchInput);
	UParadoxTimeLoopComponent* GetTimeLoopComponent() const;
	bool IsChronoSpawnSelectionActive() const;
	bool IsMovementInputAllowed() const;
	bool IsTacticalPlanningActive() const;
	void PresentPlannedMove(const FGridCellId& GoalCell);
	void ClearPlannedMovePresentation(bool bSuppressCurrentGoal);
	void HandleTacticalPauseResumed(const FTacticalPauseStateChange& Change);
	void ScheduleRecordedTimeTravelExecution();
	void ExecuteRecordedTimeTravel();
	void ClearPendingRecordedTimeTravel();
	bool RequestCameraRotation(int32 Direction);
	void UpdateFreeCamera(float RealDeltaSeconds);
	void UpdateFreeCameraPose(float AspectRatio);
	FRotator GetCurrentCameraOrientation() const;
	FRotator GetCameraOrientationForYawOffset(float YawOffsetDegrees) const;
	float GetCurrentCameraYawOffset() const;
	float GetCameraAspectRatio() const;
	bool CalculateFootprint(
		const FVector& FocusLocation,
		const FRotator& Orientation,
		float OrthoWidth,
		float AspectRatio,
		TArray<FVector>& OutCorners) const;
	bool CalculateFootprintExtents(
		const FRotator& Orientation,
		float OrthoWidth,
		float AspectRatio,
		FVector2D& OutExtents) const;
	bool CalculateRotationArcFootprintExtents(
		float OrthoWidth,
		float AspectRatio,
		float StartYawOffsetDegrees,
		float EndYawOffsetDegrees,
		FVector2D& OutExtents) const;
	float CalculateMaximumCompatibleOrthoWidth(
		const FRotator& Orientation,
		float AspectRatio) const;
	float CalculateMaximumCompatibleOrthoWidthForRotationArc(
		float StartYawOffsetDegrees,
		float EndYawOffsetDegrees,
		float AspectRatio) const;
	float CalculateMaximumRotationSafeOrthoWidth(float AspectRatio) const;
	FVector ClampCameraFocus(
		const FVector& RequestedFocus,
		const FRotator& Orientation,
		float OrthoWidth,
		float AspectRatio) const;
	bool ValidateCameraConfiguration(
		const AParadoxCameraBoundsVolume& Volume,
		const FParadoxCameraConfiguration& Configuration,
		float AspectRatio,
		FString& OutFailure) const;
	void DrawFreeCameraDebug(float AspectRatio) const;
	FGameplayActionSubmissionResult SubmitGridMoveRequest(
		EGridMovePathSource PathSource,
		const FVector& Destination,
		const FGridInjectedPath* InjectedPath);

	friend class UParadoxTimeTravelAction;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxCameraTestAccessor;
#endif
};


