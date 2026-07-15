// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/GridWorldBlueprintLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Subsystems/GridWorldSubsystem.h"

namespace UE::GridWorld::Private
{
	UGridWorldSubsystem* GetGridWorldSubsystem(const UObject* WorldContextObject)
	{
		UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
		return World != nullptr ? World->GetSubsystem<UGridWorldSubsystem>() : nullptr;
	}
}

FGridCellQueryResult UGridWorldBlueprintLibrary::ProjectPointToGrid(const UObject* WorldContextObject, const FVector& WorldLocation, const FVector& Extent)
{
	if (const UGridWorldSubsystem* Subsystem = UE::GridWorld::Private::GetGridWorldSubsystem(WorldContextObject))
	{
		return Subsystem->ProjectPoint(WorldLocation, Extent);
	}
	FGridCellQueryResult Result;
	Result.Status = EGridQueryStatus::InvalidGrid;
	return Result;
}

FGridCellQueryResult UGridWorldBlueprintLibrary::GetGridCell(const UObject* WorldContextObject, const FGridCellId& CellId)
{
	if (const UGridWorldSubsystem* Subsystem = UE::GridWorld::Private::GetGridWorldSubsystem(WorldContextObject))
	{
		return Subsystem->GetCell(CellId);
	}
	FGridCellQueryResult Result;
	Result.Status = EGridQueryStatus::InvalidGrid;
	return Result;
}

FGridPathQueryResult UGridWorldBlueprintLibrary::FindGridPath(const UObject* WorldContextObject, const FVector& Start, const FVector& Goal, TSubclassOf<UNavigationQueryFilter> FilterClass, bool bAllowPartialPath)
{
	if (const UGridWorldSubsystem* Subsystem = UE::GridWorld::Private::GetGridWorldSubsystem(WorldContextObject))
	{
		return Subsystem->FindPath(Start, Goal, FilterClass, bAllowPartialPath);
	}
	FGridPathQueryResult Result;
	Result.Status = EGridQueryStatus::InvalidGrid;
	return Result;
}

FGridReachabilityResult UGridWorldBlueprintLibrary::FindReachableGridCells(const UObject* WorldContextObject, const FVector& Origin, double MaxWorldDistance, int32 MaxCells, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	if (const UGridWorldSubsystem* Subsystem = UE::GridWorld::Private::GetGridWorldSubsystem(WorldContextObject))
	{
		return Subsystem->FindReachableCells(Origin, MaxWorldDistance, MaxCells, FilterClass);
	}
	FGridReachabilityResult Result;
	Result.Status = EGridQueryStatus::InvalidGrid;
	return Result;
}

