// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/GridWorldPathFollowingComponent.h"

#include "AI/GridCenterGate.h"
#include "AI/GridPathDrive.h"
#include "AI/GridWorldAIController.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/NavMovementInterface.h"
#include "GameFramework/Pawn.h"
#include "GridWorldModule.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridTrafficReservation.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Presentation/GridPathPresentationSubsystem.h"

namespace
{
	bool IsPreciseFollowingData(const FGridPathPointFollowingData* FollowingData)
	{
		return FollowingData != nullptr
			&& FollowingData->bIsCellCenter
			&& FollowingData->Style != EGridPathFollowingStyle::Standard;
	}
}

void UGridWorldPathFollowingComponent::SetActivePathPresentationEnabled(bool bEnabled)
{
	if (bPresentActivePath == bEnabled)
	{
		return;
	}
	bPresentActivePath = bEnabled;
	if (bPresentActivePath)
	{
		SynchronizePathPresentation(EGridPathFollowingPresentationChange::Accepted, false);
	}
	else
	{
		ReleasePathPresentation(false);
	}
}

void UGridWorldPathFollowingComponent::SetActivePathPresentationSettings(
	EGridPathProgressPresentationMode ProgressMode,
	int32 Priority)
{
	ActivePathPresentationMode = ProgressMode;
	ActivePathPresentationPriority = Priority;
	if (UWorld* World = GetWorld())
	{
		if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
		{
			if (Presentation->IsPathPresentationValid(ActivePathPresentationHandle))
			{
				Presentation->SetPathPresentationMode(ActivePathPresentationHandle, ProgressMode);
				Presentation->SetPathPresentationPriority(ActivePathPresentationHandle, Priority);
			}
		}
	}
}

void UGridWorldPathFollowingComponent::SetActivePathPresentationRenderers(bool bCellOverlay, bool bLine)
{
	bPresentActivePathAsCellOverlay = bCellOverlay;
	bPresentActivePathAsLine = bLine;
	if (UWorld* World = GetWorld())
	{
		if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
		{
			if (Presentation->IsPathPresentationValid(ActivePathPresentationHandle))
			{
				Presentation->SetPathPresentationRenderers(
					ActivePathPresentationHandle,
					bPresentActivePathAsCellOverlay,
					bPresentActivePathAsLine);
			}
		}
	}
}

void UGridWorldPathFollowingComponent::Cleanup()
{
	ReleasePathPresentation(true);
	UnbindPresentationPathObserver();
	ReleaseTrafficCorridor(false);
	ClearDynamicAgentDebug();
	ResetDynamicAgentAvoidance(true);
	PawnOccupancyComponent.Reset();
	RestoreDrivePolicy();
	Super::Cleanup();
}

void UGridWorldPathFollowingComponent::OnPathFinished(const FPathFollowingResult& Result)
{
	if (!LastPresentedPathCells.IsEmpty())
	{
		OnGridPathPresentationFinished.Broadcast(Result.Code, static_cast<int32>(Result.Flags));
	}
	ReleasePathPresentation(true);
	UnbindPresentationPathObserver();
	const FGridPathPointFollowingData* FollowingData = GetFollowingData(MoveSegmentEndIndex);
	if (IsPreciseFollowingData(FollowingData)
		&& FollowingData->DriveMode == EGridPathDriveMode::DirectVelocity
		&& NavMovementInterface.IsValid())
	{
		NavMovementInterface->StopMovementKeepPathing();
	}
	ReleaseTrafficCorridor(true);
	ClearDynamicAgentDebug();
	ResetDynamicAgentAvoidance(true);
	Super::OnPathFinished(Result);
}

void UGridWorldPathFollowingComponent::OnPathUpdated()
{
	const FNavPathSharedPtr UpdatedPath = GetPath();
	const FNavPathSharedPtr PreviouslyObservedPath = PresentationObservedPath.Pin();
	const EGridPathFollowingPresentationChange PresentationChange = LastPresentedPathCells.IsEmpty()
		? EGridPathFollowingPresentationChange::Accepted
		: (PreviouslyObservedPath.Get() == UpdatedPath.Get()
			? EGridPathFollowingPresentationChange::Recalculated
			: EGridPathFollowingPresentationChange::Replaced);

	const bool bPreserveRepathMemory = bDynamicAgentRepathPending;
	ReleaseTrafficCorridor(true);
	ResetTrafficProgress();
	ClearDynamicAgentDebug();
	ResetDynamicAgentAvoidance(!bPreserveRepathMemory);
	RestoreDrivePolicy();
	ResetPreviousLocation();

	const FGridNavigationPath* GridPath = GetGridPath();
	const FGridPathPointFollowingData* FinalData = GridPath != nullptr
		? GridPath->GetFollowingData(GridPath->GetPathPoints().Num() - 1)
		: nullptr;
	bStrictFinalPath = IsPreciseFollowingData(FinalData);
	bMoveToGoalOnLastSegment = !bStrictFinalPath;
	bCollidedWithGoal = false;

	Super::OnPathUpdated();
	BindPresentationPathObserver(UpdatedPath);
	SynchronizePathPresentation(PresentationChange, true);
}

void UGridWorldPathFollowingComponent::SetNavMovementInterface(INavMovementInterface* NavMoveInterface)
{
	ReleasePathPresentation(true);
	UnbindPresentationPathObserver();
	ReleaseTrafficCorridor(true);
	ResetTrafficProgress();
	ClearDynamicAgentDebug();
	ResetDynamicAgentAvoidance(true);
	RestoreDrivePolicy();
	Super::SetNavMovementInterface(NavMoveInterface);
	ResetPreviousLocation();
}

void UGridWorldPathFollowingComponent::Reset()
{
	ReleasePathPresentation(true);
	UnbindPresentationPathObserver();
	ReleaseTrafficCorridor(true);
	ResetTrafficProgress();
	ClearDynamicAgentDebug();
	ResetDynamicAgentAvoidance(true);
	RestoreDrivePolicy();
	bStrictFinalPath = false;
	bMoveToGoalOnLastSegment = true;
	bHasPreviousFeetLocation = false;
	PreviousFeetLocation = FVector::ZeroVector;
	Super::Reset();
}

void UGridWorldPathFollowingComponent::OnNewPawn(APawn* NewPawn)
{
	ReleasePathPresentation(true);
	UnbindPresentationPathObserver();
	ReleaseTrafficCorridor(false);
	ResetTrafficProgress();
	ClearDynamicAgentDebug();
	ResetDynamicAgentAvoidance(true);
	PawnOccupancyComponent.Reset();
	RestoreDrivePolicy();
	Super::OnNewPawn(NewPawn);
	EnsurePawnOccupancy(NewPawn);
	ResetPreviousLocation();
}

void UGridWorldPathFollowingComponent::SetMoveSegment(int32 SegmentStartIndex)
{
	Super::SetMoveSegment(SegmentStartIndex);
	UpdatePathPresentationProgress();
	ApplyDrivePolicy();
	ResetPreviousLocation();
}

