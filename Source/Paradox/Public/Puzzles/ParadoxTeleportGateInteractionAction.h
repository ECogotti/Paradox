#pragma once

#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionActionBase.h"
#include "Puzzles/ParadoxPairedTransferEndpoint.h"
#include "TimerManager.h"
#include "ParadoxTeleportGateInteractionAction.generated.h"

class AParadoxTeleportGate;
struct FParadoxTeleportGateInteractionActionTestAccessor;

/** Semantic Enter interaction that remains active until the paired Gate transfer terminates. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxEnterTeleportGateInteractionAction
	: public UParadoxInteractionActionBase
{
	GENERATED_BODY()

protected:
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual bool IsInteractionExecutionPending_Implementation() const override;
	virtual void ExecuteInteraction_Implementation() override;
	virtual void OnActionPaused_Implementation() override;
	virtual void OnActionResumed_Implementation() override;
	virtual void OnActionTick_Implementation(float DeltaSeconds) override;
	virtual void OnActionCleanup_Implementation() override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxTeleportGateInteractionActionTestAccessor;
#endif

	enum class ETunnelMoveStage : uint8
	{
		None,
		Ingress,
		Egress
	};

	void BindTransfer(AParadoxTeleportGate& Gate, const FGuid& OperationId);
	void UnbindTransfer();
	bool StartTunnelMovement(
		ETunnelMoveStage Stage,
		const FVector& GoalLocation,
		FString& OutDiagnostic);
	void ReleaseTunnelMovement();
	void HandleTunnelMoveTimedOut();
	void HandleTunnelStageSucceeded(ETunnelMoveStage Stage);
	void FailTunnelTraversal(const FString& DiagnosticMessage);

	UFUNCTION()
	void HandleTransferCompleted(const FParadoxTransferOperationContext& Context);

	UFUNCTION()
	void HandleTransferCancelled(
		const FParadoxTransferOperationContext& Context,
		EParadoxTransferCancellationReason Reason,
		bool bWasCommitted);

	TWeakObjectPtr<AParadoxTeleportGate> ActiveGate;
	FGuid ActiveOperationId;
	FTimerHandle TunnelMoveTimeoutHandle;
	FString PendingTunnelFailureDiagnostic;
	ETunnelMoveStage ActiveTunnelMoveStage = ETunnelMoveStage::None;
	FVector ActiveTunnelMoveGoal = FVector::ZeroVector;
	float ActiveTunnelMoveTimeoutSeconds = 0.0f;
	bool bCleaningUpTransfer = false;
};
