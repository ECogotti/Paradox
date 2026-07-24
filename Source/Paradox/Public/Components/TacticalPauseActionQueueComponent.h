#pragma once

#include "Components/ActorComponent.h"
#include "Types/GameplayActionTypes.h"
#include "Types/TacticalPauseTypes.h"
#include "TacticalPauseActionQueueComponent.generated.h"

class UGameplayActionComponent;
class UTacticalPauseWorldSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxNextActionChangedEvent,
	FGameplayActionHandle, PreviousHandle,
	FGameplayActionHandle, NewHandle);

/**
 * Project adapter between Tactical Pause and the generic Gameplay Actions scheduler.
 *
 * The component pauses/resumes action execution with the world, while submissions remain accepted
 * as queued work. SubmitOrReplaceNextAction exposes one replaceable planning slot that future player
 * commands can share without introducing gameplay dependencies into TacticalPause itself.
 */
UCLASS(ClassGroup = (Paradox), meta = (BlueprintSpawnableComponent))
class UTacticalPauseActionQueueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTacticalPauseActionQueueComponent();

	/** Optional explicit scheduler; otherwise the owner is searched once during BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Planning")
	TObjectPtr<UGameplayActionComponent> ActionComponentOverride = nullptr;

	/** Broadcast whenever the replaceable next-action slot changes identity. */
	UPROPERTY(BlueprintAssignable, Category = "Tactical Planning|Events")
	FParadoxNextActionChangedEvent OnNextActionChanged;

	/**
	 * Replaces the queued planning slot and submits the new request with Queue policy.
	 * This operation is valid only while TacticalPause is authoritatively paused.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tactical Planning")
	FGameplayActionSubmissionResult SubmitOrReplaceNextAction(FGameplayActionRequest Request);

	/** Cancels the currently tracked queued next action, if any. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Planning")
	EGameplayActionOperationResult ClearNextAction();

	/** Handle of the queued next action; invalid after it starts, ends, or is cleared. */
	UFUNCTION(BlueprintPure, Category = "Tactical Planning")
	FGameplayActionHandle GetNextActionHandle() const { return NextActionHandle; }

	UFUNCTION(BlueprintPure, Category = "Tactical Planning")
	bool HasNextAction() const { return NextActionHandle.IsValid(); }

	/** True when world pause and scheduler pause are both active. */
	UFUNCTION(BlueprintPure, Category = "Tactical Planning")
	bool IsAcceptingTacticalPlanning() const;

	UFUNCTION(BlueprintPure, Category = "Tactical Planning")
	UGameplayActionComponent* GetActionComponent() const { return ResolvedActionComponent; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BindDependencies();
	void UnbindDependencies();
	void ApplyTacticalPauseState(bool bPaused);
	void SetNextActionHandle(FGameplayActionHandle NewHandle);
	FGameplayActionSubmissionResult MakePlanningFailure(const FString& DiagnosticMessage) const;

	void HandleTacticalPaused(const FTacticalPauseStateChange& Change);
	void HandleTacticalResumed(const FTacticalPauseStateChange& Change);

	UFUNCTION()
	void HandleActionStarted(const FGameplayActionEvent& Event);

	UFUNCTION()
	void HandleActionEnded(const FGameplayActionEvent& Event);

	UPROPERTY(Transient)
	TObjectPtr<UGameplayActionComponent> ResolvedActionComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTacticalPauseWorldSubsystem> TacticalPauseSubsystem = nullptr;

	/** True only when this adapter, rather than another system, paused the scheduler. */
	bool bSchedulerPauseOwned = false;

	FGameplayActionHandle NextActionHandle;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxTacticalPlanningTestAccessor;
#endif
};
