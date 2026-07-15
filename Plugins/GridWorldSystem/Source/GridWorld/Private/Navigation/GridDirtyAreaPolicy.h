// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AI/Navigation/NavigationDirtyArea.h"

namespace UE::GridWorld::Private
{
	/** @return True only for automatic geometry dirtiness, excluding explicit navigation-bounds changes. */
	inline bool IsAutomaticGeometryChange(const FNavigationDirtyArea& DirtyArea, bool bRespectGeometryAutoRebuild)
	{
		return bRespectGeometryAutoRebuild
			&& DirtyArea.HasFlag(ENavigationDirtyFlag::Geometry)
			&& !DirtyArea.HasFlag(ENavigationDirtyFlag::NavigationBounds);
	}

	/** @return Whether the affected region should rebuild under its local geometry-auto-rebuild setting. */
	inline bool ShouldRebuildRegionForDirtyArea(
		const FNavigationDirtyArea& DirtyArea,
		bool bRespectGeometryAutoRebuild,
		bool bRegionAutoRebuildsGeometry)
	{
		return !IsAutomaticGeometryChange(DirtyArea, bRespectGeometryAutoRebuild)
			|| bRegionAutoRebuildsGeometry;
	}
}