void UGridWorldPathFollowingComponent::FollowPathSegment(float DeltaTime)
{
	UpdatePathPresentationProgress();
	if (bYieldingToAgent)
	{
		ApplyDynamicAgentYield();
		return;
	}

	const FGridPathPointFollowingData* FollowingData = GetFollowingData(MoveSegmentEndIndex);
	if (!IsPreciseFollowingData(FollowingData))
	{
		Super::FollowPathSegment(DeltaTime);
		return;
	}

	ApplyDrivePolicy();
	if (!ShouldUseAcceleratedDrive(*FollowingData))
	{
		if (!Path.IsValid() || !NavMovementInterface.IsValid() || DeltaTime <= UE_SMALL_NUMBER)
		{
			return;
		}

		const FVector CurrentLocation = NavMovementInterface->GetFeetLocation();
		const FVector CurrentTarget = GetCurrentTargetLocation();
		const bool bFollowingFinalPoint = MoveSegmentEndIndex == Path->GetPathPoints().Num() - 1;
		if (bFollowingFinalPoint
			&& FollowingData->bRequiresStop
			&& IsWithinCenterTolerance(CurrentLocation, CurrentTarget, *FollowingData, false))
		{
			bIsDecelerating = false;
			CurrentMoveInput = FVector::ZeroVector;
			NavMovementInterface->StopMovementKeepPathing();
			return;
		}

		const float MaxSpeed = FMath::Max(0.0f, NavMovementInterface->GetMaxSpeedForNavMovement());
		FVector MoveVelocity = UE::GridWorld::Private::CalculateDirectMoveVelocity(
			CurrentLocation,
			CurrentTarget,
			DeltaTime,
			MaxSpeed,
			bFollowingFinalPoint);
		bIsDecelerating = false;
		CurrentMoveInput = MoveVelocity;
		PostProcessMove.ExecuteIfBound(this, MoveVelocity);
		NavMovementInterface->RequestDirectMove(MoveVelocity, !bFollowingFinalPoint);
		return;
	}

	if (!FollowingData->bRequiresStop || !Path.IsValid() || !NavMovementInterface.IsValid())
	{
		Super::FollowPathSegment(DeltaTime);
		return;
	}
	if (!NavMovementInterface->UseAccelerationForPathFollowing())
	{
		Super::FollowPathSegment(DeltaTime);
		return;
	}

	const FVector CurrentLocation = NavMovementInterface->GetFeetLocation();
	const FVector CurrentTarget = GetCurrentTargetLocation();
	bIsDecelerating = false;
	CurrentMoveInput = (CurrentTarget - CurrentLocation).GetSafeNormal();

	const float MaxSpeed = FMath::Max(0.0f, NavMovementInterface->GetMaxSpeedForNavMovement());
	const float BrakingDistance = FMath::Max(
		FollowingData->CellCenterTolerance,
		NavMovementInterface->GetPathFollowingBrakingDistance(MaxSpeed));
	const FVector::FReal DistanceToTargetSquared = FVector::DistSquared(CurrentLocation, CurrentTarget);
	if (DistanceToTargetSquared < FMath::Square(static_cast<FVector::FReal>(BrakingDistance)))
	{
		bIsDecelerating = true;
		const FVector::FReal SpeedFraction = FMath::Clamp(
			FMath::Sqrt(DistanceToTargetSquared) / static_cast<FVector::FReal>(BrakingDistance),
			0.0,
			1.0);
		CurrentMoveInput *= SpeedFraction;
	}

	PostProcessMove.ExecuteIfBound(this, CurrentMoveInput);
	NavMovementInterface->RequestPathMove(CurrentMoveInput);
}

void UGridWorldPathFollowingComponent::UpdatePathSegment()
{
	if (GetStatus() == EPathFollowingStatus::Moving && UpdateDynamicAgentAvoidance())
	{
		bCollidedWithGoal = false;
		ResetBlockDetectionData();
		return;
	}

	const FGridPathPointFollowingData* FollowingData = GetFollowingData(MoveSegmentEndIndex);
	if (GetStatus() == EPathFollowingStatus::Moving
		&& IsPreciseFollowingData(FollowingData)
		&& FollowingData->DriveMode == EGridPathDriveMode::DirectVelocity
		&& !FollowingData->bUseAcceleratedFinalApproach
		&& FollowingData->bRequiresStop
		&& NavMovementInterface.IsValid()
		&& IsWithinCenterTolerance(
			NavMovementInterface->GetFeetLocation(),
			GetCurrentTargetLocation(),
			*FollowingData,
			false))
	{
		NavMovementInterface->StopMovementKeepPathing();
	}
	if (GetStatus() == EPathFollowingStatus::Moving
		&& FollowingData != nullptr
		&& FollowingData->bRequiresCenterGate
		&& NavMovementInterface.IsValid())
	{
		const ECenterGateResult GateResult = EvaluateCurrentCenterGate(NavMovementInterface->GetFeetLocation());
		if (GateResult == ECenterGateResult::Missed)
		{
			GRIDWORLD_LOG_WARNING(
				"Path following for '%s' missed the one-way center gate at cell %s:(%d,%d,%d), half width %.2f cm. The move is blocked instead of reversing toward an expired waypoint.",
				*GetNameSafe(GetOwner()),
				*FollowingData->CellId.GridId.ToString(),
				FollowingData->CellId.Coord.X,
				FollowingData->CellId.Coord.Y,
				FollowingData->CellId.Coord.Layer,
				FollowingData->CenterGateHalfWidth);
			OnPathFinished(FPathFollowingResult(EPathFollowingResult::Blocked, FPathFollowingResultFlags::None));
			return;
		}
	}

	if (bStrictFinalPath)
	{
		// Precise paths complete only from the final cell-center position and speed tests.
		bCollidedWithGoal = false;
	}

	Super::UpdatePathSegment();

	if (GetStatus() == EPathFollowingStatus::Moving && NavMovementInterface.IsValid())
	{
		PreviousFeetLocation = NavMovementInterface->GetFeetLocation();
		bHasPreviousFeetLocation = true;
	}
}

bool UGridWorldPathFollowingComponent::UpdateBlockDetection()
{
	if (bYieldingToAgent)
	{
		ResetBlockDetectionData();
		return false;
	}
	return Super::UpdateBlockDetection();
}

bool UGridWorldPathFollowingComponent::HasReachedDestination(const FVector& CurrentLocation) const
{
	const FGridNavigationPath* GridPath = GetGridPath();
	if (GridPath == nullptr || GridPath->GetPathPoints().IsEmpty())
	{
		return Super::HasReachedDestination(CurrentLocation);
	}

	const int32 FinalPointIndex = GridPath->GetPathPoints().Num() - 1;
	const FGridPathPointFollowingData* FollowingData = GridPath->GetFollowingData(FinalPointIndex);
	if (!IsPreciseFollowingData(FollowingData))
	{
		return Super::HasReachedDestination(CurrentLocation);
	}

	return IsWithinCenterTolerance(
		CurrentLocation,
		GridPath->GetPathPoints()[FinalPointIndex].Location,
		*FollowingData,
		false)
		&& IsBelowStopSpeed(*FollowingData);
}

bool UGridWorldPathFollowingComponent::HasReachedCurrentTarget(const FVector& CurrentLocation) const
{
	const FGridPathPointFollowingData* FollowingData = GetFollowingData(MoveSegmentEndIndex);
	if (!IsPreciseFollowingData(FollowingData))
	{
		return Super::HasReachedCurrentTarget(CurrentLocation);
	}
	if (FollowingData->bRequiresCenterGate)
	{
		return EvaluateCurrentCenterGate(CurrentLocation) == ECenterGateResult::Passed;
	}

	const bool bReachedCenter = IsWithinCenterTolerance(
		CurrentLocation,
		GetCurrentTargetLocation(),
		*FollowingData,
		!FollowingData->bRequiresStop);
	return bReachedCenter && (!FollowingData->bRequiresStop || IsBelowStopSpeed(*FollowingData));
}

