#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/ParadoxInventoryTypes.h"
#include "ParadoxPickupableActor.generated.h"

class AParadoxCharacter;
class AParadoxItemSlotActor;
class AParadoxPickupableActor;
class UGridNavigationOccupancyComponent;
class UParadoxInteractionComponent;
class UParadoxInventoryComponent;
class UParadoxDropAction;
class UParadoxPickupableAction;
class UParadoxPickupablePassiveEffect;
class UParadoxSelectableComponent;
class USceneComponent;
class USmartObjectComponent;
class UStaticMeshComponent;
class UWorldStateParticipantComponent;
class UTexture2D;
struct FWorldStateRestoreLifecycleContext;
struct FWorldStateRestoreResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxPickupableActionsChanged,
	AParadoxPickupableActor*, Pickupable);

/** Native, ready-to-author world object that participates in selection, interaction and reset. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxPickupableActor : public AActor
{
	GENERATED_BODY()

public:
	AParadoxPickupableActor();

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	EParadoxPickupableState GetPickupableState() const { return PickupableState; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	bool IsAvailableInWorld() const
	{
		return PickupableState == EParadoxPickupableState::World && CurrentHolder == nullptr;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	AParadoxCharacter* GetCurrentHolder() const { return CurrentHolder.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	TArray<UParadoxPickupableAction*> GetPickupableActions() const;

	/** Small presentation contract consumed by inventory/HUD widgets. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Presentation")
	FText GetPickupableDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Presentation")
	TSoftObjectPtr<UTexture2D> GetPickupableIcon() const { return PickupableIcon; }

	/** Replaces the runtime action catalog after filtering invalid and duplicate entries. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Actions")
	void SetPickupableActions(const TArray<UParadoxPickupableAction*>& NewActions);

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Inventory|Actions")
	FParadoxPickupableActionsChanged OnPickupableActionsChanged;

	const TArray<TObjectPtr<UParadoxPickupablePassiveEffect>>& GetPassiveEffects() const
	{
		return PassiveEffects;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UStaticMeshComponent* GetPickupableMesh() const { return PickupableMesh.Get(); }

	/** Final Actor transform used by both the local Drop preview and authoritative placement. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Placement")
	FTransform GetDropPlacementTransform(const FVector& CellWorldCenter) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxSelectableComponent* GetSelectableComponent() const { return SelectableComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxInteractionComponent* GetInteractionComponent() const { return InteractionComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	USmartObjectComponent* GetSmartObjectComponent() const { return SmartObjectComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UGridNavigationOccupancyComponent* GetOccupancyComponent() const { return OccupancyComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UWorldStateParticipantComponent* GetWorldStateParticipantComponent() const
	{
		return WorldStateParticipantComponent.Get();
	}

	/** Local half of the Paradox.Inventory.Debug gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Debug")
	bool bEnableDebug = false;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Applies the shared non-world capability policy without forcing held-item visibility. */
	void SetExternallyOwnedStateNative(EParadoxPickupableState NewState, bool bHideActor);

	/** Lets specialized pickupables clear non-inventory ownership before World State mutates Actors. */
	virtual void PrepareExternalOwnershipForWorldStateRestore();

	/** Rebuilds specialized ownership after World State properties restore. */
	virtual bool RestoreExternalOwnershipAfterWorldState();

	/** Default native presentation hides held Actors; designers may opt out and attach/show in the hook. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Presentation")
	bool bHideWhileHeld = true;

	/** Optional HUD label. Empty falls back to the Actor object name at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Presentation")
	FText PickupableDisplayName;

	/** Optional soft icon loaded only when this item is presented in an inventory widget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Presentation")
	TSoftObjectPtr<UTexture2D> PickupableIcon;

	/** Offset from the selected GridWorld floor center used for ordinary Drop placement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Placement")
	FVector DropPlacementOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Paradox|Inventory|Passive Effects")
	TArray<TObjectPtr<UParadoxPickupablePassiveEffect>> PassiveEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Actions")
	TArray<TObjectPtr<UParadoxPickupableAction>> PickupableActions;

	/** Derived runtime items call this after changing the protected action catalog directly. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Actions", meta = (BlueprintProtected = "true"))
	void NotifyPickupableActionsChanged();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory", meta = (DisplayName = "On Picked Up"))
	void ReceivePickedUp(AParadoxCharacter* NewHolder);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory", meta = (DisplayName = "On Dropped"))
	void ReceiveDropped(AParadoxCharacter* PreviousHolder);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory", meta = (DisplayName = "On Returned To Initial State"))
	void ReceiveReturnedToInitialState();

private:
	void CaptureInitialWorldPresentation();
	void EnforceNonBlockingPresence();
	void ApplyUnavailableWorldPresence(bool bHideActor);
	void ApplyHeldWorldPresence();
	void RestoreWorldPresence();
	void ClearSelectionPresentation();
	void SetHeldStateNative(AParadoxCharacter& NewHolder, bool bNotify);
	void SetWorldStateNative(const FTransform& WorldTransform, AParadoxCharacter* PreviousHolder, bool bNotifyDrop);
	void PrepareForWorldStateRestore();
	void FinishWorldStateRestore(bool bRestoreSucceeded);
	void LogDebugState(const TCHAR* EventName) const;
	void HandleWorldStateRestoreStarted(const FWorldStateRestoreLifecycleContext& Context);
	void HandleWorldStateRestoreCompleted(const FWorldStateRestoreResult& Result);
	void HandleWorldStateRestoreFailed(const FWorldStateRestoreResult& Result);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PickupableMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxSelectableComponent> SelectableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USmartObjectComponent> SmartObjectComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxInteractionComponent> InteractionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGridNavigationOccupancyComponent> OccupancyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipantComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxCharacter> CurrentHolder;

	UPROPERTY(Transient)
	EParadoxPickupableState PickupableState = EParadoxPickupableState::World;

	bool bInitialActorHidden = false;
	bool bInitialSelectableCanHover = true;
	bool bInitialSelectableCanSelect = true;
	bool bInitialSmartObjectEnabled = true;
	bool bInitialOccupancyEnabled = true;
	bool bInitialPresentationCaptured = false;

	friend class UParadoxInventoryComponent;
	friend class UParadoxDropAction;
	friend class AParadoxItemSlotActor;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxInventoryTestAccessor;
#endif
};
