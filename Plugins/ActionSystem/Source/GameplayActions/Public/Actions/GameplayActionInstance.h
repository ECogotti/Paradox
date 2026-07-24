#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/GameplayActionTypes.h"
#include "GameplayActionInstance.generated.h"

class UGameplayActionComponent;
class UGameplayActionDefinition;

/**
 * One transient execution of a Gameplay Action Definition.
 *
 * The owning component creates and retains the UObject, owns every state transition, and releases
 * the instance after the Ended event has been delivered. Consumers should retain the action handle,
 * not this pointer. Configuration and parameters are immutable snapshots copied at acceptance time.
 */
UCLASS(Abstract, Blueprintable, Transient)
class GAMEPLAYACTIONS_API UGameplayActionInstance : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;

	/** Component-local runtime identity. It remains queryable after this instance has been released. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	FGameplayActionHandle GetHandle() const { return Handle; }

	/** Current authoritative lifecycle state assigned by the owning component. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	EGameplayActionState GetState() const { return State; }

	/** Component that owns this transient instance and all of its transitions. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	UGameplayActionComponent* GetOwningComponent() const { return OwningComponent; }

	/** Shared authored Definition. Runtime code must not mutate it. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	const UGameplayActionDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	FGameplayTag GetActionTag() const { return ActionTag; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	int32 GetPriority() const { return Priority; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	FGameplayTagContainer GetExecutionLocks() const { return ExecutionLocks; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	bool IsInterruptible() const { return bInterruptible; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	FGameplayActionTimeout GetOptionalTimeout() const { return OptionalTimeout; }

	/** Returns the immutable queue residence limit. Zero means unlimited. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions|Queue")
	double GetMaxQueueTimeSeconds() const { return MaxQueueTimeSeconds; }

	/** Returns gameplay-scaled seconds accumulated while this instance was actually queued. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions|Queue")
	double GetQueueElapsedSeconds() const { return QueueElapsedSeconds; }

	/** Returns remaining queue time clamped to zero, or zero when queue residence is unlimited. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions|Queue")
	double GetQueueRemainingSeconds() const
	{
		return MaxQueueTimeSeconds > 0.0
			? FMath::Max(0.0, MaxQueueTimeSeconds - QueueElapsedSeconds)
			: 0.0;
	}

	/** True when this instance expires automatically if it remains queued for the configured limit. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions|Queue")
	bool HasQueueTimeout() const { return MaxQueueTimeSeconds > 0.0; }

	/** True when this instance may remain queued indefinitely. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions|Queue")
	bool IsQueueTimeUnlimited() const { return MaxQueueTimeSeconds <= 0.0; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	EGameplayActionJournalRequirement GetJournalRequirement() const { return JournalRequirement; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	FGameplayTag GetOriginTag() const { return OriginTag; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	FGameplayActionCorrelationData GetCorrelation() const { return Correlation; }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	UObject* GetRequester() const { return Requester.Get(); }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	int64 GetSubmissionSequence() const { return SubmissionSequence; }

	/** True when this running action asks its owning component to dispatch Action Tick. */
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions|Tick")
	bool IsActionTickEnabled() const { return bActionTickEnabled; }

	/** Native read-only access to the isolated parameter snapshot. */
	const FInstancedPropertyBag& GetParameters() const { return Parameters; }
	double GetAcceptedTimeSeconds() const { return AcceptedTimeSeconds; }

