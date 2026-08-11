#pragma once

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Interaction/ParadoxSelectionTypes.h"
#include "ParadoxSelectableComponent.generated.h"

class UParadoxInteractionWidgetBase;
class UParadoxSelectionComponent;
class UPrimitiveComponent;
class UWidgetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxSelectableHoverChanged,
	UParadoxSelectableComponent*, Selectable,
	bool, bIsHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxSelectableSelectionChanged,
	UParadoxSelectableComponent*, Selectable,
	bool, bIsSelected);

/** Adds hover, single-selection presentation, and an optional world-space widget to an Actor. */
UCLASS(ClassGroup = (Paradox), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxSelectableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxSelectableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection")
	bool bCanBeHovered = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection")
	bool bCanBeSelected = true;

	/** Shows owner-scoped GridWorld interaction cells while selected when an interaction catalog is present. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Interaction")
	bool bShowInteractionCellsWhenSelected = false;

	/** Shows the read-only PuzzleSystem circuit overlay while this Actor is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Puzzle Overlay")
	bool bShowPuzzleConnectionsWhenSelected = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Widget")
	TSubclassOf<UParadoxInteractionWidgetBase> SelectionWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Widget")
	FComponentReference WidgetAnchor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Widget")
	FVector WidgetRelativeOffset = FVector(0.0f, 0.0f, 100.0f);

	/** Keeps the world-space widget facing the selecting local player's camera while visible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Widget")
	bool bFaceOwningPlayerCamera = true;

	/** Rotation used when camera-facing is disabled. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Selection|Widget",
		meta = (EditCondition = "!bFaceOwningPlayerCamera"))
	FRotator WidgetRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Widget", meta = (ClampMin = "1"))
	FIntPoint WidgetDrawSize = FIntPoint(400, 160);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Selection|Debug")
	bool bEnableDebug = false;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Selection")
	FParadoxSelectableHoverChanged OnHoverChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Selection")
	FParadoxSelectableSelectionChanged OnSelectionChanged;

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	bool IsHovered() const { return bIsHovered; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	bool IsSelected() const { return bIsSelected; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	EParadoxSelectionPresentationState GetSelectionPresentationState() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Selection")
	UWidgetComponent* GetInteractionWidget() const { return InteractionWidgetComponent.Get(); }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Selection", meta = (DisplayName = "Handle Hover Changed"))
	void HandleHoverChanged(bool bNewHovered);
	virtual void HandleHoverChanged_Implementation(bool bNewHovered);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Selection", meta = (DisplayName = "Handle Selection Changed"))
	void HandleSelectionChanged(bool bNewSelected);
	virtual void HandleSelectionChanged_Implementation(bool bNewSelected);

private:
	struct FCachedPrimitiveRenderState
	{
		bool bRenderCustomDepth = false;
		int32 CustomDepthStencilValue = 0;
		ERendererStencilMask CustomDepthStencilWriteMask = ERendererStencilMask::ERSM_Default;
	};

	void SetHoveredFromSelection(bool bNewHovered);
	void SetSelectedFromSelection(bool bNewSelected, UParadoxSelectionComponent* InSelectionComponent);
	void ResetPresentationState();
	void ApplyPresentationState();
	void ApplyOutline(int32 StencilValue);
	void RestoreOutline();
	bool EnsureInteractionWidget(UParadoxSelectionComponent* InSelectionComponent);
	void ShowInteractionWidget(UParadoxSelectionComponent* InSelectionComponent);
	void HideInteractionWidget();
	void DestroyInteractionWidget();
	void UpdateInteractionWidgetFacing();

	bool bIsHovered = false;
	bool bIsSelected = false;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FCachedPrimitiveRenderState> CachedPrimitiveRenderStates;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> InteractionWidgetComponent = nullptr;

	TWeakObjectPtr<UParadoxSelectionComponent> ActiveSelectionComponent;

	friend class UParadoxSelectionComponent;
};