int32 UGridWorldPathFollowingComponent::DetermineStartingPathPoint(const FNavigationPath* ConsideredPath) const
{
	const FGridNavigationPath* GridPath = ConsideredPath != nullptr
		? ConsideredPath->CastPath<FGridNavigationPath>()
		: nullptr;
	const FGridPathPointFollowingData* FirstData = GridPath != nullptr ? GridPath->GetFollowingData(0) : nullptr;
	if (GridPath != nullptr
		&& GridPath->RequiresPrecisePathFollowing()
		&& FirstData != nullptr
		&& !FirstData->bIsCellCenter)
	{
		return 0;
	}

	return Super::DetermineStartingPathPoint(ConsideredPath);
}

int32 UGridWorldPathFollowingComponent::DetermineCurrentTargetPathPoint(int32 StartIndex)
{
	const FGridNavigationPath* GridPath = GetGridPath();
	if (GridPath == nullptr || !GridPath->RequiresPrecisePathFollowing())
	{
		return Super::DetermineCurrentTargetPathPoint(StartIndex);
	}

	const int32 TargetIndex = GridPath->GetNextFollowingTargetIndex(StartIndex);
	return TargetIndex != INDEX_NONE
		? TargetIndex
		: Super::DetermineCurrentTargetPathPoint(StartIndex);
}

const FGridNavigationPath* UGridWorldPathFollowingComponent::GetGridPath() const
{
	return Path.IsValid() ? Path->CastPath<FGridNavigationPath>() : nullptr;
}

void UGridWorldPathFollowingComponent::BindPresentationPathObserver(const FNavPathSharedPtr& InPath)
{
	const FNavPathSharedPtr ObservedPath = PresentationObservedPath.Pin();
	if (ObservedPath.Get() == InPath.Get() && PresentationPathObserverHandle.IsValid())
	{
		return;
	}
	UnbindPresentationPathObserver();
	if (!InPath.IsValid())
	{
		return;
	}
	PresentationObservedPath = InPath;
	PresentationPathObserverHandle = InPath->AddObserver(
		FNavigationPath::FPathObserverDelegate::FDelegate::CreateUObject(
			this,
			&UGridWorldPathFollowingComponent::HandlePresentationPathEvent));
}

void UGridWorldPathFollowingComponent::UnbindPresentationPathObserver()
{
	if (PresentationPathObserverHandle.IsValid())
	{
		if (const FNavPathSharedPtr ObservedPath = PresentationObservedPath.Pin())
		{
			ObservedPath->RemoveObserver(PresentationPathObserverHandle);
		}
	}
	PresentationPathObserverHandle.Reset();
	PresentationObservedPath.Reset();
}

void UGridWorldPathFollowingComponent::HandlePresentationPathEvent(
	FNavigationPath* InPath,
	ENavPathEvent::Type Event)
{
	const FNavPathSharedPtr ObservedPath = PresentationObservedPath.Pin();
	if (!ObservedPath.IsValid() || ObservedPath.Get() != InPath)
	{
		return;
	}

	EGridPathPresentationInvalidationReason Reason;
	if (Event == ENavPathEvent::Invalidated)
	{
		Reason = EGridPathPresentationInvalidationReason::Invalidated;
	}
	else if (Event == ENavPathEvent::RePathFailed)
	{
		Reason = EGridPathPresentationInvalidationReason::RepathFailed;
	}
	else
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
		{
			Presentation->MarkPathPresentationInvalid(ActivePathPresentationHandle);
		}
	}
	OnGridPathInvalidated.Broadcast(Reason);
}

void UGridWorldPathFollowingComponent::SynchronizePathPresentation(
	EGridPathFollowingPresentationChange Change,
	bool bBroadcastChange)
{
	const FGridNavigationPath* GridPath = GetGridPath();
	if (GridPath == nullptr || GridPath->CellPath.IsEmpty())
	{
		return;
	}
	const int32 CurrentCellPathIndex = ResolvePresentationCellPathIndex(*GridPath);
	if (!GridPath->CellPath.IsValidIndex(CurrentCellPathIndex))
	{
		return;
	}

	if (bPresentActivePath)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
			{
				if (Presentation->IsPathPresentationValid(ActivePathPresentationHandle))
				{
					Presentation->UpdatePathPresentation(ActivePathPresentationHandle, *GridPath, CurrentCellPathIndex);
					Presentation->SetPathPresentationMode(ActivePathPresentationHandle, ActivePathPresentationMode);
					Presentation->SetPathPresentationPriority(ActivePathPresentationHandle, ActivePathPresentationPriority);
					Presentation->SetPathPresentationRenderers(
						ActivePathPresentationHandle,
						bPresentActivePathAsCellOverlay,
						bPresentActivePathAsLine);
				}
				else
				{
					FGridPathPresentationRequest Request;
					Request.Purpose = EGridPathPresentationPurpose::Active;
					Request.ProgressMode = ActivePathPresentationMode;
					Request.ReplacementPolicy = EGridPathReplacementPolicy::ReplaceImmediately;
					Request.Lifetime = EGridPathPresentationLifetime::OwnerLifetime;
					Request.Owner = this;
					Request.Priority = ActivePathPresentationPriority;
					Request.CurrentCellIndex = CurrentCellPathIndex;
					Request.bRenderCellOverlay = bPresentActivePathAsCellOverlay;
					Request.bRenderLine = bPresentActivePathAsLine;
					Presentation->CreatePathPresentation(*GridPath, Request, ActivePathPresentationHandle);
				}
			}
		}
	}

	LastPresentedPathCells = GridPath->CellPath;
	LastPresentedPathRevisions = GridPath->Revisions;
	if (bBroadcastChange)
	{
		OnGridPathChanged.Broadcast(Change, LastPresentedPathCells, LastPresentedPathRevisions);
	}
	if (PresentationCurrentCellPathIndex != CurrentCellPathIndex)
	{
		PresentationCurrentCellPathIndex = CurrentCellPathIndex;
		OnGridPathProgressChanged.Broadcast(
			PresentationCurrentCellPathIndex,
			GridPath->CellPath[PresentationCurrentCellPathIndex]);
	}
}

void UGridWorldPathFollowingComponent::UpdatePathPresentationProgress()
{
	if (!bPresentActivePath && !OnGridPathProgressChanged.IsBound())
	{
		return;
	}
	const FGridNavigationPath* GridPath = GetGridPath();
	if (GridPath == nullptr || GridPath->CellPath.IsEmpty())
	{
		return;
	}
	const int32 CurrentCellPathIndex = ResolvePresentationCellPathIndex(*GridPath);
	if (!GridPath->CellPath.IsValidIndex(CurrentCellPathIndex)
		|| PresentationCurrentCellPathIndex == CurrentCellPathIndex)
	{
		return;
	}
	PresentationCurrentCellPathIndex = CurrentCellPathIndex;
	if (UWorld* World = GetWorld())
	{
		if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
		{
			Presentation->UpdatePathPresentationProgress(ActivePathPresentationHandle, CurrentCellPathIndex);
		}
	}
	OnGridPathProgressChanged.Broadcast(CurrentCellPathIndex, GridPath->CellPath[CurrentCellPathIndex]);
}

