// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridNavDataGenerator.h"

#include "Navigation/GridNavigationData.h"

FGridNavDataGenerator::FGridNavDataGenerator(AGridNavigationData& InOwner)
	: Owner(&InOwner)
{
}

bool FGridNavDataGenerator::RebuildAll()
{
	AGridNavigationData* NavData = Owner.Get();
	if (NavData == nullptr || bIsBuilding)
	{
		return false;
	}
	TGuardValue<bool> BuildingGuard(bIsBuilding, true);
	return NavData->BuildFromWorld();
}

void FGridNavDataGenerator::RebuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas)
{
	AGridNavigationData* NavData = Owner.Get();
	if (NavData == nullptr || bIsBuilding)
	{
		return;
	}
	TGuardValue<bool> BuildingGuard(bIsBuilding, true);
	NavData->BuildDirtyAreas(DirtyAreas, true);
}

void FGridNavDataGenerator::OnNavigationBoundsChanged()
{
	RebuildAll();
}
