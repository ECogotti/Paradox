#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimeLoop/ParadoxTimeLoopTypes.h"
#include "Types/WorldStateTypes.h"
#include "ParadoxChronoSpawn.generated.h"

class UStaticMeshComponent;
class UParadoxSelectableComponent;
class UParadoxInteractionComponent;
class UPuzzleReceiverComponent;
class UWorldStateParticipantComponent;
class APuzzleController;
class AParadoxChronoSpawn;
struct FWorldStateRestoreResult;
enum class EPuzzleGraphTopologyChangeKind : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxChronoSpawnActivationChanged,
	AParadoxChronoSpawn*, ChronoSpawn,
	bool, bIsActive);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxChronoSpawnStateChanged,
	AParadoxChronoSpawn*, ChronoSpawn,
	EParadoxChronoSpawnState, PreviousState,
	EParadoxChronoSpawnState, NewState);

/** Designer-placeable entry point for one playable timeline. */
UCLASS(Blueprintable)
class PARADOX_API AParadoxChronoSpawn : public AActor
{
	GENERATED_BODY()

public:
	AParadoxChronoSpawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	EParadoxChronoSpawnState GetChronoSpawnState() const { return ChronoSpawnState; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	bool IsChronoSpawnEnabled() const { return bChronoSpawnEnabled; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	bool IsAvailableForSelection() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	bool IsChronoSpawnActive() const { return bChronoSpawnActive; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	bool IsAssignedToTimeline() const { return bAssignedToTimeline; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	bool CanAssignToNewTimeline() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	UParadoxSelectableComponent* GetSelectableComponent() const { return SelectableComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	UParadoxInteractionComponent* GetInteractionComponent() const { return InteractionComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	UPuzzleReceiverComponent* GetPuzzleReceiverComponent() const { return PuzzleReceiverComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	UWorldStateParticipantComponent* GetWorldStateParticipantComponent() const { return WorldStateParticipant.Get(); }

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Chrono Spawn")
	FParadoxChronoSpawnActivationChanged OnChronoSpawnActivationChanged;

	/** Presentation hook for external Blueprint/C++ effects. State is committed before broadcast. */
	UPROPERTY(BlueprintAssignable, Category = "Paradox|Chrono Spawn")
	FParadoxChronoSpawnStateChanged OnChronoSpawnStateChanged;

	/** Runtime/design-time switch. Disabling immediately makes this spawn unselectable. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Chrono Spawn")
	void SetChronoSpawnEnabled(bool bEnabled);

	/**
	 * Optional Actor-local presentation hook. The native implementation deliberately does nothing;
	 * authored Blueprint effects own all state presentation.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Chrono Spawn", meta = (DisplayName = "On Chrono Spawn Visual State Changed"))
	void ReceiveVisualStateChanged(EParadoxChronoSpawnState NewState);
	virtual void ReceiveVisualStateChanged_Implementation(
		EParadoxChronoSpawnState NewState);

	/**
	 * Reinitializes authored presentation from the already reconciled authoritative state.
	 * Called once at BeginPlay and after every successful Time Loop world reset, even when
	 * the state value itself did not change and ReceiveVisualStateChanged did not fire.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Chrono Spawn", meta = (DisplayName = "On Chrono Spawn State Initialized"))
	void ReceiveStateInitialized(EParadoxChronoSpawnState InitialState);
	virtual void ReceiveStateInitialized_Implementation(
		EParadoxChronoSpawnState InitialState);

private:
	void CommitChronoSpawnState(EParadoxChronoSpawnState NewState);
	void SetRuntimeState(EParadoxChronoSpawnState NewState);
	void SetAssignedToTimeline(bool bAssigned);
	void NotifyStateInitialized();
	void RefreshActivationState();
	void RefreshPresentationState();
	void HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bIsActive);
	void HandleGraphTopologyChanged(
		int64 GraphTopologyRevision,
		APuzzleController* AffectedController,
		EPuzzleGraphTopologyChangeKind ChangeKind);

	UFUNCTION()
	void HandleWorldStatePreRestore(FWorldStateParticipantId ParticipantId);

	UFUNCTION()
	void HandleWorldStatePropertiesRestored(FWorldStateParticipantId ParticipantId);

	void HandleWorldStateRestoreCompleted(const FWorldStateRestoreResult& Result);
	void HandleWorldStateRestoreFailed(const FWorldStateRestoreResult& Result);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> SelectionMesh;

	/** Project-level hover, selection, outline, and optional world-widget capability. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxSelectableComponent> SelectableComponent;

	/** Non-spatial semantic Spawn interaction; deliberately owns no Smart Object Component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxInteractionComponent> InteractionComponent;

	/** Automatic PuzzleSystem endpoint controlling runtime availability. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPuzzleReceiverComponent> PuzzleReceiverComponent;

	/**
	 * Restores only baseline-owned Actor state. Puzzle activation and timeline assignment remain
	 * derived from their respective authorities and are deliberately not captured properties.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipant;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	bool bChronoSpawnEnabled = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	EParadoxChronoSpawnState ChronoSpawnState = EParadoxChronoSpawnState::Available;

	UPROPERTY(Transient)
	bool bChronoSpawnActive = true;

	UPROPERTY(Transient)
	bool bAssignedToTimeline = false;

	bool bWorldStateRestoreInProgress = false;
	FDelegateHandle GraphTopologyChangedHandle;
	FDelegateHandle WorldStateRestoreCompletedHandle;
	FDelegateHandle WorldStateRestoreFailedHandle;

	friend class UParadoxTimeLoopComponent;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxTimeLoopTestAccessor;
#endif
};