void UGridWorldPathFollowingComponent::ReleasePathPresentation(bool bResetCorrelationState)
{
	if (ActivePathPresentationHandle.IsSet())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
			{
				Presentation->ReleasePathPresentation(ActivePathPresentationHandle);
			}
		}
		ActivePathPresentationHandle = FGridPathPresentationHandle();
	}
	if (bResetCorrelationState)
	{
		PresentationCurrentCellPathIndex = INDEX_NONE;
		LastPresentedPathCells.Reset();
		LastPresentedPathRevisions = FGridRevisionSet();
	}
}

int32 UGridWorldPathFollowingComponent::ResolvePresentationCellPathIndex(const FGridNavigationPath& GridPath) const
{
	if (GridPath.CellPath.IsEmpty())
	{
		return INDEX_NONE;
	}
	if (NavMovementInterface.IsValid())
	{
		const int32 LocatedIndex = FindCurrentCellPathIndex(GridPath, NavMovementInterface->GetFeetLocation());
		if (GridPath.CellPath.IsValidIndex(LocatedIndex))
		{
			return LocatedIndex;
		}
	}
	return FMath::Clamp(
		MoveSegmentStartIndex - GridPath.GetCellPathPointOffset(),
		0,
		GridPath.CellPath.Num() - 1);
}

const FGridPathPointFollowingData* UGridWorldPathFollowingComponent::GetFollowingData(int32 PathPointIndex) const
{
	const FGridNavigationPath* GridPath = GetGridPath();
	return GridPath != nullptr ? GridPath->GetFollowingData(PathPointIndex) : nullptr;
}

UGridWorldPathFollowingComponent::ECenterGateResult UGridWorldPathFollowingComponent::EvaluateCurrentCenterGate(
	const FVector& CurrentLocation) const
{
	const FGridNavigationPath* GridPath = GetGridPath();
	const FGridPathPointFollowingData* FollowingData = GetFollowingData(MoveSegmentEndIndex);
	if (GridPath == nullptr
		|| FollowingData == nullptr
		|| !FollowingData->bRequiresCenterGate
		|| !NavMovementInterface.IsValid())
	{
		return ECenterGateResult::Pending;
	}

	FGridCenterGateDebugData GateGeometry;
	if (!GridPath->GetCenterGateGeometry(MoveSegmentStartIndex, MoveSegmentEndIndex, GateGeometry))
	{
		return ECenterGateResult::Missed;
	}

	float AgentRadius = 0.0f;
	float AgentHalfHeight = 0.0f;
	NavMovementInterface->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);
	const FQuat WorldToGrid = FollowingData->GridRotation.Quaternion().Inverse();
	const FVector TargetLocation = GridPath->GetPathPoints()[MoveSegmentEndIndex].Location;
	const FVector LocalForward3D = WorldToGrid.RotateVector(GateGeometry.Forward);
	UE::GridWorld::Private::FGridCenterGate Gate;
	Gate.Forward = FVector2D(LocalForward3D.X, LocalForward3D.Y).GetSafeNormal();
	Gate.HalfWidth = FollowingData->CenterGateHalfWidth;
	Gate.CenterTolerance = FollowingData->CellCenterTolerance;
	Gate.HeightTolerance = FMath::Max(FollowingData->CellCenterTolerance, AgentHalfHeight * MinAgentHalfHeightPct);

	const FVector PreviousLocalOffset = WorldToGrid.RotateVector(PreviousFeetLocation - TargetLocation);
	const FVector CurrentLocalOffset = WorldToGrid.RotateVector(CurrentLocation - TargetLocation);
	switch (Gate.Evaluate(PreviousLocalOffset, CurrentLocalOffset, bHasPreviousFeetLocation))
	{
	case UE::GridWorld::Private::EGridCenterGateTraversalResult::Passed:
		return ECenterGateResult::Passed;
	case UE::GridWorld::Private::EGridCenterGateTraversalResult::Missed:
		return ECenterGateResult::Missed;
	default:
		return ECenterGateResult::Pending;
	}
}

