#pragma once

#include "Actions/GameplayActionInstance.h"
#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionTypes.h"
#include "SmartObjectRuntime.h"
#include "ParadoxInteractionActionBase.generated.h"

class AActor;
class AController;
class UParadoxInteractionComponent;
class UGridMoveToCellExecution;
struct FGridMoveToCellExecutionResult;

/**
 * Replay-safe Gameplay Action template for one Paradox interaction.
 *
 * The base owns target/slot resolution and the Smart Object claim. Concrete Blueprint or C++
 * actions implement ExecuteInteraction and complete through the protected helpers.
 */
UCLASS(Abstract, BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxInteractionActionBase : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	UParadoxInteractionActionBase();

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	AActor* GetInteractionRequester() const { return InteractionRequester.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	AActor* GetInteractionTarget() const { return InteractionTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	UParadoxInteractionComponent* GetInteractionComponent() const
	{
		return InteractionComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	FGameplayTag GetInteractionTag() const { return SemanticParameters.InteractionTag; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	FSmartObjectSlotHandle GetResolvedSlotHandle() const { return ResolvedOption.SlotHandle; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	FTransform GetResolvedSlotWorldTransform() const
	{
		return ResolvedOption.SlotWorldTransform;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	FGridCellId GetResolvedGridCellId() const { return ResolvedOption.GridCellId; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	bool HasInteractionClaim() const { return ClaimHandle.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	bool IsMovingToInteraction() const { return bMovingToInteraction; }

protected:
	/** Local half of the Paradox.Interaction.Debug gate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Interaction|Debug", meta = (BlueprintProtected = "true"))
	bool bEnableDebug = false;

	virtual bool CanStartAction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionStarted_Implementation() override;
	virtual void OnActionPaused_Implementation() override;
	virtual void OnActionResumed_Implementation() override;
	virtual void OnActionCancelled_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionInterrupted_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionAborted_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionCleanup_Implementation() override;

	/** Effect-only preflight evaluated before claims, before movement and again after arrival. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	bool CanSatisfyInteractionPreconditions(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const;
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const;

	/** Lets a running semantic request complete successfully when another authority reached its outcome. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	bool IsInteractionOutcomeSatisfied() const;
	virtual bool IsInteractionOutcomeSatisfied_Implementation() const;

	/** Concrete pre-execution policy evaluated after the current runtime context is resolved. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	bool CanExecuteInteraction(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const;
	virtual bool CanExecuteInteraction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionContextResolved();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionSlotClaimed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionMovementStarted();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionMovementCompleted();

	/** Starts concrete behavior. The native default fails explicitly instead of remaining Running. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void ExecuteInteraction();
	virtual void ExecuteInteraction_Implementation();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionSucceeded();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionFailed(FGameplayTag ReasonTag, const FString& DiagnosticMessage);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionCancelled(FGameplayTag ReasonTag);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionInterrupted(FGameplayTag ReasonTag);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionAborted(FGameplayTag ReasonTag);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void OnInteractionCleanup();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void CompleteInteractionSuccess(
		FGameplayTag ReasonTag,
		const FString& DiagnosticMessage);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction", meta = (BlueprintProtected = "true"))
	void CompleteInteractionFailure(
		FGameplayTag ReasonTag,
		const FString& DiagnosticMessage);

	/** Revalidates a running movement after a concrete Puzzle endpoint changed. */
	void ReevaluateRunningInteraction();

private:
	struct FExecutionCandidate
	{
		FParadoxInteractionOption Option;
		double PathCost = 0.0;
		bool bAlreadyInPlace = false;
	};

	bool ReadSemanticParameters(
		FParadoxInteractionActionParameters& OutParameters,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const;
	bool ResolveCurrentContext(
		FParadoxInteractionOption& OutOption,
		EParadoxInteractionRequestStatus& OutStatus,
		EParadoxInteractionQueryStatus& OutQueryStatus,
		FString& OutDiagnostic) const;
	bool BuildExecutionCandidates(FString& OutDiagnostic);
	bool HasReachableExecutionCandidate(
		AActor* RequesterActor,
		UParadoxInteractionComponent* Component,
		FGameplayTag ExactInteractionTag,
		FString& OutDiagnostic) const;
	bool TryStartNextCandidate(FString& OutDiagnostic);
	bool ClaimResolvedOption(FString& OutDiagnostic);
	bool StartMovementToResolvedOption(FString& OutDiagnostic);
	void ExecuteResolvedInteraction();
	AController* ResolveMovementController() const;
	bool ReadMovementParameters(
		FParadoxInteractionMovementParameters& OutParameters,
		FString& OutDiagnostic) const;
	void HandleMovementFinished(const FGridMoveToCellExecutionResult& Result);
	void ReleaseMovement(bool bCancel);
	void FailInteraction(FGameplayTag ReasonTag, const FString& DiagnosticMessage);
	void ReleaseInteractionClaim();
	void LogDebugState(const TCHAR* EventName, FGameplayTag ReasonTag = FGameplayTag()) const;

	UFUNCTION()
	void HandleInteractionTargetDestroyed(AActor* DestroyedActor);
	void HandleInteractionAffordanceChanged(
		UParadoxInteractionComponent* ChangedInteractionComponent);
	void HandleInteractionSlotInvalidated(
		const FSmartObjectClaimHandle& InvalidatedClaim,
		ESmartObjectSlotState CurrentState);

	UPROPERTY(Transient)
	FParadoxInteractionActionParameters SemanticParameters;

	UPROPERTY(Transient)
	FParadoxInteractionOption ResolvedOption;

	UPROPERTY(Transient)
	TObjectPtr<UGridMoveToCellExecution> MovementExecution;

	TWeakObjectPtr<AActor> InteractionRequester;
	TWeakObjectPtr<AActor> InteractionTarget;
	TWeakObjectPtr<UParadoxInteractionComponent> InteractionComponent;
	FSmartObjectClaimHandle ClaimHandle;
	TArray<FExecutionCandidate> ExecutionCandidates;
	TSet<FGridCellId> AttemptedMovementCells;
	int32 NextCandidateIndex = 0;
	FDelegateHandle MovementFinishedHandle;
	FDelegateHandle InteractionAffordanceChangedHandle;
	bool bSlotInvalidationCallbackRegistered = false;
	bool bMovingToInteraction = false;
	bool bCompletionRequested = false;
};
