// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AI/NavDataGenerator.h"

class AGridNavigationData;

/** Synchronous Game-Thread generator. Published snapshots are safe for async query threads. */
class FGridNavDataGenerator final : public FNavDataGenerator
{
public:
	explicit FGridNavDataGenerator(AGridNavigationData& InOwner);
	virtual bool RebuildAll() override;
	virtual void RebuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas) override;
	virtual void OnNavigationBoundsChanged() override;
	virtual bool IsBuildInProgressCheckDirty() const override { return bIsBuilding; }

private:
	/** Weak owner avoids callbacks into destroyed navigation data during world teardown. */
	TWeakObjectPtr<AGridNavigationData> Owner;
	/** Synchronous reentrancy guard exposed through IsBuildInProgressCheckDirty. */
	bool bIsBuilding = false;
};