bool UGridWorldPathFollowingComponent::IsWithinCenterTolerance(
	const FVector& CurrentLocation,
	const FVector& TargetLocation,
	const FGridPathPointFollowingData& FollowingData,
	bool bAllowSweptPass) const
{
	if (!NavMovementInterface.IsValid())
	{
		return false;
	}

	float AgentRadius = 0.0f;
	float AgentHalfHeight = 0.0f;
	NavMovementInterface->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);
	const float HeightTolerance = FMath::Max(FollowingData.CellCenterTolerance, AgentHalfHeight * MinAgentHalfHeightPct);
	const FQuat WorldToGrid = FollowingData.GridRotation.Quaternion().Inverse();
	const FVector CurrentOffset = WorldToGrid.RotateVector(CurrentLocation - TargetLocation);
	if (FMath::Abs(CurrentOffset.Z) > HeightTolerance)
	{
		return false;
	}

	const float ToleranceSquared = FMath::Square(FMath::Max(0.1f, FollowingData.CellCenterTolerance));
	if (FVector2D(CurrentOffset.X, CurrentOffset.Y).SizeSquared() <= ToleranceSquared)
	{
		return true;
	}

	if (!bAllowSweptPass || !bHasPreviousFeetLocation)
	{
		return false;
	}

	const FVector PreviousOffset = WorldToGrid.RotateVector(PreviousFeetLocation - TargetLocation);
	if (FMath::Abs(PreviousOffset.Z) > HeightTolerance)
	{
		return false;
	}

	const FVector2D SegmentStart(PreviousOffset.X, PreviousOffset.Y);
	const FVector2D SegmentEnd(CurrentOffset.X, CurrentOffset.Y);
	const FVector2D Segment = SegmentEnd - SegmentStart;
	const float SegmentLengthSquared = Segment.SizeSquared();
	if (SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float ClosestAlpha = FMath::Clamp(-FVector2D::DotProduct(SegmentStart, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
	return (SegmentStart + Segment * ClosestAlpha).SizeSquared() <= ToleranceSquared;
}

bool UGridWorldPathFollowingComponent::IsBelowStopSpeed(const FGridPathPointFollowingData& FollowingData) const
{
	if (!NavMovementInterface.IsValid())
	{
		return false;
	}

	const FQuat WorldToGrid = FollowingData.GridRotation.Quaternion().Inverse();
	const FVector LocalVelocity = WorldToGrid.RotateVector(NavMovementInterface->GetVelocityForNavMovement());
	return FVector2D(LocalVelocity.X, LocalVelocity.Y).SizeSquared()
		<= FMath::Square(FMath::Max(0.0f, FollowingData.StopSpeedTolerance));
}

bool UGridWorldPathFollowingComponent::ShouldUseAcceleratedDrive(
	const FGridPathPointFollowingData& FollowingData) const
{
	return FollowingData.DriveMode == EGridPathDriveMode::Accelerated
		|| (FollowingData.bRequiresStop && FollowingData.bUseAcceleratedFinalApproach);
}

UCharacterMovementComponent* UGridWorldPathFollowingComponent::GetCharacterMovementComponent() const
{
	return NavMovementInterface.IsValid()
		? Cast<UCharacterMovementComponent>(NavMovementInterface.GetObject())
		: nullptr;
}

void UGridWorldPathFollowingComponent::ApplyDrivePolicy()
{
	const FGridPathPointFollowingData* FollowingData = GetFollowingData(MoveSegmentEndIndex);
	if (!IsPreciseFollowingData(FollowingData) || !NavMovementInterface.IsValid())
	{
		RestoreDrivePolicy();
		return;
	}

	FNavMovementProperties* MovementProperties = NavMovementInterface->GetNavMovementProperties();
	if (MovementProperties == nullptr)
	{
		return;
	}

	if (!bHasSavedUseAccelerationForPaths)
	{
		bSavedUseAccelerationForPaths = MovementProperties->bUseAccelerationForPaths;
		bHasSavedUseAccelerationForPaths = true;
	}
	const bool bUseAcceleratedDrive = ShouldUseAcceleratedDrive(*FollowingData);
	MovementProperties->bUseAccelerationForPaths = bUseAcceleratedDrive;

	if (FollowingData->DriveMode == EGridPathDriveMode::DirectVelocity)
	{
		if (UCharacterMovementComponent* CharacterMovement = GetCharacterMovementComponent())
		{
			if (!bHasSavedRequestedMoveUseAcceleration)
			{
				SavedCharacterMovement = CharacterMovement;
				bSavedRequestedMoveUseAcceleration = CharacterMovement->bRequestedMoveUseAcceleration;
				bHasSavedRequestedMoveUseAcceleration = true;
			}
			CharacterMovement->bRequestedMoveUseAcceleration = bUseAcceleratedDrive;
		}
	}
	else if (bHasSavedRequestedMoveUseAcceleration)
	{
		if (UCharacterMovementComponent* CharacterMovement = SavedCharacterMovement.Get())
		{
			CharacterMovement->bRequestedMoveUseAcceleration = bSavedRequestedMoveUseAcceleration;
		}
	}
}

void UGridWorldPathFollowingComponent::RestoreDrivePolicy()
{
	if (bHasSavedUseAccelerationForPaths && NavMovementInterface.IsValid())
	{
		if (FNavMovementProperties* MovementProperties = NavMovementInterface->GetNavMovementProperties())
		{
			MovementProperties->bUseAccelerationForPaths = bSavedUseAccelerationForPaths;
		}
	}
	if (bHasSavedRequestedMoveUseAcceleration)
	{
		if (UCharacterMovementComponent* CharacterMovement = SavedCharacterMovement.Get())
		{
			CharacterMovement->bRequestedMoveUseAcceleration = bSavedRequestedMoveUseAcceleration;
		}
	}

	bHasSavedUseAccelerationForPaths = false;
	bSavedUseAccelerationForPaths = false;
	SavedCharacterMovement.Reset();
	bHasSavedRequestedMoveUseAcceleration = false;
	bSavedRequestedMoveUseAcceleration = false;
}

void UGridWorldPathFollowingComponent::ResetPreviousLocation()
{
	if (NavMovementInterface.IsValid())
	{
		PreviousFeetLocation = NavMovementInterface->GetFeetLocation();
		bHasPreviousFeetLocation = true;
	}
	else
	{
		PreviousFeetLocation = FVector::ZeroVector;
		bHasPreviousFeetLocation = false;
	}
}

void UGridWorldPathFollowingComponent::EnsurePawnOccupancy(APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		PawnOccupancyComponent.Reset();
		return;
	}
	const AGridWorldAIController* GridController = Cast<AGridWorldAIController>(GetOwner());
	float AgentRadius = 42.0f;
	float AgentHalfHeight = 96.0f;
	if (NavMovementInterface.IsValid())
	{
		NavMovementInterface->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);
	}
	AgentRadius = AgentRadius > 0.0f ? AgentRadius : 42.0f;
	AgentHalfHeight = AgentHalfHeight > 0.0f ? AgentHalfHeight : 96.0f;
	const bool bShouldAutoCreate = bAutoRegisterPawnOccupancy
		&& (GridController == nullptr || GridController->bAutoRegisterPawnOccupancy);
	UGridNavigationOccupancyComponent* Component =
		UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
			*Pawn,
			AgentRadius,
			AgentHalfHeight * 2.0f,
			bShouldAutoCreate);
	if (Component == nullptr && bShouldAutoCreate)
	{
		GRIDWORLD_LOG_WARNING(
			"GridWorld controller '%s' could not create occupancy tracking for pawn '%s'.",
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Pawn));
	}
	PawnOccupancyComponent = Component;
}

bool UGridWorldPathFollowingComponent::UpdateTrafficCorridor(
	const FGridNavigationPath& GridPath,
	FGridTrafficCorridorResult& OutResult,
	TArray<FVector>& OutRequestedCellCenters)
{
	OutResult = FGridTrafficCorridorResult();
	OutRequestedCellCenters.Reset();
	if (GridPath.DynamicAgentPolicy != EGridDynamicAgentPolicy::ReservedCorridor
		|| !NavMovementInterface.IsValid())
	{
		return false;
	}

	AController* Controller = Cast<AController>(GetOwner());
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	EnsurePawnOccupancy(Pawn);
	UGridNavigationOccupancyComponent* Occupancy = PawnOccupancyComponent.Get();
	AGridNavigationData* GridNavData = Cast<AGridNavigationData>(GridPath.GetNavigationDataUsed());
	const FGridWorldSnapshotPtr Snapshot = GridNavData != nullptr ? GridNavData->GetSnapshot() : nullptr;
	if (!IsValid(Pawn)
		|| Occupancy == nullptr
		|| !Occupancy->OccupantId.IsValid()
		|| GridNavData == nullptr
		|| !Snapshot.IsValid()
		|| GridPath.CellPath.IsEmpty())
	{
		return false;
	}

	const FVector CurrentLocation = NavMovementInterface->GetFeetLocation();
	AdvanceTrafficProgress(GridPath, *Snapshot, CurrentLocation);
	if (!GridPath.CellPath.IsValidIndex(TrafficCurrentCellPathIndex))
	{
		return false;
	}

	float AgentRadius = 42.0f;
	float AgentHalfHeight = 96.0f;
	NavMovementInterface->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);
	AgentRadius = AgentRadius > 0.0f ? AgentRadius : 42.0f;
	AgentHalfHeight = AgentHalfHeight > 0.0f ? AgentHalfHeight : 96.0f;
	const float MaxSpeed = FMath::Max(0.0f, NavMovementInterface->GetMaxSpeedForNavMovement());
	const float BrakingDistance = NavMovementInterface->UseAccelerationForPathFollowing()
		? FMath::Max(0.0f, NavMovementInterface->GetPathFollowingBrakingDistance(MaxSpeed))
		: 0.0f;
	const float RequiredProtectedDistance = BrakingDistance
		+ AgentRadius * 2.0f
		+ FMath::Max(0.0f, GridPath.AdditionalAgentSeparation);

	FGridTrafficCorridorRequest Request;
	Request.OwnerId = Occupancy->OccupantId;
	Request.Source = this;
	Request.Pawn = Pawn;
	Request.AgentRadius = AgentRadius;
	Request.AgentHeight = AgentHalfHeight * 2.0f;
	Request.AdditionalSeparation = GridPath.AdditionalAgentSeparation;
	Request.bRepathing = bDynamicAgentRepathPending;
	const FGridCellData* CurrentCell = Snapshot->FindCell(GridPath.CellPath[TrafficCurrentCellPathIndex]);
	if (CurrentCell == nullptr)
	{
		return false;
	}
	Request.CurrentCell = {CurrentCell->Id, CurrentCell->WorldCenter};

	float AccumulatedDistance = 0.0f;
	FVector PreviousCenter = CurrentCell->WorldCenter;
	const int32 MinimumCellCount = FMath::Max(1, GridPath.ReservedLookAheadCells);
	for (int32 PathIndex = TrafficCurrentCellPathIndex + 1; PathIndex < GridPath.CellPath.Num(); ++PathIndex)
	{
		const FGridCellData* FutureCell = Snapshot->FindCell(GridPath.CellPath[PathIndex]);
		if (FutureCell == nullptr)
		{
			break;
		}
		AccumulatedDistance += FVector::Distance(PreviousCenter, FutureCell->WorldCenter);
		Request.DesiredFutureCells.Add({FutureCell->Id, FutureCell->WorldCenter});
		OutRequestedCellCenters.Add(FutureCell->WorldCenter);
		PreviousCenter = FutureCell->WorldCenter;
		if (Request.DesiredFutureCells.Num() >= MinimumCellCount
			&& AccumulatedDistance + UE_KINDA_SMALL_NUMBER >= RequiredProtectedDistance)
		{
			break;
		}
	}

	if (!bWarnedStandardTrafficFollowing)
	{
		const FGridRegionData* Region = Snapshot->FindRegion(CurrentCell->Id.GridId);
		if (Region != nullptr && Region->PathFollowingStyle == EGridPathFollowingStyle::Standard)
		{
			bWarnedStandardTrafficFollowing = true;
			GRIDWORLD_LOG_WARNING(
				"Controller '%s' is using Reserved Corridor with Standard path following. Traffic reservation remains active, but strict cell-centered separation requires Center-Constrained or Cell-by-Cell on the Grid Navigation Bounds Volume.",
				*GetNameSafe(Controller));
		}
	}

	TrafficNavigationData = GridNavData;
	TrafficOwnerId = Occupancy->OccupantId;
	return GridNavData->UpdateTrafficCorridor(Request, OutResult);
}

