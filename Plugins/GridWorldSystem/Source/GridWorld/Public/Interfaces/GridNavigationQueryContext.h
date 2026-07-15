// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GridNavigationQueryContext.generated.h"

/** Reflection wrapper for optional per-query GridWorld context. */
UINTERFACE(BlueprintType)
class GRIDWORLD_API UGridNavigationQueryContext : public UInterface
{
	GENERATED_BODY()
};

/** Optional per-querier context copied into an immutable Grid query filter on the Game Thread. */
class GRIDWORLD_API IGridNavigationQueryContext
{
	GENERATED_BODY()

public:
	/** @return Reservation identity allowed to overlap authored reservation occupancy. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Grid World")
	FGuid GetGridReservationId() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Grid World")
	uint8 GetGridTraversalChannel() const;
};
	/** @return Traversal channel index in the inclusive range 0-15. */
