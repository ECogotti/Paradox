// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridWorldTypes.h"
#include "GridWorldSubsystem.generated.h"

class AGridNavigationData;
class UNavigationQueryFilter;

/** Broadcast after a topology or runtime overlay publication. ChangeSet identifies the affected cells and revisions. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGridWorldChanged, const FGridChangeSet&, ChangeSet);

/** Read/query facade for the authoritative Grid navigation data in this world. */
UCLASS()
class GRIDWORLD_API UGridWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Designer-facing notification for localized topology and overlay changes. */
	UPROPERTY(BlueprintAssignable, Category = "Grid World")
	FOnGridWorldChanged OnGridWorldChanged;

	UFUNCTION(BlueprintPure, Category = "Grid World")
	AGridNavigationData* GetNavigationData() const;

	UFUNCTION(BlueprintCallable, Category = "Grid World")
	FGridCellQueryResult ProjectPoint(const FVector& WorldLocation, const FVector& Extent = FVector(50.0, 50.0, 200.0)) const;

	UFUNCTION(BlueprintCallable, Category = "Grid World")
	FGridCellQueryResult GetCell(const FGridCellId& CellId) const;

	UFUNCTION(BlueprintCallable, Category = "Grid World")
	FGridPathQueryResult FindPath(
		const FVector& Start,
		const FVector& Goal,
		TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr,
		bool bAllowPartialPath = true) const;

	UFUNCTION(BlueprintCallable, Category = "Grid World")
	FGridReachabilityResult FindReachableCells(
		const FVector& Origin,
		double MaxWorldDistance,
		int32 MaxCells = 4096,
		TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr) const;
};
	/** @return Borrowed authoritative Grid nav data for this World, or nullptr when unavailable. */
	/** Projects WorldLocation within Extent. @return Cell query result with an explicit failure status. */
	/** Resolves CellId against the current immutable snapshot. @return Stale/invalid status when unavailable. */
	/** Finds a path from Start to Goal. FilterClass controls optimization/traffic; bAllowPartialPath permits the closest reachable prefix. */
	/** Enumerates cells reachable from Origin within MaxWorldDistance and MaxCells using optional FilterClass. */
