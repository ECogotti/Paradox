#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Signals/PuzzleSignalTypes.h"
#include "PuzzleEmitterComponent.generated.h"

class UPuzzleSignalPayload;
class UPuzzleEmitterComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPuzzleSignalChangedDelegate, UPuzzleEmitterComponent*, Emitter, FGameplayTag, SignalTag, FPuzzleSignalState, SignalState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPuzzleEmitterInvalidatedDelegate, UPuzzleEmitterComponent*, Emitter);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FPuzzleSignalChangedNativeDelegate, UPuzzleEmitterComponent*, FGameplayTag, FPuzzleSignalState);
DECLARE_MULTICAST_DELEGATE_OneParam(FPuzzleEmitterInvalidatedNativeDelegate, UPuzzleEmitterComponent*);

/** Actor component that publishes persistent gameplay-tagged puzzle signal states. */
UCLASS(ClassGroup = (Puzzle), Blueprintable, meta = (BlueprintSpawnableComponent))
class PUZZLESYSTEM_API UPuzzleEmitterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPuzzleEmitterComponent();

	/**
	 * Notifies listeners before component shutdown can leave controller inputs stale.
	 *
	 * @param EndPlayReason Unreal reason for the component ending play.
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Notifies listeners when the component is explicitly destroyed.
	 *
	 * @param bDestroyingHierarchy True when destruction is part of owner hierarchy teardown.
	 */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Blueprint event fired after a signal state is changed or explicitly republished. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Emitter")
	FPuzzleSignalChangedDelegate OnSignalChanged;

	/** Blueprint event fired when this emitter can no longer provide valid input state. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Emitter")
	FPuzzleEmitterInvalidatedDelegate OnEmitterInvalidated;

	/** Native event used by controllers to avoid dynamic delegate overhead. */
	FPuzzleSignalChangedNativeDelegate OnSignalChangedNative;

	/** Native invalidation event used by controllers during shutdown/destruction. */
	FPuzzleEmitterInvalidatedNativeDelegate OnEmitterInvalidatedNative;

	/**
	 * Sets or creates a signal channel state.
	 *
	 * @param SignalTag Gameplay tag identifying the signal channel.
	 * @param bNewActive New active/inactive state for the channel.
	 * @param Payload Optional typed payload associated with the state.
	 * @return True when the signal was accepted and either changed or already matched the requested state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Emitter")
	bool SetSignalState(FGameplayTag SignalTag, bool bNewActive, UPuzzleSignalPayload* Payload);

	/**
	 * Re-broadcasts an existing signal without changing its active flag.
	 *
	 * @param SignalTag Gameplay tag identifying the existing signal channel.
	 * @return True when the signal existed and was republished.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Emitter")
	bool RepublishSignal(FGameplayTag SignalTag);

	/**
	 * Reads the latest cached state for a signal channel.
	 *
	 * @param SignalTag Gameplay tag identifying the signal channel.
	 * @param OutSignalState Receives the latest state when found.
	 * @return True when the signal exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Emitter")
	bool TryGetSignalState(FGameplayTag SignalTag, FPuzzleSignalState& OutSignalState) const;

	/**
	 * Returns all signal channels currently owned by this emitter.
	 *
	 * @return Read-only map of gameplay tag to signal state.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Emitter")
	const TMap<FGameplayTag, FPuzzleSignalState>& GetSignalStates() const;

private:
	/** Current state for each published signal tag. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Emitter", meta = (AllowPrivateAccess = "true"))
	TMap<FGameplayTag, FPuzzleSignalState> SignalStates;

	/** Prevents duplicate invalidation broadcasts during paired EndPlay/destroy paths. */
	bool bHasBroadcastInvalidated = false;

	/** Broadcasts invalidation once and marks every bound controller input invalid. */
	void BroadcastInvalidated();

	/**
	 * Broadcasts a changed or republished signal to Blueprint and native observers.
	 *
	 * @param SignalTag Gameplay tag identifying the signal channel.
	 * @param SignalState Updated state for the channel.
	 */
	void BroadcastSignalChanged(FGameplayTag SignalTag, const FPuzzleSignalState& SignalState);
};
