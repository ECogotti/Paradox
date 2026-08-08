#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Receivers/PuzzleReceiverTypes.h"
#include "PuzzleReceiverComponent.generated.h"

class APuzzleController;
class UPuzzleReceiverComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPuzzleReceiverTransitionDelegate, UPuzzleReceiverComponent*, Receiver);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPuzzleReceiverStateChangedDelegate, UPuzzleReceiverComponent*, Receiver, bool, bIsActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FPuzzleReceiverPrerequisitesChangedDelegate,
	UPuzzleReceiverComponent*, Receiver,
	bool, bPrerequisitesSatisfied);
DECLARE_MULTICAST_DELEGATE_TwoParams(FPuzzleReceiverNativeStateChangedDelegate, UPuzzleReceiverComponent*, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FPuzzleReceiverNativePrerequisitesChangedDelegate,
	UPuzzleReceiverComponent*,
	bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FPuzzleReceiverInvalidatedNativeDelegate, UPuzzleReceiverComponent*);

/** Actor component that aggregates controller activation requests into one effective receiver state. */
UCLASS(ClassGroup = (Puzzle), Blueprintable, meta = (BlueprintSpawnableComponent))
class PUZZLESYSTEM_API UPuzzleReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPuzzleReceiverComponent();

	/**
	 * Determines whether Controller prerequisites activate this Receiver immediately or require a
	 * separate manual command. Automatic preserves the historical behavior and is the default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Receiver|Activation")
	EPuzzleReceiverActivationMode ActivationMode = EPuzzleReceiverActivationMode::Automatic;

	/**
	 * Clears outstanding requests when the component leaves play.
	 *
	 * @param EndPlayReason Unreal reason for the component ending play.
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Notifies native observers during EndPlay or explicit component destruction. */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Blueprint event fired after the effective receiver state changes to active. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Receiver")
	FPuzzleReceiverTransitionDelegate OnReceiverActivated;

	/** Blueprint event fired after the effective receiver state changes to inactive. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Receiver")
	FPuzzleReceiverTransitionDelegate OnReceiverDeactivated;

	/** Blueprint event fired after any effective receiver state transition. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Receiver")
	FPuzzleReceiverStateChangedDelegate OnReceiverStateChanged;

	/** Fired after the OR-aggregated Controller prerequisite state changes. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Receiver|Activation")
	FPuzzleReceiverPrerequisitesChangedDelegate OnReceiverActivationPrerequisitesChanged;

	/** Native C++ notification fired after Receiver virtual hooks and before Blueprint-assignable delegates. */
	FPuzzleReceiverNativeStateChangedDelegate OnReceiverStateChangedNative;

	/** Native counterpart to OnReceiverActivationPrerequisitesChanged. */
	FPuzzleReceiverNativePrerequisitesChangedDelegate OnReceiverActivationPrerequisitesChangedNative;

	/** Native lifecycle notification used by read-only observers before this endpoint becomes stale. */
	FPuzzleReceiverInvalidatedNativeDelegate OnReceiverInvalidatedNative;

	/**
	 * Adds or updates one controller's activation request.
	 *
	 * @param SourceController Controller that owns this request.
	 * @param bRequestedActive True when the controller wants this receiver active.
	 * @return True when the update changes prerequisites, manual intent, or effective state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Receiver")
	bool SetControllerRequest(APuzzleController* SourceController, bool bRequestedActive);

	/**
	 * Removes one controller's request.
	 *
	 * @param SourceController Controller whose request should be removed.
	 * @return True when removal changes prerequisites, manual intent, or effective state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Receiver")
	bool RemoveControllerRequest(APuzzleController* SourceController);

	/**
	 * Requests activation while in Manual mode.
	 *
	 * The request is accepted only while at least one valid Controller requests this Receiver active.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Receiver|Activation")
	FPuzzleReceiverActivationCommandResult RequestManualActivation();

	/** Requests deactivation while in Manual mode. Deactivation never requires prerequisites. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Receiver|Activation")
	FPuzzleReceiverActivationCommandResult RequestManualDeactivation();

	/** Returns whether a new manual activation request can currently be accepted. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver|Activation")
	bool CanRequestManualActivation() const;

	/** Returns the OR-aggregated prerequisite state supplied by valid Controllers. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver|Activation")
	bool AreActivationPrerequisitesSatisfied() const { return bActivationPrerequisitesSatisfied; }

	/** Returns whether a currently valid manual activation request is latched. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver|Activation")
	bool IsManualActivationRequested() const { return bManualActivationRequested; }

	/** Returns the fixed activation policy configured for this Receiver instance. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver|Activation")
	EPuzzleReceiverActivationMode GetActivationMode() const { return ActivationMode; }

	/**
	 * Returns the effective state after applying Controller prerequisites and activation policy.
	 *
	 * @return True when this Receiver capability is currently active.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver")
	bool IsReceiverActive() const;

	/**
	 * Counts valid controllers currently requesting active.
	 *
	 * @return Number of active requests whose Controller source remains valid.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver")
	int32 GetActiveRequestCount() const;

	/**
	 * Returns valid controllers currently tracked by this receiver.
	 *
	 * @param OutControllers Receives valid Controllers currently requesting active.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Receiver")
	void GetRequestingControllers(TArray<APuzzleController*>& OutControllers) const;

protected:
	/**
	 * C++ extension hook called on every effective state transition before Blueprint delegates.
	 *
	 * @param bNewActive New effective receiver state.
	 */
	virtual void HandleReceiverStateChanged(bool bNewActive);

	/** C++ extension hook called when the effective state becomes active. */
	virtual void HandleReceiverActivated();

	/** C++ extension hook called when the effective state becomes inactive. */
	virtual void HandleReceiverDeactivated();

	/** C++ extension hook called when aggregated Controller prerequisites change. */
	virtual void HandleReceiverActivationPrerequisitesChanged(bool bPrerequisitesSatisfied);

