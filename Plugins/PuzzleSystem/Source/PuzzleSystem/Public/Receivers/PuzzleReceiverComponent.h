#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PuzzleReceiverComponent.generated.h"

class APuzzleController;
class UPuzzleReceiverComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPuzzleReceiverTransitionDelegate, UPuzzleReceiverComponent*, Receiver);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPuzzleReceiverStateChangedDelegate, UPuzzleReceiverComponent*, Receiver, bool, bIsActive);

/** Actor component that aggregates controller activation requests into one effective receiver state. */
UCLASS(ClassGroup = (Puzzle), Blueprintable, meta = (BlueprintSpawnableComponent))
class PUZZLESYSTEM_API UPuzzleReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPuzzleReceiverComponent();

	/**
	 * Clears outstanding requests when the component leaves play.
	 *
	 * @param EndPlayReason Unreal reason for the component ending play.
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Blueprint event fired after the effective receiver state changes to active. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Receiver")
	FPuzzleReceiverTransitionDelegate OnReceiverActivated;

	/** Blueprint event fired after the effective receiver state changes to inactive. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Receiver")
	FPuzzleReceiverTransitionDelegate OnReceiverDeactivated;

	/** Blueprint event fired after any effective receiver state transition. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Receiver")
	FPuzzleReceiverStateChangedDelegate OnReceiverStateChanged;

	/**
	 * Adds or updates one controller's activation request.
	 *
	 * @param SourceController Controller that owns this request.
	 * @param bRequestedActive True when the controller wants this receiver active.
	 * @return True when the request source was valid and accepted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Receiver")
	bool SetControllerRequest(APuzzleController* SourceController, bool bRequestedActive);

	/**
	 * Removes one controller's request.
	 *
	 * @param SourceController Controller whose request should be removed.
	 * @return True when a stored request was removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Receiver")
	bool RemoveControllerRequest(APuzzleController* SourceController);

	/**
	 * Returns the effective receiver state after aggregating valid controller requests.
	 *
	 * @return True when at least one valid controller currently requests active.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver")
	bool IsReceiverActive() const;

	/**
	 * Counts valid controllers currently requesting active.
	 *
	 * @return Number of active requests after pruning invalid sources.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Receiver")
	int32 GetActiveRequestCount() const;

	/**
	 * Returns valid controllers currently tracked by this receiver.
	 *
	 * @param OutControllers Receives all valid controller sources, active and inactive.
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

private:
	/** Effective receiver state after OR-aggregating valid controller requests. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Receiver", meta = (AllowPrivateAccess = "true"))
	bool bIsReceiverActive = false;

	/** Source-aware request table that prevents controllers from overwriting one another. */
	TMap<TWeakObjectPtr<APuzzleController>, bool> ControllerRequests;

	/**
	 * Recomputes effective state from valid request sources.
	 *
	 * @return True when the effective state changed.
	 */
	bool RecomputeEffectiveState();

	/**
	 * Invokes C++ hooks and Blueprint delegates for a state transition.
	 *
	 * @param bNewActive New effective receiver state.
	 */
	void BroadcastReceiverStateChanged(bool bNewActive);
};
