#pragma once

#include "Behavior/ParadoxCloneBehaviorTypes.h"
#include "Components/ActorComponent.h"
#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxCloneInvestigationComponent.generated.h"

class UGameplayActionComponent;
class UGridMoveToCellActionDefinition;
class UParadoxInvestigationWaitActionDefinition;

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FParadoxInvestigationFinishedNativeDelegate,
	const FParadoxInvestigationContext&,
	const FGameplayActionResult&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FParadoxInvestigationRetargetedNativeDelegate,
	const FParadoxInvestigationContext&);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FParadoxRecoveryMoveFinishedNativeDelegate,
	int32,
	bool);

/** Request-writing source used only for reflected, type-safe Property Bag assignment. */
USTRUCT()
struct FParadoxInvestigationMoveRequestValues
{
	GENERATED_BODY()

	UPROPERTY()
	EGridMovePathSource PathSource = EGridMovePathSource::Destination;

	UPROPERTY()
	FVector GoalLocation = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<AActor> GoalActor = nullptr;

	UPROPERTY()
	EGridGoalContentionPolicy GoalContentionPolicy =
		EGridGoalContentionPolicy::RedirectOnCompletion;
};

USTRUCT()
struct FParadoxInvestigationWaitRequestValues
{
	GENERATED_BODY()

	UPROPERTY()
	double Duration = 2.0;
};

/**
 * GameplayActions-only investigation executor.
 *
 * It owns at most one authoritative action handle, validates a retarget request before interrupting
 * the previous handle, and guards every callback by exact handle/correlation plus revision.
 */
UCLASS(ClassGroup = (Paradox), meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxCloneInvestigationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxCloneInvestigationComponent();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Investigation")
	FParadoxCloneBehaviorOperationResult InitializeInvestigation(
		UGameplayActionComponent* InActionComponent);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Investigation")
	FParadoxCloneBehaviorOperationResult StartInvestigation(
		const FParadoxInvestigationContext& Context);

	/** Side-effect-free destination/request validation used before coordinator state commits. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Investigation")
	FParadoxCloneBehaviorOperationResult ValidateInvestigation(
		const FParadoxInvestigationContext& Context) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Investigation")
	FParadoxCloneBehaviorOperationResult RetargetInvestigation(
		const FParadoxInvestigationContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Investigation")
	FParadoxCloneBehaviorOperationResult CancelInvestigation(FGameplayTag ReasonTag);

	/** Recovery seam for a non-movement intent that requires reacquiring an execution position. */
	FParadoxCloneBehaviorOperationResult StartReplayRecoveryMove(
		const FVector& Location,
		int32 InvestigationRevision,
		int32 SchedulingPriority);

	UFUNCTION(BlueprintPure, Category = "Paradox|Investigation")
	bool IsInvestigationActive() const { return bActive; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Investigation")
	FGameplayActionHandle GetActiveActionHandle() const { return ActiveActionHandle; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Investigation")
	int32 GetActiveRevision() const { return ActiveContext.InvestigationRevision; }

	FParadoxInvestigationFinishedNativeDelegate& OnInvestigationFinishedNative()
	{
		return InvestigationFinishedNative;
	}

	FParadoxInvestigationRetargetedNativeDelegate& OnInvestigationRetargetedNative()
	{
		return InvestigationRetargetedNative;
	}

	FParadoxRecoveryMoveFinishedNativeDelegate& OnRecoveryMoveFinishedNative()
	{
		return RecoveryMoveFinishedNative;
	}

	/** GameplayActions wait duration after movement and orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Investigation", meta = (ClampMin = "0.0", Units = "s"))
	float InspectionDuration = 2.0f;

	/** Distance at which investigation movement is considered complete. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Investigation", meta = (ClampMin = "0.0", Units = "cm"))
	float InvestigationAcceptanceRadius = 75.0f;

	/**
	 * Endpoint policy used by investigation and replay-recovery positioning.
	 *
	 * Redirect On Completion deliberately lets Reserved Corridor arbitrate transient reservations
	 * while the clone approaches. It only redirects when the endpoint remains unavailable near
	 * completion, instead of rejecting or shortening the move before it starts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Investigation")
	EGridGoalContentionPolicy MovementGoalContentionPolicy =
		EGridGoalContentionPolicy::RedirectOnCompletion;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EExecutionPhase : uint8
	{
		None,
		Moving,
		Inspecting,
		RecoveryMoving
	};

	bool BuildMoveRequest(
		const FVector& Location,
		AActor* SourceActor,
		int32 SchedulingPriority,
		FGameplayActionRequest& OutRequest,
		FString& OutDiagnostic,
		FGuid CorrelationOverride = FGuid()) const;
	bool BuildWaitRequest(
		const FParadoxInvestigationContext& Context,
		FGameplayActionRequest& OutRequest,
		FString& OutDiagnostic) const;
	FParadoxCloneBehaviorOperationResult SubmitPreparedAction(
		const FGameplayActionRequest& Request,
		EExecutionPhase NewPhase);
	void HandleActionEnded(const FGameplayActionEvent& Event);
	void HandleMovementFinished(const FGameplayActionResult& Result);
	void HandleInspectionFinished(const FGameplayActionResult& Result);
	void OrientTowardInvestigation();
	void FinishInvestigationOnce(const FGameplayActionResult& Result);
	void ResetExecution(bool bKeepContext);
	FParadoxCloneBehaviorOperationResult MakeResult(
		EParadoxCloneBehaviorOperationStatus Status,
		FString Diagnostic,
		FName Reason = NAME_None) const;

	UPROPERTY(Transient)
	TObjectPtr<UGameplayActionComponent> ActionComponent;

	UPROPERTY(Transient)
	TObjectPtr<UGridMoveToCellActionDefinition> MoveDefinition;

	UPROPERTY(Transient)
	TObjectPtr<UParadoxInvestigationWaitActionDefinition> WaitDefinition;

	UPROPERTY(Transient)
	FParadoxInvestigationContext ActiveContext;

	FGameplayActionHandle ActiveActionHandle;
	FGuid ActiveCorrelationId;
	FDelegateHandle ActionEndedHandle;
	EExecutionPhase ExecutionPhase = EExecutionPhase::None;
	bool bInitialized = false;
	bool bActive = false;
	bool bCompletionBroadcast = false;

	FParadoxInvestigationFinishedNativeDelegate InvestigationFinishedNative;
	FParadoxInvestigationRetargetedNativeDelegate InvestigationRetargetedNative;
	FParadoxRecoveryMoveFinishedNativeDelegate RecoveryMoveFinishedNative;
};