void UGridWorldPathFollowingComponent::ReleaseTrafficCorridor(bool bKeepCurrentCell)
{
	if (AGridNavigationData* GridNavData = TrafficNavigationData.Get();
		GridNavData != nullptr && TrafficOwnerId.IsValid())
	{
		GridNavData->ReleaseTrafficCorridor(TrafficOwnerId, this, bKeepCurrentCell);
	}
	TrafficNavigationData.Reset();
	TrafficOwnerId.Invalidate();
}

void UGridWorldPathFollowingComponent::ResetTrafficProgress()
{
	TrafficCurrentCellPathIndex = INDEX_NONE;
}

void UGridWorldPathFollowingComponent::AdvanceTrafficProgress(
	const FGridNavigationPath& GridPath,
	const FGridWorldSnapshot& Snapshot,
	const FVector& CurrentLocation)
{
	if (GridPath.CellPath.IsEmpty())
	{
		TrafficCurrentCellPathIndex = INDEX_NONE;
		return;
	}
	if (!GridPath.CellPath.IsValidIndex(TrafficCurrentCellPathIndex))
	{
		TrafficCurrentCellPathIndex = FMath::Clamp(
			FindCurrentCellPathIndex(GridPath, CurrentLocation),
			0,
			GridPath.CellPath.Num() - 1);
	}

	while (GridPath.CellPath.IsValidIndex(TrafficCurrentCellPathIndex + 1))
	{
		const FGridCellData* CurrentCell = Snapshot.FindCell(GridPath.CellPath[TrafficCurrentCellPathIndex]);
		const FGridCellData* NextCell = Snapshot.FindCell(GridPath.CellPath[TrafficCurrentCellPathIndex + 1]);
		const FGridRegionData* Region = NextCell != nullptr ? Snapshot.FindRegion(NextCell->Id.GridId) : nullptr;
		if (CurrentCell == nullptr || NextCell == nullptr || Region == nullptr)
		{
			break;
		}

		const FQuat WorldToGrid = Region->GridTransform.Rotation.Quaternion().Inverse();
		const FVector LocalSegment = WorldToGrid.RotateVector(NextCell->WorldCenter - CurrentCell->WorldCenter);
		const FVector2D LocalForward(LocalSegment.X, LocalSegment.Y);
		if (LocalForward.IsNearlyZero())
		{
			break;
		}
		const FVector2D Forward = LocalForward.GetSafeNormal();
		const FVector PreviousOffset = WorldToGrid.RotateVector(PreviousFeetLocation - NextCell->WorldCenter);
		const FVector CurrentOffset = WorldToGrid.RotateVector(CurrentLocation - NextCell->WorldCenter);
		const float PreviousAlong = FVector2D::DotProduct(FVector2D(PreviousOffset.X, PreviousOffset.Y), Forward);
		const float CurrentAlong = FVector2D::DotProduct(FVector2D(CurrentOffset.X, CurrentOffset.Y), Forward);
		const float CenterTolerance = FMath::Max(0.1f, Region->CellCenterTolerance);
		const bool bAtCenter = FVector2D(CurrentOffset.X, CurrentOffset.Y).SizeSquared() <= FMath::Square(CenterTolerance);
		const bool bCrossedGate = bHasPreviousFeetLocation
			? PreviousAlong <= 0.0f && CurrentAlong >= 0.0f
			: CurrentAlong >= 0.0f;
		const bool bAlreadyBeyondGate = bHasPreviousFeetLocation && PreviousAlong > 0.0f && CurrentAlong > 0.0f;
		if (!bAtCenter && !bCrossedGate && !bAlreadyBeyondGate)
		{
			break;
		}
		++TrafficCurrentCellPathIndex;
	}
}

int32 UGridWorldPathFollowingComponent::FindCurrentCellPathIndex(
	const FGridNavigationPath& GridPath,
	const FVector& CurrentLocation) const
{
	if (GridPath.CellPath.IsEmpty())
	{
		return INDEX_NONE;
	}

	const int32 PointOffset = GridPath.GetCellPathPointOffset();
	const int32 FirstCandidate = FMath::Clamp(MoveSegmentStartIndex - PointOffset, 0, GridPath.CellPath.Num() - 1);
	const int32 LastCandidate = FMath::Clamp(MoveSegmentEndIndex - PointOffset, FirstCandidate, GridPath.CellPath.Num() - 1);
	const TArray<FNavPathPoint>& Points = GridPath.GetPathPoints();
	int32 BestIndex = FirstCandidate;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 CellPathIndex = FirstCandidate; CellPathIndex <= LastCandidate; ++CellPathIndex)
	{
		const int32 PointIndex = CellPathIndex + PointOffset;
		if (!Points.IsValidIndex(PointIndex))
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared(CurrentLocation, Points[PointIndex].Location);
		if (DistanceSquared < BestDistanceSquared
			|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared) && CellPathIndex > BestIndex))
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = CellPathIndex;
		}
	}
	return BestIndex;
}

