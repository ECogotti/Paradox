// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GridWorldTypes.h"
#include "Interfaces/GridNavigationContributor.h"
#include "GridNavigationOccupancyComponent.generated.h"

/** Runtime occupancy source used by occupancy filters, goal contention, and traffic reservations. */
UCLASS(ClassGroup = (GridWorld), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class GRIDWORLD_API UGridNavigationOccupancyComponent : public USceneComponent, public IGridNavigationContributor
{
	GENERATED_BODY()

public:
	UGridNavigationOccupancyComponent();

	/** Stable runtime identity used to ignore an agent's own occupancy. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Grid World|Identity")
	FGuid OccupantId;

	/** Local half extent used to determine which generated cells are occupied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Shape", meta = (ClampMin = "0.0"))
	FVector BoxExtent = FVector(25.0, 25.0, 100.0);

	/** Makes affected cells impassable when the query filter uses Occupancy Policy = Block. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Occupancy")
	bool bBlocksWhenConsidered = true;

	/** Fixed-point cost applied when the query filter uses Occupancy Policy = Add Cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Occupancy", meta = (ClampMin = "0"))
	int32 AdditionalCost = 1000;

	/** Marks authored reservation occupancy separately from a live agent footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Reservation")
	bool bIsReservation = false;

	/** Enables local occupancy diagnostics when global GridWorld debug is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bEnableDebug = false;

	/** @return True when WorldPoint lies inside this component's oriented occupancy box. */
	bool AffectsPoint(const FVector& WorldPoint) const;
	/** Finds an active, non-reservation occupancy source owned by Actor. @return Borrowed component pointer or nullptr. */
	static UGridNavigationOccupancyComponent* FindActiveAgentOccupancy(const AActor& Actor);
	/** Resolves a live occupancy source by stable ID without scanning every Actor. @return Borrowed component pointer or nullptr. */
	static UGridNavigationOccupancyComponent* FindOccupantById(const UWorld& World, const FGuid& InOccupantId);
	UFUNCTION(BlueprintCallable, Category = "Grid World")
	void SetOccupancyEnabled(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Grid World")
	void RefreshOccupancy();
	virtual FBox GetGridContributionBounds_Implementation() const override;
	virtual bool IsGridContributionEnabled_Implementation() const override;
	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Binding removed symmetrically during unregistration. */
	FDelegateHandle TransformUpdatedHandle;
	/** Previously published cells, used to localize overlay change sets. */
	TSet<FGridCellId> CachedOccupiedCells;
	/** Creates a persistent identity when needed. @param bForceNewId Replaces an existing valid ID. */
	void EnsureStableId(bool bForceNewId = false);
	/** TransformUpdated callback used instead of Tick. */
	void HandleTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);
	/** Refreshes CachedOccupiedCells. @return True when the occupied set changed. */
	bool UpdateCachedOccupiedCells();
	/** Republishes occupancy to the authoritative Grid nav data. */
	void NotifyNavigationData() const;
};
	/** Enables or disables occupancy and republishes affected cells. @param bEnabled New component state. */
	/** Re-evaluates occupancy after programmatic shape/property changes. */
