// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "Navigation/GridWorldSnapshot.h"

class UWorld;

/** Deterministically composes modifiers, links, occupancy, and reservations over generated topology. */
class FGridOverlayComposer
{
public:
	/** @param OutChangeSet Receives localized cell/link and revision changes. @return New immutable publication candidate. */
	static TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Compose(
		UWorld& World,
		const FGridWorldSnapshot& BaseTopology,
		const FGridWorldSnapshot* PreviousSnapshot,
		bool bOccupancyOnly,
		FGridChangeSet& OutChangeSet);
};
