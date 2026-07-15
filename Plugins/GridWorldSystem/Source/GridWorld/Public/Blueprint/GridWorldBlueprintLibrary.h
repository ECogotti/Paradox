// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GridWorldTypes.h"
#include "GridWorldBlueprintLibrary.generated.h"

class UNavigationQueryFilter;

/** Stateless Blueprint facade for querying the authoritative GridWorld navigation data. */
UCLASS()
class GRIDWORLD_API UGridWorldBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Projects a world point to the nearest navigable cell.
	 * @param WorldContextObject Object used to resolve the World and GridWorld subsystem.
	 * @param WorldLocation Point to project.
	 * @param Extent Symmetric X/Y/Z search extent in centimetres.
	 * @return Explicit status plus cell identity and floor data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid World", meta = (WorldContext = "WorldContextObject"))
	static FGridCellQueryResult ProjectPointToGrid(const UObject* WorldContextObject, const FVector& WorldLocation, const FVector& Extent = FVector(50.0, 50.0, 200.0));

	UFUNCTION(BlueprintCallable, Category = "Grid World", meta = (WorldContext = "WorldContextObject"))
	static FGridCellQueryResult GetGridCell(const UObject* WorldContextObject, const FGridCellId& CellId);

	UFUNCTION(BlueprintCallable, Category = "Grid World", meta = (WorldContext = "WorldContextObject"))
	static FGridPathQueryResult FindGridPath(
		const UObject* WorldContextObject,
		const FVector& Start,
		const FVector& Goal,
		TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr,
		bool bAllowPartialPath = true);

	UFUNCTION(BlueprintCallable, Category = "Grid World", meta = (WorldContext = "WorldContextObject"))
	static FGridReachabilityResult FindReachableGridCells(
		const UObject* WorldContextObject,
		const FVector& Origin,
		double MaxWorldDistance,
		int32 MaxCells = 4096,
		TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr);
};
	/**
	 * Resolves a persistent cell identity.
	 * @param WorldContextObject Object used to resolve the World.
	 * @param CellId Persistent cell identity to inspect.
	 * @return InvalidGrid or InvalidInput when the cell is unavailable.
	 */
	/**
	 * Finds a GridWorld path using the supplied filter class.
	 * @param WorldContextObject Object used to resolve the World.
	 * @param Start World-space start position.
	 * @param Goal World-space requested destination.
	 * @param FilterClass Optional Grid/Unreal navigation query filter class.
	 * @param bAllowPartialPath Returns the closest reachable prefix when true.
	 * @return Ordered cells, points, real cost, turns, and snapshot revisions.
	 */
	/**
	 * Finds cells reachable within a world-distance budget.
	 * @param WorldContextObject Object used to resolve the World.
	 * @param Origin World-space projection origin.
	 * @param MaxWorldDistance Maximum accumulated path length in centimetres.
	 * @param MaxCells Hard result/search bound.
	 * @param FilterClass Optional query filter class.
	 * @return Bounded read-only reachability result.
	 */
