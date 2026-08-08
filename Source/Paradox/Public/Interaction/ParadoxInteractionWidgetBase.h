#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionTypes.h"
#include "ParadoxInteractionWidgetBase.generated.h"

class AActor;
class APlayerController;
class APawn;
class UParadoxInteractionComponent;
class UParadoxSelectableComponent;
class UParadoxSelectionComponent;

/**
 * Presentation-only base for selected-Actor world widgets.
 *
 * Concrete Blueprint widgets read one selected presentation cache and submit through the same
 * UParadoxInteractionComponent request path used by AI.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class PARADOX_API UParadoxInteractionWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	AActor* GetSelectedActor() const { return SelectedActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	UParadoxSelectableComponent* GetSelectableComponent() const { return SelectableComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	UParadoxSelectionComponent* GetSelectionComponent() const { return SelectionComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	APlayerController* GetOwningPlayerController() const { return OwningPlayerController.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	APawn* GetCurrentRequester() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	UParadoxInteractionComponent* GetInteractionComponent() const
	{
		return InteractionComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	FParadoxInteractionQueryResult GetInteractionOptions() const
	{
		return CachedInteractionOptions;
	}

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	void RefreshInteractionOptions();

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	FParadoxInteractionAvailabilityResult GetInteractionAvailability(FGameplayTag InteractionTag) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	TArray<FParadoxInteractionAvailabilityResult> GetInteractionAvailabilities() const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	void RefreshInteractionAvailability();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	bool CanRequestInteraction(FGameplayTag InteractionTag) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	FParadoxInteractionRequestResult RequestInteraction(FGameplayTag InteractionTag);

protected:
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Selection", meta = (DisplayName = "On Selection Context Assigned"))
	void OnSelectionContextAssigned();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Selection", meta = (DisplayName = "On Selection Context Cleared"))
	void OnSelectionContextCleared();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (DisplayName = "On Interaction Options Refreshed"))
	void OnInteractionOptionsRefreshed(
		const FParadoxInteractionQueryResult& InteractionOptions);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (DisplayName = "On Interaction Availability Refreshed"))
	void OnInteractionAvailabilityRefreshed(
		const TArray<FParadoxInteractionAvailabilityResult>& InteractionAvailabilities);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (DisplayName = "On Interaction Request Accepted"))
	void OnInteractionRequestAccepted(
		const FParadoxInteractionRequestResult& RequestResult);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Interaction", meta = (DisplayName = "On Interaction Request Rejected"))
	void OnInteractionRequestRejected(
		const FParadoxInteractionRequestResult& RequestResult);

private:
	void AssignSelectionContext(
		AActor* InSelectedActor,
		UParadoxSelectableComponent* InSelectableComponent,
		UParadoxSelectionComponent* InSelectionComponent,
		APlayerController* InOwningPlayerController);
	void ClearSelectionContext();
	void HandleInteractionOptionsRefreshed(
		const FParadoxInteractionQueryResult& InteractionOptions);

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> SelectedActor;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxSelectableComponent> SelectableComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxSelectionComponent> SelectionComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> OwningPlayerController;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxInteractionComponent> InteractionComponent;

	UPROPERTY(Transient)
	FParadoxInteractionQueryResult CachedInteractionOptions;

	FDelegateHandle InteractionOptionsRefreshedHandle;

	friend class UParadoxSelectableComponent;
};
