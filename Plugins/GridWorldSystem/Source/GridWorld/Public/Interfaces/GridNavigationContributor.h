// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GridNavigationContributor.generated.h"

/** Reflection wrapper for objects that contribute runtime GridWorld data. */
UINTERFACE(BlueprintType)
class GRIDWORLD_API UGridNavigationContributor : public UInterface
{
	GENERATED_BODY()
};

/** Common inspection contract for runtime objects contributing links or overlays. */
class GRIDWORLD_API IGridNavigationContributor
{
	GENERATED_BODY()

public:
	/** @return World-space bounds used to localize overlay regeneration. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Grid World")
	FBox GetGridContributionBounds() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Grid World")
	bool IsGridContributionEnabled() const;
};
	/** @return True when this object's contribution should be composed. */
