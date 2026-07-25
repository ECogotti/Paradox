// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridNavigationPath.h"

const FNavPathType FGridNavigationPath::Type;

FGridNavigationPath::FGridNavigationPath()
{
	PathType = Type;
}

void FGridNavigationPath::ResetForRepath()
{
	Super::ResetForRepath();
	CellPath.Reset();
	NodePath.Reset();
	SegmentCosts.Reset();
	TraversedLinks.Reset();
	PathPointFollowingData.Reset();
	CellPathPointOffset = 0;
	Revisions = FGridRevisionSet();
	TotalLength = 0.0;
	MaximumFloorSlopeDegrees = 0.0f;
	OptimizationMode = EGridPathOptimizationMode::ShortestPath;
	TurnCount = 0;
	VisitedNodes = 0;
	OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	DynamicAgentPolicy = EGridDynamicAgentPolicy::Ignore;
	MinimumAgentLookAheadCells = 3;
	ReservedLookAheadCells = 3;
	AdditionalAgentSeparation = 5.0f;
	StationaryAgentSpeedThreshold = 5.0f;
	DynamicAgentRepathDelay = 0.35f;
	IgnoredOccupancyOwnerId.Invalidate();
	TrafficReservationRevision = 0;
	bUsedDynamicAgentFallback = false;
	Origin = EGridNavigationPathOrigin::Computed;
	PathInstanceId.Invalidate();
	ParentPathInstanceId.Invalidate();
	SourcePreviewId.Invalidate();
	InjectedInvalidationPolicy = EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;
	bAllowDynamicAgentConflictsDuringValidation = false;
}

FVector::FReal FGridNavigationPath::GetCostFromIndex(int32 PathPointIndex) const
{
	FVector::FReal Cost = 0.0;
	const int32 FirstCellSegment = FMath::Max(0, PathPointIndex - CellPathPointOffset);
	for (int32 SegmentIndex = FirstCellSegment; SegmentIndex < SegmentCosts.Num(); ++SegmentIndex)
	{
		Cost += SegmentCosts[SegmentIndex];
	}
	return Cost;
}

FVector::FReal FGridNavigationPath::GetCostFromNode(NavNodeRef PathNode) const
{
	const int32 NodeIndex = NodePath.Find(PathNode);
	return NodeIndex != INDEX_NONE ? GetCostFromIndex(NodeIndex + CellPathPointOffset) : 0.0;
}

bool FGridNavigationPath::ContainsNode(NavNodeRef NodeRef) const
{
	return NodePath.Contains(NodeRef);
}

const FGridPathPointFollowingData* FGridNavigationPath::GetFollowingData(int32 PathPointIndex) const
{
	return PathPointFollowingData.IsValidIndex(PathPointIndex)
		? &PathPointFollowingData[PathPointIndex]
		: nullptr;
}

bool FGridNavigationPath::RequiresPrecisePathFollowing() const
{
	return PathPointFollowingData.ContainsByPredicate([](const FGridPathPointFollowingData& Data)
	{
		return Data.Style != EGridPathFollowingStyle::Standard;
	});
}

int32 FGridNavigationPath::GetNextFollowingTargetIndex(int32 StartIndex) const
{
	const TArray<FNavPathPoint>& Points = GetPathPoints();
	if (!Points.IsValidIndex(StartIndex) || StartIndex >= Points.Num() - 1)
	{
		return INDEX_NONE;
	}

	for (int32 CandidateIndex = StartIndex + 1; CandidateIndex < Points.Num(); ++CandidateIndex)
	{
		const FGridPathPointFollowingData* Data = GetFollowingData(CandidateIndex);
		if (Data == nullptr
			|| !Data->bIsCellCenter
			|| Data->Style == EGridPathFollowingStyle::Standard
			|| Data->bRequiresCenterGate
			|| Data->bRequiresStop)
		{
			return CandidateIndex;
		}
	}

	return Points.Num() - 1;
}

