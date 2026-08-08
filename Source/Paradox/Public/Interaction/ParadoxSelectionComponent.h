#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionTypes.h"
#include "Presentation/GridCellOverlayPresentationTypes.h"
#include "ParadoxSelectionComponent.generated.h"

class AActor;
class AGridNavigationData;
class UParadoxInteractionComponent;
class UParadoxSelectableComponent;
class UGameplayActionComponent;
struct FGameplayActionEvent;
struct FHitResult;
struct FWorldStateRestoreLifecycleContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxHoveredActorChanged,
	AActor*, PreviousActor,
	AActor*, NewActor);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FParadoxSelectedInteractionOptionsRefreshedNative,
	const FParadoxInteractionQueryResult&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxSelectedActorChanged,
	AActor*, PreviousActor,
	AActor*, NewActor);

/** Player-owned authority for one hovered and one selected world Actor. */
UCLASS(ClassGroup = (Paradox), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxSelectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxSelectionComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection")
	bool bSelectionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Debug")
	bool bEnableDebug = false;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Selection")
	FParadoxHoveredActorChanged OnHoveredActorChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Selection")
	FParadoxSelectedActorChanged OnSelectedActorChanged;

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	AActor* GetHoveredActor() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	AActor* GetSelectedActor() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	bool IsSelectionEnabled() const { return bSelectionEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Paradox|Selection")
	void SetSelectionEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Selection")
	void DeselectCurrentActor();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Selection")
	void ResetSelectionState();

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection|Interaction")
	UParadoxInteractionComponent* GetSelectedInteractionComponent() const
	{
		return SelectedInteractionComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection|Interaction")
	FParadoxInteractionQueryResult GetSelectedInteractionOptions() const
	{
		return CachedSelectedInteractionOptions;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection|Interaction")
	FParadoxInteractionAvailabilityResult GetSelectedInteractionAvailability(FGameplayTag InteractionTag) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection|Interaction")
	TArray<FParadoxInteractionAvailabilityResult> GetSelectedInteractionAvailabilities() const;

	/** Refreshes only the currently selected Actor's event-driven interaction presentation. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Selection|Interaction")
	void RefreshSelectedInteractionOptions();

	FParadoxSelectedInteractionOptionsRefreshedNative&
	OnSelectedInteractionOptionsRefreshedNative()
	{
		return SelectedInteractionOptionsRefreshedNative;
	}

	/** Updates hover from the controller's shared cursor query. */
	void UpdateHoverFromHitResult(const FHitResult& HitResult, bool bHitSuccessful);

	/** Applies RMB select/toggle semantics; a hit without a selectable clears the current selection. */
	bool HandleSelectionPointerHit(const FHitResult& HitResult, bool bHitSuccessful);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UParadoxSelectableComponent* ResolveSelectable(
		const FHitResult& HitResult,
		bool bHitSuccessful,
		bool bRequireHover,
		bool bRequireSelection) const;
	void SetHoveredSelectable(UParadoxSelectableComponent* NewHoveredSelectable);
	void SetSelectedSelectable(UParadoxSelectableComponent* NewSelectedSelectable);
	void HandleSelectableEndingPlay(UParadoxSelectableComponent* Selectable);
	void BeginInteractionCellPresentation(UParadoxSelectableComponent* SelectedSelectable);
	void EndInteractionCellPresentation();
	void RefreshInteractionCellPresentation();
	void ReconcileTrafficReservationBinding();
	void HandleInteractionAffordanceChanged(UParadoxInteractionComponent* InteractionComponent);
	UFUNCTION()
	void HandleGridWorldChanged(const FGridChangeSet& ChangeSet);
	void HandleTrafficReservationsChanged();
	UFUNCTION()
	void HandleRequesterActionEvent(const FGameplayActionEvent& Event);
	void HandleWorldStateRestoreStarted(const FWorldStateRestoreLifecycleContext& Context);

	TWeakObjectPtr<UParadoxSelectableComponent> CurrentHoveredSelectable;
	TWeakObjectPtr<UParadoxSelectableComponent> CurrentSelectedSelectable;
	TWeakObjectPtr<UParadoxInteractionComponent> SelectedInteractionComponent;
	TWeakObjectPtr<AGridNavigationData> BoundGridNavigationData;
	TWeakObjectPtr<UGameplayActionComponent> BoundRequesterActionComponent;
	FGridCellOverlayPresentationHandle InteractionCellPresentationHandle;
	FDelegateHandle InteractionAffordanceChangedHandle;
	FDelegateHandle TrafficReservationsChangedHandle;
	FDelegateHandle WorldStateRestoreStartedHandle;
	FParadoxInteractionQueryResult CachedSelectedInteractionOptions;
	TMap<FGameplayTag, FParadoxInteractionAvailabilityResult> CachedInteractionAvailabilityByTag;
	FParadoxSelectedInteractionOptionsRefreshedNative
		SelectedInteractionOptionsRefreshedNative;
	bool bRefreshingInteractionCells = false;

	friend class UParadoxSelectableComponent;
};