protected:
	/**
	 * Opt-in per-instance tick. Set this in a native constructor or Blueprint Class Defaults,
	 * or change it at runtime with Set Action Tick Enabled.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gameplay Actions|Tick", meta = (BlueprintProtected = "true"))
	bool bActionTickEnabled = false;

	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Can Start Action"))
	bool CanStartAction(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const;
	virtual bool CanStartAction_Implementation(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const;

	/**
	 * Called exactly once after the submission and initial journal transaction have been accepted.
	 *
	 * Queued instances receive this hook before they wait, while immediately executable instances
	 * receive it in Starting state. Use it to cache parameters and establish reversible internal
	 * state. Do not begin gameplay work that requires locks here. Component mutations and terminal
	 * completion are rejected while this hook is executing.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Init"))
	void OnActionInit();
	virtual void OnActionInit_Implementation();

	/**
	 * Called exactly once after all locks have been acquired and the state has become Running.
	 *
	 * This hook is never called merely because an action entered the queue. Start timers, bind
	 * execution callbacks, and perform gameplay effects here.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Start"))
	void OnActionStarted();
	virtual void OnActionStarted_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Paused"))
	void OnActionPaused();
	virtual void OnActionPaused_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Resumed"))
	void OnActionResumed();
	virtual void OnActionResumed_Implementation();

	/**
	 * Called on the Game Thread while this action is Running and action ticking is enabled.
	 * It is never dispatched during Init, queue residence, component pause, cleanup, or terminal state.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions|Tick", meta = (DisplayName = "Action Tick"))
	void OnActionTick(float DeltaSeconds);
	virtual void OnActionTick_Implementation(float DeltaSeconds);

	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Cancelled"))
	void OnActionCancelled(FGameplayTag ReasonTag);
	virtual void OnActionCancelled_Implementation(FGameplayTag ReasonTag);

	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Interrupted"))
	void OnActionInterrupted(FGameplayTag ReasonTag);
	virtual void OnActionInterrupted_Implementation(FGameplayTag ReasonTag);

	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Aborted"))
	void OnActionAborted(FGameplayTag ReasonTag);
	virtual void OnActionAborted_Implementation(FGameplayTag ReasonTag);

	/**
	 * Final symmetric cleanup hook for every accepted action, including queued actions that never start.
	 *
	 * Clear timers, remove only bindings owned by this instance, and invalidate external callbacks here.
	 * The component calls cleanup before publishing the immutable terminal state and Ended event.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Gameplay Actions", meta = (DisplayName = "Action Cleanup"))
	void OnActionCleanup();
	virtual void OnActionCleanup_Implementation();

	/** Requests successful completion through the component's single authoritative terminal path. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions", meta = (BlueprintProtected = "true"))
	void SucceedAction(FGameplayTag ReasonTag, const FString& DiagnosticMessage);

	/** Requests failed completion through the component's single authoritative terminal path. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions", meta = (BlueprintProtected = "true"))
	void FailAction(FGameplayTag ReasonTag, const FString& DiagnosticMessage);

	/** Enables or disables Action Tick for this instance without affecting other actions. */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Tick", meta = (BlueprintProtected = "true"))
	void SetActionTickEnabled(bool bEnabled);

private:
	void InitializeInstance(
		UGameplayActionComponent* InOwningComponent,
		UGameplayActionDefinition* InDefinition,
		const FGameplayActionRequest& Request,
		FGameplayActionHandle InHandle,
		int64 InSubmissionSequence,
		int32 InPriority,
		EGameplayActionBlockedPolicy InBlockedPolicy,
		double InAcceptedTimeSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UGameplayActionComponent> OwningComponent;

	UPROPERTY(Transient)
	TObjectPtr<UGameplayActionDefinition> Definition;

	UPROPERTY(Transient)
	FGameplayActionHandle Handle;

	UPROPERTY(Transient)
	EGameplayActionState State = EGameplayActionState::Created;

	UPROPERTY(Transient)
	FInstancedPropertyBag Parameters;

	UPROPERTY(Transient)
	FGameplayTag ActionTag;

	UPROPERTY(Transient)
	int32 Priority = 0;

	UPROPERTY(Transient)
	EGameplayActionBlockedPolicy BlockedPolicy = EGameplayActionBlockedPolicy::Queue;

	UPROPERTY(Transient)
	FGameplayTagContainer ExecutionLocks;

	UPROPERTY(Transient)
	bool bInterruptible = true;

	UPROPERTY(Transient)
	FGameplayActionTimeout OptionalTimeout;

	UPROPERTY(Transient)
	double MaxQueueTimeSeconds = 0.0;

	UPROPERTY(Transient)
	double QueueElapsedSeconds = 0.0;

	UPROPERTY(Transient)
	EGameplayActionJournalRequirement JournalRequirement = EGameplayActionJournalRequirement::Disabled;

	UPROPERTY(Transient)
	FGameplayTag OriginTag;

	UPROPERTY(Transient)
	FGameplayActionCorrelationData Correlation;

	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> Requester;

	UPROPERTY(Transient)
	int64 SubmissionSequence = 0;

	/** Internal once-only guards; lifecycle hooks remain component-owned even under reentrant callbacks. */
	bool bHasInitialized = false;
	bool bHasStarted = false;

	double AcceptedTimeSeconds = 0.0;

	friend class UGameplayActionComponent;
};