private:
	/** Effective receiver state after OR-aggregating valid controller requests. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Receiver", meta = (AllowPrivateAccess = "true"))
	bool bIsReceiverActive = false;

	/** True while at least one valid Controller currently requests activation. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Receiver|Activation", meta = (AllowPrivateAccess = "true"))
	bool bActivationPrerequisitesSatisfied = false;

	/** Explicit request used only by Manual mode; cleared whenever prerequisites fail. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Receiver|Activation", meta = (AllowPrivateAccess = "true"))
	bool bManualActivationRequested = false;

	/** Source-aware request table that prevents controllers from overwriting one another. */
	TMap<TWeakObjectPtr<APuzzleController>, bool> ControllerRequests;

	/** Prevents duplicate invalidation notifications during paired EndPlay/destroy paths. */
	bool bHasBroadcastInvalidated = false;

	/** Prevents recursive state reconciliation during synchronous Receiver notification chains. */
	bool bIsReconcilingState = false;

	/** Requests one collapsed follow-up reconciliation after a reentrant Controller update. */
	bool bReconciliationRequested = false;

	/**
	 * Recomputes effective state from valid request sources.
	 *
	 * @return True when prerequisites, manual intent, or effective state changed.
	 */
	bool RecomputeEffectiveState();

	/**
	 * Invokes C++ hooks and Blueprint delegates for a state transition.
	 *
	 * @param bNewActive New effective receiver state.
	 */
	void BroadcastReceiverStateChanged(bool bNewActive);

	/** Invokes the prerequisite hook and native/Blueprint delegates in deterministic order. */
	void BroadcastActivationPrerequisitesChanged(bool bPrerequisitesSatisfied);

	/** Builds a complete command result from the Receiver's settled runtime state. */
	FPuzzleReceiverActivationCommandResult MakeActivationCommandResult(
		EPuzzleReceiverActivationCommandStatus Status,
		const FString& DiagnosticMessage) const;

	/** Refreshes read-only graph snapshots after an explicit manual command settles. */
	void NotifyGraphStateChanged();

	/** Broadcasts endpoint invalidation exactly once. */
	void BroadcastInvalidated();
};