bool FGridNavigationPath::GetCenterGateGeometry(
	int32 StartIndex,
	int32 GateIndex,
	FGridCenterGateDebugData& OutGate) const
{
	const TArray<FNavPathPoint>& Points = GetPathPoints();
	const FGridPathPointFollowingData* GateData = GetFollowingData(GateIndex);
	if (!Points.IsValidIndex(StartIndex)
		|| !Points.IsValidIndex(GateIndex)
		|| GateIndex <= StartIndex
		|| GateData == nullptr
		|| !GateData->bRequiresCenterGate)
	{
		return false;
	}

	const FQuat GridRotation = GateData->GridRotation.Quaternion();
	const FQuat WorldToGrid = GridRotation.Inverse();
	FVector LocalForward3D = WorldToGrid.RotateVector(Points[GateIndex].Location - Points[StartIndex].Location);
	FVector2D LocalForward(LocalForward3D.X, LocalForward3D.Y);
	if (LocalForward.IsNearlyZero())
	{
		for (int32 PointIndex = GateIndex - 1; PointIndex >= 0 && LocalForward.IsNearlyZero(); --PointIndex)
		{
			LocalForward3D = WorldToGrid.RotateVector(Points[GateIndex].Location - Points[PointIndex].Location);
			LocalForward = FVector2D(LocalForward3D.X, LocalForward3D.Y);
		}
	}
	if (LocalForward.IsNearlyZero())
	{
		for (int32 PointIndex = GateIndex + 1; PointIndex < Points.Num() && LocalForward.IsNearlyZero(); ++PointIndex)
		{
			LocalForward3D = WorldToGrid.RotateVector(Points[PointIndex].Location - Points[GateIndex].Location);
			LocalForward = FVector2D(LocalForward3D.X, LocalForward3D.Y);
		}
	}
	LocalForward = LocalForward.GetSafeNormal();
	if (LocalForward.IsNearlyZero())
	{
		return false;
	}

	const FVector LocalTangent(-LocalForward.Y, LocalForward.X, 0.0f);
	OutGate.Center = Points[GateIndex].Location;
	OutGate.Forward = GridRotation.RotateVector(FVector(LocalForward.X, LocalForward.Y, 0.0f)).GetSafeNormal();
	OutGate.Tangent = GridRotation.RotateVector(LocalTangent).GetSafeNormal();
	OutGate.Up = GridRotation.RotateVector(FVector::UpVector).GetSafeNormal();
	OutGate.HalfWidth = FMath::Max(0.1f, GateData->CenterGateHalfWidth);
	OutGate.Style = GateData->Style;
	return true;
}

void FGridNavigationPath::GetCenterGateDebugData(TArray<FGridCenterGateDebugData>& OutGates) const
{
	OutGates.Reset();
	const TArray<FNavPathPoint>& Points = GetPathPoints();
	int32 StartIndex = 0;
	while (Points.IsValidIndex(StartIndex) && StartIndex < Points.Num() - 1)
	{
		const int32 TargetIndex = GetNextFollowingTargetIndex(StartIndex);
		if (!Points.IsValidIndex(TargetIndex) || TargetIndex <= StartIndex)
		{
			break;
		}

		FGridCenterGateDebugData Gate;
		if (GetCenterGateGeometry(StartIndex, TargetIndex, Gate))
		{
			OutGates.Add(Gate);
		}
		StartIndex = TargetIndex;
	}
}

bool FGridNavigationPath::GetDriveDebugData(FGridPathDriveDebugData& OutDriveData) const
{
	const TArray<FNavPathPoint>& Points = GetPathPoints();
	if (Points.IsEmpty())
	{
		return false;
	}

	for (const FGridPathPointFollowingData& Data : PathPointFollowingData)
	{
		if (Data.bIsCellCenter && Data.Style != EGridPathFollowingStyle::Standard)
		{
			OutDriveData.Location = Points[0].Location;
			OutDriveData.DriveMode = Data.DriveMode;
			OutDriveData.bUseAcceleratedFinalApproach = Data.bUseAcceleratedFinalApproach;
			return true;
		}
	}
	return false;
}

void FGridNavigationPath::GetRequiredStopLocations(TArray<FVector>& OutLocations) const
{
	OutLocations.Reset();
	const TArray<FNavPathPoint>& Points = GetPathPoints();
	for (int32 PointIndex = 0; PointIndex < PathPointFollowingData.Num() && PointIndex < Points.Num(); ++PointIndex)
	{
		if (PathPointFollowingData[PointIndex].bRequiresStop)
		{
			OutLocations.Add(Points[PointIndex].Location);
		}
	}
}