bool UGridWorldPathFollowingComponent::FindBlockingAgent(
	const FGridNavigationPath& GridPath,
	const FVector& CurrentLocation,
	FGuid& OutOccupantId,
	FGridCellId& OutCellId,
	FVector& OutCellCenter,
	float& OutOccupantSpeed,
	TArray<FVector>& OutLookAheadCellCenters) const
{
	OutOccupantId.Invalidate();
	OutCellId = FGridCellId();
	OutCellCenter = FVector::ZeroVector;
	OutOccupantSpeed = 0.0f;
	OutLookAheadCellCenters.Reset();
	if (!NavMovementInterface.IsValid() || GridPath.DynamicAgentPolicy == EGridDynamicAgentPolicy::Ignore)
	{
		return false;
	}

	const AGridNavigationData* GridNavData = Cast<AGridNavigationData>(GridPath.GetNavigationDataUsed());
	const FGridWorldSnapshotPtr Snapshot = GridNavData != nullptr ? GridNavData->GetSnapshot() : nullptr;
	const int32 CurrentCellPathIndex = FindCurrentCellPathIndex(GridPath, CurrentLocation);
	const int32 LastCorridorCellIndex = GridPath.DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor
		? GridPath.CellPath.Num() - 1
		: GridPath.CellPath.Num() - 2;
	if (!Snapshot.IsValid() || CurrentCellPathIndex == INDEX_NONE || CurrentCellPathIndex > LastCorridorCellIndex)
	{
		return false;
	}

	float AgentRadius = 42.0f;
	float AgentHalfHeight = 96.0f;
	NavMovementInterface->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);
	AgentRadius = AgentRadius > 0.0f ? AgentRadius : 42.0f;
	const float MaxSpeed = FMath::Max(0.0f, NavMovementInterface->GetMaxSpeedForNavMovement());
	const float BrakingDistance = NavMovementInterface->UseAccelerationForPathFollowing()
		? FMath::Max(0.0f, NavMovementInterface->GetPathFollowingBrakingDistance(MaxSpeed))
		: 0.0f;
	const FGridCellData* CurrentCell = Snapshot->FindCell(GridPath.CellPath[CurrentCellPathIndex]);
	const FGridRegionData* CurrentRegion = CurrentCell != nullptr ? Snapshot->FindRegion(CurrentCell->Id.GridId) : nullptr;
	const float MinimumCellSize = CurrentRegion != nullptr
		? static_cast<float>(FMath::Max(1.0, FMath::Min(
			FMath::Abs(CurrentRegion->GridTransform.CellSize.X),
			FMath::Abs(CurrentRegion->GridTransform.CellSize.Y))))
		: 50.0f;
	const float MinimumLookAheadDistance = FMath::Max(1, GridPath.MinimumAgentLookAheadCells) * MinimumCellSize;
	const float BaseLookAheadDistance = FMath::Max(
		MinimumLookAheadDistance,
		BrakingDistance + AgentRadius * 2.0f + FMath::Max(0.0f, GridPath.AdditionalAgentSeparation));

	const UWorld* World = GetWorld();
	FVector PreviousCenter = CurrentLocation;
	double PathDistance = 0.0;
	for (int32 CellPathIndex = CurrentCellPathIndex; CellPathIndex <= LastCorridorCellIndex; ++CellPathIndex)
	{
		const FGridCellData* Cell = Snapshot->FindCell(GridPath.CellPath[CellPathIndex]);
		if (Cell == nullptr)
		{
			break;
		}
		PathDistance += FVector::Dist2D(PreviousCenter, Cell->WorldCenter);
		if (PathDistance > BaseLookAheadDistance + MinimumCellSize * 0.5f)
		{
			break;
		}
		OutLookAheadCellCenters.Add(Cell->WorldCenter);
		PreviousCenter = Cell->WorldCenter;

		FGuid CandidateOccupantId;
		for (const FGuid& OwnerId : Cell->OccupancyOwners)
		{
			if (OwnerId == GridPath.IgnoredOccupancyOwnerId || Cell->ReservationOwners.Contains(OwnerId))
			{
				continue;
			}
			if (!CandidateOccupantId.IsValid() || OwnerId < CandidateOccupantId)
			{
				CandidateOccupantId = OwnerId;
			}
		}
		if (!CandidateOccupantId.IsValid())
		{
			continue;
		}

		float OtherRadius = AgentRadius;
		UGridNavigationOccupancyComponent* Occupant = World != nullptr
			? UGridNavigationOccupancyComponent::FindOccupantById(*World, CandidateOccupantId)
			: nullptr;
		if (Occupant != nullptr)
		{
			OtherRadius = FMath::Max(0.0f, FMath::Max(Occupant->BoxExtent.X, Occupant->BoxExtent.Y));
			OutOccupantSpeed = Occupant->GetOwner() != nullptr
				? Occupant->GetOwner()->GetVelocity().Size2D()
				: Occupant->GetComponentVelocity().Size2D();
		}
		const float RequiredDistance = FMath::Max(
			MinimumLookAheadDistance,
			BrakingDistance + AgentRadius + OtherRadius + FMath::Max(0.0f, GridPath.AdditionalAgentSeparation));
		if (PathDistance <= RequiredDistance + MinimumCellSize * 0.5f)
		{
			OutOccupantId = CandidateOccupantId;
			OutCellId = Cell->Id;
			OutCellCenter = Cell->WorldCenter;
			return true;
		}
	}
	return false;
}

