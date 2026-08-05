// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Interfaces/GridNavigationContributor.h"
#include "GridNavigationModifierComponent.generated.h"

/** Authored runtime overlay that blocks, unblocks, or changes traversal costs inside a box. */
UCLASS(ClassGroup = (GridWorld), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class GRIDWORLD_API UGridNavigationModifierComponent : public USceneComponent, public IGridNavigationContributor
{
	GENERATED_BODY()

public:
	UGridNavigationModifierComponent();

	/** Stable identity used to resolve deterministic priority ties across rebuilds. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Grid World|Identity")
	FGuid ModifierId;

	/** Local half extent of the box that contributes the modifier, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Shape", meta = (ClampMin = "0.0"))
	FVector BoxExtent = FVector(50.0, 50.0, 100.0);

	/** When enabled, affected cells are unavailable to pathfinding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Blocking")
	bool bBlockCells = true;

	/** Requests removal of authored blocks explicitly marked as removable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Blocking")
	bool bRequestAuthoredUnblock = false;

	/** Fixed-point cost added before multipliers are applied. One ordinary cell costs 1000. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cost")
	int32 AdditiveCost = 0;

	/** Multiplies the cost after all additive contributions have been accumulated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cost", meta = (ClampMin = "0.001"))
	double CostMultiplier = 1.0;

	/** Enables an absolute traversal-cost override resolved by Priority and ModifierId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cost")
	bool bUseCostOverride = false;

	/** Absolute fixed-point traversal cost used when the override wins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cost", meta = (EditCondition = "bUseCostOverride", ClampMin = "1"))
	int32 OverrideCost = 1000;

	/** Higher values win cost-override conflicts; stable identity breaks equal-priority ties. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cost")
	int32 Priority = 0;

	/** Enables local modifier diagnostics when the global GridWorld debug switch is also enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bEnableDebug = false;

	/** @return True when WorldPoint lies inside this component's oriented box. */
	bool AffectsPoint(const FVector& WorldPoint) const;
	/** Changes blocking and immediately republishes the runtime overlay. @param bInBlockCells New blocking state. */
	UFUNCTION(BlueprintCallable, Category = "Grid World")
	void SetBlockingEnabled(bool bInBlockCells);
	UFUNCTION(BlueprintCallable, Category = "Grid World")
	void RefreshModifier();
	virtual FBox GetGridContributionBounds_Implementation() const override;
	virtual bool IsGridContributionEnabled_Implementation() const override;

	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Binding used to remove TransformUpdated symmetrically during unregistration. */
	FDelegateHandle TransformUpdatedHandle;
	/** Ensures a persistent identity; duplication may request a new one. @param bForceNewId Forces regeneration even when the current ID is valid. */
	void EnsureStableId(bool bForceNewId = false);
	/** TransformUpdated callback that republishes only the affected overlay. */
	void HandleTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);
	/** Notifies every Grid nav data instance in this World that the overlay changed. */
	void NotifyNavigationData() const;
};
	/** Re-evaluates affected cells after programmatic property changes. */