bool UGridWorldPathFollowingComponent::UpdateDynamicAgentAvoidance()
{
	const FGridNavigationPath* GridPath = GetGridPath();
	if (GridPath == nullptr || GridPath->DynamicAgentPolicy == EGridDynamicAgentPolicy::Ignore || !NavMovementInterface.IsValid())
	{
		ClearDynamicAgentDebug();
		ResetDynamicAgentAvoidance(true);
		return false;
	}

	if (bDynamicAgentRepathPending)
	{
		bYieldingToAgent = true;
		PublishDynamicAgentDebug({}, BlockingCellCenter, BlockingCellCenter, BlockingOccupantId, true);
		return true;
	}

	FGuid NewBlockingOccupantId;
	FGridCellId NewBlockingCellId;
	FVector NewBlockingCellCenter = FVector::ZeroVector;
	float OccupantSpeed = 0.0f;
	TArray<FVector> LookAheadCellCenters;
	bool bHasBlocker = false;
	if (GridPath->DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor)
	{
		FGridTrafficCorridorResult ReservationResult;
		if (UpdateTrafficCorridor(*GridPath, ReservationResult, LookAheadCellCenters)
			&& ReservationResult.Status == EGridTrafficReservationStatus::Waiting)
		{
			bHasBlocker = true;
			NewBlockingOccupantId = ReservationResult.BlockingOwnerId;
			NewBlockingCellId = ReservationResult.BlockingCellId;
			NewBlockingCellCenter = ReservationResult.BlockingCellCenter;
			if (const UWorld* World = GetWorld())
			{
				const UGridNavigationOccupancyComponent* Occupant =
					UGridNavigationOccupancyComponent::FindOccupantById(*World, NewBlockingOccupantId);
				if (Occupant != nullptr)
				{
					OccupantSpeed = Occupant->GetOwner() != nullptr
						? Occupant->GetOwner()->GetVelocity().Size2D()
						: Occupant->GetComponentVelocity().Size2D();
				}
			}
		}
	}
	if (!bHasBlocker)
	{
		TArray<FVector> ReactiveLookAheadCellCenters;
		bHasBlocker = FindBlockingAgent(
			*GridPath,
			NavMovementInterface->GetFeetLocation(),
			NewBlockingOccupantId,
			NewBlockingCellId,
			NewBlockingCellCenter,
			OccupantSpeed,
			ReactiveLookAheadCellCenters);
		if (bHasBlocker || LookAheadCellCenters.IsEmpty())
		{
			LookAheadCellCenters = MoveTemp(ReactiveLookAheadCellCenters);
		}
	}
	const FVector LabelLocation = !LookAheadCellCenters.IsEmpty()
		? LookAheadCellCenters[0]
		: NavMovementInterface->GetFeetLocation();
	if (!bHasBlocker)
	{
		ResetDynamicAgentAvoidance(true);
		PublishDynamicAgentDebug(LookAheadCellCenters, LabelLocation, FVector::ZeroVector, FGuid(), false);
		return false;
	}

	const bool bSameBlocker = BlockingOccupantId == NewBlockingOccupantId && BlockingCellId == NewBlockingCellId;
	BlockingOccupantId = NewBlockingOccupantId;
	BlockingCellId = NewBlockingCellId;
	BlockingCellCenter = NewBlockingCellCenter;
	bYieldingToAgent = true;
	const double CurrentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bStationary = OccupantSpeed <= FMath::Max(0.0f, GridPath->StationaryAgentSpeedThreshold);
	if (!bSameBlocker || !bStationary)
	{
		StationaryBlockerSince = bStationary ? CurrentTime : -1.0;
	}
	else if (StationaryBlockerSince < 0.0)
	{
		StationaryBlockerSince = CurrentTime;
	}

	const bool bStationaryLongEnough = bStationary
		&& StationaryBlockerSince >= 0.0
		&& CurrentTime - StationaryBlockerSince >= FMath::Max(0.0f, GridPath->DynamicAgentRepathDelay);
	const bool bPreserveExactPath = GridPath->Origin == EGridNavigationPathOrigin::Injected
		&& GridPath->bAllowDynamicAgentConflictsDuringValidation;
	const bool bPersistentFinalCellConflict = bPreserveExactPath
		&& bStationaryLongEnough
		&& !GridPath->CellPath.IsEmpty()
		&& BlockingCellId == GridPath->CellPath.Last()
		&& TrafficCurrentCellPathIndex >= GridPath->CellPath.Num() - 2;
	if (bPersistentFinalCellConflict)
	{
		PublishDynamicAgentDebug(
			LookAheadCellCenters,
			LabelLocation,
			BlockingCellCenter,
			BlockingOccupantId,
			false);
		GRIDWORLD_LOG_INFO(
			"Controller '%s' reached the predecessor of exact-path goal (%d,%d,%d), which remains reserved by %s; reporting Blocked for goal-contention resolution.",
			*GetNameSafe(GetOwner()),
			BlockingCellId.Coord.X,
			BlockingCellId.Coord.Y,
			BlockingCellId.Coord.Layer,
			*BlockingOccupantId.ToString());
		OnPathFinished(FPathFollowingResult(EPathFollowingResult::Blocked, FPathFollowingResultFlags::None));
		return true;
	}

	bool bShouldRepath = !bPreserveExactPath
		&& (GridPath->DynamicAgentPolicy == EGridDynamicAgentPolicy::YieldThenRepath
			|| GridPath->DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor)
		&& bStationaryLongEnough
		&& Path.IsValid()
		&& Path->IsUpToDate()
		&& !Path->IsWaitingForRepath();
	const AGridNavigationData* GridNavData = Cast<AGridNavigationData>(GridPath->GetNavigationDataUsed());
	const FGridWorldSnapshotPtr Snapshot = GridNavData != nullptr ? GridNavData->GetSnapshot() : nullptr;
	const FGridTrafficReservationSnapshotPtr TrafficSnapshot = GridNavData != nullptr
		? GridNavData->GetTrafficReservationSnapshot()
		: nullptr;
	const bool bFallbackAlreadyEvaluatedCurrentOccupancy = GridPath->bUsedDynamicAgentFallback
		&& (GridPath->DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor
			? TrafficSnapshot.IsValid() && GridPath->TrafficReservationRevision == TrafficSnapshot->Revision
			: Snapshot.IsValid() && GridPath->Revisions.Occupancy == Snapshot->Revisions.Occupancy);
	const bool bSameFallbackBlocker = GridPath->bUsedDynamicAgentFallback
		&& LastRepathBlockingOccupantId == BlockingOccupantId
		&& LastRepathBlockingCellId == BlockingCellId;
	bShouldRepath &= !bFallbackAlreadyEvaluatedCurrentOccupancy && !bSameFallbackBlocker;

	if (bShouldRepath)
	{
		LastRepathBlockingOccupantId = BlockingOccupantId;
		LastRepathBlockingCellId = BlockingCellId;
		bDynamicAgentRepathPending = true;
		PublishDynamicAgentDebug(LookAheadCellCenters, LabelLocation, BlockingCellCenter, BlockingOccupantId, true);
		GRIDWORLD_LOG_INFO(
			"Controller '%s' is repathing around stationary occupant %s in cell (%d,%d,%d).",
			*GetNameSafe(GetOwner()),
			*BlockingOccupantId.ToString(),
			BlockingCellId.Coord.X,
			BlockingCellId.Coord.Y,
			BlockingCellId.Coord.Layer);
		Path->Invalidate();
		return true;
	}

	PublishDynamicAgentDebug(LookAheadCellCenters, LabelLocation, BlockingCellCenter, BlockingOccupantId, false);
	return true;
}

void UGridWorldPathFollowingComponent::ApplyDynamicAgentYield()
{
	if (!NavMovementInterface.IsValid())
	{
		return;
	}
	CurrentMoveInput = FVector::ZeroVector;
	if (NavMovementInterface->UseAccelerationForPathFollowing())
	{
		bIsDecelerating = true;
		NavMovementInterface->RequestPathMove(FVector::ZeroVector);
	}
	else
	{
		bIsDecelerating = false;
		NavMovementInterface->StopMovementKeepPathing();
	}
}

void UGridWorldPathFollowingComponent::ResetDynamicAgentAvoidance(bool bClearRepathMemory)
{
	BlockingOccupantId.Invalidate();
	BlockingCellId = FGridCellId();
	BlockingCellCenter = FVector::ZeroVector;
	StationaryBlockerSince = -1.0;
	bYieldingToAgent = false;
	bDynamicAgentRepathPending = false;
	if (bClearRepathMemory)
	{
		LastRepathBlockingOccupantId.Invalidate();
		LastRepathBlockingCellId = FGridCellId();
	}
}

void UGridWorldPathFollowingComponent::ClearDynamicAgentDebug()
{
	const FGridNavigationPath* GridPath = GetGridPath();
	if (AGridNavigationData* GridNavData = GridPath != nullptr
		? Cast<AGridNavigationData>(GridPath->GetNavigationDataUsed())
		: nullptr)
	{
		GridNavData->ClearAgentAvoidanceDebug(this);
	}
}

void UGridWorldPathFollowingComponent::PublishDynamicAgentDebug(
	TConstArrayView<FVector> LookAheadCellCenters,
	const FVector& LabelLocation,
	const FVector& InBlockingCellCenter,
	const FGuid& InBlockingOccupantId,
	bool bRepathing) const
{
	const FGridNavigationPath* GridPath = GetGridPath();
	AGridNavigationData* GridNavData = GridPath != nullptr
		? Cast<AGridNavigationData>(GridPath->GetNavigationDataUsed())
		: nullptr;
	if (GridNavData == nullptr || !GridNavData->bDrawPaths)
	{
		return;
	}

	FGridAgentAvoidanceDebugData DebugData;
	DebugData.LookAheadCellCenters.Append(LookAheadCellCenters);
	DebugData.LabelLocation = LabelLocation;
	DebugData.BlockingCellCenter = InBlockingCellCenter;
	DebugData.BlockingOccupantId = InBlockingOccupantId;
	DebugData.State = bRepathing
		? EGridAgentAvoidanceDebugState::Repathing
		: (InBlockingOccupantId.IsValid()
			? (GridPath->DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor
				? EGridAgentAvoidanceDebugState::WaitingReservation
				: EGridAgentAvoidanceDebugState::Yielding)
			: EGridAgentAvoidanceDebugState::Monitoring);
	GridNavData->SetAgentAvoidanceDebug(const_cast<UGridWorldPathFollowingComponent*>(this), DebugData);
}
